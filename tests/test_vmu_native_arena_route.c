/* SPDX-License-Identifier: GPL-3.0-only */
/* The stage's discovery route for an executable that links no SDK allocator,
 * on the owner's own executable: the same calls, in the same order, as the
 * dual stage's relay, installer and the worker's arm. No retail bytes are
 * stored here; without an argument only the refusals of the route run. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui/hash.h"
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_exit.h"
#include "kui/vmu_native_layout.h"
#include "kui/vmu_native_lifetime.h"
#include "kui/vmu_native_program.h"
#include "kui/vmu_native_stage_placement.h"
#include "kui/vmu_native_background.h"

#define BASE UINT32_C(0x8c010000)
#define STAGE UINT32_C(0x8ce00000)
/* A linked compact stage ends below 0x8ce60000, BSS and installer stack
 * included; the route must not depend on how far below. */
#define STAGE_END UINT32_C(0x8ce60000)
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint8_t *load_file(const char *path,size_t *bytes) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
    uint8_t *data=malloc((size_t)n);CHECK(data);CHECK(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);
    *bytes=(size_t)n;return data;
}
static bool overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    a&=UINT32_C(0x1fffffff);b&=UINT32_C(0x1fffffff);
    return a<b+bn && b<a+an;
}
/* The worker's own first eight header words at the arena start. */
static bool header_reader(void *context,uint32_t address,uint32_t *value) {
    (void)context;*value=UINT32_C(0x4b554900)|(address&31u);return true;
}
int main(int argc,char **argv) {
    if(argc<2) {
        printf("Native arena route: no executable given\n");return 0;
    }
    size_t n;uint8_t *image=load_file(argv[1],&n);
    /* Relay: the whole file is the program, the early driver resolves with
     * its layout, and its digest is the image's own SHA-256. */
    size_t program=0u;
    CHECK(kui_vmu_native_program_extent(image,n,BASE,&program) && program==n);
    struct kui_vmu_native_discovery driver;struct kui_vmu_native_layout layout;
    CHECK(kui_vmu_native_discover_layout(image,n,BASE,&driver,&layout));
    CHECK(driver.family==KUI_VMU_NATIVE_FAMILY_EARLY_V1 && driver.load_address==BASE &&
          driver.image_bytes==n);
    struct kui_sha256 s;uint8_t sha[32];
    kui_sha256_init(&s);kui_sha256_update(&s,image,n);kui_sha256_digest(&s,sha);
    CHECK(!memcmp(sha,driver.image_sha256,32u));
    /* No SDK allocator; only the reviewed arena plan for this image. */
    struct kui_vmu_native_heap heap,owner;struct kui_vmu_native_heap_call call;
    struct kui_vmu_native_heap_fixed_hook hook;
    CHECK(kui_vmu_native_heap_discover(image,n,BASE,&heap)==KUI_VMU_NATIVE_HEAP_NONE);
    CHECK(kui_vmu_native_heap_arena_plan(image,n,driver.image_sha256,BASE,&owner,&call,&hook));
    CHECK(kui_vmu_native_stage_arena_safe((uint32_t)n,STAGE,STAGE_END,call.entry_begin,call.entry_bytes));
    CHECK(call.entry_begin+call.entry_bytes==STAGE &&
          call.entry_bytes==KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES);
    /* The installer's lease, from the low heap hook's arguments. */
    struct kui_vmu_native_heap_lease lease;
    CHECK(kui_vmu_native_heap_arena_lease(&owner,call.entry_begin,call.entry_bytes,
        KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES,&lease));
    CHECK(kui_vmu_native_heap_lease_valid(&lease,NULL,NULL,1u));
    /* Prepare: the exits, guarded by the submit window alone. */
    const struct kui_vmu_native_patch_window windows[1]={{driver.submit,12u}};
    struct kui_vmu_native_exit exits;
    CHECK(kui_vmu_native_exit_discover_guarded(image,n,BASE,windows,1u,&exits));
    CHECK(exits.family==KUI_VMU_NATIVE_EXIT_EARLY_PAIR && exits.count==2u &&
          exits.entries[1]==exits.entries[0]+8u);
    for(unsigned i=0;i<exits.count;i++) CHECK(!overlap(exits.entries[i],12u,driver.submit,12u));
    /* Arm: every span the worker protects lies outside the arena, and the
     * lifetime plan admits the lease with exactly those spans. */
    struct kui_vmu_native_lifetime_range ranges[KUI_VMU_NATIVE_LIFETIME_RANGES];size_t count=0u;
#define PROTECT(address,bytes) do {CHECK(count<KUI_VMU_NATIVE_LIFETIME_RANGES); \
    ranges[count++]=(struct kui_vmu_native_lifetime_range){(address),(bytes)}; \
    CHECK(!overlap((address),(bytes),lease.payload,lease.lease_bytes));} while(0)
    CHECK(layout.inputs && layout.inputs<=KUI_VMU_NATIVE_LAYOUT_INPUTS);
    PROTECT(driver.load_address,driver.image_bytes);PROTECT(layout.tx_base,0x6000u);
    PROTECT(driver.manager_initial,0x1100u);PROTECT(driver.manager_cell,4u);
    PROTECT(driver.lock_cell,5u);PROTECT(driver.completion_slot,4u);PROTECT(layout.rx_base,4u);
    PROTECT(layout.input_array,layout.inputs*4u);PROTECT(layout.index_array,layout.inputs*4u);
    PROTECT(layout.gun_flag,4u);PROTECT(layout.timer1_callback,4u);PROTECT(layout.timer2_callback,4u);
    unsigned slots=0u;
    for(unsigned i=0;i<KUI_VMU_NATIVE_RX_SLOTS;i++)
        if(layout.rx_slots[i]) {PROTECT(layout.rx_slots[i],1024u);++slots;}
#undef PROTECT
    struct kui_vmu_native_lifetime_plan plan;
    CHECK(kui_vmu_native_lifetime_prepare(&plan,&lease,header_reader,NULL,1u,ranges,count));
    CHECK(plan.ready==KUI_VMU_NATIVE_LIFETIME_READY && !plan.count && !plan.sentinel && !plan.cursor);
    /* The same spans moved into the arena refuse the plan. */
    ranges[1].begin=lease.payload+0x100u;
    CHECK(!kui_vmu_native_lifetime_prepare(&plan,&lease,header_reader,NULL,1u,ranges,count));
    /* The digest the stage covers instead of allocator code: the borrowed
     * site lies in the image, and the literal is the hook's slot. */
    CHECK(call.load>=BASE && call.load+6u<=BASE+n && call.call==call.load+2u);
    CHECK(hook.slot==owner.initializer_slot);
    printf("Native arena route: %u checks; program %zu bytes, early driver, early exit pair at %08x,"
        " arena %08x+%x outside %zu protected spans (%u receive slots)\n",
        checks,program,exits.entries[0],call.entry_begin,call.entry_bytes,count,slots);
    free(image);return 0;
}
