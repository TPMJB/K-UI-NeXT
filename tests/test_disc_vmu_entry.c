/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_vmu_entry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define LOAD UINT32_C(0x8c010000)
static unsigned checks;
static uint8_t image[512];
static void put(unsigned at,uint16_t value) {
    assert(at+2u<=sizeof(image));image[at]=(uint8_t)value;image[at+1u]=(uint8_t)(value>>8);
}
static void fresh(void) {memset(image,0,sizeof(image));}
static void check(size_t bytes,bool expected,uint32_t reason,uint32_t at,uint32_t opcode) {
    uint8_t original[sizeof(image)];memcpy(original,image,sizeof(image));
    struct kui_disc_vmu_entry_failure f={UINT32_MAX,UINT32_MAX,UINT32_MAX};
    bool got=kui_disc_vmu_entry_drops_inherited_pr(image,bytes,LOAD,&f);
    if(got!=expected) fprintf(stderr,"entry case%u expected%u got%u first%02x%02x bytes%zu failure%u at%08x op%04x\n",
        checks,(unsigned)expected,(unsigned)got,image[1],image[0],bytes,f.reason,f.address,f.opcode);
    assert(got==expected);++checks;
    assert(!memcmp(original,image,sizeof(image)));
    if(expected) assert(!f.address && !f.opcode && !f.reason);
    else assert(f.reason==reason && f.address==LOAD+at && f.opcode==opcode);
    assert(kui_disc_vmu_entry_drops_inherited_pr(image,bytes,LOAD,NULL)==expected);++checks;
}
static void calls_and_explicit_replacements(void) {
    for(unsigned reg=0u;reg<16u;reg++) {
        fresh();put(0u,(uint16_t)(0x400bu|reg<<8));put(2u,9u);
        check(4u,true,0u,0u,0u);
        fresh();put(0u,(uint16_t)(0x402au|reg<<8));check(2u,true,0u,0u,0u);
        fresh();put(0u,(uint16_t)(0x4026u|reg<<8));check(2u,true,0u,0u,0u);
    }
    /* This predicate ends at the call; target ownership is a separate game
     * code contract, and a large relative target does not retain old PR. */
    fresh();put(0u,0xb7ffu);put(2u,0x64f3u);check(4u,true,0u,0u,0u);
    fresh();put(0u,0xb800u);put(2u,9u);check(4u,true,0u,0u,0u);
}
static void inherited_pr_reads_and_delay_slots(void) {
    for(unsigned reg=0u;reg<16u;reg++) for(unsigned memory=0u;memory<2u;memory++) {
        uint16_t read=(uint16_t)((memory?0x4022u:0x002au)|reg<<8);
        fresh();put(0u,read);put(2u,0x430bu);put(4u,9u);
        check(6u,false,KUI_DISC_VMU_ENTRY_PR_READ,0u,read);
        /* Even though JSR replaces PR before its slot, conservative slot
         * admission keeps all STS PR out of this initial-entry proof. */
        fresh();put(0u,0x430bu);put(2u,read);
        check(4u,false,KUI_DISC_VMU_ENTRY_PR_READ,2u,read);
        fresh();put(0u,0xa000u);put(2u,read);put(4u,0x402au);
        check(6u,false,KUI_DISC_VMU_ENTRY_PR_READ,2u,read);
    }
    const uint16_t illegal[]={0x9000u,0xd000u,0xc700u,0xa000u,0xb000u,
        0x8d00u,0x8f00u,0x8900u,0x8b00u,0x430bu,0x432bu,0x0303u,
        0x0323u,0x000bu,0x002bu,0xc300u,0x430eu,0x4307u,0xfffdu};
    for(unsigned i=0u;i<sizeof(illegal)/sizeof(illegal[0]);i++) {
        fresh();put(0u,0xb000u);put(2u,illegal[i]);
        check(4u,false,KUI_DISC_VMU_ENTRY_CONTROL,2u,illegal[i]);
    }
    fresh();put(0u,0xb000u);put(2u,0xf000u);
    check(4u,false,KUI_DISC_VMU_ENTRY_UNKNOWN,2u,0xf000u);
    fresh();put(0u,0xa000u);put(2u,0x4f26u);
    check(4u,true,0u,0u,0u); /* Explicit PR replacement before taking branch. */
}
static void all_branch_paths_and_convergence(void) {
    fresh();put(0u,0x8902u);put(2u,0x402au);put(8u,0x420bu);put(10u,9u);
    check(12u,true,0u,0u,0u);
    put(2u,0x002au);check(12u,false,KUI_DISC_VMU_ENTRY_PR_READ,2u,0x002au);
    put(2u,0x402au);put(8u,0x4f22u);
    check(12u,false,KUI_DISC_VMU_ENTRY_PR_READ,8u,0x4f22u);
    fresh();put(0u,0x8d02u);put(2u,9u);put(4u,0x4f26u);put(8u,0x430bu);put(10u,9u);
    check(12u,true,0u,0u,0u);
    put(0u,0x8f02u);check(12u,true,0u,0u,0u);
    put(0u,0x8b02u);check(12u,true,0u,0u,0u);
    /* Both paths reach the same instruction. The visit set must deduplicate
     * convergence without losing either branch's proof obligation. */
    fresh();put(0u,0x8901u);put(2u,0xa000u);put(4u,9u);put(6u,0x402au);
    check(8u,true,0u,0u,0u);
    fresh();put(0u,0xa002u);put(2u,9u);put(8u,0x4f26u);
    check(10u,true,0u,0u,0u);
}
static void initialization_whitelist(void) {
    const uint16_t plain[]={0xe001u,0x7ffcu,0x1234u,0x5678u,0x6123u,0x2f06u,
        0x312cu,0x8000u,0x8100u,0x8400u,0x8500u,0x8800u,0x9000u,0xd000u,
        0xc700u,0xc900u,0x0008u,0x0018u,0x0028u,0x0019u,0x0009u,
        0x0102u,0x0122u,0x011au,0x016au,0x0129u,0x01a3u,0x0124u,
        0x412cu,0x412du,0x412fu,0x4100u,0x4109u,0x4110u,0x411bu,
        0x4128u,0x4103u,0x4112u,0x4162u,0x410eu,0x412eu,0x4107u,
        0x4127u,0x416au,0x4156u,0x4166u};
    for(unsigned i=0u;i<sizeof(plain)/sizeof(plain[0]);i++) {
        fresh();put(0u,plain[i]);put(2u,0x430bu);put(4u,9u);
        check(6u,true,0u,0u,0u);
    }
    const uint16_t controls[]={0x000bu,0x002bu,0x001bu,0xc37fu,0x432bu,0x0323u,0x0303u,0xfffdu};
    for(unsigned i=0u;i<sizeof(controls)/sizeof(controls[0]);i++) {
        fresh();put(0u,controls[i]);put(2u,9u);
        check(4u,false,KUI_DISC_VMU_ENTRY_CONTROL,0u,controls[i]);
    }
    const uint16_t unknown[]={0x0000u,0x2313u,0x3121u,0x3129u,0x8200u,0x4132u,0xf000u};
    for(unsigned i=0u;i<sizeof(unknown)/sizeof(unknown[0]);i++) {
        fresh();put(0u,unknown[i]);put(2u,0x402au);
        check(4u,false,KUI_DISC_VMU_ENTRY_UNKNOWN,0u,unknown[i]);
    }
}
static void bounds_cycles_budgets_and_arguments(void) {
    fresh();put(0u,0xaffeu);put(2u,9u);
    check(4u,false,KUI_DISC_VMU_ENTRY_BACKWARD,0u,0xaffeu);
    put(0u,0xaffdu);check(4u,false,KUI_DISC_VMU_ENTRY_BACKWARD,0u,0xaffdu);
    put(0u,0x8bfeu);check(4u,false,KUI_DISC_VMU_ENTRY_BACKWARD,0u,0x8bfeu);
    put(0u,0xa7ffu);check(4u,false,KUI_DISC_VMU_ENTRY_BOUNDS,4098u,0u);
    fresh();put(0u,9u);check(2u,false,KUI_DISC_VMU_ENTRY_BOUNDS,2u,0u);
    fresh();put(0u,0x430bu);check(2u,false,KUI_DISC_VMU_ENTRY_BOUNDS,2u,0u);
    check(3u,false,KUI_DISC_VMU_ENTRY_BOUNDS,2u,0u);
    fresh();for(unsigned i=0u;i<64u;i++) put(2u*i,9u);
    put(128u,0x402au);check(130u,false,KUI_DISC_VMU_ENTRY_LIMIT,128u,0u);
    put(126u,0x402au);check(128u,true,0u,0u,0u);
    put(126u,0x430bu);put(128u,9u);
    check(130u,false,KUI_DISC_VMU_ENTRY_LIMIT,128u,9u);
    fresh();put(0u,0x402au);
    struct kui_disc_vmu_entry_failure f={0};
    assert(!kui_disc_vmu_entry_drops_inherited_pr(NULL,2u,LOAD,&f) && f.reason==KUI_DISC_VMU_ENTRY_ARGUMENT);++checks;
    assert(!kui_disc_vmu_entry_drops_inherited_pr(image,1u,LOAD,&f) && f.reason==KUI_DISC_VMU_ENTRY_ARGUMENT);++checks;
    assert(!kui_disc_vmu_entry_drops_inherited_pr(image,sizeof(image),LOAD+1u,&f) && f.reason==KUI_DISC_VMU_ENTRY_ARGUMENT);++checks;
    assert(!kui_disc_vmu_entry_drops_inherited_pr(image,sizeof(image),0x8cffff00u,&f) && f.reason==KUI_DISC_VMU_ENTRY_ARGUMENT);++checks;
    assert(!kui_disc_vmu_entry_drops_inherited_pr(image,sizeof(image),0x9c010000u,&f) && f.reason==KUI_DISC_VMU_ENTRY_ARGUMENT);++checks;
    assert(!kui_disc_vmu_entry_drops_inherited_pr(image,SIZE_MAX,LOAD,&f));++checks;
    assert(kui_disc_vmu_entry_drops_inherited_pr(image,2u,0x0c010000u,&f));++checks;
    assert(kui_disc_vmu_entry_drops_inherited_pr(image,2u,0xac010000u,&f));++checks;
    uint8_t before[sizeof(image)];memcpy(before,image,sizeof(before));
    assert(!kui_disc_vmu_entry_drops_inherited_pr(image,sizeof(image),LOAD,(void *)(image+4u)));++checks;
    assert(!memcmp(before,image,sizeof(image)));
}
int main(void) {
    calls_and_explicit_replacements();inherited_pr_reads_and_delay_slots();
    all_branch_paths_and_convergence();initialization_whitelist();
    bounds_cycles_budgets_and_arguments();
    printf("Physical-disc inherited PR lifetime: %u call, branch, delay-slot, PR-save, cycle, bound and no-mutation checks passed\n",checks);
    return 0;
}
