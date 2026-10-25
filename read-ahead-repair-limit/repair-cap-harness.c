/* Scratch experiment: reuse the repository's actual SCI/card/DMA fixture. */
#define main original_native_readahead_aux_test_main
#include "/workspace/scratch/45de2288a575/K-UI-hybrid/tests/test_native_readahead_aux.c"
#undef main

int main(void) {
    for(unsigned count=0u;count<=2u;++count) {
        setup_real();begin(DISC_BASE,1u);complete(DISC_BASE,1u);
        CHECK(visit(KUI_NATIVE_RA_TICK)==1u && kui_sci_stream_busy());
        region.shared.stream.unrepaired=(uint8_t)count;
        m.overrun_after=180u;m.rx_delay=0u;region.engine.in_irq=1u;
        CHECK(visit(KUI_NATIVE_RA_DELIVER)==1u);
        CHECK(!C.stats.crc_errors && !C.stats.token_errors &&
            !region.shared.stream.stats.crc_errors && !region.shared.stream.stats.token_errors);
        if(count<2u) {
            CHECK(C.deferred && C.spec_owned && !C.abandoned &&
                region.shared.stream.state==RESUME && !region.shared.stream.stats.overruns &&
                region.shared.stream.stats.repaired==1u && !C.diagnostics.failures);
            printf("unrepaired=%u: poll=PENDING state=RESUME repairs=1 overrun=0 aux_abandon=0 CRC=0 TOKEN=0\n",count);
        } else {
            CHECK(!C.deferred && !C.spec_owned && C.abandoned &&
                region.shared.stream.state==LOST && region.shared.stream.stats.overruns==1u &&
                !region.shared.stream.stats.repaired && C.diagnostics.failures==1u &&
                C.diagnostics.first.stage==KUI_NATIVE_RA_STAGE_POLL &&
                C.diagnostics.first.result==KUI_SCI_STREAM_OVERRUN &&
                C.diagnostics.first.loan_state>>16==2u);
            printf("unrepaired=%u: poll=OVERRUN state=LOST repairs=0 overrun=1 aux_abandon=1 snapshot=POLL/OVERRUN unrepaired=2 CRC=0 TOKEN=0\n",count);
        }
        CHECK(region.shared.stream.unrepaired==count);
    }
    puts("PASS: identical injected mid-block ORER at byte180; existing unrepaired<2 gate changes optional repair into abandonment. Software fixture only; hardware counter unknown.");
    return 0;
}
