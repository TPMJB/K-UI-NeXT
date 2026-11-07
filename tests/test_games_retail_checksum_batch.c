/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual private launch checksum helpers and common reader. */
#include "kui/game_image.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned ui_allocations,ui_releases,ui_live,deny_ui;
static size_t ui_peak;
static void *checksum_malloc(size_t bytes) {
    ++ui_allocations;if(bytes>ui_peak)ui_peak=bytes;
    if(deny_ui)return NULL;
    void *p=malloc(bytes);if(p)++ui_live;return p;
}
static void checksum_free(void *p) {if(p){assert(ui_live);--ui_live;++ui_releases;}free(p);}
static enum kui_game_result checksum_image_read(const struct kui_game_image *,uint32_t,
    uint32_t,enum kui_game_sector_format,void *,size_t);
#define malloc checksum_malloc
#define free checksum_free
#define kui_game_image_read checksum_image_read
#include "../src/apps/games_retail.c"
#undef kui_game_image_read
#undef malloc
#undef free

/* Comparing the production callback identity retains its code under linker
 * GC. The generated image below never invokes that unrelated FatFs seam. */
bool kui_destination_name_valid(const char *name) {(void)name;assert(!"unexpected production file callback");return false;}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {(void)file;(void)path;(void)mode;assert(!"unexpected FatFs open");return FR_DISK_ERR;}
FRESULT f_close(FIL *file) {(void)file;assert(!"unexpected FatFs close");return FR_DISK_ERR;}
FRESULT f_lseek(FIL *file,FSIZE_t at) {(void)file;(void)at;assert(!"unexpected FatFs seek");return FR_DISK_ERR;}
FRESULT f_read(FIL *file,void *out,UINT bytes,UINT *got) {(void)file;(void)out;(void)bytes;(void)got;assert(!"unexpected FatFs read");return FR_DISK_ERR;}

