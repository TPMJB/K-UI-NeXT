/* SPDX-License-Identifier: GPL-3.0-only */
/* An independent FAT32 sector disk, with real FatFs allocation and directory
 * handling. Only the removable storage device is simulated. */
#include "kui/vmu_collection.h"
#include "kui/media.h"
#include "kui/hash.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESERVED 32u
#define FAT_SECTORS 513u
#define CLUSTERS 65536u
#define SECTORS (RESERVED + FAT_SECTORS + CLUSTERS)
#define SECTOR_BYTES 512u
#define DISK_BYTES ((size_t)SECTORS * SECTOR_BYTES)
#define ROOT "0:" KUI_VMU_COLLECTION_ROOT

static uint8_t *cache,*durable,*seed;
static uint8_t dirty[SECTORS];
static uint8_t source[KUI_VMU_IMAGE_BYTES],source_before[KUI_VMU_IMAGE_BYTES];
static uint8_t scratch[KUI_VMU_IMAGE_BYTES + 1u];
static FATFS filesystem;
static unsigned reads,writes,syncs,mutations,fail_at,fail_read_at,cancel_at,cancel_calls;
static bool offline,publishing,race_armed,race_injected,marker_opened;
static const char preserved[]="Existing user data remains byte-exact";
static const char *const leaves[]={"card.vmd","card.journal","identity.bin","card.vmd.crc","ready.bin"};
static const size_t lengths[]={KUI_VMU_IMAGE_BYTES,1024u,16u,32u,512u};

