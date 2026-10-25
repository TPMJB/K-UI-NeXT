/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual frontend include, actual seed/cache codecs and detector; bounded
 * fake file/image I/O injects failures at publication boundaries. */
#define main resolution_cache_codec_main
#include "test_vmu_native_resolution_cache.c"
#undef main
#define KUI_PROBE_H
typedef void (*kui_log_fn)(const char *,...);
typedef bool (*kui_cancel_fn)(void);
#include "kui/runtime.h"
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"
#include "kui/vmu_native_cache_seed.h"
#include "kui/native_dual_reader.h"

typedef unsigned UINT;
typedef unsigned FRESULT;
enum {FR_OK,FR_NO_FILE,FR_NO_PATH,FR_EXIST,FR_DISK_ERR,FR_DENIED};
enum {AM_DIR=16u,FA_READ=1u,FA_WRITE=2u,FA_CREATE_NEW=4u};
typedef struct {size_t fsize;unsigned fattrib;} FILINFO;
typedef struct {unsigned node;size_t at;} FIL;
struct fake_node {char path[128];uint8_t data[1024];size_t size;bool directory,exists;};
static struct fake_node disk[20];
static struct {
    const uint8_t *boot;size_t bytes;unsigned reads,opens,closed,writes,syncs,renames,unlinks,builds,replays,logs;
    unsigned cancel_checks,cancel_at,allocation_at,allocations;
    unsigned fail,cancel_when;bool cancelled;
} io;
enum {FAIL_NONE,FAIL_OPEN,FAIL_WRITE,FAIL_SHORT,FAIL_SYNC,FAIL_CLOSE,FAIL_READBACK,FAIL_RENAME,FAIL_MKDIR,
    FAIL_BOOT_READ,FAIL_BOOT_CANCEL};
