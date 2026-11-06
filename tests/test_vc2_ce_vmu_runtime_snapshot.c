/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual terminal getter. No platform callback or SD provider
 * is linked; every field is authored private state, never a guest address. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/loader/armada_ce_vmu_runtime.c"

int main(void) {
    struct {uint32_t before[4],word[60],after[4];} out;
    memset(&out,0xa5,sizeof(out));
    kui_armada_ce_vmu_runtime_snapshot(out.word);
    for(unsigned i=0;i<60u;i++)assert(!out.word[i]);
    runtime.started=1u;runtime.ready=2u;runtime.owner=3u;runtime.faulted=4u;
    runtime.busy_rounds=5u;runtime.endpoint.requests=6u;runtime.endpoint.reads=7u;
    runtime.endpoint.write_phases=8u;runtime.endpoint.commits=9u;
    runtime.endpoint.read_finalizations=10u;
    runtime.endpoint.protocol.staged_block=11u;runtime.endpoint.protocol.phases=12u;
    runtime.endpoint.protocol.active=13u;runtime.endpoint.protocol.commit_attempted=14u;
    runtime.endpoint.protocol.read_block=15u;
    runtime.queue.state=16u;runtime.queue.ticket=17u;runtime.queue.operation=18u;
    runtime.queue.block=19u;runtime.queue.failures=20u;runtime.queue.submissions=21u;
    runtime.queue.completions=22u;runtime.queue.provider_busy=23u;
    runtime.endpoint.busy=24u;runtime.endpoint.io_errors=25u;
    runtime.sd.started=26u;runtime.sd.bound=27u;runtime.sd.busy=28u;
    runtime.sd.faulted=29u;runtime.sd.operation=30u;runtime.sd.block=31u;
    runtime.sd.pending_crc=32u;runtime.sd.reads=33u;runtime.sd.commits=34u;
    runtime.sd.bus_busy=35u;runtime.sd.failures=36u;runtime.sd.identity_checks=37u;
    runtime.sd.sector_reads=38u;runtime.sd.sector_writes=39u;runtime.sd.last_lba=40u;
    runtime.sd.last_result=-41;runtime.sd.last_sd_result=-42;
    runtime.sd.last_store_result=-43;runtime.sd.last_loan_result=-44;
    runtime.sd.pio.fault=45u;runtime.sd.pio.detail=46u;runtime.sd.pio.stage=47u;
    runtime.sd.pio.acquired=48u;runtime.sd.pio.captured=49u;runtime.sd.pio.selected=50u;
    kui_armada_ce_vmu_runtime_handoff.base_count=51u;
    kui_armada_ce_vmu_runtime_handoff.journal_count=52u;
    kui_armada_ce_vmu_runtime_handoff.high_capacity=53u;
    kui_armada_ce_vmu_runtime_handoff.card_sectors=UINT64_C(0x3700000036);
    runtime.sd.pio.mode=56u;runtime.sd.pio.work=57u;
    kui_armada_ce_vmu_runtime_services.loan_begin=58u;
    kui_armada_ce_vmu_runtime_services.loan_end=59u;
    kui_armada_ce_vmu_runtime_handoff.clean_checkpoint=60u;
    uint8_t old_runtime[sizeof(runtime)],old_handoff[sizeof(kui_armada_ce_vmu_runtime_handoff)];
    uint8_t old_services[sizeof(kui_armada_ce_vmu_runtime_services)];
    memcpy(old_runtime,&runtime,sizeof(runtime));
    memcpy(old_handoff,&kui_armada_ce_vmu_runtime_handoff,sizeof(old_handoff));
    memcpy(old_services,&kui_armada_ce_vmu_runtime_services,sizeof(old_services));
    kui_armada_ce_vmu_runtime_snapshot(NULL);
    kui_armada_ce_vmu_runtime_snapshot(out.word);
    for(unsigned i=0;i<60u;i++) {
        uint32_t expected=i+1u;
        if(i>=40u && i<=43u)expected=0u-expected;
        assert(out.word[i]==expected);
    }
    for(unsigned i=0;i<4u;i++)assert(out.before[i]==UINT32_C(0xa5a5a5a5) &&
        out.after[i]==UINT32_C(0xa5a5a5a5));
    assert(!memcmp(old_runtime,&runtime,sizeof(runtime)));
    assert(!memcmp(old_handoff,&kui_armada_ce_vmu_runtime_handoff,sizeof(old_handoff)));
    assert(!memcmp(old_services,&kui_armada_ce_vmu_runtime_services,sizeof(old_services)));
    puts("PASS runtime snapshot:60 exact private words, signed first-cause results, "
        "full64-bit capacity, zero/partial init, NULL, canaries and no source mutation/callbacks");
    return 0;
}
