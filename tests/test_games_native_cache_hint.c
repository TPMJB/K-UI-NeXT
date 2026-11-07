/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual hint include and canonical cache/seed/replay APIs. Fake directory
 * I/O exercises conservative selection without any executable read adapter. */
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
typedef unsigned FRESULT;
enum {FR_OK,FR_NO_PATH,FR_DISK_ERR,FR_EXIST,FR_INVALID_NAME,AM_DIR=16u};
typedef struct {unsigned at;} DIR;
typedef struct {size_t fsize;unsigned fattrib;char fname[256];} FILINFO;
enum native_cache_result {NATIVE_CACHE_SKIPPED,NATIVE_CACHE_READY,NATIVE_CACHE_CANCELLED};
struct hint_node {char name[256];uint8_t wire[1024];size_t size;unsigned attributes;};
static struct hint_node nodes[8];static unsigned count;
static struct {
    unsigned opens,reads,closes,allocations,logs,cancel_checks,cancel_at;
    unsigned directory_padding;
    bool fail_open,fail_read_directory,fail_close,fail_read_file,fail_malloc;
    unsigned alias_reads,canonical_reads,publishes;
    bool alias_present,fail_alias_read,fail_publish,fail_directories;
    char alias_path[128];uint8_t alias_wire[1024];
} io;
static uint8_t module[4096],before[4096];
static struct kui_runtime_image package;
static struct kui_retail_manifest map;
static uint8_t image[1024];
static bool last_indexed;
static bool cancelled(void) {return ++io.cancel_checks==io.cancel_at;}
static bool native_cache_cancelled(kui_cancel_fn cancel) {return cancel && cancel();}
static bool native_cache_source_conflict(const char *root) {
    /* The launcher owns full alias/subtree admission; exercise its result. */
    return !root || !strncmp(root,"/KUI/VMU",8u);
}
static uint32_t native_cache_dual_lease(const struct kui_runtime_image *runtime) {
    size_t at=0u;
    return kui_native_dual_descriptor_find(runtime->data,runtime->info.payload_bytes,&at)==1?
        kui_native_dual_u32((const uint8_t *)runtime->data+at+24u):0u;
}
static FRESULT f_opendir(DIR *dir,const char *path) {
    CHECK(!strcmp(path,"0:/KUI/VMU/native-cache/v8"));++io.opens;
    dir->at=0u;return io.fail_open?FR_NO_PATH:FR_OK;
}
static FRESULT f_readdir(DIR *dir,FILINFO *entry) {
    ++io.reads;if(io.fail_read_directory)return FR_DISK_ERR;
    memset(entry,0,sizeof(*entry));
    if(dir->at>=count) {
        if(dir->at-count<io.directory_padding) {++dir->at;strcpy(entry->fname,"notes.txt");}
        return FR_OK;
    }
    const struct hint_node *node=&nodes[dir->at++];
    entry->fsize=node->size;entry->fattrib=node->attributes;
    strcpy(entry->fname,node->name);return FR_OK;
}
static FRESULT f_closedir(DIR *dir) {(void)dir;++io.closes;return io.fail_close?FR_DISK_ERR:FR_OK;}
static bool native_cache_file_read(const char *path,uint8_t wire[1024]) {
    if(strstr(path,"/hints/")) {
        ++io.alias_reads;
        if(!io.alias_present || io.fail_alias_read || strcmp(path,io.alias_path))return false;
        memcpy(wire,io.alias_wire,1024u);return true;
    }
    if(io.fail_read_file)return false;
    ++io.canonical_reads;
    const char *name=strrchr(path,'/');CHECK(name);++name;
    for(unsigned i=0u;i<count;i++)if(!strcmp(name,nodes[i].name)) {
        CHECK(nodes[i].size==1024u && !(nodes[i].attributes&AM_DIR));
        memcpy(wire,nodes[i].wire,1024u);return true;
    }
    CHECK(false);return false;
}
static void logger(const char *format,...) {CHECK(format);++io.logs;}
static bool native_cache_directories(void) {return !io.fail_directories;}
static FRESULT f_mkdir(const char *path) {
    CHECK(!strcmp(path,"0:/KUI/VMU/native-cache/v8/hints"));return FR_OK;
}
static FRESULT f_stat(const char *path,FILINFO *entry) {
    CHECK(!strcmp(path,"0:/KUI/VMU/native-cache/v8/hints"));
    memset(entry,0,sizeof(*entry));entry->fattrib=AM_DIR;return FR_OK;
}
/* The complete temporary/sync/readback/rename publisher has separate actual
 * I/O fault coverage in test_games_native_cache.c. Keep this seam focused on
 * advisory selection, unchanged bytes and the no-write fast path. */
