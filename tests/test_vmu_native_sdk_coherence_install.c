/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual temporary installer against owner-supplied
 * SDK images and cache metadata, including both initializer replay families. */
#define _GNU_SOURCE
#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1
#define KUI_VMU_NATIVE_READONLY_PROBE 1
#define KUI_VMU_NATIVE_CACHED_CODE 1
#include "kui/vmu_native_passive.h"
#include "kui/vmu_native_patch.h"
#include "kui/vmu_native_program.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#define KUI_RETAIL_EXEC_ADDRESS UINT32_C(0x8c010000)
#define DEST UINT32_C(0xac700000)
#define BYTES UINT32_C(0x4000)
static uint32_t exec_bytes;
static struct kui_vmu_native_discovery native_driver;
static struct kui_vmu_native_layout passive_layout;
static struct kui_vmu_native_heap native_heap;
static struct kui_vmu_native_exit native_exits;
static uint32_t call_slot;
static unsigned publishes,patches;
static uint32_t target;
static uint32_t native_word(uint32_t at) {return *(const uint32_t *)(uintptr_t)at;}
static bool passive_entry(uint32_t at,uint32_t dest,uint32_t bytes) {
    dest-=UINT32_C(0x20000000);
    return !(at&1u) && at>=dest+80u && at<dest+bytes;
}
static void kui_native_passive_publish(uint32_t begin,uint32_t end) {
    assert(begin==call_slot && end==begin+4u);
    memcpy((void *)(uintptr_t)(begin|UINT32_C(0x20000000)),(void *)(uintptr_t)begin,4u);
    ++patches;++publishes;target=*(const uint32_t *)(uintptr_t)begin;
}
#include "../src/loader/vmu_native_sdk_coherence_stage.inc"
static struct kui_vmu_native_passive_exports exports;
static uint32_t table_words[10],state_words[12];
static uint8_t *original;
static size_t source_bytes;
static uint8_t source_digest[32];
static void digest_bytes(const void *bytes,size_t count,uint8_t digest[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,bytes,count);kui_sha256_digest(&hash,digest);
}
static const char *const names[]={"evolution1","ps2","skies","sa2","grandia2","evolution2"};
static uint32_t *table(void) {return (void *)(uintptr_t)(DEST+0x80u);}
static uint32_t *state(void) {return (void *)(uintptr_t)(DEST+BYTES);}
static void reset(void) {
    memcpy((void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,original,exec_bytes);
    memcpy((void *)(uintptr_t)(KUI_RETAIL_EXEC_ADDRESS|UINT32_C(0x20000000)),original,exec_bytes);
    memcpy(table(),table_words,sizeof(table_words));memcpy(state(),state_words,sizeof(state_words));
    exports=(struct kui_vmu_native_passive_exports){.trace_exports=DEST+0x80u,
        .bss_begin=DEST+BYTES,.bss_end=DEST+BYTES+0x100u,.stack_bottom=DEST+0x1f000u};
    publishes=patches=0u;target=0u;
}
static void refused(void) {
    uint8_t before[12],mirror[12];uint32_t private_before[12];
    uint32_t pointer_before=native_word(call_slot),pointer_mirror=native_word(call_slot|UINT32_C(0x20000000));
    uint32_t at=passive_layout.initializer;
    memcpy(before,(void *)(uintptr_t)at,sizeof(before));
    memcpy(mirror,(void *)(uintptr_t)(at|UINT32_C(0x20000000)),sizeof(mirror));
    memcpy(private_before,state(),sizeof(private_before));
    assert(native_sdk_coherence_install(&exports,DEST,BYTES)==-1);
    assert(!publishes && !patches);
    assert(native_word(call_slot)==pointer_before && native_word(call_slot|UINT32_C(0x20000000))==pointer_mirror);
    assert(!memcmp(before,(void *)(uintptr_t)at,sizeof(before)));
    assert(!memcmp(mirror,(void *)(uintptr_t)(at|UINT32_C(0x20000000)),sizeof(mirror)));
    assert(!memcmp(private_before,state(),sizeof(private_before)));
}
static void map_ram(uint32_t at) {
    void *p=mmap((void *)(uintptr_t)at,0xc00000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(p==(void *)(uintptr_t)at);
}
static void load(unsigned game,const char *path) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long bytes=ftell(f);assert(bytes>0 && bytes<=0xc00000);rewind(f);
    source_bytes=(size_t)bytes;original=malloc(source_bytes);assert(original);
    assert(fread(original,1,source_bytes,f)==source_bytes);assert(!fclose(f));
    /* The owner file remains the raw source. Only its independently proved
     * loaded-program prefix participates in metadata binding and RAM copies. */
    digest_bytes(original,source_bytes,source_digest);
    size_t program_bytes=SIZE_MAX;
    assert(kui_vmu_native_program_extent(original,source_bytes,KUI_RETAIL_EXEC_ADDRESS,&program_bytes));
    assert(program_bytes>0u && program_bytes<=source_bytes && program_bytes<=UINT32_MAX);
    exec_bytes=(uint32_t)program_bytes;
    char fixture[128];assert(snprintf(fixture,sizeof(fixture),"tests/fixtures/native-cache/%s.nvr",names[game])>0);
    f=fopen(fixture,"rb");assert(f);uint8_t wire[1024];
    assert(fread(wire,1,sizeof(wire),f)==sizeof(wire));assert(fgetc(f)==EOF);assert(!fclose(f));
    memcpy(&native_driver,wire+64u,sizeof(native_driver));
    memcpy(&passive_layout,wire+168u,sizeof(passive_layout));
    memcpy(&native_heap,wire+608u,sizeof(native_heap));memcpy(&native_exits,wire+700u,sizeof(native_exits));
    assert(native_driver.image_bytes==exec_bytes && native_driver.load_address==KUI_RETAIL_EXEC_ADDRESS);
    assert(native_heap.image_bytes==exec_bytes && native_heap.load==KUI_RETAIL_EXEC_ADDRESS);
    assert(native_exits.image_bytes==exec_bytes && native_exits.load==KUI_RETAIL_EXEC_ADDRESS);
    uint8_t digest[32];digest_bytes(original,exec_bytes,digest);
    assert(!memcmp(digest,native_driver.image_sha256,sizeof(digest)));
    if(program_bytes!=source_bytes) assert(memcmp(source_digest,digest,sizeof(digest)));
    const uint32_t words[10]={UINT32_C(0x4b545231),2u,40u,
        UINT32_C(0x8c700100),UINT32_C(0x8c700200),UINT32_C(0x8c700300),UINT32_C(0x8c700400),
        UINT32_C(0xac700500),UINT32_C(0xac700600),DEST+BYTES};
    memcpy(table_words,words,sizeof(words));memset(state_words,0,sizeof(state_words));
    uint32_t lock=(native_driver.lock_cell&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    uint32_t init=(passive_layout.initializer&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    state_words[0]=lock;state_words[1]=native_driver.manager_cell;state_words[2]=native_driver.manager_initial;
    state_words[3]=init+12u;state_words[7]=UINT32_C(0x4e434f31);
    state_words[8]=(lock+4u)|UINT32_C(0xa0000000);
    state_words[9]=passive_layout.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48?0u:1u;
    state_words[10]=init;
    uint32_t load=passive_layout.initializer_wrapper+(state_words[9]?46u:42u);
    uint16_t instruction;memcpy(&instruction,original+(load-KUI_RETAIL_EXEC_ADDRESS),2u);
    call_slot=((load+4u)&~3u)+4u*(instruction&255u);
}
int main(int argc,char **argv) {
    assert(argc==6 || argc==7);map_ram(KUI_RETAIL_EXEC_ADDRESS);map_ram(KUI_RETAIL_EXEC_ADDRESS|UINT32_C(0x20000000));
    unsigned refusals=0u,interiors=0u;
    for(unsigned game=0;game<(unsigned)argc-1u;game++) {
        load(game,argv[game+1u]);reset();assert(native_sdk_coherence_window_valid());
        assert(native_sdk_coherence_cached_admit());
        assert(native_sdk_coherence_install(&exports,DEST,BYTES)==1 && patches==1u && publishes==1u);
        assert(target==table_words[7u+state_words[9]]);
        assert(native_word(call_slot)==target && native_word(call_slot|UINT32_C(0x20000000))==target);
        assert(!memcmp((void *)(uintptr_t)passive_layout.initializer,
            original+passive_layout.initializer-KUI_RETAIL_EXEC_ADDRESS,12u));
        patches=publishes=0u;refused();++refusals;
        for(unsigned byte=0;byte<12u;byte++) for(unsigned bit=0;bit<8u;bit++) {
            reset();*(uint8_t *)(uintptr_t)(passive_layout.initializer+byte)^=(uint8_t)(1u<<bit);refused();++refusals;
        }
        for(unsigned i=0;i<10u;i++) {reset();table()[i]^=1u;refused();++refusals;}
        for(unsigned i=0;i<12u;i++) {reset();state()[i]^=1u;refused();++refusals;}
        /* The initializer function-pointer slot is admitted through its
         * actual source load/call and IRQ-masked argument wrapper. */
        unsigned variable=state_words[9],load_at=variable?46u:42u;
        const unsigned wrappers[]={6u,variable?32u:28u,variable?34u:30u,
            variable?40u:36u,load_at,load_at+6u,load_at+8u};
        for(unsigned i=0;i<sizeof(wrappers)/sizeof(*wrappers);i++) {
            reset();uint16_t *instruction=(void *)(uintptr_t)(passive_layout.initializer_wrapper+wrappers[i]);
            *instruction^=(uint16_t)(i==3u?0x1000u:i<5u?0x0100u:1u);refused();++refusals;
        }
        reset();uint16_t *load=(void *)(uintptr_t)(passive_layout.initializer_wrapper+load_at);
        *load^=1u;refused();++refusals;
        reset();exports.bss_begin+=4u;refused();++refusals;
        reset();exports.bss_end=exports.bss_begin+47u;refused();++refusals;
        reset();table()[9]=exports.bss_end-44u;refused();++refusals;
        reset();table()[7]-=UINT32_C(0x20000000);refused();++refusals;
        reset();table()[8]-=UINT32_C(0x20000000);refused();++refusals;
        reset();table()[9]-=UINT32_C(0x20000000);refused();++refusals;
        /* Existing direct pointers/branches into the untouched SDK entry
         * remain legal; only the proved indirect call-target cell changes. */
        reset();*(uint32_t *)(uintptr_t)(KUI_RETAIL_EXEC_ADDRESS+0x80u)=passive_layout.initializer+2u;
        assert(native_sdk_coherence_cached_admit() && !patches && !publishes);++interiors;
        reset();*(uint32_t *)(uintptr_t)call_slot+=4u;refused();++refusals;
        reset();uint32_t prior=passive_layout.gun_flag;passive_layout.gun_flag=call_slot;refused();
        passive_layout.gun_flag=prior;++refusals;
        reset();prior=native_exits.entries[0];native_exits.entries[0]=call_slot;refused();
        native_exits.entries[0]=prior;++refusals;
        if(passive_layout.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48) {
            const uint32_t literals[]={((passive_layout.initializer+6u)&~3u)+4u*0x3eu,
                ((passive_layout.initializer+8u)&~3u)+4u*0x3eu,passive_layout.initializer+10u+2u*0x71u};
            for(unsigned i=0;i<3u;i++) {reset();*(uint8_t *)(uintptr_t)literals[i]^=1u;refused();++refusals;}
        }
        printf("Cold SDK installer accepted %s %s initializer %08x; source %zuB/program %uB; exact prefix, private state and P2 target\n",
            names[game],state_words[9]?"variable":"uniform",passive_layout.initializer,source_bytes,exec_bytes);
        uint8_t source_after[32];digest_bytes(original,source_bytes,source_after);
        assert(!memcmp(source_after,source_digest,sizeof(source_after)));
        free(original);
    }
    printf("Cold SDK installer: %u mutation refusals before any write; %u untouched initializer interior pointers preserved\n",refusals,interiors);
    return 0;
}
