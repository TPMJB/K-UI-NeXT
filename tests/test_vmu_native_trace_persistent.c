/* SPDX-License-Identifier: GPL-3.0-only */
/* Integration of the real persistent bridge and real Evolution sampler.
 * The existing RAM/SD bus remains synthetic. Faults are injected after the
 * live boundary, and the production guard/builder/verifier makes the refusal. */
#define KUI_VMU_NATIVE_TRACE_HOST_TEST 1
#include "kui/vmu_volatile_chain.h"
static int integration_chain_verify(const struct kui_vmu_volatile_chain_plan *,const uint8_t *,size_t);
#define kui_vmu_volatile_chain_verify integration_chain_verify
#define main persistent_suite_main
#include "test_vmu_native_persistent.c"
#undef main
#undef kui_vmu_volatile_chain_verify
#include "../src/loader/vmu_native_trace.c"

enum injected_fault { INJECT_NONE,INJECT_CONTEXT,INJECT_TARGET,INJECT_VERIFY };
static enum injected_fault injection;
static unsigned sampler_reads,sampler_pixels,sampler_stops;
static uint32_t sampler_control=4u;
static uint32_t stop_reason,stop_value,stop_a,stop_b;

uint32_t kui_vmu_trace_read(uint32_t address,unsigned width) {
    ++sampler_reads;
    int index=mmio_index(address);
    if(index>=0) {CHECK(width==4u);return mmio[index];}
    if(address==0xa05f8044u) return sampler_control;
    if(address==0xa05f805cu) return 319u|(479u<<10)|(1u<<20);
    if(address==0xa05f8050u) return 0u;
    if(address==0xa05f8054u) return 0x100000u;
    if(address==0xffd80004u) {CHECK(width==1u);return 1u;}
    if(address==0xffd80010u) {CHECK(width==2u);return 2u;}
    CHECK(width==4u);
    if(address==0xffd8000cu) return UINT32_MAX-sampler_reads;
    if(address==0xff00001cu) return 0x105u;
    if(address==0xa05f810cu) {
        /* This is the final observation before trace11 returns. The complete
         * live checker has already passed, while DMA has not started. */
        if(trace.hook==0x11u && injection==INJECT_CONTEXT) {
            put(config.driver.manager_initial+0x10c4u,4u,1u);injection=INJECT_NONE;
        } else if(trace.hook==0x11u && injection==INJECT_TARGET) {
            config.lease.payload+=WORKER_SPAN;injection=INJECT_NONE;
        }
        return 0x3820u;
    }
    CHECK(address==0x8c3049a8u || address==0xffa00000u || address==0xffa00004u ||
        address==0xffa00008u || address==0xffa0000cu);
    return 0u;
}
void kui_vmu_trace_pixel(uint32_t address,unsigned width,uint32_t value) {
    (void)value;CHECK(width==2u || width==4u);
    CHECK(address>=0xa5000000u && address+width<=0xa5800000u);++sampler_pixels;
}
void kui_vmu_trace_stop(uint32_t reason,uint32_t value,uint32_t a,uint32_t b) {
    ++sampler_stops;stop_reason=reason;stop_value=value;stop_a=a;stop_b=b;
    longjmp(terminal,1);
}
static int integration_chain_verify(const struct kui_vmu_volatile_chain_plan *plan,
    const uint8_t *bytes,size_t capacity) {
    /* Corruption occurs after the actual builder, before the actual verifier.
     * Return values are never substituted. */
    if(injection==INJECT_VERIFY && trace.enabled && trace.hook==0x11u) {
        CHECK(bytes==issued_chain);issued_chain[0]^=1u;injection=INJECT_NONE;
    }
    return kui_vmu_volatile_chain_verify(plan,bytes,capacity);
}
static void traced_setup(unsigned mode) {
    setup();config.diagnostics=mode;
    struct kui_vmu_native_passive_config exact=config;
    exact.driver.image_bytes=2143652u;exact.driver.load_address=0x8c010000u;
    memcpy(exact.driver.image_sha256,evolution_digest,sizeof(evolution_digest));
    kui_vmu_native_trace_init(&exact);CHECK(trace.enabled && !trace.sequence);
    sampler_control=4u;injection=INJECT_NONE;sampler_stops=0u;
    stop_reason=stop_value=stop_a=stop_b=0u;
}
static void traced_transfers(void) {
    for(unsigned mode=0u;mode<2u;mode++) {
        traced_setup(mode?KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN:KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL);
        sampler_control=5u;unsigned pixels_before=sampler_pixels,reads_before=sampler_reads;
        for(unsigned i=0u;i<16u;i++) {
            if(mode) {begin_batch(i&1u?1u:0u,0u,0u,NULL);physical_complete();complete_return();idle();}
            else transfer_batch(i&1u?1u:0u,0u,0u,NULL);
            CHECK(trace.hook==0x2fu && trace.submitted==i+1u && trace.completed==i+1u);
            CHECK(!trace.failure_present && !sampler_stops);
        }
        CHECK(sampler_pixels>pixels_before && sampler_reads>reads_before);
        CHECK(state.submitted==16u && state.completed==16u && trace.surface_seen);
        CHECK(!lease_calls && !sd.bytes && !sd.writes && !durable_card.commits);
        if(!mode) {
            uint8_t saved[512];memset(saved,0x6du,sizeof(saved));
            stage_block(42u,saved);transfer_batch(13u,42u,4u,NULL);read_compare(42u,saved);
            CHECK(durable_card.commits==1u && durable_card.reads==1u && lease_calls==2u);
            CHECK(trace.submitted==state.submitted && trace.completed==state.completed);
            CHECK(!trace.failure_present && !sampler_stops);idle();
        }
        frame.r4=0u;kui_vmu_native_readonly_release_check(&frame);
        CHECK(trace.hook==0x31u && trace.frees==1u && !sampler_stops);
    }
}
static void refusal_witness(unsigned which) {
    traced_setup(KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN);stage_batch(1u,0u);
    uint32_t source=value_at(0xa05f6c04u);
    if(which==0u) {
        /* Main B command3 is structurally valid, but the physical packet
         * admission whitelist refuses it. Keep the required A parent valid. */
        put(source+36u,4u,0x00406003u);
    } else injection=which==1u?INJECT_CONTEXT:which==2u?INJECT_TARGET:INJECT_VERIFY;
    uint8_t before[3][1024];memcpy(before[0],rx_bytes(0),1024u);
    memcpy(before[1],rx_bytes(1),1024u);memcpy(before[2],rx_bytes(6),1024u);
    unsigned pixels_before=sampler_pixels;
    if(!setjmp(terminal)) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(sampler_stops==1u && stop_reason==0x40u && trace.hook==0x11u && trace.sequence==2u);
    CHECK(!state.pending && !state.submitted && !state.completed && !state.proof);
    CHECK(value_at(0xa05f6c04u)==source && value_at(0xa05f6c18u)==0u);
    CHECK(!memcmp(before[0],rx_bytes(0),1024u) && !memcmp(before[1],rx_bytes(1),1024u));
    CHECK(!memcmp(before[2],rx_bytes(6),1024u) && sampler_pixels==pixels_before);
    CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes && !durable_card.commits);
    CHECK(trace.failure_present && trace.failure_count==3u && !trace.failure.frames);
    CHECK(trace.failure.check==state.runtime_result && trace.failure.address==state.fault_at);
    CHECK(trace.failure.value==state.fault_value && trace.failure.expected==state.fault_expect);
    CHECK(stop_value==state.runtime_result && stop_a==state.fault_at && stop_b==state.fault_value);
    CHECK(trace.failure.source==source && trace.failure.source_bytes==40u && trace.failure.frame_count==3u);
    CHECK(trace.failure.capacity==sizeof(issued_chain) && !trace.failure.nops);
    CHECK(!memcmp(trace.failure_frames,chain_frames,3u*sizeof(chain_frames[0])));
    if(which==0u) CHECK(state.runtime_result==0x30002u &&
        state.fault_value==(uint32_t)KUI_VMU_VOLATILE_CHAIN_UNSUPPORTED && state.fault_at==source);
    else if(which==1u) CHECK(state.runtime_result==0x20044u &&
        state.fault_at==config.driver.manager_initial+0x10c4u && state.fault_value==1u && !state.fault_expect);
    else if(which==2u) CHECK(state.runtime_result==0x30001u && !trace.failure.target);
    else CHECK(state.runtime_result==0x30003u &&
        state.fault_value==(uint32_t)KUI_VMU_VOLATILE_CHAIN_CHANGED && state.fault_at==trace.failure.target);
}
static void all_a1_refused_before_builder(void) {
    traced_setup(KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN);stage_batch(1u,0u);
    uint32_t source=value_at(0xa05f6c04u);
    put(source,4u,0x80000000u);put(source+4u,4u,rx_at(1));put(source+8u,4u,0x00000101u);
    uint8_t before[1024];memcpy(before,rx_bytes(1),sizeof(before));
    if(!setjmp(terminal)) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(sampler_stops==1u && stop_reason==21u && trace.hook==0x11u);
    CHECK(state.overlay_result==(uint32_t)KUI_VMU_OVERLAY_PARENT && !state.submitted && !state.pending);
    CHECK(!lease_calls && !sd.bytes && !sd.writes && !durable_card.commits);
    CHECK(value_at(0xa05f6c04u)==source && !memcmp(before,rx_bytes(1),sizeof(before)));
}
int main(void) {
    CHECK(persistent_suite_main()==0); /* Existing full suite, sampler disabled. */
    traced_transfers();for(unsigned which=0u;which<4u;which++) refusal_witness(which);
    all_a1_refused_before_builder();
    printf("Native trace/persistent integration: %u checks; real sampler with normal/raw transfers, target/build/verify/context witnesses and all-A1 refusal passed\n",checks);
    return 0;
}
