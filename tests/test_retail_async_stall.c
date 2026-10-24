/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the actual B reader/data/bounds suite with diagnostic timing enabled.
 * The timer stub deliberately wraps; it never changes card/service timing. */
#include <assert.h>
#include <stdint.h>
static uint32_t clock_counter=5u;
uint32_t kui_retail_async_test_read32(uint32_t address) {
    assert(address==UINT32_C(0xffd8000c));
    uint32_t result=clock_counter;clock_counter-=19u;return result;
}
#define KUI_NATIVE_HYBRID_MAIN hybrid_suite_main
#include "test_retail_async_native_hybrid.c"
int main(void) {
    hybrid_suite_main();
    ready_batch(true,X,3u);
    assert(R.engine.stats.execs==19u);
    assert(R.engine.stats.exec_int==19u);
    puts("reader diagnostic: real B scheduling and wrapped raw-timer maxima passed");
    return 0;
}
