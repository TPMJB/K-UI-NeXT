/* SPDX-License-Identifier: GPL-3.0-only */
/* General cannot activate Evolution sampling, even with an exact image and
 * tracing selector. Test both its linked ABI leaves and header fast paths. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef KUI_TEST_TRACE_HEADER
#define KUI_VMU_NATIVE_TRACE_IMPLEMENTATION 1
#endif
#include "kui/vmu_native_trace.h"

static jmp_buf terminal;
static uint32_t reported[4];
void kui_vmu_native_runtime_fault(uint32_t kind,uint32_t value,uint32_t a,uint32_t b) {
    reported[0]=kind;reported[1]=value;reported[2]=a;reported[3]=b;
    longjmp(terminal,1);
}

int main(void) {
    static const uint8_t digest[32]={
        0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
        0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
    struct kui_vmu_native_passive_config config={0};
    config.driver.image_bytes=2143652u;config.driver.load_address=UINT32_C(0x8c010000);
    memcpy(config.driver.image_sha256,digest,sizeof(digest));
    struct kui_vmu_native_passive_config before;
    for(unsigned mode=0u;mode<=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE+1u;mode++) {
        config.diagnostics=mode;before=config;
        assert(!kui_vmu_native_trace_image(&config.driver));
        kui_vmu_native_trace_init(&config);
        assert(!kui_vmu_native_trace_enabled());
        for(unsigned code=0u;code<256u;code++) {
            kui_vmu_native_trace_point(code,(const void *)(uintptr_t)1u,UINT32_MAX,7u);
            kui_vmu_native_trace_game(code,(const void *)(uintptr_t)1u);
        }
        kui_vmu_native_trace_failure((const void *)(uintptr_t)1u);
        assert(!kui_vmu_native_trace_enabled() && !memcmp(&config,&before,sizeof(config)));
    }
    assert(!kui_vmu_native_trace_image(NULL));
    kui_vmu_native_trace_init(NULL);kui_vmu_native_trace_failure(NULL);
    /* No sampler/MMIO/display stubs are linked. Any accidental live read
     * faults or fails this link; terminal delivery keeps all four words. */
    if(!setjmp(terminal)) kui_vmu_native_trace_report(0x1234u,0x5678u,0x9abcu,0xdef0u);
    assert(reported[0]==0x1234u && reported[1]==0x5678u && reported[2]==0x9abcu && reported[3]==0xdef0u);
    puts("General trace: every selector inactive, no context access, ordinary terminal report preserved");
    return 0;
}
