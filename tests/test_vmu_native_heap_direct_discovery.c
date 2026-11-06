/* SPDX-License-Identifier: GPL-3.0-only */
/* Original-backed direct allocator recognition and cold-code mutation tests.
 * Executable bytes are supplied by the owner, never embedded in this test. */
#include "kui/vmu_native_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,mutants,originals;
#define CHECK(test) do {++checks;assert(test);} while(0)
struct source {uint8_t *data;size_t bytes;uint32_t load;unsigned reads,fail;};
static uint32_t physical(uint32_t address) {return address&UINT32_C(0x1fffffff);}
static bool source_read(void *context,uint32_t address,uint32_t *out) {
    struct source *s=context;uint32_t at=physical(address),base=physical(s->load);
    CHECK(address && !(address&3u) && at>=base && at-base<=s->bytes && 4u<=s->bytes-(at-base));
    if(++s->reads==s->fail)return false;
    memcpy(out,s->data+at-base,4u);return true;
}
static void mutate_word_bits(struct source *source,const struct kui_vmu_native_heap *owner,
    uint32_t address,unsigned width) {
    size_t offset=physical(address)-physical(source->load);
    CHECK(offset+width<=source->bytes);
    for(unsigned bit=0u;bit<width*8u;bit++) {
        source->data[offset+bit/8u]^=(uint8_t)(1u<<(bit&7u));
        CHECK(!kui_vmu_native_heap_bottom_code_valid(owner,source_read,source));
        source->data[offset+bit/8u]^=(uint8_t)(1u<<(bit&7u));++mutants;
    }
}
static void original(const char *path) {
    FILE *file=fopen(path,"rb");CHECK(file && !fseek(file,0,SEEK_END));
    long size=ftell(file);CHECK(size>0 && size<=0xc00000 && !fseek(file,0,SEEK_SET));
    struct source source={malloc((size_t)size),(size_t)size,UINT32_C(0x8c010000),0u,0u};
    CHECK(source.data && fread(source.data,1u,source.bytes,file)==source.bytes && !fclose(file));
    struct kui_vmu_native_heap owner;
    CHECK(kui_vmu_native_heap_discover(source.data,source.bytes,source.load,&owner)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK(sizeof(owner)==72u && owner.family==KUI_VMU_NATIVE_HEAP_DIRECT32 &&
        owner.layout==KUI_VMU_NATIVE_HEAP_DIRECT && !owner.dispatch_cell && !owner.dispatch_table);
    CHECK(owner.allocate==owner.initializer+0xaeu && owner.release==owner.initializer+0x160u);
    CHECK(kui_vmu_native_heap_metadata_valid(&owner) &&
        kui_vmu_native_heap_bottom_code_valid(&owner,source_read,&source));
    unsigned reads=source.reads;
    for(unsigned missing=1u;missing<=reads;missing++) {
        source.reads=0u;source.fail=missing;
        CHECK(!kui_vmu_native_heap_bottom_code_valid(&owner,source_read,&source));
    }
    source.fail=0u;
    /* All executable words in the actual allocation/release entries are
     * mandatory, including their exact PC-relative and branch operands. */
    for(unsigned offset=0u;offset<0x9au;offset+=2u) {
        mutate_word_bits(&source,&owner,owner.allocate+offset,2u);
        mutate_word_bits(&source,&owner,owner.release+offset,2u);
    }
    for(unsigned role=0u;role<5u;role++)
        mutate_word_bits(&source,&owner,owner.initializer+0x14cu+4u*role,4u);
    mutate_word_bits(&source,&owner,owner.allocate+0x9au,2u);
    mutate_word_bits(&source,&owner,owner.release+0x102u,2u);
    mutate_word_bits(&source,&owner,owner.release+0x104u,4u);
    /* The unexecuted padding halfword beside the allocation mask is not a
     * pointer or instruction consumer; harmless changes remain accepted. */
    size_t padding=physical(owner.allocate+0x9cu)-physical(source.load);
    for(unsigned bit=0u;bit<16u;bit++) {
        source.data[padding+bit/8u]^=(uint8_t)(1u<<(bit&7u));
        CHECK(kui_vmu_native_heap_bottom_code_valid(&owner,source_read,&source));
        source.data[padding+bit/8u]^=(uint8_t)(1u<<(bit&7u));
    }
    /* Direct metadata cannot acquire a fictitious dispatch table, even one
     * geometrically inside this executable or at a plausible control cell. */
    for(unsigned role=0u;role<2u;role++) for(unsigned bit=0u;bit<32u;bit++) {
        struct kui_vmu_native_heap changed=owner;
        if(role)changed.dispatch_table=UINT32_C(1)<<bit;
        else changed.dispatch_cell=UINT32_C(1)<<bit;
        CHECK(!kui_vmu_native_heap_metadata_valid(&changed));
        CHECK(!kui_vmu_native_heap_bottom_code_valid(&changed,source_read,&source));
    }
    struct kui_vmu_native_heap changed=owner;
    changed.family=KUI_VMU_NATIVE_HEAP_TAIL32;CHECK(!kui_vmu_native_heap_metadata_valid(&changed));
    changed=owner;changed.layout=KUI_VMU_NATIVE_HEAP_DISPATCH2;CHECK(!kui_vmu_native_heap_metadata_valid(&changed));
    changed=owner;changed.image_bytes=changed.initializer-changed.load+0x267u;
    CHECK(!kui_vmu_native_heap_metadata_valid(&changed));
    /* Two complete copies are ambiguous before pointer-slot selection. */
    if(source.bytes<=0x600000u && !(source.bytes&3u)) {
        uint8_t *twice=malloc(source.bytes*2u);CHECK(twice);
        memcpy(twice,source.data,source.bytes);memcpy(twice+source.bytes,source.data,source.bytes);
        memset(&changed,0xa5,sizeof(changed));
        CHECK(kui_vmu_native_heap_discover(twice,source.bytes*2u,source.load,&changed)==KUI_VMU_NATIVE_HEAP_AMBIGUOUS);
        CHECK(!memcmp(&changed,&(struct kui_vmu_native_heap){0},sizeof(changed)));free(twice);
    }
    CHECK(kui_vmu_native_heap_bottom_code_valid(&owner,source_read,&source));
    ++originals;free(source.data);
}
int main(int argc,char **argv) {
    CHECK(argc>1);
    for(int i=1;i<argc;i++)original(argv[i]);
    printf("Original direct allocator discovery: %u checks, %u bit mutants refused, %u originals; no fabricated dispatch or partial-body admission\n",
        checks,mutants,originals);
    return 0;
}