static bool native_cache_file_publish(const char *path,const uint8_t wire[1024],
    uint8_t compare[1024],kui_cancel_fn cancel) {
    CHECK(strstr(path,"/hints/") && strlen(path)<sizeof(io.alias_path));++io.publishes;
    if(io.fail_publish || (cancel && cancel()))return false;
    strcpy(io.alias_path,path);memcpy(io.alias_wire,wire,1024u);memcpy(compare,wire,1024u);
    io.alias_present=true;return true;
}
static void *hint_malloc(size_t bytes) {++io.allocations;return io.fail_malloc?NULL:malloc(bytes);}
#define malloc hint_malloc
#include "../src/apps/games_native_cache_hint.inc"
#undef malloc
static void setup(void) {
    memset(&io,0,sizeof(io));memset(nodes,0,sizeof(nodes));count=0u;
    for(unsigned i=0u;i<sizeof(image);i++)image[i]=(uint8_t)(i*17u+1u);
    memset(module,0x59,sizeof(module));kui_vmu_native_cache_seed_template(module+128u);
    memcpy(before,module,sizeof(module));
    package=(struct kui_runtime_image){.data=module,.info={.payload_bytes=sizeof(module)}};
    map=(struct kui_retail_manifest){.storage_transport=KUI_STORAGE_SCI,
        .reader=KUI_RETAIL_READER_STANDARD,.boot_bytes=sizeof(image)};
}
static unsigned add(const struct kui_vmu_native_resolution *record,char character) {
    CHECK(count<sizeof(nodes)/sizeof(nodes[0]));unsigned at=count++;
    memset(nodes[at].name,character,64u);strcpy(nodes[at].name+64u,".nvr");nodes[at].size=1024u;
    CHECK(kui_vmu_native_resolution_cache_encode(record,nodes[at].wire,1024u));return at;
}
static unsigned add_regular(char character) {
    struct kui_vmu_native_resolution record;mock(image,sizeof(image),&record);return add(&record,character);
}
static void dual_module(uint32_t lease) {
    struct kui_native_dual_capability cap={KUI_NATIVE_DUAL_MAGIC0,KUI_NATIVE_DUAL_MAGIC1,1u,
        sizeof(cap),KUI_RETAIL_READER_ASYNC,KUI_RETAIL_READER_ASYNC_EAGER,lease,0u};
    memcpy(module+2048u,&cap,sizeof(cap));memcpy(before,module,sizeof(module));map.reader=KUI_RETAIL_READER_ASYNC;
}
static enum native_cache_result prepare(bool dual) {
    bool indexed=false;
    enum native_cache_result result=native_cache_hint_prepare(&map,dual,&package,"/Games/Test",
        "/Games/Test/Test.gdi",&indexed,logger,cancelled);
    last_indexed=indexed;
    if(indexed)CHECK(result==NATIVE_CACHE_READY && io.alias_present && !io.opens && !io.publishes);
    CHECK(io.closes==(io.opens && !io.fail_open?1u:0u));
    if(result!=NATIVE_CACHE_READY)CHECK(!memcmp(module,before,sizeof(module)));
    else CHECK(!memcmp(module,before,128u) && !memcmp(module+1248u,before+1248u,sizeof(module)-1248u));
    return result;
}
static void inspect_tests(void) {
    setup();add_regular('a');struct kui_vmu_native_resolution out,expected;
    mock(image,sizeof(image),&expected);memset(&out,0xa5,sizeof(out));
    CHECK(kui_vmu_native_resolution_cache_inspect_config(nodes[0].wire,1024u,&out));
    CHECK(!memcmp(&out,&expected,sizeof(out)));
    struct kui_vmu_native_resolution saved=out;nodes[0].wire[300]^=1u;
    CHECK(!kui_vmu_native_resolution_cache_inspect_config(nodes[0].wire,1024u,&out));CHECK(!memcmp(&saved,&out,sizeof(out)));
    CHECK(!kui_vmu_native_resolution_cache_inspect_config(nodes[0].wire,1023u,&out));
    CHECK(!kui_vmu_native_resolution_cache_inspect_config(nodes[0].wire,1024u,(void *)nodes[0].wire));
    CHECK(!kui_vmu_native_resolution_cache_inspect_config(nodes[0].wire,1024u,NULL));
}
static void selection_tests(void) {
    setup();CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    setup();add_regular('a');CHECK(prepare(false)==NATIVE_CACHE_READY);CHECK(io.logs==1u);
    const uint8_t *wire,*pin;struct kui_vmu_native_resolution out;
    CHECK(kui_vmu_native_cache_seed_read(module+128u,1120u,&wire,&pin));
    CHECK(kui_vmu_native_resolution_cache_replay(image,sizeof(image),load,wire,1024u,pin,&out));
    setup();add_regular('a');add_regular('b');CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    setup();add_regular('a');nodes[0].wire[200]^=1u;CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    setup();add_regular('a');nodes[0].wire[16]^=1u;seal(nodes[0].wire);CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    setup();add_regular('a');nodes[0].wire[752]=1u;seal(nodes[0].wire);CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    for(unsigned variant=0u;variant<9u;variant++) {
        setup();add_regular('a');
        if(variant==0u)nodes[0].name[0]='A';
        if(variant==1u)nodes[0].name[3]='g';
        if(variant==2u)strcpy(nodes[0].name+64u,".NVR");
        if(variant==3u)strcpy(nodes[0].name+64u,".nvr.tmp0");
        if(variant==4u)nodes[0].name[63]='\0';
        if(variant==5u)strcpy(nodes[0].name+64u,"0.nvr");
        if(variant==6u)nodes[0].attributes=AM_DIR;
        if(variant==7u)nodes[0].size=1023u;
        if(variant==8u)nodes[0].size=1025u;
        CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    }
    /* A malformed named candidate invalidates a prior selection; temporary
     * and unrelated files do not become candidates at all. */
    setup();add_regular('a');add_regular('b');nodes[1].wire[300]^=1u;CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    setup();add_regular('a');add_regular('b');strcpy(nodes[1].name,"notes.txt");CHECK(prepare(false)==NATIVE_CACHE_READY);
    setup();add_regular('a');io.directory_padding=255u;CHECK(prepare(false)==NATIVE_CACHE_READY);
    CHECK(io.reads==257u);
    setup();add_regular('a');io.directory_padding=256u;CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    CHECK(io.reads==257u);
    setup();struct kui_vmu_native_resolution record;mock(image,sizeof(image),&record);
    record.driver.image_bytes=record.heap.image_bytes=record.exits.image_bytes=768u;add(&record,'a');CHECK(prepare(false)==NATIVE_CACHE_READY);
    setup();mock(image,sizeof(image),&record);record.driver.load_address=record.heap.load=record.exits.load=load+4u;
    add(&record,'a');CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
}
static void module_tests(void) {
    struct kui_vmu_native_resolution record;
    setup();mock(image,sizeof(image),&record);record.heap.initializer_slot=record.heap.initializer_target=0u;
    record.call.return_pr=record.call.call+4u;add(&record,'a');CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    setup();mock(image,sizeof(image),&record);arena_record(&record);record.call.entry_bytes=KUI_NATIVE_DUAL_COMPACT_LEASE_BYTES;
    add(&record,'a');CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    setup();mock(image,sizeof(image),&record);arena_record(&record);record.call.entry_bytes=KUI_NATIVE_DUAL_COMPACT_LEASE_BYTES;
    add(&record,'a');dual_module(KUI_NATIVE_DUAL_COMPACT_LEASE_BYTES);CHECK(prepare(true)==NATIVE_CACHE_READY);
    setup();mock(image,sizeof(image),&record);arena_record(&record);record.call.entry_bytes=KUI_NATIVE_DUAL_COMPACT_LEASE_BYTES;
    add(&record,'a');dual_module(KUI_NATIVE_DUAL_GENERAL_LEASE_BYTES);CHECK(prepare(true)==NATIVE_CACHE_SKIPPED);
    setup();add_regular('a');map.reader=KUI_RETAIL_READER_ASYNC;CHECK(prepare(true)==NATIVE_CACHE_SKIPPED);
    setup();add_regular('a');dual_module(KUI_NATIVE_DUAL_GENERAL_LEASE_BYTES);CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    setup();add_regular('a');module[128u]^=1u;memcpy(before,module,sizeof(module));CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    for(unsigned variant=0u;variant<3u;variant++) {
        setup();add_regular('a');if(!variant)map.storage_transport=0u;
        if(variant==1u)map.flags|=KUI_RETAIL_IMAGE_CD;
        if(variant==2u)map.flags|=KUI_RETAIL_IMAGE_SCRAMBLED;
        CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    }
    setup();add_regular('a');CHECK(native_cache_hint_prepare(&map,false,&package,"/KUI/VMU/Game",
        "/KUI/VMU/Game/Test.gdi",NULL,logger,cancelled)==NATIVE_CACHE_SKIPPED);
    CHECK(!io.opens && !memcmp(module,before,sizeof(module)));
}
static void failure_tests(void) {
    for(unsigned variant=0u;variant<5u;variant++) {
        setup();add_regular('a');
        if(variant==0u)io.fail_open=true;
        if(variant==1u)io.fail_read_directory=true;
        if(variant==2u)io.fail_close=true;
        if(variant==3u)io.fail_read_file=true;
        if(variant==4u)io.fail_malloc=true;
        CHECK(prepare(false)==NATIVE_CACHE_SKIPPED);
    }
    for(unsigned cancel_at=1u;cancel_at<=4u;cancel_at++) {
        setup();add_regular('a');io.cancel_at=cancel_at;CHECK(prepare(false)==NATIVE_CACHE_CANCELLED);
    }
}
static void stale_hint_tests(void) {
    setup();add_regular('a');CHECK(prepare(false)==NATIVE_CACHE_READY);
    CHECK(native_cache_alias_publish("/Games/Test/Test.gdi",&map,false,&package,"/Games/Test",cancelled));
    const uint8_t *wire,*pin;struct kui_vmu_native_resolution out;
    CHECK(kui_vmu_native_cache_seed_read(module+128u,1120u,&wire,&pin));
    image[500]^=1u;memset(&out,0xa5,sizeof(out));struct kui_vmu_native_resolution saved=out;
    io.opens=io.reads=io.closes=io.publishes=0u;
    CHECK(prepare(false)==NATIVE_CACHE_READY && last_indexed && !io.reads);
    /* Same bytes/load and a valid canonical record are deliberately not
     * sufficient: actual stage replay hashes the newly loaded image. */
    CHECK(!kui_vmu_native_resolution_cache_replay(image,sizeof(image),load,wire,1024u,pin,&out));
    CHECK(!memcmp(&saved,&out,sizeof(out)));
    uint8_t fresh[1024];memset(fresh,0xa5,sizeof(fresh));
    /* Exercise the stage's next operation with the actual full detector.
     * This authored image has no SDK: rediscovery refuses it safely. */
    CHECK(!kui_vmu_native_resolution_cache_build(image,sizeof(image),load,fresh,sizeof(fresh),&out));
    CHECK(!memcmp(&saved,&out,sizeof(out)));for(unsigned i=0u;i<sizeof(fresh);i++)CHECK(fresh[i]==0xa5u);
}
static void indexed_hint_tests(void) {
    char key[128],same[128],other[128];
    setup();CHECK(native_cache_alias_path(key,"/Games/Test/Test.gdi",&map,false,&package));
    CHECK(native_cache_alias_path(same,"0:\\games\\TEST\\test.GDI",&map,false,&package));
    CHECK(!strcmp(key,same) && strstr(key,"/v8/hints/") && strlen(key)<120u);
    for(unsigned field=0u;field<6u;field++) {
        struct kui_retail_manifest changed=map;
        if(field==0u)++changed.boot_lba;
        if(field==1u)++changed.boot_bytes;
        if(field==2u)++changed.storage_transport;
        if(field==3u)++changed.reader;
        if(field==4u)++changed.gdi_crc32;
        const char *selected=field==5u?"/Games/Other/Test.gdi":"/Games/Test/Test.gdi";
        CHECK(native_cache_alias_path(other,selected,&changed,false,&package) && strcmp(key,other));
    }
    strcpy(other,"unchanged");CHECK(!native_cache_alias_path(other,"/Games/../Test.gdi",&map,false,&package));
    CHECK(!strcmp(other,"unchanged"));
    dual_module(KUI_NATIVE_DUAL_COMPACT_LEASE_BYTES);
    CHECK(native_cache_alias_path(key,"/Games/Test/Test.gdi",&map,true,&package));
    dual_module(KUI_NATIVE_DUAL_GENERAL_LEASE_BYTES);
    CHECK(native_cache_alias_path(other,"/Games/Test/Test.gdi",&map,true,&package) && strcmp(key,other));
    CHECK(native_cache_alias_path(other,"/Games/Test/Test.gdi",&map,false,&package) && strcmp(key,other));

    /* First discovery keeps the complete original bounded directory scan.
     * The next identical image uses one advisory file regardless of size. */
    setup();add_regular('a');io.directory_padding=255u;
    CHECK(prepare(false)==NATIVE_CACHE_READY && !last_indexed && io.reads==257u && io.canonical_reads==1u);
    CHECK(native_cache_alias_publish("/Games/Test/Test.gdi",&map,false,&package,"/Games/Test",cancelled));
    CHECK(io.publishes==1u && io.alias_present && !memcmp(io.alias_wire,nodes[0].wire,1024u));
    io.opens=io.reads=io.closes=io.publishes=io.alias_reads=io.canonical_reads=0u;
    io.directory_padding=10000u; /* Direct indexing is independent of scan limits. */
    CHECK(prepare(false)==NATIVE_CACHE_READY && last_indexed);
    CHECK(io.alias_reads==1u && !io.canonical_reads && !io.reads && !io.publishes);
    CHECK(native_cache_alias_publish("/Games/Test/Test.gdi",&map,false,&package,"/Games/Test",cancelled));
    CHECK(!io.publishes); /* A defensive publisher call still cannot rewrite unchanged bytes. */

    /* Malformed or incompatible alias bytes cannot change fallback policy. */
    for(unsigned fault=0u;fault<4u;fault++) {
        setup();add_regular('a');CHECK(prepare(false)==NATIVE_CACHE_READY);
        CHECK(native_cache_alias_publish("/Games/Test/Test.gdi",&map,false,&package,"/Games/Test",cancelled));
        if(fault==0u)io.alias_wire[300]^=1u;
        if(fault==1u)io.fail_alias_read=true;
        if(fault==2u)io.alias_path[0]='1';
        if(fault==3u) {
            struct kui_vmu_native_resolution record;mock(image,sizeof(image),&record);
            record.driver.image_bytes=record.heap.image_bytes=record.exits.image_bytes=1280u;
            CHECK(kui_vmu_native_resolution_cache_encode(&record,io.alias_wire,sizeof(io.alias_wire)));
        }
        io.opens=io.reads=io.closes=io.publishes=0u;
        CHECK(prepare(false)==NATIVE_CACHE_READY && !last_indexed && io.opens==1u && io.reads==2u);
    }
    for(unsigned fault=0u;fault<3u;fault++) {
        setup();add_regular('a');CHECK(prepare(false)==NATIVE_CACHE_READY);
        uint8_t saved[sizeof(module)];memcpy(saved,module,sizeof(saved));
        io.fail_publish=fault==0u;io.fail_directories=fault==1u;
        if(fault==2u){io.cancel_checks=0u;io.cancel_at=1u;}
        CHECK(!native_cache_alias_publish("/Games/Test/Test.gdi",&map,false,&package,"/Games/Test",cancelled));
        CHECK(!io.alias_present && !memcmp(saved,module,sizeof(saved)));
    }
    puts("PASS indexed native hints: canonical path/extent/epoch/class/lease keys,257-directory-call old control versus one direct read/no writes, unchanged NVR bytes, bad-alias fallback and failed publication");
}
/* The old production predicate discarded every reclaimed-program record
 * before the stage could check it. Keep that exact predicate as a control. */
