/* SPDX-License-Identifier: GPL-3.0-only */
/* Retain the production synchronous GD/payload replay for TRACE's raw DATA
 * branch. Startup async scheduling never owns 2352-byte reads, so no fake
 * raw IRQ credit is introduced by this fixture. */
#define kui_toy_pilot_gd_dispatch raw_trace_fixture_dispatch
#define main retained_trace_fixture_main
#include "test_toy_loader_trace.c"
#undef main
#undef kui_toy_pilot_gd_dispatch

int32_t raw_trace_fixture_dispatch(struct kui_retail_gd *s,uint32_t a,
    uint32_t b,uint32_t function,uintptr_t original) {
    typedef int32_t (*sync_base)(uint32_t,uint32_t,uint32_t,uint32_t);
    kui_toy_loader_trace_begin(s,function,a,b);
    int32_t result=((sync_base)original)(a,b,0u,function);
    kui_toy_loader_trace_end(s,function,a,b,result);
    return result;
}
int main(void) {
    read_round(5u,2352u,KUI_GD_PIOREAD);
    read_round(17u,2352u,KUI_GD_DMAREAD);
    puts("Budget TRACE raw synchronous fallback: production PIO/DMA requests, exact 2352-byte payloads, sector totals and consumed terminal handles PASS");
    return 0;
}