static void put16(uint8_t *p,unsigned value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);
}
static void put32(uint8_t *p,uint32_t value) {
    put16(p,value);put16(p+2u,value>>16);
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static bool zero(const void *bytes,size_t length) {
    const uint8_t *p=bytes;
    for(size_t i=0;i<length;i++) if(p[i]) return false;
    return true;
}
static void quiet(const char *format,...) {(void)format;}
static bool cancelled(void) {return cancel_at && ++cancel_calls>=cancel_at;}
static uint64_t blocks(void *context) {assert(context==cache);return SECTORS;}
static int read_disk(void *context,uint32_t sector,size_t count,uint8_t *out) {
    assert(context==cache && sector<SECTORS && count<=SECTORS-sector);
    ++reads;if(offline || (fail_read_at && reads==fail_read_at)) {offline=true;return -1;}
    memcpy(out,cache+(size_t)sector*512u,count*512u);return 0;
}
static bool cut(void) {
    ++mutations;
    if(fail_at && mutations==fail_at) {offline=true;return true;}
    return false;
}
static int write_disk(void *context,uint32_t sector,size_t count,const uint8_t *bytes) {
    assert(context==cache && sector<SECTORS && count<=SECTORS-sector);
    ++writes;if(offline || cut()) return -1;
    memcpy(cache+(size_t)sector*512u,bytes,count*512u);
    memset(dirty+sector,1,count);return 0;
}
static int sync_disk(void *context) {
    assert(context==cache);++syncs;if(offline || cut()) return -1;
    for(unsigned sector=0;sector<SECTORS;sector++) if(dirty[sector]) {
        memcpy(durable+(size_t)sector*512u,cache+(size_t)sector*512u,512u);
        dirty[sector]=0;
    }
    return 0;
}
static struct kui_media_ops media;

static void path_for(char out[192],unsigned id,const char *leaf) {
    int n=snprintf(out,192,ROOT "/v%04u/%s",id,leaf);assert(n>0 && n<192);
}
static void write_file(const char *path,const void *bytes,size_t length) {
    FIL file;UINT done;
    assert(f_open(&file,path,FA_CREATE_NEW|FA_WRITE)==FR_OK);
    assert(f_write(&file,bytes,(UINT)length,&done)==FR_OK && done==length);
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void read_file(const char *path,void *out,size_t length) {
    FIL file;UINT done;
    assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==length);
    assert(f_read(&file,out,(UINT)length,&done)==FR_OK && done==length);
    assert(f_close(&file)==FR_OK);
}
static void equal_file(const char *path,const void *expected,size_t length) {
    assert(length<=sizeof(scratch));read_file(path,scratch,length);
    assert(!memcmp(scratch,expected,length));
}
static void rewrite_file(const char *path,const void *bytes,size_t length) {
    FIL file;UINT done;
    assert(f_open(&file,path,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    assert(f_write(&file,bytes,(UINT)length,&done)==FR_OK && done==length);
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}

/* The wrappers exercise a real directory collision between the publisher's
 * candidate selection and mkdir, and enforce exclusive file creation. */
FRESULT __real_f_mkdir(const TCHAR *path);
FRESULT __real_f_open(FIL *file,const TCHAR *path,BYTE mode);
FRESULT __wrap_f_mkdir(const TCHAR *path) {
    size_t n=strlen(path);
    if(race_armed && n>=6u && !strcmp(path+n-6u,"/v0001")) {
        race_armed=false;race_injected=true;
        assert(__real_f_mkdir(path)==FR_OK);
        write_file(ROOT "/v0001/keep.bin",preserved,sizeof(preserved));
        return FR_EXIST;
    }
    return __real_f_mkdir(path);
}
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE mode) {
    if(publishing && (mode&FA_WRITE) && strstr(path,ROOT "/v")) {
        assert(mode&FA_CREATE_NEW);
        assert(!(mode&FA_CREATE_ALWAYS));
        if(strstr(path,"/ready.bin")) marker_opened=true;
        else assert(!marker_opened);
    }
    return __real_f_open(file,path,mode);
}

static void mount_disk(void) {
    kui_media_set(&media);assert(f_mount(&filesystem,"0:",1)==FR_OK);
    assert(filesystem.fs_type==FS_FAT32);
}
static void recover_disk(void) {
    publishing=false;assert(f_mount(NULL,"0:",0)==FR_OK);
    memcpy(cache,durable,DISK_BYTES);memset(dirty,0,sizeof(dirty));
    offline=false;fail_at=fail_read_at=cancel_at=cancel_calls=0;mount_disk();
}
static void reset_disk(void) {
    publishing=false;assert(f_mount(NULL,"0:",0)==FR_OK);
    memcpy(cache,seed,DISK_BYTES);memcpy(durable,seed,DISK_BYTES);
    memset(dirty,0,sizeof(dirty));offline=false;
    reads=writes=syncs=mutations=fail_at=fail_read_at=cancel_at=cancel_calls=0;
    race_armed=race_injected=marker_opened=false;mount_disk();
}
static void format_disk(void) {
    memset(cache,0,DISK_BYTES);
    uint8_t *b=cache;
    b[0]=0xebu;b[1]=0x58u;b[2]=0x90u;memcpy(b+3u,"MSDOS5.0",8u);
    put16(b+11u,512u);b[13]=1u;put16(b+14u,RESERVED);b[16]=1u;b[21]=0xf8u;
    put16(b+24u,63u);put16(b+26u,255u);put32(b+32u,SECTORS);
    put32(b+36u,FAT_SECTORS);put32(b+44u,2u);put16(b+48u,1u);put16(b+50u,6u);
    b[64]=0x80u;b[66]=0x29u;put32(b+67u,0x11223344u);
    memcpy(b+71u,"VMU TEST   ",11u);memcpy(b+82u,"FAT32   ",8u);put16(b+510u,0xaa55u);
    memcpy(cache+6u*512u,b,512u);
    b=cache+512u;put32(b,0x41615252u);put32(b+484u,0x61417272u);
    put32(b+488u,CLUSTERS-1u);put32(b+492u,3u);put32(b+508u,0xaa550000u);
    memcpy(cache+7u*512u,b,512u);
    b=cache+RESERVED*512u;put32(b,0x0ffffff8u);put32(b+4u,0x0fffffffu);put32(b+8u,0x0fffffffu);
    memcpy(durable,cache,DISK_BYTES);mount_disk();
    write_file("0:/keep.bin",preserved,sizeof(preserved));
    memcpy(seed,durable,DISK_BYTES);
}
static void make_source(bool populated) {
    for(unsigned block=0;block<256u;block++)
        assert(!kui_vmu_image_blank_block((uint16_t)block,source+block*512u));
    /* Nonzero free bytes and opaque metadata must survive publication. */
    for(unsigned i=0;i<512u;i++) source[37u*512u+i]=(uint8_t)(i*17u+3u);
    source[255u*512u+64u]=0xa7u;
    if(populated) {
        uint8_t *entry=source+253u*512u;entry[0]=0x33u;entry[1]=0xffu;
        put16(entry+2u,199u);memcpy(entry+4u,"COLLECT",7u);put16(entry+24u,2u);
        put16(source+254u*512u+199u*2u,17u);put16(source+254u*512u+17u*2u,0xfffau);
        for(unsigned i=0;i<512u;i++) {
            source[199u*512u+i]=(uint8_t)(i*7u+1u);
            source[17u*512u+i]=(uint8_t)(i*13u+9u);
        }
    }
    memcpy(source_before,source,sizeof(source));
}
static int publish(const char *label,struct kui_vmu_collection_card *out) {
    marker_opened=false;publishing=true;
    int result=kui_vmu_collection_publish(source,sizeof(source),label,out,quiet,cancelled);
    publishing=false;assert(!memcmp(source,source_before,sizeof(source)));return result;
}
static void expect_zero_card(const struct kui_vmu_collection_card *out) {
    assert(zero(out,sizeof(*out)));
}
static void expect_page(unsigned page,unsigned count,unsigned total) {
    struct kui_vmu_collection_page out;memset(&out,0xa5,sizeof(out));
    assert(!kui_vmu_collection_list(page,&out,quiet,cancelled));
    if(out.page!=page || out.count!=count || out.total!=total)
        fprintf(stderr,"collection page %u: got page/count/total %u/%u/%u, expected %u/%u/%u\n",
            page,out.page,out.count,out.total,page,count,total);
    assert(out.page==page && out.count==count && out.total==total);
    for(unsigned i=1u;i<out.count;i++) assert(out.entries[i-1u].id<out.entries[i].id);
}
static void check_card(const struct kui_vmu_collection_card *out,unsigned files) {
    uint8_t bytes[1024],digest[32];char path[192];struct kui_sha256 hash;
    assert(out->id && out->id<=9999u);
    path_for(path,out->id,"card.vmd");assert(!strcmp(path,out->path));
    assert(kui_vmu_collection_path_id(out->path)==out->id);
    equal_file(out->path,source,sizeof(source));
    assert(out->summary.files==files && !out->summary.game_files);
    assert(out->summary.used_blocks==files*2u && out->summary.free_blocks==200u-files*2u);
    kui_sha256_init(&hash);kui_sha256_update(&hash,source,sizeof(source));kui_sha256_digest(&hash,digest);
    assert(!memcmp(digest,out->sha256,32u));
    path_for(path,out->id,"card.journal");read_file(path,bytes,1024u);assert(zero(bytes,1024u));
    path_for(path,out->id,"identity.bin");read_file(path,bytes,16u);assert(!zero(bytes,16u));
    path_for(path,out->id,"ready.bin");read_file(path,bytes,512u);
    assert(!memcmp(bytes,"KUIVMC1",8u));assert(get32(bytes+8u)==1u);
    assert(get32(bytes+12u)==out->id && get32(bytes+16u)==sizeof(source));
    assert(get32(bytes+20u)==files && get32(bytes+28u)==files*2u && get32(bytes+32u)==200u-files*2u);
    assert(!strcmp((char *)bytes+36u,out->label));assert(!memcmp(bytes+68u,digest,32u));
    assert(zero(bytes+100u,408u) && get32(bytes+508u)==kui_crc32(0,bytes,508u));
    struct kui_vmu_collection_card verified;memset(&verified,0xa5,sizeof(verified));
    assert(!kui_vmu_collection_verify(out->path,&verified,quiet,cancelled));
    assert(!memcmp(&verified,out,sizeof(verified)));
    equal_file("0:/keep.bin",preserved,sizeof(preserved));
}

static void test_empty_and_bad_input(void) {
    reset_disk();make_source(false);unsigned before=writes;expect_page(0,0,0);assert(writes==before);
    struct kui_vmu_collection_card out;FILINFO info;
    const size_t sizes[]={0u,1u,sizeof(source)-1u,sizeof(source)+1u};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        memset(&out,0xa5,sizeof(out));
        assert(kui_vmu_collection_publish(source,sizes[i],"Bad",&out,quiet,cancelled));expect_zero_card(&out);
    }
    memset(&out,0xa5,sizeof(out));assert(kui_vmu_collection_publish(NULL,sizeof(source),"Bad",&out,quiet,cancelled));
    expect_zero_card(&out);
    source[255u*512u]^=1u;memcpy(source_before,source,sizeof(source));
    memset(&out,0xa5,sizeof(out));assert(publish("Bad",&out));expect_zero_card(&out);
    assert(writes==before && f_stat(ROOT,&info)==FR_NO_PATH);
    equal_file("0:/keep.bin",preserved,sizeof(preserved));
}
static void test_distinct_and_pages(void) {
    reset_disk();make_source(true);struct kui_vmu_collection_card cards[10];
    for(unsigned i=0;i<10u;i++) {
        char label[16];snprintf(label,sizeof(label),"Snapshot %u",i+1u);
        assert(!publish(label,&cards[i]));assert(cards[i].id==i+1u);
        assert(!strcmp(cards[i].label,label));check_card(&cards[i],1u);
        if(i) assert(strcmp(cards[i-1u].path,cards[i].path));
    }
    uint8_t first_id[16],second_id[16];char path[192];
    path_for(path,1u,"identity.bin");read_file(path,first_id,16u);
    path_for(path,2u,"identity.bin");read_file(path,second_id,16u);assert(memcmp(first_id,second_id,16u));
    expect_page(0u,8u,10u);expect_page(1u,2u,10u);expect_page(2u,0u,10u);
    expect_page(9999u/KUI_VMU_COLLECTION_ROWS,0u,10u);
    struct kui_vmu_collection_page invalid;memset(&invalid,0xa5,sizeof(invalid));
    assert(kui_vmu_collection_list(UINT32_MAX,&invalid,quiet,cancelled)==KUI_VMU_COLLECTION_ARGUMENT);
    assert(zero(&invalid,sizeof(invalid)));
    recover_disk();expect_page(0u,8u,10u);
    for(unsigned i=0;i<10u;i++) check_card(&cards[i],1u);
}
static void test_labels_and_aliases(void) {
    reset_disk();make_source(false);struct kui_vmu_collection_card out;
    assert(!publish(NULL,&out));assert(!strcmp(out.label,"Virtual card"));check_card(&out,0u);
    assert(!publish("../Bad\nName/and-too-long",&out));assert(!strcmp(out.label,"../Bad_Name/and"));check_card(&out,0u);
    unsigned before=writes;
    assert(kui_vmu_collection_publish(source,sizeof(source),"Alias",
        (struct kui_vmu_collection_card *)(void *)source,quiet,cancelled)==KUI_VMU_COLLECTION_ARGUMENT);
    assert(!memcmp(source,source_before,sizeof(source)) && writes==before);
    assert(kui_vmu_collection_publish(source,sizeof(source),"No output",NULL,quiet,cancelled)==KUI_VMU_COLLECTION_ARGUMENT);
    assert(kui_vmu_collection_list(0u,NULL,quiet,cancelled)==KUI_VMU_COLLECTION_ARGUMENT);
    assert(kui_vmu_collection_verify(out.path,NULL,quiet,cancelled)==KUI_VMU_COLLECTION_ARGUMENT);
}
static void test_canonical_paths(void) {
    const char *bad[]={NULL,"","/KUI/vmu/cards/v0001/card.vmd","1:/KUI/vmu/cards/v0001/card.vmd",
        "0:/KUI/vmu/cards/v0000/card.vmd","0:/KUI/vmu/cards/v00001/card.vmd",
        "0:/KUI/vmu/cards/v10000/card.vmd","0:/KUI/vmu/cards/v0001x/card.vmd",
        "0:/KUI/vmu/cards/v0001/../v0002/card.vmd","0:/KUI/vmu/cards/v0001/./card.vmd",
        "0:/KUI/vmu/cards/v0001//card.vmd","0:/KUI/vmu/cards/v0001/card.vmd/",
        "0:/KUI/vmu/cards/v0001/card.vmd.extra","0:/KUI/vmu/cards/v0001/card.VMD",
        "0:/kui/vmu/cards/v0001/card.vmd","0:/KUI/vmu-test/p0001/card.vmd"};
    reset_disk();make_source(false);struct kui_vmu_collection_card card,out;assert(!publish("Paths",&card));
    unsigned before=reads;
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        assert(!kui_vmu_collection_path_id(bad[i]));memset(&out,0xa5,sizeof(out));
        assert(kui_vmu_collection_verify(bad[i],&out,quiet,cancelled));expect_zero_card(&out);
    }
    assert(reads==before);check_card(&card,0u);
}
static void copy_pair(unsigned from,const char *folder) {
    assert(f_mkdir(folder)==FR_OK);
    for(unsigned i=0;i<5u;i++) {
        char old[192],path[192];path_for(old,from,leaves[i]);read_file(old,scratch,lengths[i]);
        int n=snprintf(path,sizeof(path),"%s/%s",folder,leaves[i]);assert(n>0 && n<(int)sizeof(path));
        write_file(path,scratch,lengths[i]);
    }
}
static void test_incomplete_and_foreign(void) {
    reset_disk();make_source(false);struct kui_vmu_collection_card card;assert(!publish("Kept",&card));
    assert(f_mkdir(ROOT "/v0002")==FR_OK);
    write_file(ROOT "/v0002/card.vmd",source,sizeof(source));
    copy_pair(1u,ROOT "/v0003"); /* valid files with a mismatched manifest ID */
    copy_pair(1u,ROOT "/v0001evil");copy_pair(1u,ROOT "/v00001");
    copy_pair(1u,ROOT "/p0001");copy_pair(1u,ROOT "/v0000");
    expect_page(0u,1u,1u);assert(!publish("Next",&card));assert(card.id==4u);check_card(&card,0u);
    equal_file(ROOT "/v0002/card.vmd",source,sizeof(source));expect_page(0u,2u,2u);
}
static void test_directory_race(void) {
    reset_disk();make_source(false);struct kui_vmu_collection_card out;
    race_armed=true;assert(!publish("Raced",&out));assert(race_injected && out.id==2u);
    equal_file(ROOT "/v0001/keep.bin",preserved,sizeof(preserved));check_card(&out,0u);expect_page(0u,1u,1u);
}