#include "fixtures/native-hint-length-original.inc"
static void prefix_candidate_tests(void) {
    setup();struct kui_vmu_native_resolution record,out;mock(image,sizeof(image),&record);
    map.boot_bytes=sizeof(image)+512u;add(&record,'a');
    CHECK(!native_cache_hint_admitted_original(&map,false,&package,&record));
    CHECK(native_cache_hint_admitted(&map,false,&package,&record));
    CHECK(prepare(false)==NATIVE_CACHE_READY);
    CHECK(native_cache_alias_publish("/Games/Test/Test.gdi",&map,false,&package,"/Games/Test",cancelled));
    io.opens=io.reads=io.closes=io.publishes=io.alias_reads=io.canonical_reads=0u;
    CHECK(prepare(false)==NATIVE_CACHE_READY && last_indexed);
    const uint8_t *wire,*pin;CHECK(kui_vmu_native_cache_seed_read(module+128u,1120u,&wire,&pin));
    /* No reviewed suffix exists in this generated full source. The actual
     * lifetime proof keeps it whole, and exact-length replay must reject the
     * smaller advisory record. A source truncation cannot manufacture a
     * reviewed suffix; changing the hashed prefix also causes a SHA miss. */
    uint8_t full[sizeof(image)+512u];memcpy(full,image,sizeof(image));memset(full+sizeof(image),0,512u);
    size_t program=0u;CHECK(kui_vmu_native_program_extent(full,sizeof(full),load,&program) && program==sizeof(full));
    CHECK(!kui_vmu_native_resolution_cache_replay(full,program,load,wire,1024u,pin,&out));
    CHECK(kui_vmu_native_program_extent(full,sizeof(full)-1u,load,&program) && program==sizeof(full)-1u);
    CHECK(!kui_vmu_native_resolution_cache_replay(full,program,load,wire,1024u,pin,&out));
    full[500]^=1u;CHECK(!kui_vmu_native_resolution_cache_replay(full,sizeof(image),load,wire,1024u,pin,&out));
    record.driver.image_bytes=record.heap.image_bytes=record.exits.image_bytes=map.boot_bytes+1u;
    CHECK(!native_cache_hint_admitted(&map,false,&package,&record));
    puts("PASS shorter advisory record: old production equality rejects; shared directory/index selection succeeds; fresh full lifetime/length/SHA checks still reject unproved truncation and changed prefix");
}
static void actual_rediscovery(const char *path) {
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long length=ftell(file);CHECK(length>0);
    rewind(file);uint8_t *boot=malloc((size_t)length);CHECK(boot);CHECK(fread(boot,1,(size_t)length,file)==(size_t)length);CHECK(!fclose(file));
    size_t bytes;CHECK(kui_vmu_native_program_extent(boot,(size_t)length,load,&bytes));
    struct kui_vmu_native_resolution record,out;uint8_t fresh[1024];
    CHECK(kui_vmu_native_resolution_cache_build(boot,bytes,load,fresh,sizeof(fresh),&record));
    uint8_t proof_pin[32];CHECK(kui_vmu_native_resolution_cache_record_digest(fresh,sizeof(fresh),proof_pin));
    CHECK(kui_vmu_native_resolution_cache_replay(boot,bytes,load,fresh,sizeof(fresh),proof_pin,&out));
    /* A GUI candidate does not authorize the source's lifetime or prefix.
     * Reexecute the real stage's extent then exact-length/SHA boundary. */
    boot[0x200u]^=1u;size_t changed_extent=0u;
    if(kui_vmu_native_program_extent(boot,(size_t)length,load,&changed_extent))
        CHECK(!kui_vmu_native_resolution_cache_replay(boot,changed_extent,load,fresh,sizeof(fresh),proof_pin,&out));
    boot[0x200u]^=1u;
    if((size_t)length-bytes==0x8000u) {
        boot[(size_t)length-1u]^=1u;
        CHECK(!kui_vmu_native_program_extent(boot,(size_t)length,load,&changed_extent));
        boot[(size_t)length-1u]^=1u;
    }
    setup();map.boot_bytes=(uint32_t)length;
    CHECK(native_cache_hint_admitted(&map,true,&package,&record)==
        (record.heap.family!=KUI_VMU_NATIVE_HEAP_ARENA));
    if(bytes!=(size_t)length)CHECK(!native_cache_hint_admitted_original(&map,false,&package,&record));
    record.driver.image_sha256[0]^=1u;add(&record,'a');
    if(!record.heap.initializer_slot || record.heap.family==KUI_VMU_NATIVE_HEAP_ARENA) {
        dual_module(record.heap.family==KUI_VMU_NATIVE_HEAP_ARENA?record.call.entry_bytes:KUI_NATIVE_DUAL_GENERAL_LEASE_BYTES);
        CHECK(prepare(true)==NATIVE_CACHE_READY);
    } else CHECK(prepare(false)==NATIVE_CACHE_READY);
    const uint8_t *wire,*pin;CHECK(kui_vmu_native_cache_seed_read(module+128u,1120u,&wire,&pin));
    size_t fresh_bytes=0u;CHECK(kui_vmu_native_program_extent(boot,(size_t)length,load,&fresh_bytes) && fresh_bytes==bytes);
    CHECK(!kui_vmu_native_resolution_cache_replay(boot,fresh_bytes,load,wire,1024u,pin,&out));
    CHECK(kui_vmu_native_resolution_cache_build(boot,bytes,load,fresh,sizeof(fresh),&out));
    uint8_t expected[32];digest(boot,bytes,expected);CHECK(!memcmp(out.driver.image_sha256,expected,32u));
    printf("PASS actual advisory source: full_source_bytes=%lu freshly_proved_program_bytes=%lu; old equality refused shorter configuration; fresh extent/length/SHA, changed-prefix and suffix refusal retained\n",
        (unsigned long)length,(unsigned long)bytes);free(boot);
}
static void actual_module_slot(const char *path) {
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long total=ftell(file);CHECK(total>64);
    rewind(file);uint8_t *envelope=malloc((size_t)total);CHECK(envelope);
    CHECK(fread(envelope,1,(size_t)total,file)==(size_t)total && !fclose(file));
    CHECK(!memcmp(envelope,"KUIRUN1\0",8u) && get32(envelope+12u)==64u);
    size_t bytes=get32(envelope+16u);CHECK(bytes==(size_t)total-64u);
    CHECK(kui_crc32(0,envelope,60u)==get32(envelope+60u));
    CHECK(kui_crc32(0,envelope+64u,bytes)==get32(envelope+32u));
    uint8_t *payload=envelope+64u,*original=malloc(bytes);CHECK(original);memcpy(original,payload,bytes);
    size_t slot=kui_vmu_native_cache_seed_find(payload,bytes);CHECK(slot!=SIZE_MAX);
    bool dual=kui_native_dual_descriptor_find(payload,bytes,NULL)==1;
    setup();add_regular('a');package=(struct kui_runtime_image){.data=payload,.info={.payload_bytes=(uint32_t)bytes}};
    map.reader=dual?KUI_RETAIL_READER_ASYNC:KUI_RETAIL_READER_STANDARD;
    bool indexed=false;
    CHECK(native_cache_hint_prepare(&map,dual,&package,"/Games/Test","/Games/Test/Test.gdi",
        &indexed,logger,cancelled)==NATIVE_CACHE_READY && !indexed);
    CHECK(native_cache_alias_publish("/Games/Test/Test.gdi",&map,dual,&package,"/Games/Test",cancelled));
    memcpy(payload+slot,original+slot,KUI_VMU_NATIVE_CACHE_SEED_BYTES);
    io.opens=io.reads=io.closes=io.publishes=io.alias_reads=io.canonical_reads=0u;
    CHECK(native_cache_hint_prepare(&map,dual,&package,"/Games/Test","/Games/Test/Test.gdi",
        &indexed,logger,cancelled)==NATIVE_CACHE_READY && indexed);
    CHECK(io.alias_reads==1u && !io.opens && !io.reads && !io.canonical_reads && !io.publishes);
    CHECK(!memcmp(payload,original,slot));
    CHECK(!memcmp(payload+slot+KUI_VMU_NATIVE_CACHE_SEED_BYTES,original+slot+KUI_VMU_NATIVE_CACHE_SEED_BYTES,
        bytes-slot-KUI_VMU_NATIVE_CACHE_SEED_BYTES));
    printf("PASS actual native module %.12s advisory slot: %s; only its existing seed changed in RAM, indexed hit one read/no writes\n",
        envelope+40u,path);
    free(original);free(envelope);
}
int main(int argc,char **argv) {
    inspect_tests();selection_tests();module_tests();failure_tests();stale_hint_tests();indexed_hint_tests();prefix_candidate_tests();
    bool modules=argc>1 && !strcmp(argv[1],"--modules");
    if(modules) {CHECK(argc>2);for(int i=2;i<argc;i++)actual_module_slot(argv[i]);}
    else if(argc>1)actual_rediscovery(argv[1]);
    printf("Native mapping hints: %u checks passed%s\n",checks,argc>1?(modules?
        " including actual packaged module slots":" including successful actual-image rediscovery"):"");return 0;
}
