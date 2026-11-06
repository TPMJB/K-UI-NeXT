/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute actual native A1 VMU-lite completion before the automatic MDE
 * admission report. No packet, ownership, or storage code is stubbed. */
#include "kui/smash_native_child.h"
#define KUI_SPLIT_CONTROLLER_TEST_MAIN unused_split_regression
#include "test_vmu_native_split_controller.c"

static unsigned checkpoint_reads,status_reads;
static void smash_read_observer(uint32_t address,unsigned width) {
    if(address==KUI_SMASH_SRAM_BYTES) {CHECK(width==4u);++checkpoint_reads;}
    if(address==KUI_SMASH_BUP_APP) {CHECK(width==2u);++status_reads;}
}
static void smash_setup(unsigned sram) {
    static const uint8_t digest[32]={0x04,0xf4,0x9d,0x58,0x3b,0x39,0xbd,0xc7,
        0x10,0xce,0xc6,0xd0,0xa4,0x34,0xb5,0xd7,0x10,0xb9,0xbc,0xce,0xdd,0x53,0x9b,0xed,
        0xda,0x29,0x75,0x51,0x35,0xf0,0xfe,0x61};
    split_prepare();
    input.driver.image_bytes=KUI_SMASH_NATIVE_ENGINE_BYTES;
    memcpy(input.driver.image_sha256,digest,sizeof(digest));
    input.exits.image_bytes=input.lease.owner.image_bytes=input.driver.image_bytes;
    memset(&smash_vmu_probe,0,sizeof(smash_vmu_probe));
    checkpoint_reads=status_reads=0u;
    arm();install_hooks();CHECK(split_controller_geometry());
    put(KUI_SMASH_SRAM_BYTES,4u,sram);
    put(KUI_SMASH_BUP_APP,4u,0u);
    /* Missing BUP registration is itself the useful failure: the probe must
     * not require the very callback/mask whose absence it needs to report. */
    put(KUI_SMASH_BUP_JOB+116u,4u,0u);put(KUI_SMASH_BUP_MASK,4u,0u);
    read_observer=smash_read_observer;
}
static void smash_round(bool expect_report) {
    split_batch(1u,0u,0u);split_submit_ok();split_done(1u,0u,0x01000000u);
    unsigned completed=state.completed;
    if(setjmp(terminal)==0) {
        kui_vmu_native_passive_complete(&frame);
        CHECK(!expect_report && !state.pending && state.submitted==state.completed);
    } else {
        CHECK(expect_report && terminal_phase==16u && terminal_error==KUI_SMASH_VMU_PROBE_DETAIL);
        CHECK(state.error==KUI_SMASH_VMU_PROBE_DETAIL && !state.pending);
    }
    CHECK(state.completed==completed+1u && !controller_pair.pending);
    CHECK(!measured_work.storage && !lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
}
int main(void) {
    CHECK(VIRTUAL==1u && PHYSICAL==2u && WORKER_SPAN==0x12000u);
    smash_setup(8192u);
    for(unsigned n=1u;n<=576u;n++) {
        smash_round(n==576u);
        CHECK(smash_vmu_probe.completions==n && !smash_vmu_probe.ready);
        CHECK(checkpoint_reads==n/64u && status_reads==n/64u);
        CHECK(smash_vmu_probe.registered_at==(n>=64u?64u:0u));
    }
    CHECK(state.completed==576u && overlay.stats.advertisements==576u);
    CHECK(!value_at(KUI_SMASH_BUP_JOB+116u) && !value_at(KUI_SMASH_BUP_MASK));

    smash_setup(8192u);
    for(unsigned n=1u;n<=255u;n++)smash_round(false);
    put(KUI_SMASH_BUP_APP,4u,1u);
    for(unsigned n=256u;n<=900u;n++)smash_round(false);
    CHECK(smash_vmu_probe.ready==1u && smash_vmu_probe.completions==256u);
    CHECK(checkpoint_reads==4u && status_reads==4u && !state.error);

    smash_setup(0u);
    for(unsigned n=1u;n<=640u;n++)smash_round(false);
    CHECK(!smash_vmu_probe.registered_at && !smash_vmu_probe.ready);
    CHECK(checkpoint_reads==10u && !status_reads && !state.error);
    printf("PASS MDE native A1 worker probe: %u checks;576 real completions report missing card registration, ready card disables, no pre-ROM report or SD work\n",checks);
    return 0;
}
