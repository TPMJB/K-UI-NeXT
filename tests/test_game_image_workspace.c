/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the production parser with allocation faults and nested file I/O. */
#include "kui/game_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocations,live,fail_allocation;
static void *workspace_calloc(size_t count,size_t bytes) {
    if(++allocations==fail_allocation)return NULL;
    void *memory=calloc(count,bytes);if(memory)++live;return memory;
}
static void *workspace_malloc(size_t bytes) {
    if(++allocations==fail_allocation)return NULL;
    void *memory=malloc(bytes);if(memory)++live;return memory;
}
static void workspace_free(void *memory) {
    if(memory) {assert(live);--live;}free(memory);
}
#define calloc workspace_calloc
#define malloc workspace_malloc
#define free workspace_free
#include "../src/core/game_image.c"
#undef calloc
#undef malloc
#undef free

static const char outer_gdi[]="1\n1 45000 4 2048 outer.bin 0\n";
static const char nested_gdi[]="1\n1 45100 4 2048 nested.bin 0\n";
static const char outer_cue[]="FILE outer.bin BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n";
static const char invalid_cue[]="FILE outer.bin BINARY\nTRACK 01 MODE1/2048\n";
struct workspace_fixture {
    unsigned stats,reads,fail_stat,fail_read,nested;
    bool reenter,inside,malformed;
};
static enum kui_game_result workspace_stat(void *,const char *,uint64_t *);
static enum kui_game_result workspace_read(void *,const char *,uint64_t,void *,size_t);
static struct kui_game_file_ops workspace_ops(struct workspace_fixture *f) {
    return (struct kui_game_file_ops){f,workspace_stat,workspace_read};
}
static const char *workspace_descriptor(const struct workspace_fixture *f,const char *name) {
    if(!strcmp(name,"outer.gdi"))return outer_gdi;
    if(!strcmp(name,"nested.gdi"))return nested_gdi;
    if(!strcmp(name,"outer.cue"))return f->malformed?invalid_cue:outer_cue;
    return NULL;
}
static enum kui_game_result workspace_stat(void *context,const char *name,uint64_t *bytes) {
    struct workspace_fixture *f=context;
    if(++f->stats==f->fail_stat)return KUI_GAME_CANCELLED;
    const char *descriptor=workspace_descriptor(f,name);
    if(descriptor) {*bytes=strlen(descriptor);return KUI_GAME_OK;}
    if(strcmp(name,"outer.bin") && strcmp(name,"nested.bin"))return KUI_GAME_NOT_FOUND;
    if(f->reenter && !f->inside && !strcmp(name,"outer.bin")) {
        f->inside=true;
        struct kui_game_image *nested=malloc(sizeof(*nested));assert(nested);
        struct kui_game_file_ops ops=workspace_ops(f);
        assert(kui_game_image_open_named("nested.gdi",&ops,nested)==KUI_GAME_OK);
        assert(nested->count==1u && nested->data_lba==45100u && !strcmp(nested->tracks[0].name,"nested.bin"));
        free(nested);++f->nested;f->inside=false;
    }
    *bytes=64u*2048u;return KUI_GAME_OK;
}
static enum kui_game_result workspace_read(void *context,const char *name,uint64_t offset,void *out,size_t bytes) {
    struct workspace_fixture *f=context;
    if(++f->reads==f->fail_read)return KUI_GAME_CANCELLED;
    const char *descriptor=workspace_descriptor(f,name);
    if(!descriptor)return KUI_GAME_NOT_FOUND;
    assert(offset<=strlen(descriptor) && bytes<=strlen(descriptor)-offset);
    memcpy(out,descriptor+offset,bytes);return KUI_GAME_OK;
}
static void reset_allocations(unsigned fail) {assert(!live);allocations=0;fail_allocation=fail;}
static void unchanged(const struct kui_game_image *image) {
    const unsigned char *bytes=(const unsigned char *)image;
    for(size_t i=0;i<sizeof(*image);i++)assert(bytes[i]==0xa5u);
    assert(!live);
}
int main(void) {
    struct kui_game_image *image=malloc(sizeof(*image));assert(image);
    struct workspace_fixture fixture={0};struct kui_game_file_ops ops=workspace_ops(&fixture);
    reset_allocations(1u);memset(image,0xa5,sizeof(*image));
    assert(kui_game_gdi_layout(outer_gdi,strlen(outer_gdi),image)==KUI_GAME_IO);unchanged(image);
    reset_allocations(1u);
    assert(kui_game_image_open(outer_gdi,strlen(outer_gdi),&ops,image)==KUI_GAME_IO);unchanged(image);
    assert(!fixture.stats && !fixture.reads);
    const char *names[]={"outer.gdi","outer.cue"};
    for(unsigned kind=0;kind<2u;kind++)for(unsigned fail=1u;fail<=2u;fail++) {
        fixture=(struct workspace_fixture){0};reset_allocations(fail);memset(image,0xa5,sizeof(*image));
        assert(kui_game_image_open_named(names[kind],&ops,image)==KUI_GAME_IO);unchanged(image);
        assert(allocations==fail && fixture.stats==fail-1u && !fixture.reads);
    }
    for(unsigned kind=0;kind<2u;kind++)for(unsigned failure=0;failure<3u;failure++) {
        fixture=(struct workspace_fixture){0};reset_allocations(0u);memset(image,0xa5,sizeof(*image));
        if(failure==0u)fixture.fail_stat=1u;
        else if(failure==1u)fixture.fail_stat=2u;
        else fixture.fail_read=1u;
        assert(kui_game_image_open_named(names[kind],&ops,image)==KUI_GAME_CANCELLED);unchanged(image);
    }
    fixture=(struct workspace_fixture){.malformed=true};reset_allocations(0u);
    assert(kui_game_image_open_named("outer.cue",&ops,image)==KUI_GAME_SYNTAX);unchanged(image);
    /* A nested open from a track-stat callback must not corrupt the outer
     * scratch image, and named GDI must allocate only scratch + descriptor. */
    for(unsigned kind=0;kind<2u;kind++) {
        fixture=(struct workspace_fixture){.reenter=true};reset_allocations(0u);
        assert(kui_game_image_open_named(names[kind],&ops,image)==KUI_GAME_OK);
        assert(fixture.nested==1u && allocations==4u && !live && image->count==1u &&
               !strcmp(image->tracks[0].name,"outer.bin"));
        assert(image->format==(kind?KUI_GAME_IMAGE_CUE:KUI_GAME_IMAGE_GDI));
        assert(image->data_lba==(kind?0u:45000u));
    }
    fixture=(struct workspace_fixture){.reenter=true};reset_allocations(0u);
    assert(kui_game_image_open(outer_gdi,strlen(outer_gdi),&ops,image)==KUI_GAME_OK);
    assert(fixture.nested==1u && allocations==3u && !live && image->data_lba==45000u);
    free(image);
    puts("PASS image workspace: OOM/I/O/parse failures preserve output, every allocation released, nested GDI/CUE callbacks remain independent, one image workspace per public open");
    return 0;
}