#define SECTORS 257u
#define FIRST 45000u
#define OFFSET 497u
static uint8_t backing[SECTORS*2448u+OFFSET],logical[SECTORS*2048u];
static struct kui_game_image image;
static unsigned physical_calls,logical_calls,max_logical,max_physical,fail_at;
static enum kui_game_result failure;
static bool cancelled_now,transient_mode,cancel_after_mode;
static char diagnostic_log[160];
static uint8_t generated(unsigned sector,unsigned byte) {
    return (uint8_t)(sector*37u+byte*13u+(byte>>8)+11u);
}
static uint32_t reference_crc(const void *data,size_t bytes) {
    const uint8_t *p=data;uint32_t crc=UINT32_MAX;
    while(bytes--){crc^=*p++;for(unsigned i=0;i<8u;i++)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);}
    return ~crc;
}
static enum kui_game_result file_stat(void *ctx,const char *name,uint64_t *bytes) {
    (void)ctx;(void)name;(void)bytes;assert(!"checksum must not restat the source");return KUI_GAME_IO;
}
static enum kui_game_result file_read(void *ctx,const char *name,uint64_t offset,void *out,size_t bytes) {
    assert(ctx==&image && !strcmp(name,"generated.bin") && bytes && offset>=OFFSET && offset<=sizeof(backing) && bytes<=sizeof(backing)-offset);
    if(cancelled_now)return KUI_GAME_CANCELLED;
    ++physical_calls;if(bytes>max_physical)max_physical=(unsigned)bytes;
    if(fail_at && physical_calls==fail_at){memset(out,0xddu,bytes);return failure;}
    memcpy(out,backing+offset,bytes);
    if(transient_mode && physical_calls==2u) {
        assert(image.tracks[0].sector_bytes==2352u && bytes>=2352u);
        ((uint8_t *)out)[15]=0u;
        if(cancel_after_mode)cancelled_now=true;
    }
    return KUI_GAME_OK;
}
static enum kui_game_result checksum_image_read(const struct kui_game_image *source,uint32_t lba,
    uint32_t count,enum kui_game_sector_format format,void *out,size_t bytes) {
    ++logical_calls;if(count>max_logical)max_logical=count;
    assert(count<=16u && bytes==(size_t)count*2048u && format==KUI_GAME_SECTOR_MODE1);
    return kui_game_image_read(source,lba,count,format,out,bytes);
}
static void fixture(unsigned stride,unsigned mode,unsigned sectors,enum kui_game_image_format format) {
    assert(!ui_live);ui_allocations=ui_releases=deny_ui=0u;ui_peak=0u;
    physical_calls=logical_calls=max_logical=max_physical=fail_at=0u;
    failure=KUI_GAME_IO;cancelled_now=transient_mode=cancel_after_mode=false;
    memset(backing,0xe3u,sizeof(backing));memset(&image,0,sizeof(image));
    image.count=1u;image.format=format;image.data_lba=FIRST;
    image.files=(struct kui_game_file_ops){&image,file_stat,file_read};
    struct kui_game_image_track *t=&image.tracks[0];
    *t=(struct kui_game_image_track){.start_lba=FIRST,.end_lba=FIRST+sectors,.number=1u,
        .control=4u,.sector_bytes=stride,.sector_mode=mode,.file_bytes=sizeof(backing),.file_offset=OFFSET,
        .data_offset=stride==2048u?0u:mode==1u?16u:stride==2336u?8u:24u};
    strcpy(t->name,"generated.bin");
    for(unsigned n=0;n<sectors;n++) {
        uint8_t *raw=backing+OFFSET+(size_t)n*stride;
        if(stride!=2048u && stride!=2336u){raw[0]=raw[11]=0u;memset(raw+1u,255u,10u);raw[15]=(uint8_t)mode;}
        if(mode==2u){uint8_t *sub=raw+(stride==2336u?0u:16u);const uint8_t xa[4]={1u,7u,8u,3u};memcpy(sub,xa,4u);memcpy(sub+4u,xa,4u);}
        for(unsigned b=0;b<2048u;b++)logical[n*2048u+b]=raw[t->data_offset+b]=generated(n,b);
    }
}
static void reset_calls(void) {physical_calls=logical_calls=max_logical=max_physical=0u;}
static void exact_matrix(void) {
    static const unsigned counts[]={1u,2u,15u,16u,17u,33u,257u};
    static const unsigned layouts[][2]={{2048u,1u},{2352u,1u},{2352u,2u},{2336u,2u},{2448u,1u},{2448u,2u}};
    for(unsigned layout=0;layout<6u;layout++)for(unsigned format=0;format<5u;format++)
    for(unsigned size=0;size<7u;size++)for(unsigned tail=0;tail<2u;tail++) {
        unsigned count=counts[size],bytes=count*2048u-(tail?1001u:0u),calls=(count+15u)/16u;
        fixture(layouts[layout][0],layouts[layout][1],count,(enum kui_game_image_format)format);
        uint32_t crc=0u,failed=0u;uint8_t actual[32],expected[32];
        struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,logical,bytes);kui_sha256_digest(&hash,expected);
        assert(extent_crc(&image,FIRST,bytes,&crc,&failed)==KUI_GAME_OK && crc==reference_crc(logical,bytes));
        assert(logical_calls==calls && physical_calls==calls && max_logical<=16u && max_physical<=16u*layouts[layout][0] && !ui_live);
        reset_calls();crc=0u;
        assert(extent_sha_crc(&image,FIRST,bytes,actual,&crc)==KUI_GAME_OK && !memcmp(actual,expected,32u) && crc==reference_crc(logical,bytes));
        assert(logical_calls==calls && physical_calls==calls && max_logical<=16u && !ui_live);
        reset_calls();assert(extent_sha_crc(&image,FIRST,bytes,actual,NULL)==KUI_GAME_OK && !memcmp(actual,expected,32u));
        assert(physical_calls==calls && !ui_live && ui_peak<=32768u && ui_allocations==ui_releases);
    }
    puts("Actual launcher checksum matrix PASS:6 physical layouts x5 container labels x7 sizes x2 tails;257 sectors use17 reads,33 use3");
}
static void fallback_and_preflight(void) {
    for(unsigned cooked=0;cooked<2u;cooked++) {
        fixture(cooked?2048u:2352u,1u,33u,KUI_GAME_IMAGE_GDI);deny_ui=1u;
        uint32_t crc=0u;uint8_t digest[32];
        assert(!extent_crc(&image,FIRST,66537u,&crc,NULL) && crc==reference_crc(logical,66537u));
        assert(physical_calls==33u && max_logical==1u && !ui_live && ui_allocations==1u && !ui_releases);
        reset_calls();assert(!extent_sha_crc(&image,FIRST,66537u,digest,&crc) && physical_calls==33u && max_logical==1u && !ui_live);
    }
    fixture(2352u,1u,33u,KUI_GAME_IMAGE_GDI);uint32_t crc=0x12345678u;uint8_t digest[32];memset(digest,0x77u,32u);
    assert(extent_crc(&image,FIRST,0u,&crc,NULL)==KUI_GAME_INVALID);
    assert(extent_crc(&image,FIRST,1u,NULL,NULL)==KUI_GAME_INVALID);
    assert(extent_sha_crc(&image,FIRST,1u,NULL,&crc)==KUI_GAME_INVALID);
    assert(extent_crc(&image,FIRST,UINT32_MAX,&crc,NULL)==KUI_GAME_RANGE);
    assert(extent_sha_crc(&image,FIRST,UINT32_MAX,digest,&crc)==KUI_GAME_RANGE);
    assert(extent_crc(&image,FIRST+32u,4096u,&crc,NULL)==KUI_GAME_RANGE);
    assert(!physical_calls && !logical_calls && !ui_allocations && crc==0x12345678u);
}
static void logger(const char *format,...) {snprintf(diagnostic_log,sizeof(diagnostic_log),"%s",format);}
static void failure_guards(void) {
    for(unsigned kind=0;kind<2u;kind++)for(unsigned at=1u;at<=3u;at++) {
        fixture(2352u,1u,33u,KUI_GAME_IMAGE_GDI);failure=kind?KUI_GAME_CANCELLED:KUI_GAME_IO;fail_at=at;
        uint32_t crc=0x12345678u,failed=0u;uint8_t digest[32];memset(digest,0x77u,32u);
        assert(extent_crc(&image,FIRST,66537u,&crc,&failed)==failure && physical_calls==at && crc==0x12345678u && !ui_live);
        assert(failed==(at==3u?FIRST+32u:UINT32_MAX));
        reset_calls();assert(extent_sha_crc(&image,FIRST,66537u,digest,&crc)==failure && physical_calls==at && crc==0x12345678u && !ui_live);
        for(unsigned b=0;b<32u;b++)assert(digest[b]==0x77u);
    }
    fixture(2352u,1u,33u,KUI_GAME_IMAGE_GDI);backing[OFFSET+17u*2352u+15u]=0u;
    uint32_t crc=0x12345678u,failed=0u;
    assert(extent_crc(&image,FIRST,66537u,&crc,&failed)==KUI_GAME_MODE && failed==FIRST+17u && crc==0x12345678u && !ui_live);
    fixture(2352u,1u,33u,KUI_GAME_IMAGE_GDI);transient_mode=true;
    assert(extent_crc(&image,FIRST,66537u,&crc,&failed)==KUI_GAME_MODE && failed==UINT32_MAX && !ui_live);
    diagnostic_log[0]=0;log_bad_sector(&image,failed,logger);assert(strstr(diagnostic_log,"no single failing LBA"));
    fixture(2352u,1u,33u,KUI_GAME_IMAGE_GDI);transient_mode=cancel_after_mode=true;
    assert(extent_crc(&image,FIRST,66537u,&crc,&failed)==KUI_GAME_MODE && failed==UINT32_MAX && physical_calls==2u && !ui_live);
    puts("Actual checksum guards PASS: exact partial tails, complete preflight,2KiB OOM fallback,cancel/I/O refusal,repeatable and transient mode diagnostics");
}
int main(void) {exact_matrix();fallback_and_preflight();failure_guards();return 0;}