static int node_find(const char *path) {for(unsigned i=0;i<20u;i++) if(disk[i].exists && !strcmp(disk[i].path,path)) return (int)i;return -1;}
static unsigned node_add(const char *path,bool directory) {
    int existing=node_find(path);if(existing>=0) return (unsigned)existing;
    for(unsigned i=0;i<20u;i++) if(!disk[i].exists) {
        memset(&disk[i],0,sizeof(disk[i]));CHECK(strlen(path)<sizeof(disk[i].path));
        strcpy(disk[i].path,path);disk[i].exists=true;disk[i].directory=directory;return i;
    }
    CHECK(false);return 0;
}
static FRESULT f_stat(const char *path,FILINFO *out) {
    int n=node_find(path);if(n<0) return FR_NO_FILE;
    *out=(FILINFO){disk[n].size,disk[n].directory?AM_DIR:0u};return FR_OK;
}
static FRESULT f_open(FIL *f,const char *path,unsigned flags) {
    if((flags&FA_WRITE) && io.fail==FAIL_OPEN) return FR_DENIED;
    int n=node_find(path);
    if(flags&FA_CREATE_NEW) {if(n>=0) return FR_EXIST;n=(int)node_add(path,false);}
    if(n<0 || disk[n].directory) return FR_NO_FILE;
    *f=(FIL){(unsigned)n,0};++io.opens;
    if((flags&FA_WRITE) && io.cancel_when==2u) io.cancelled=true;
    return FR_OK;
}
static FRESULT f_read(FIL *f,void *out,UINT size,UINT *got) {
    if(io.fail==FAIL_READBACK && strstr(disk[f->node].path,".tmp")) {*got=0;return FR_DISK_ERR;}
    if(strstr(disk[f->node].path,".tmp") && io.cancel_when==6u) io.cancelled=true;
    size_t take=disk[f->node].size-f->at;if(take>size) take=size;
    memcpy(out,disk[f->node].data+f->at,take);f->at+=take;*got=(UINT)take;return FR_OK;
}
static FRESULT f_write(FIL *f,const void *in,UINT size,UINT *put) {
    ++io.writes;CHECK(strstr(disk[f->node].path,".tmp"));CHECK(f->at+size<=sizeof(disk[0].data));
    if(io.cancel_when==3u) io.cancelled=true;
    if(io.fail==FAIL_WRITE) {*put=0;return FR_DISK_ERR;}
    UINT take=io.fail==FAIL_SHORT?size-1u:size;
    memcpy(disk[f->node].data+f->at,in,take);f->at+=take;disk[f->node].size=f->at;*put=take;return FR_OK;
}
static FRESULT f_sync(FIL *f) {
    (void)f;++io.syncs;if(io.cancel_when==4u) io.cancelled=true;
    return io.fail==FAIL_SYNC?FR_DISK_ERR:FR_OK;
}
static FRESULT f_close(FIL *f) {
    ++io.closed;
    if(strstr(disk[f->node].path,".tmp") && io.cancel_when==5u) io.cancelled=true;
    return io.fail==FAIL_CLOSE && strstr(disk[f->node].path,".tmp")?FR_DISK_ERR:FR_OK;
}
static FRESULT f_mkdir(const char *path) {
    if(io.cancel_when==1u) io.cancelled=true;
    if(io.fail==FAIL_MKDIR) return FR_DENIED;
    if(node_find(path)>=0) return FR_EXIST;
    (void)node_add(path,true);return FR_OK;
}
static FRESULT f_unlink(const char *path) {
    ++io.unlinks;int n=node_find(path);if(n<0) return FR_NO_FILE;
    if(!strstr(path,".tmp") && io.cancel_when==7u) io.cancelled=true;
    CHECK(!disk[n].directory);disk[n].exists=false;return FR_OK;
}
static FRESULT f_rename(const char *from,const char *to) {
    ++io.renames;if(io.fail==FAIL_RENAME) return FR_DISK_ERR;
    CHECK(node_find(to)<0);int n=node_find(from);CHECK(n>=0 && !disk[n].directory);
    CHECK(strstr(from,".tmp") && strstr(to,".nvr"));strcpy(disk[n].path,to);
    if(io.cancel_when==8u) io.cancelled=true;
    return FR_OK;
}
enum kui_game_result kui_game_image_read(const struct kui_game_image *image,uint32_t lba,
    unsigned count,enum kui_game_sector_format format,void *out,size_t capacity) {
    (void)image;CHECK(lba>=100u && count==1u && format==KUI_GAME_SECTOR_MODE1 && capacity==2048u);
    size_t at=(size_t)(lba-100u)*2048u;CHECK(at<io.bytes);++io.reads;
    if(io.fail==FAIL_BOOT_READ)return KUI_GAME_IO;
    if(io.fail==FAIL_BOOT_CANCEL)return KUI_GAME_CANCELLED;
    size_t n=io.bytes-at;if(n>2048u) n=2048u;memset(out,0,capacity);memcpy(out,io.boot+at,n);
    return KUI_GAME_OK;
}
static bool fixture_build(const void *image,size_t bytes,uint32_t address,void *wire,size_t wire_bytes,
    struct kui_vmu_native_resolution *out) {
    ++io.builds;return kui_vmu_native_resolution_cache_build(image,bytes,address,wire,wire_bytes,out);
}
static bool fixture_replay(const void *image,size_t bytes,uint32_t address,const void *wire,size_t wire_bytes,
    const uint8_t pin[32],struct kui_vmu_native_resolution *out) {
    ++io.replays;return kui_vmu_native_resolution_cache_replay(image,bytes,address,wire,wire_bytes,pin,out);
}
static void *fixture_malloc(size_t bytes) {
    if(++io.allocations==io.allocation_at) return NULL;
    return malloc(bytes);
}
#define kui_vmu_native_resolution_cache_build fixture_build
#define kui_vmu_native_resolution_cache_replay fixture_replay
#define malloc fixture_malloc
#include "../src/apps/games_native_cache.inc"
#undef malloc
#undef kui_vmu_native_resolution_cache_build
#undef kui_vmu_native_resolution_cache_replay

