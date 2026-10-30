/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual private wire codec/bounds. Filesystem/public API tests
 * link this same production file separately against real FAT32/exFAT. */
#include "../src/core/vmu_game_candidates.c"
#include "../src/core/vmu_game_assign.c"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static struct candidate_table fixture,back,before,reverse;
static uint8_t wire[KUI_VMU_GAME_CANDIDATES_WIRE_BYTES],bad[KUI_VMU_GAME_CANDIDATES_WIRE_BYTES+1u];
/* Public preflight runs against a small read-only filesystem model. This keeps
 * legacy migration/counting in the production path; it does not model media
 * durability (the separate real FAT32/exFAT suite does). */
struct test_record {const char *path;const uint8_t *data;size_t bytes;BYTE attributes;};
static struct test_record records[8];
static unsigned record_count,read_opens;
static const char *fault_path;
static char fault_operation,diagnostics[4096];
static size_t diagnostic_bytes;
static struct kui_vmu_game_assign_table legacy_fixture;
static uint8_t legacy_wire[KUI_VMU_GAME_ASSIGN_WIRE_BYTES],identity_fixture[16],import_fixture[64];
static struct kui_vmu_game_candidates_repair repair_fixture,repair_before,repair_bad;
static int record_index(const char *path) {
    for(unsigned i=0;i<record_count;i++)if(!strcmp(path,records[i].path))return (int)i;
    return -1;
}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    if(fault_path && fault_operation=='s' && !strcmp(path,fault_path))return FR_DISK_ERR;
    int i=record_index(path);if(i<0)return FR_NO_FILE;
    memset(info,0,sizeof(*info));info->fsize=records[i].bytes;info->fattrib=records[i].attributes;return FR_OK;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    CHECK(mode==FA_READ);++read_opens;
    if(fault_path && fault_operation=='o' && !strcmp(path,fault_path))return FR_DISK_ERR;
    int i=record_index(path);if(i<0)return FR_NO_FILE;
    memset(file,0,sizeof(*file));file->obj.sclust=(DWORD)i+1u;file->obj.objsize=records[i].bytes;return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT bytes,UINT *got) {
    CHECK(file->obj.sclust && file->obj.sclust<=record_count);
    const struct test_record *record=records+file->obj.sclust-1u;*got=0;
    if(fault_path && fault_operation=='r' && !strcmp(record->path,fault_path))return FR_DISK_ERR;
    size_t available=record->bytes-(size_t)file->fptr;
    if(bytes>available)bytes=(UINT)available;
    if(fault_path && fault_operation=='h' && !strcmp(record->path,fault_path) && bytes)--bytes;
    CHECK(record->data);memcpy(out,record->data+(size_t)file->fptr,bytes);file->fptr+=bytes;*got=bytes;return FR_OK;
}
FRESULT f_close(FIL *file) {
    CHECK(file->obj.sclust && file->obj.sclust<=record_count);
    bool fail=fault_path && fault_operation=='c' && !strcmp(records[file->obj.sclust-1u].path,fault_path);
    memset(file,0,sizeof(*file));return fail?FR_DISK_ERR:FR_OK;
}
static void diagnostic(const char *format,...) {
    if(diagnostic_bytes>=sizeof(diagnostics)-1u)return;
    va_list args;va_start(args,format);
    int n=vsnprintf(diagnostics+diagnostic_bytes,sizeof(diagnostics)-diagnostic_bytes,format,args);va_end(args);
    if(n<0)return;
    size_t wrote=(size_t)n;if(wrote>=sizeof(diagnostics)-diagnostic_bytes)wrote=sizeof(diagnostics)-diagnostic_bytes-1u;
    diagnostic_bytes+=wrote;
    if(diagnostic_bytes<sizeof(diagnostics)-1u)diagnostics[diagnostic_bytes++]='\n';
    diagnostics[diagnostic_bytes]=0;
}
static void diagnostic_reset(void) {memset(diagnostics,0,sizeof(diagnostics));diagnostic_bytes=read_opens=0;}
static uint32_t independent_crc(uint32_t previous,const void *data,size_t bytes) {
    const uint8_t *p=data;uint32_t crc=~previous;
    for(size_t i=0;i<bytes;i++) {
        crc^=p[i];for(unsigned bit=0;bit<8u;bit++)crc=(crc>>1)^((crc&1u)?UINT32_C(0xedb88320):0u);
    }
    return ~crc;
}
static void repair_crc(uint8_t *p) {
    uint32_t crc=independent_crc(0,p,508u);
    crc=independent_crc(crc,p+512u,KUI_VMU_GAME_CANDIDATES_WIRE_BYTES-512u);put32(p+508u,crc);
}
static void initialize(struct candidate_table *t) {
    memset(t,0,sizeof(*t));t->sequence=UINT64_C(0x123456789abcdef0);
    for(unsigned i=0;i<16u;i++)t->storage.cid[i]=(uint8_t)(i+1u);
    t->storage.card_sectors=UINT64_C(0x100000000);t->storage.partition_first=2048u;
    t->storage.partition_end=UINT64_C(0xffffffff);t->storage.high_capacity=1u;
}
static struct kui_vmu_game_assignment entry(unsigned game,unsigned pair) {
    struct kui_vmu_game_assignment e={0};char product[12];
    snprintf(product,sizeof(product),"T-%04u",game);CHECK(kui_vmu_game_card_key(product,e.key));
    e.pair=pair;e.identity[0]=0x5au;e.identity[1]=(uint8_t)pair;e.identity[2]=(uint8_t)(pair>>8u);return e;
}
static void refused(size_t bytes) {
    memset(&back,0x5a,sizeof(back));CHECK(!decode(bad,bytes,&back));
}
static void roundtrip(void) {
    initialize(&fixture);struct kui_vmu_game_assignment e=entry(2u,5u);CHECK(!add(&fixture,&e));
    e=entry(1u,4u);CHECK(!add(&fixture,&e));e=entry(1u,2u);CHECK(!add(&fixture,&e));
    fixture.entries[1].flags=DEFAULT_FLAG;
    CHECK(valid_table(&fixture));CHECK(encode(&fixture,wire));
    CHECK(!memcmp(wire,"KUIVMUC2",8u) && get32(wire+8u)==2u);
    CHECK(wire[16u]==0xf0u && wire[23u]==0x12u && get32(wire+24u)==3u);
    CHECK(get32(wire+28u)==40u && get32(wire+32u)==512u && get32(wire+36u)==2048u);
    CHECK(!memcmp(wire+40u,fixture.storage.cid,16u) && get64(wire+56u)==UINT64_C(0x100000000));
    CHECK(get64(wire+64u)==2048u && get64(wire+72u)==UINT64_C(0xffffffff) && get32(wire+80u)==1u);
    CHECK(!memcmp(wire+512u,"T-0001",6u) && get32(wire+524u)==2u);
    CHECK(get32(wire+552u+32u)==1u && zero(wire+84u,424u));
    uint32_t crc=independent_crc(0,wire,508u);
    CHECK(independent_crc(crc,wire+512u,sizeof(wire)-512u)==get32(wire+508u));
    CHECK(decode(wire,sizeof(wire),&back) && !memcmp(&back,&fixture,sizeof(back)));
    initialize(&reverse);e=entry(1u,2u);CHECK(!add(&reverse,&e));e=entry(2u,5u);CHECK(!add(&reverse,&e));
    e=entry(1u,4u);CHECK(!add(&reverse,&e));reverse.entries[1].flags=DEFAULT_FLAG;
    CHECK(!memcmp(&fixture,&reverse,sizeof(fixture)));
    before=fixture;CHECK(!add(&fixture,&e) && !memcmp(&fixture,&before,sizeof(fixture)));
    e.identity[15]=1u;CHECK(add(&fixture,&e)==KUI_VMU_GAME_CANDIDATES_STALE && !memcmp(&fixture,&before,sizeof(fixture)));
}
static void corruption(void) {
    /* Every header/live-record byte and a byte in every payload sector is
     * covered; use independent CRC repair below to test semantic validation. */
    for(size_t at=0;at<512u+3u*40u;at++) {
        memcpy(bad,wire,sizeof(wire));bad[at]^=1u;refused(sizeof(wire));
    }
    for(size_t at=512u;at<sizeof(wire);at+=512u) {
        memcpy(bad,wire,sizeof(wire));bad[at+511u]^=0x80u;refused(sizeof(wire));
    }
    memcpy(bad,wire,sizeof(wire));refused(sizeof(wire)-1u);bad[sizeof(wire)]=0u;refused(sizeof(bad));
    static const size_t fields[]={8u,12u,28u,32u,36u,84u,507u,512u+10u,512u+36u,512u+3u*40u};
    for(size_t i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
        memcpy(bad,wire,sizeof(wire));bad[fields[i]]^=0x80u;repair_crc(bad);refused(sizeof(wire));
    }
    memcpy(bad,wire,sizeof(wire));put64(bad+16u,0u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+24u,2049u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+80u,2u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));memset(bad+40u,0,16u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put64(bad+72u,2048u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put64(bad+72u,UINT64_C(0x100000001));repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+524u,0u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+524u,10000u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));memset(bad+528u,0,16u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+544u,2u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+544u,1u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));put32(bad+564u,2u);repair_crc(bad);refused(sizeof(wire));
    memcpy(bad,wire,sizeof(wire));uint8_t saved[40];memcpy(saved,bad+512u,40u);
    memcpy(bad+512u,bad+552u,40u);memcpy(bad+552u,saved,40u);repair_crc(bad);refused(sizeof(wire));
    CHECK(!decode(NULL,sizeof(wire),&back));
}
static void limits(void) {
    initialize(&fixture);
    for(unsigned pair=1u;pair<=32u;pair++) {struct kui_vmu_game_assignment e=entry(1u,pair);CHECK(!add(&fixture,&e));}
    CHECK(valid_table(&fixture));before=fixture;struct kui_vmu_game_assignment e=entry(1u,33u);
    CHECK(add(&fixture,&e)==KUI_VMU_GAME_CANDIDATES_FULL && !memcmp(&fixture,&before,sizeof(fixture)));
    fixture.entries[0].flags=fixture.entries[1].flags=1u;CHECK(!valid_table(&fixture));
    initialize(&fixture);
    for(unsigned game=1u;game<=256u;game++) {e=entry(game,game);CHECK(!add(&fixture,&e));}
    CHECK(valid_table(&fixture));before=fixture;e=entry(257u,257u);
    CHECK(add(&fixture,&e)==KUI_VMU_GAME_CANDIDATES_FULL && !memcmp(&fixture,&before,sizeof(fixture)));
    initialize(&fixture);
    for(unsigned game=1u;game<=256u;game++)for(unsigned variant=1u;variant<=8u;variant++) {
        e=entry(game,(game-1u)*8u+variant);CHECK(!add(&fixture,&e));
    }
    CHECK(fixture.count==2048u && valid_table(&fixture));CHECK(encode(&fixture,wire));
    CHECK(decode(wire,sizeof(wire),&back) && !memcmp(&fixture,&back,sizeof(fixture)));
    before=fixture;e=entry(1u,9999u);
    CHECK(add(&fixture,&e)==KUI_VMU_GAME_CANDIDATES_FULL && !memcmp(&fixture,&before,sizeof(fixture)));
    fixture.sequence=0u;CHECK(!encode(&fixture,wire) && zero(wire,sizeof(wire)));
    fixture=before;fixture.sequence=UINT64_MAX;CHECK(encode(&fixture,wire));
    CHECK(decode(wire,sizeof(wire),&back) && back.sequence==UINT64_MAX);
    fixture=before;fixture.storage.high_capacity=2u;CHECK(!encode(&fixture,wire) && zero(wire,sizeof(wire)));
}
static void shared_legacy_preflight(void) {
    initialize(&fixture);memset(&legacy_fixture,0,sizeof(legacy_fixture));legacy_fixture.sequence=7u;legacy_fixture.count=34u;
    for(unsigned i=0;i<34u;i++)legacy_fixture.entries[i]=entry(i+1u,5u);
    CHECK(kui_vmu_game_assign_encode(&legacy_fixture,legacy_wire));
    memcpy(identity_fixture,legacy_fixture.entries[0].identity,16u);
    memset(import_fixture,0,sizeof(import_fixture));memcpy(import_fixture,"KUIVMUI1",8u);memcpy(import_fixture+16u,identity_fixture,16u);
    put32(import_fixture+60u,independent_crc(0,import_fixture,60u));
    records[0]=(struct test_record){legacy_paths[0],legacy_wire,sizeof(legacy_wire),0};
    records[1]=(struct test_record){"0:/KUI/vmu-test/p0005/identity.bin",identity_fixture,16u,0};
    records[2]=(struct test_record){"0:/KUI/vmu-test/p0005/import.bin",import_fixture,64u,0};
    records[3]=(struct test_record){"0:/KUI/vmu-test/p0005/card.vmd",NULL,131072u,0};
    records[4]=(struct test_record){"0:/KUI/vmu-test/p0005/card.journal",NULL,1024u,0};
    records[5]=(struct test_record){"0:/KUI/vmu-test/p0005",NULL,0u,AM_DIR};record_count=6u;
    struct kui_vmu_game_candidates_plan plans[4]={0};
    for(unsigned i=0;i<4u;i++) {memcpy(plans[i].key,legacy_fixture.entries[i].key,12u);plans[i].additional=1u;}
    memcpy(bad,legacy_wire,sizeof(legacy_wire));diagnostic_reset();
    CHECK(kui_vmu_game_candidates_preflight(plans,4u,&fixture.storage,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_OK);
    CHECK(strstr(diagnostics,"4 existing products, 4 candidates; 4 planned products"));
    CHECK(strstr(diagnostics,"result 0") && read_opens && !memcmp(bad,legacy_wire,sizeof(legacy_wire)));
    /* Thirty-four games pointing to one old card are not 34 versions of each
     * selected game. Only its one old target plus the new variants count. */
    plans[0].additional=31u;CHECK(!kui_vmu_game_candidates_preflight(plans,1u,&fixture.storage,NULL,NULL));
    plans[0].additional=32u;diagnostic_reset();
    CHECK(kui_vmu_game_candidates_preflight(plans,1u,&fixture.storage,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_FULL);
    CHECK(strstr(diagnostics,"1 candidates + 32 planned; per-game limit 32"));plans[0].additional=1u;
    struct test_record marker=records[2];records[2].path="0:/missing-marker";diagnostic_reset();
    CHECK(kui_vmu_game_candidates_preflight(plans,4u,&fixture.storage,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_STALE);
    CHECK(strstr(diagnostics,"T-0001 p0005 import marker is missing") && !strstr(diagnostics,"limit"));records[2]=marker;
    static const char operations[]={'s','o','r','h','c'};
    for(unsigned i=0;i<sizeof(operations);i++) {
        fault_path=records[1].path;fault_operation=operations[i];diagnostic_reset();
        CHECK(kui_vmu_game_candidates_preflight(plans,4u,&fixture.storage,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_IO);
        CHECK(strstr(diagnostics,"p0005 identity could not be read") && !strstr(diagnostics,"limit"));
    }
    fault_path=records[3].path;fault_operation='s';diagnostic_reset();
    CHECK(kui_vmu_game_candidates_preflight(plans,4u,&fixture.storage,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_IO);
    CHECK(strstr(diagnostics,"card.vmd could not be inspected"));fault_path=NULL;fault_operation=0;
    identity_fixture[0]^=1u;diagnostic_reset();
    CHECK(kui_vmu_game_candidates_preflight(plans,4u,&fixture.storage,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_STALE);
    CHECK(strstr(diagnostics,"identity is missing, malformed or changed"));identity_fixture[0]^=1u;
    legacy_wire[508u]^=1u;diagnostic_reset();
    CHECK(kui_vmu_game_candidates_preflight(plans,4u,&fixture.storage,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_INVALID);
    CHECK(strstr(diagnostics,"legacy product T-0001 refused (result -3)"));legacy_wire[508u]^=1u;
    memcpy(plans[1].key,plans[0].key,12u);diagnostic_reset();
    CHECK(kui_vmu_game_candidates_preflight(plans,4u,&fixture.storage,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_ARGUMENT);
    CHECK(strstr(diagnostics,"duplicate product T-0001") && !read_opens);
    CHECK(!memcmp(bad,legacy_wire,sizeof(legacy_wire)));
    for(unsigned i=0;i<4u;i++)memcpy(plans[i].key,legacy_fixture.entries[i].key,12u);
    memset(&repair_fixture,0x5a,sizeof(repair_fixture));
    CHECK(!kui_vmu_game_candidates_preflight_repair(plans,4u,&fixture.storage,&repair_fixture,NULL,NULL));
    CHECK(zero(&repair_fixture,sizeof(repair_fixture)));
    /* The positively absent entire directory is the sole repair condition.
     * References remain untouched until a separately confirmed publication. */
    struct test_record saved_records[6];memcpy(saved_records,records,sizeof(saved_records));
    record_count=1u;diagnostic_reset();
    CHECK(kui_vmu_game_candidates_preflight(plans,4u,&fixture.storage,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_MISSING);
    CHECK(strstr(diagnostics,"legacy T-0001 p0005 folder is absent") && !strstr(diagnostics,"limit"));
    CHECK(kui_vmu_game_candidates_preflight_repair(plans,4u,&fixture.storage,&repair_fixture,diagnostic,NULL)==KUI_VMU_GAME_CANDIDATES_MISSING);
    CHECK(repair_fixture.count==4u && repair_fixture.catalogue_sequence==0u && repair_fixture.legacy_sequence==7u);
    CHECK(storage_equal(&repair_fixture.storage,&fixture.storage));
    CHECK(!memcmp(repair_fixture.entries,legacy_fixture.entries,4u*sizeof(legacy_fixture.entries[0])));
    repair_before=repair_fixture;
    CHECK(!kui_vmu_game_candidates_preflight_confirmed(plans,4u,&fixture.storage,&repair_fixture,diagnostic,NULL));
    CHECK(!memcmp(&repair_fixture,&repair_before,sizeof(repair_fixture)) && !memcmp(bad,legacy_wire,sizeof(legacy_wire)));
    CHECK(kui_vmu_game_candidates_preflight_confirmed(plans,1u,&fixture.storage,&repair_fixture,NULL,NULL)==KUI_VMU_GAME_CANDIDATES_ARGUMENT);
    repair_bad=repair_fixture;repair_bad.entries[1]=repair_bad.entries[0];
    CHECK(kui_vmu_game_candidates_preflight_confirmed(plans,4u,&fixture.storage,&repair_bad,NULL,NULL)==KUI_VMU_GAME_CANDIDATES_ARGUMENT);
    repair_bad=repair_fixture;repair_bad.storage.cid[0]^=1u;
    CHECK(kui_vmu_game_candidates_preflight_confirmed(plans,4u,&fixture.storage,&repair_bad,NULL,NULL)==KUI_VMU_GAME_CANDIDATES_STALE);
    legacy_fixture.sequence=8u;CHECK(kui_vmu_game_assign_encode(&legacy_fixture,legacy_wire));
    CHECK(kui_vmu_game_candidates_preflight_confirmed(plans,4u,&fixture.storage,&repair_fixture,NULL,NULL)==KUI_VMU_GAME_CANDIDATES_STALE);
    legacy_fixture.sequence=7u;legacy_fixture.entries[0].identity[15]=1u;CHECK(kui_vmu_game_assign_encode(&legacy_fixture,legacy_wire));
    CHECK(kui_vmu_game_candidates_preflight_confirmed(plans,4u,&fixture.storage,&repair_fixture,NULL,NULL)==KUI_VMU_GAME_CANDIDATES_STALE);
    legacy_fixture.entries[0].identity[15]=0u;CHECK(kui_vmu_game_assign_encode(&legacy_fixture,legacy_wire));
    fixture.sequence=1u;CHECK(encode(&fixture,wire));records[1]=(struct test_record){candidate_paths[0],wire,sizeof(wire),0};record_count=2u;
    CHECK(kui_vmu_game_candidates_preflight_confirmed(plans,4u,&fixture.storage,&repair_fixture,NULL,NULL)==KUI_VMU_GAME_CANDIDATES_STALE);
    memcpy(records,saved_records,sizeof(saved_records));record_count=6u;
    CHECK(kui_vmu_game_candidates_preflight_confirmed(plans,4u,&fixture.storage,&repair_fixture,NULL,NULL)==KUI_VMU_GAME_CANDIDATES_STALE);
    fault_path=records[5].path;fault_operation='s';memset(&repair_bad,0x5a,sizeof(repair_bad));
    CHECK(kui_vmu_game_candidates_preflight_repair(plans,4u,&fixture.storage,&repair_bad,NULL,NULL)==KUI_VMU_GAME_CANDIDATES_IO);
    CHECK(zero(&repair_bad,sizeof(repair_bad)));fault_path=NULL;fault_operation=0;
    records[2].path="0:/missing-marker";
    CHECK(kui_vmu_game_candidates_preflight_repair(plans,4u,&fixture.storage,&repair_bad,NULL,NULL)==KUI_VMU_GAME_CANDIDATES_STALE);
    CHECK(zero(&repair_bad,sizeof(repair_bad)));records[2]=saved_records[2];
    CHECK(!memcmp(bad,legacy_wire,sizeof(legacy_wire)));
}
int main(void) {
    roundtrip();corruption();limits();shared_legacy_preflight();
    printf("VMU game candidates codec/bounds: %u checks passed\n",checks);return 0;
}