static void rejected_card(const char *path) {
    struct kui_vmu_collection_card out;memset(&out,0xa5,sizeof(out));
    assert(kui_vmu_collection_verify(path,&out,quiet,cancelled));expect_zero_card(&out);
    unsigned before=writes;expect_page(0u,0u,0u);assert(writes==before);
    equal_file("0:/keep.bin",preserved,sizeof(preserved));
}
static void test_artifact_corruption(void) {
    /* Each artifact must have the exact length, and its complete contents are
     * bound to the manifest or checked against the pair format. */
    for(unsigned file=0;file<5u;file++) for(unsigned mode=0;mode<4u;mode++) {
        reset_disk();make_source(true);struct kui_vmu_collection_card out;assert(!publish("Corrupt",&out));
        char path[192];path_for(path,1u,leaves[file]);read_file(path,scratch,lengths[file]);
        if(mode==0u) scratch[lengths[file]/2u]^=0x80u;
        if(mode==1u) scratch[lengths[file]-1u]^=1u;
        size_t length=lengths[file];if(mode==2u) --length;
        if(mode==3u) scratch[length++]=0u;
        rewrite_file(path,scratch,length);rejected_card(out.path);
    }
    /* Semantically invalid markers retain a valid CRC, so these checks cannot
     * be satisfied by CRC-only admission. */
    const unsigned positions[]={0u,8u,12u,16u,20u,24u,28u,32u,36u,51u,52u,68u,100u,507u};
    for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);i++) {
        reset_disk();make_source(true);struct kui_vmu_collection_card out;assert(!publish("Semantic",&out));
        char path[192];path_for(path,1u,"ready.bin");read_file(path,scratch,512u);
        scratch[positions[i]]^=0x80u;put32(scratch+508u,kui_crc32(0,scratch,508u));
        rewrite_file(path,scratch,512u);rejected_card(out.path);
    }
    for(unsigned file=0;file<5u;file++) {
        reset_disk();make_source(false);struct kui_vmu_collection_card out;assert(!publish("Missing",&out));
        char path[192];path_for(path,1u,leaves[file]);assert(f_unlink(path)==FR_OK);rejected_card(out.path);
    }
}
static void test_cancelled(void) {
    reset_disk();make_source(true);struct kui_vmu_collection_card out;
    cancel_at=1u;memset(&out,0xa5,sizeof(out));assert(publish("Cancel",&out)==KUI_VMU_COLLECTION_CANCELLED);
    expect_zero_card(&out);assert(!writes);cancel_at=0u;expect_page(0u,0u,0u);
    /* Locate cancellation checks throughout writing and verification using a
     * successful call's observed callback count. */
    reset_disk();make_source(true);cancel_at=UINT32_MAX;assert(!publish("Count",&out));
    unsigned checks=cancel_calls;assert(checks>8u);
    const unsigned fractions[]={1u,2u,3u,4u,5u,6u,7u,8u};
    for(unsigned i=0;i<8u;i++) {
        reset_disk();make_source(true);cancel_at=(checks*fractions[i])/9u;
        memset(&out,0xa5,sizeof(out));assert(publish("Cancel",&out)==KUI_VMU_COLLECTION_CANCELLED);
        expect_zero_card(&out);cancel_at=0u;expect_page(0u,0u,0u);
        equal_file("0:/keep.bin",preserved,sizeof(preserved));
    }
    reset_disk();make_source(true);assert(!publish("List stop",&out));
    struct kui_vmu_collection_page page;memset(&page,0xa5,sizeof(page));cancel_at=1u;cancel_calls=0u;
    assert(kui_vmu_collection_list(7u,&page,quiet,cancelled)==KUI_VMU_COLLECTION_CANCELLED);
    assert(!page.count && !page.total && zero(page.entries,sizeof(page.entries)));
}
static void test_list_io_failure(void) {
    for(unsigned fraction=1u;fraction<9u;fraction++) {
        reset_disk();make_source(true);struct kui_vmu_collection_card out;
        assert(!publish("First",&out));assert(!publish("Second",&out));
        reads=0u;expect_page(0u,2u,2u);unsigned total_reads=reads;assert(total_reads>16u);
        recover_disk();reads=0u;fail_read_at=total_reads*fraction/9u;
        struct kui_vmu_collection_page page;memset(&page,0xa5,sizeof(page));
        assert(kui_vmu_collection_list(0u,&page,quiet,cancelled)==KUI_VMU_COLLECTION_IO);
        assert(offline && zero(&page,sizeof(page)));recover_disk();expect_page(0u,2u,2u);
    }
}
static void test_io_cuts(void) {
    reset_disk();make_source(true);struct kui_vmu_collection_card out;assert(!publish("Count",&out));
    unsigned events=mutations;assert(events>8u);
    /* Every write or durability boundary is cut once. Reconnect discards
     * unsynced sectors, just as a removed card does. Any surviving visible
     * snapshot must pass the full independent byte/content checks. */
    unsigned failed=0u;
    for(unsigned event=1u;event<=events;event++) {
        reset_disk();make_source(true);fail_at=event;memset(&out,0xa5,sizeof(out));
        int result=publish("Count",&out);
        assert(result && offline);expect_zero_card(&out);++failed;recover_disk();
        struct kui_vmu_collection_page page;assert(!kui_vmu_collection_list(0u,&page,quiet,cancelled));
        assert(page.total<=1u && page.count==page.total);
        if(page.count) check_card(&page.entries[0],1u);
        equal_file("0:/keep.bin",preserved,sizeof(preserved));
    }
    assert(failed==events);printf("PASS collection I/O cuts: %u write/sync boundaries\n",events);
}

int main(void) {
    cache=calloc(1,DISK_BYTES);durable=calloc(1,DISK_BYTES);seed=calloc(1,DISK_BYTES);
    assert(cache && durable && seed);media=(struct kui_media_ops){cache,blocks,read_disk,write_disk,sync_disk};
    format_disk();
    test_empty_and_bad_input();test_distinct_and_pages();test_labels_and_aliases();test_canonical_paths();
    test_incomplete_and_foreign();test_directory_race();test_artifact_corruption();
    test_cancelled();test_list_io_failure();test_io_cuts();
    assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);free(seed);free(durable);free(cache);
    puts("PASS VMU collection: immutable real-FatFs snapshots, strict admission, races, corruption, cancellation, durability");
    return 0;
}