static struct kui_game_image game;
static struct kui_retail_manifest map;
static uint8_t module[4096],before[4096];
static struct kui_runtime_image package;
static struct native_cache_source_proof source_proof;
static bool cancelled(void) {
    if(io.cancel_at && ++io.cancel_checks==io.cancel_at) io.cancelled=true;
    return io.cancelled;
}
static void logger(const char *format,...) {CHECK(format);++io.logs;}
static void setup(const uint8_t *boot,size_t bytes) {
    memset(&io,0,sizeof(io));memset(disk,0,sizeof(disk));io.boot=boot;io.bytes=bytes;
    memset(&game,0,sizeof(game));game.format=KUI_GAME_IMAGE_GDI;
    memset(&map,0,sizeof(map));map.storage_transport=KUI_STORAGE_SCI;map.boot_lba=100u;map.boot_bytes=(uint32_t)bytes;
    memset(module,0x59,sizeof(module));kui_vmu_native_cache_seed_template(module+128u);
    memcpy(before,module,sizeof(module));package=(struct kui_runtime_image){.data=module,.info={.payload_bytes=sizeof(module)}};
}
static void cache_path(char out[120],const uint8_t *boot,size_t bytes) {
    uint8_t hash[32];digest(boot,bytes,hash);char hex[65];
    for(unsigned i=0;i<32u;i++) snprintf(hex+2u*i,3u,"%02x",hash[i]);
    CHECK(snprintf(out,120,"0:/KUI/VMU/native-cache/v%u/%s.nvr",KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH,hex)<120);
}
static unsigned seed_cache(const uint8_t *boot,size_t bytes,bool actual) {
    char path[120];cache_path(path,boot,bytes);unsigned n=node_add(path,false);
    if(actual) {
        size_t program_bytes;CHECK(kui_vmu_native_program_extent(boot,bytes,load,&program_bytes));
        CHECK(kui_vmu_native_resolution_cache_build(boot,program_bytes,load,disk[n].data,1024u,NULL));
    }
    else {struct kui_vmu_native_resolution source;mock(boot,bytes,&source);CHECK(kui_vmu_native_resolution_cache_encode(&source,disk[n].data,1024u));}
    disk[n].size=1024u;return n;
}
static enum native_cache_result prepare(bool native,uint32_t options,bool patches,const char *source) {
    source_proof=(struct native_cache_source_proof){true,UINT32_MAX};
    enum native_cache_result result=native_cache_prepare(&game,&map,native,options,patches,&package,source,logger,cancelled,&source_proof);
    CHECK(io.opens==io.closed);
    if(source_proof.complete) {
        CHECK(io.reads==(io.bytes+2047u)/2048u);
        CHECK(source_proof.crc32==kui_crc32(0u,io.boot,io.bytes));
    } else CHECK(!source_proof.crc32);
    if(result!=NATIVE_CACHE_READY) CHECK(!memcmp(module,before,sizeof(module)));
    else {
        CHECK(!memcmp(module,before,128u) && !memcmp(module+128u+1120u,before+128u+1120u,sizeof(module)-128u-1120u));
        const uint8_t *wire=NULL,*pin=NULL;struct kui_vmu_native_resolution out;
        CHECK(kui_vmu_native_cache_seed_read(module+128u,1120u,&wire,&pin));
        size_t program_bytes;CHECK(kui_vmu_native_program_extent(io.boot,io.bytes,load,&program_bytes));
        CHECK(kui_vmu_native_resolution_cache_replay(io.boot,program_bytes,load,wire,1024u,pin,&out));
        CHECK(out.driver.image_bytes==program_bytes);
    }
    return result;
}
static void gates_and_hits(void) {
    uint8_t boot[1024];for(unsigned i=0;i<sizeof(boot);i++) boot[i]=(uint8_t)(i*17u+1u);
    setup(boot,sizeof(boot));seed_cache(boot,sizeof(boot),false);
    CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_READY);
    CHECK(io.reads==1u && io.replays==1u && !io.builds && !io.writes && !io.renames);
    for(unsigned variant=0;variant<15u;variant++) {
        setup(boot,sizeof(boot));bool native=true,patched=false;uint32_t options=0;
        if(variant==0u) native=false; /* WinCE */
        if(variant==1u) options=1u; /* background */
        if(variant==2u) options=UINT32_C(0x20); /* music / other option */
        if(variant==3u) options=UINT32_C(0x40); /* forced encoding */
        if(variant==4u) patched=true;
        if(variant==5u) game.format=KUI_GAME_IMAGE_ZSO;
        if(variant==6u) game.compressed=true;
        if(variant==7u) game.scrambled=true;
        if(variant==8u) game.cd_image=true;
        if(variant==9u) map.storage_transport=KUI_STORAGE_SCIF;
        if(variant==10u) map.reader=KUI_RETAIL_READER_ASYNC;
        if(variant==11u) memset(module+128u,0,1120u);
        if(variant==12u) kui_vmu_native_cache_seed_template(module+2048u);
        if(variant==13u) module[128u+56u]=1u;
        if(variant==14u) memcpy(module+128u,"KUINVS01untyped coincidence",26u);
        memcpy(before,module,sizeof(module));
        CHECK(prepare(native,options,patched,"/Games/PS2")==NATIVE_CACHE_SKIPPED);
        CHECK(!io.reads && !io.opens && !io.writes && !io.builds && !io.replays);
    }
    for(unsigned allocation=1;allocation<=2u;allocation++) {
        setup(boot,sizeof(boot));io.allocation_at=allocation;
        CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_SKIPPED && !io.reads && !io.writes);
    }
    setup(boot,sizeof(boot));io.cancel_at=1u;
    CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_CANCELLED && !io.reads);
    setup(boot,sizeof(boot));seed_cache(boot,sizeof(boot),false);io.cancel_at=3u;
    CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_CANCELLED && !io.writes);
    setup(boot,sizeof(boot));CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_SKIPPED && io.builds==1u && !io.writes);
}
static void dual_gates_and_hits(void) {
    uint8_t boot[1024];for(unsigned i=0;i<sizeof(boot);i++)boot[i]=(uint8_t)(i*17u+1u);
    const struct kui_native_dual_capability cap={KUI_NATIVE_DUAL_MAGIC0,KUI_NATIVE_DUAL_MAGIC1,
        1u,KUI_NATIVE_DUAL_CAPABILITY_BYTES,KUI_RETAIL_READER_ASYNC,
        KUI_RETAIL_READER_ASYNC_EAGER,KUI_NATIVE_DUAL_LEASE_BYTES,0u};
    for(unsigned reader=KUI_RETAIL_READER_ASYNC;reader<=KUI_RETAIL_READER_ASYNC_EAGER;reader++) {
        setup(boot,sizeof(boot));map.reader=reader;memcpy(module+3072u,&cap,sizeof(cap));
        memcpy(before,module,sizeof(module));seed_cache(boot,sizeof(boot),false);
        CHECK(prepare(true,reader,false,"/Games/PS2")==NATIVE_CACHE_READY);
        CHECK(io.reads==1u && io.replays==1u && !io.builds && !io.writes);
        for(unsigned fault=0u;fault<16u;fault++) {
            setup(boot,sizeof(boot));map.reader=reader;memcpy(module+3072u,&cap,sizeof(cap));
            uint32_t options=reader;bool native=true,patched=false;
            if(fault==0u)memset(module+3072u,0,sizeof(cap));
            if(fault==1u)memcpy(module+3501u,&cap,sizeof(cap)); /* unaligned duplicate */
            if(fault>=2u && fault<10u)module[3072u+4u*(fault-2u)]^=1u;
            if(fault==10u)options=reader==1u?2u:1u;
            if(fault==11u)options|=0x20u;
            if(fault==12u)map.storage_transport=KUI_STORAGE_SCIF;
            if(fault==13u)native=false;
            if(fault==14u)patched=true;
            if(fault==15u)map.flags|=KUI_RETAIL_IMAGE_CD;
            memcpy(before,module,sizeof(module));
            CHECK(prepare(native,options,patched,"/Games/PS2")==NATIVE_CACHE_SKIPPED);
            CHECK(!io.reads && !io.opens && !io.writes && !io.builds && !io.replays);
        }
        setup(boot,sizeof(boot));map.reader=reader;memcpy(module+3072u,&cap,sizeof(cap));
        memcpy(before,module,sizeof(module));io.cancel_at=1u;
        CHECK(prepare(true,reader,false,"/Games/PS2")==NATIVE_CACHE_CANCELLED && !io.reads);
        for(unsigned fault=FAIL_BOOT_READ;fault<=FAIL_BOOT_CANCEL;fault++) {
            setup(boot,sizeof(boot));map.reader=reader;memcpy(module+3072u,&cap,sizeof(cap));
            memcpy(before,module,sizeof(module));io.fail=fault;
            CHECK(prepare(true,reader,false,"/Games/PS2")==
                (fault==FAIL_BOOT_CANCEL?NATIVE_CACHE_CANCELLED:NATIVE_CACHE_SKIPPED));
            CHECK(!source_proof.complete && !source_proof.crc32 && io.reads==1u && !io.builds);
        }
    }
    uint8_t tail[4097];for(unsigned i=0;i<sizeof(tail);i++)tail[i]=(uint8_t)(i*43u+3u);
    setup(tail,sizeof(tail));map.reader=KUI_RETAIL_READER_ASYNC;
    memcpy(module+3072u,&cap,sizeof(cap));memcpy(before,module,sizeof(module));
    CHECK(prepare(true,KUI_RETAIL_READER_ASYNC,false,"/Games/Unknown")==NATIVE_CACHE_SKIPPED);
    CHECK(source_proof.complete && io.reads==3u && io.builds==1u && !io.writes);
}
static void actual_frontend(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f && !fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>128 && length<=0xc00000);rewind(f);
    size_t bytes=(size_t)length;uint8_t *boot=malloc(bytes);CHECK(boot && fread(boot,1,bytes,f)==bytes && !fclose(f));
    setup(boot,bytes);CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_READY);
    CHECK(io.builds==1u && io.writes==1u && io.syncs==1u && io.renames==1u);
    char name[120];cache_path(name,boot,bytes);int stored=node_find(name);CHECK(stored>=0 && disk[stored].size==1024u);
    setup(boot,bytes);seed_cache(boot,bytes,true);
    CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_READY);
    CHECK(io.replays==1u && !io.builds && !io.writes && !io.renames);
    size_t program_bytes;CHECK(kui_vmu_native_program_extent(boot,bytes,load,&program_bytes));
    if(program_bytes!=bytes) {
        /* A cache cannot authorize a changed or malformed source footer. */
        uint8_t saved=boot[bytes-1u];boot[bytes-1u]^=1u;setup(boot,bytes);
        CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_SKIPPED);
        CHECK(!io.builds && !io.replays && !io.writes && !io.renames);
        boot[bytes-1u]=saved;
    }
    /* Corrupt, truncated, old epoch and other-image records all rebuild and
     * replace the stale name; successful next launch no longer rescans. */
    for(unsigned corruption=0;corruption<4u;corruption++) {
        setup(boot,bytes);unsigned n=seed_cache(boot,bytes,true);
        if(corruption==0u) disk[n].data[600]^=1u;
        if(corruption==1u) disk[n].size--;
        if(corruption==2u) {disk[n].data[16]++;seal(disk[n].data);}
        if(corruption==3u) {disk[n].data[32]^=1u;seal(disk[n].data);}
        CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_READY && io.builds==1u && io.renames==1u);
        CHECK(node_find(name)>=0);
    }
    for(unsigned failure=FAIL_OPEN;failure<=FAIL_MKDIR;failure++) {
        setup(boot,bytes);io.fail=failure;
        CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_READY && io.builds==1u);
        CHECK(node_find(name)<0); /* no partial canonical cache publication */
    }
    const char *conflicts[]={"/KUI/VMU","/kui/vmu/native-cache/v1","/KUI/VMU/NATIVE~1/V1"};
    for(unsigned i=0;i<sizeof(conflicts)/sizeof(conflicts[0]);i++) {
        setup(boot,bytes);CHECK(prepare(true,0u,false,conflicts[i])==NATIVE_CACHE_READY);
        CHECK(io.builds==1u && !io.opens && !io.writes && !io.renames);
    }
    for(unsigned cancel_at=1u;cancel_at<=14u;cancel_at++) {
        setup(boot,bytes);io.cancel_at=cancel_at;
        CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_CANCELLED);
    }
    for(unsigned boundary=1u;boundary<=8u;boundary++) {
        setup(boot,bytes);
        if(boundary==7u) {unsigned n=seed_cache(boot,bytes,true);disk[n].data[600]^=1u;}
        io.cancel_when=boundary;
        CHECK(prepare(true,0u,false,"/Games/PS2")==NATIVE_CACHE_CANCELLED);
        CHECK(io.renames==(boundary==8u?1u:0u)); /* only a complete cache file may have been renamed */
    }
    /* Both native background choices share the actual detector/cache path,
     * while their distinct wire reader values remain bound to the request. */
    const struct kui_native_dual_capability cap={KUI_NATIVE_DUAL_MAGIC0,KUI_NATIVE_DUAL_MAGIC1,
        1u,KUI_NATIVE_DUAL_CAPABILITY_BYTES,KUI_RETAIL_READER_ASYNC,
        KUI_RETAIL_READER_ASYNC_EAGER,KUI_NATIVE_DUAL_LEASE_BYTES,0u};
    for(unsigned reader=KUI_RETAIL_READER_ASYNC;reader<=KUI_RETAIL_READER_ASYNC_EAGER;reader++) {
        setup(boot,bytes);map.reader=reader;memcpy(module+3072u,&cap,sizeof(cap));
        memcpy(before,module,sizeof(module));
        CHECK(prepare(true,reader,false,"/Games/PS2")==NATIVE_CACHE_READY);
        CHECK(io.builds==1u && io.writes==1u && io.syncs==1u && io.renames==1u);
        setup(boot,bytes);seed_cache(boot,bytes,true);map.reader=reader;
        memcpy(module+3072u,&cap,sizeof(cap));memcpy(before,module,sizeof(module));
        CHECK(prepare(true,reader,false,"/Games/PS2")==NATIVE_CACHE_READY);
        CHECK(io.replays==1u && !io.builds && !io.writes && !io.renames);
    }
    free(boot);
}
int main(int argc,char **argv) {
    CHECK(argc>=1 && argc<=3);codec_and_trust_tests();gates_and_hits();dual_gates_and_hits();
    for(int i=1;i<argc;i++) actual_frontend(argv[i]);
    printf("Native launcher cache: %u checks passed%s\n",checks,argc>1?" including actual-image rebuild/publication faults":"");return 0;
}
