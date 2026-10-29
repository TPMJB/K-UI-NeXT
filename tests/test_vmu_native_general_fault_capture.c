/* SPDX-License-Identifier: GPL-3.0-only */
#include "vmu_native_fault_capture.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const struct kui_vmu_native_discovery *inputs[]={NULL,
        (const struct kui_vmu_native_discovery *)(uintptr_t)1u};
    for(unsigned i=0;i<8u;i++) kui_vmu_native_fault_capture_state[i]=0x12340000u+i;
    for(unsigned round=0;round<32u;round++)
        for(unsigned i=0;i<sizeof(inputs)/sizeof(*inputs);i++) {
            assert(kui_vmu_native_fault_capture_install(false,inputs[i])==0);
            assert(kui_vmu_native_fault_capture_install(true,inputs[i])==-1);
            for(unsigned j=0;j<8u;j++)
                assert(kui_vmu_native_fault_capture_state[j]==0x12340000u+j);
        }
    puts("General optional fault capture: enable refused, no owner reads or scratch writes");
    return 0;
}
