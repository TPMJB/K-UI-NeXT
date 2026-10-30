/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_terminal_dispatch.h"
#define main lifetime_original_main
#include "test_vmu_native_lifetime.c"
#undef main
#include <stdlib.h>
#include "kui/vmu_native_program.h"
static struct kui_vmu_native_exit terminal_exit;
static const uint32_t terminal_target=UINT32_C(0x8c005400);
static bool normalized(void *context,uint32_t address,uint32_t *value) {
    if(!read_word(context,address,value)) return false;
    (void)kui_vmu_native_terminal_dispatch_normalize(&terminal_exit,read_word,context,
        terminal_target,address,lease.owner.dispatch_cell,lease.owner.dispatch_table,value);
    return true;
}
static void half(uint32_t at,uint16_t value) {
    uint32_t old=0u;unsigned shift=(at&2u)*8u;
    (void)read_word(NULL,at&~3u,&old);
    put(at&~3u,(old&~(UINT32_C(0xffff)<<shift))|((uint32_t)value<<shift));
}
static void terminal_fixture(uint32_t alias) {
    fixture(alias);
    terminal_exit=(struct kui_vmu_native_exit){.version=KUI_VMU_NATIVE_EXIT_VERSION,.bytes=sizeof(terminal_exit),
        .family=KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH,.load=UINT32_C(0x8c010000),
        .image_bytes=UINT32_C(0x200000),.count=3u,
        .entries={UINT32_C(0x8c012400),UINT32_C(0x8c012430),UINT32_C(0x8c0206e6)},
        .terminal_dispatch={UINT32_C(0x8c030000)}};
    uint32_t common=terminal_exit.entries[2];
    for(unsigned i=0;i<4u;i++) put(terminal_exit.terminal_dispatch.table+4u*i,
        common+kui_vmu_native_terminal_dispatch_entries[i]);
    for(unsigned i=0;i<8u;i++) put(common+kui_vmu_native_terminal_dispatch_words[i].offset,
        kui_vmu_native_terminal_dispatch_words[i].value);
    uint16_t patch[6];CHECK(kui_vmu_native_patch_encode(common,terminal_target,patch));
    for(unsigned i=0;i<6u;i++) half(common+2u*i,patch[i]);
    reads=fail_read=0u;
}
static bool live(void) {
    return kui_vmu_native_terminal_dispatch_live(&terminal_exit,read_word,NULL,terminal_target);
}
static void synthetic(uint32_t alias) {
    terminal_fixture(alias);CHECK(sizeof(terminal_exit)==52u);CHECK(live());
    struct kui_vmu_native_lifetime_plan plan;
    CHECK(kui_vmu_native_lifetime_prepare(&plan,&lease,read_word,NULL,1u,NULL,0u));
    reads=0u;CHECK(kui_vmu_native_lifetime_identity_prepared_inline(&plan,read_word,NULL,1u));unsigned plain=reads;
    reads=0u;CHECK(kui_vmu_native_lifetime_identity_prepared_inline(&plan,normalized,NULL,1u));CHECK(reads==plain);
    put(lease.owner.dispatch_cell,terminal_exit.terminal_dispatch.table);
    CHECK(!kui_vmu_native_lifetime_identity_prepared_inline(&plan,read_word,NULL,1u));
    CHECK(kui_vmu_native_lifetime_identity_prepared_inline(&plan,normalized,NULL,1u));
    CHECK(kui_vmu_native_heap_lease_valid(&lease,normalized,NULL,1u));
    CHECK(kui_vmu_native_lifetime_release_prepared_inline(&plan,normalized,NULL,1u,candidate));
    CHECK(!kui_vmu_native_lifetime_release_prepared_inline(&plan,normalized,NULL,1u,lease.payload));
    CHECK(!kui_vmu_native_heap_lease_valid_with_release(&lease,normalized,NULL,1u,lease.payload,NULL));
    for(unsigned i=0;i<8u;i++) if(i!=3u) {
        uint32_t at=plan.identity[i].address,old;CHECK(read_word(NULL,at,&old));put(at,old^1u);
        CHECK(!kui_vmu_native_lifetime_identity_prepared_inline(&plan,normalized,NULL,1u));
        CHECK(!kui_vmu_native_heap_lease_valid(&lease,normalized,NULL,1u));put(at,old);
    }
    for(unsigned i=0;i<4u;i++) for(unsigned bit=0;bit<32u;bit++) {
        uint32_t at=terminal_exit.terminal_dispatch.table+4u*i,old;CHECK(read_word(NULL,at,&old));put(at,old^(UINT32_C(1)<<bit));
        CHECK(!live());CHECK(!kui_vmu_native_heap_lease_valid(&lease,normalized,NULL,1u));put(at,old);
    }
    for(unsigned i=0;i<8u;i++) for(unsigned bit=0;bit<32u;bit++) {
        const struct kui_vmu_native_terminal_dispatch_word *w=&kui_vmu_native_terminal_dispatch_words[i];
        if(!(w->mask&(UINT32_C(1)<<bit))) continue;
        uint32_t at=terminal_exit.entries[2]+w->offset,old;CHECK(read_word(NULL,at,&old));put(at,old^(UINT32_C(1)<<bit));
        CHECK(!live());CHECK(!kui_vmu_native_heap_lease_valid(&lease,normalized,NULL,1u));put(at,old);
    }
    for(unsigned i=0;i<6u;i++) for(unsigned bit=0;bit<16u;bit++) {
        uint32_t at=terminal_exit.entries[2]+2u*i,old;CHECK(read_word(NULL,at&~3u,&old));
        put(at&~3u,old^(UINT32_C(1)<<(bit+(at&2u)*8u)));
        CHECK(!live());CHECK(!kui_vmu_native_heap_lease_valid(&lease,normalized,NULL,1u));put(at&~3u,old);
    }
    const uint32_t unknown[]={0u,UINT32_MAX,UINT32_C(0x8c030004),UINT32_C(0xac030000),UINT32_C(0x0c030000)};
    for(unsigned i=0;i<sizeof(unknown)/sizeof(unknown[0]);i++) {
        put(lease.owner.dispatch_cell,unknown[i]);CHECK(!kui_vmu_native_heap_lease_valid(&lease,normalized,NULL,1u));
        uint32_t value=unknown[i];CHECK(!kui_vmu_native_terminal_dispatch_normalize(&terminal_exit,read_word,NULL,
            terminal_target,lease.owner.dispatch_cell,lease.owner.dispatch_cell,lease.owner.dispatch_table,&value));CHECK(value==unknown[i]);
    }
    put(lease.owner.dispatch_cell,terminal_exit.terminal_dispatch.table);
    reads=0u;CHECK(live());unsigned count=reads;
    for(unsigned i=1u;i<=count;i++) {reads=0u;fail_read=i;CHECK(!live());}fail_read=0u;
    struct kui_vmu_native_exit saved=terminal_exit;
    terminal_exit.family=KUI_VMU_NATIVE_EXIT_PAIR_ONLY;CHECK(!live());terminal_exit=saved;
    terminal_exit.count=2u;CHECK(!live());terminal_exit=saved;
    terminal_exit.entries[2]+=2u;CHECK(!live());terminal_exit=saved;
    terminal_exit.terminal_dispatch.table=UINT32_C(0x8c20fff4);CHECK(!live());terminal_exit=saved;
    terminal_exit.image_bytes=UINT32_MAX;CHECK(!live());terminal_exit=saved;
    terminal_exit.load=UINT32_C(0xec010000);CHECK(!live());terminal_exit=saved;
    CHECK(!kui_vmu_native_terminal_dispatch_live(&terminal_exit,NULL,NULL,terminal_target));
    CHECK(!kui_vmu_native_terminal_dispatch_live(NULL,read_word,NULL,terminal_target));
    put(lease.begin+4u,UINT32_MAX);CHECK(!kui_vmu_native_heap_lease_valid(&lease,normalized,NULL,1u));
}
struct actual_image {unsigned char *bytes;size_t count;uint32_t load;};
static bool actual_read(void *context,uint32_t address,uint32_t *value) {
    struct actual_image *a=context;uint32_t at=physical(address),base=physical(a->load);
    if((address&3u) || at<base || at-base>a->count || a->count-(at-base)<4u) return false;
    const unsigned char *p=a->bytes+at-base;
    *value=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;return true;
}
static void actual(const char *name) {
    FILE *f=fopen(name,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long count=ftell(f);CHECK(count>0);rewind(f);
    struct actual_image a={malloc((size_t)count),(size_t)count,UINT32_C(0x8c010000)};
    CHECK(a.bytes);CHECK(fread(a.bytes,1,a.count,f)==a.count);fclose(f);
    size_t extent;CHECK(kui_vmu_native_program_extent(a.bytes,a.count,a.load,&extent));a.count=extent;
    struct kui_vmu_native_exit e;CHECK(kui_vmu_native_exit_discover(a.bytes,a.count,a.load,&e));
    if(e.family==KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH) {
        CHECK(kui_vmu_native_terminal_dispatch_tagged(&e));
        CHECK(!kui_vmu_native_terminal_dispatch_live(&e,actual_read,&a,terminal_target));
        uint16_t patch[6];CHECK(kui_vmu_native_patch_encode(e.entries[2],terminal_target,patch));
        size_t at=e.entries[2]-a.load;unsigned char saved[12];memcpy(saved,a.bytes+at,12u);
        memcpy(a.bytes+at,patch,sizeof(patch));CHECK(kui_vmu_native_terminal_dispatch_live(&e,actual_read,&a,terminal_target));
        for(unsigned i=0;i<12u;i++) {a.bytes[at+i]^=1u;CHECK(!kui_vmu_native_terminal_dispatch_live(&e,actual_read,&a,terminal_target));a.bytes[at+i]^=1u;}
        memcpy(a.bytes+at,saved,12u);
        const unsigned mutation[]={2u,4u,8u,10u,14u,18u,24u,26u,30u,32u,34u,36u,0x26u,0x2au,0x7au,0x7eu,0x82u,0x86u,0x8au,0x8eu,0x90u,0x92u,0x94u,0x96u,0x98u,0x9au,0x9cu,0x9eu,0xe6u,0xeau,0xeeu};
        for(unsigned i=0;i<sizeof(mutation)/sizeof(mutation[0]);i++) {
            size_t pos=at+mutation[i];a.bytes[pos]^=1u;struct kui_vmu_native_exit changed;
            bool ok=kui_vmu_native_exit_discover(a.bytes,a.count,a.load,&changed);
            CHECK(!ok || changed.family!=KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH);a.bytes[pos]^=1u;
        }
        unsigned char before[16];memcpy(before,a.bytes+0x200u,sizeof(before));
        memcpy(a.bytes+0x200u,a.bytes+(e.terminal_dispatch.table-a.load),16u);
        struct kui_vmu_native_exit duplicate;CHECK(!kui_vmu_native_exit_discover(a.bytes,a.count,a.load,&duplicate));
        memcpy(a.bytes+0x200u,before,sizeof(before));
        printf("Actual terminal-dispatch capability: %s common=%08x table=%08x\n",name,e.entries[2],e.terminal_dispatch.table);
    } else printf("No terminal-dispatch capability: %s family=%u\n",name,e.family);
    free(a.bytes);
}
int main(int argc,char **argv) {
    synthetic(0u);synthetic(UINT32_C(0x80000000));synthetic(UINT32_C(0xa0000000));
    for(int i=1;i<argc;i++) actual(argv[i]);
    printf("Terminal dispatch: %u checks passed\n",checks);return 0;
}
