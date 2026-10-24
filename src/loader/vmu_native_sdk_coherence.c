/* SPDX-License-Identifier: GPL-3.0-only */
#include "vmu_native_sdk_coherence.h"

volatile uint32_t kui_vmu_native_sdk_coherence_state[12];
static bool ram(uint32_t a,uint32_t bytes,unsigned alignment) {
    uint32_t area=a&UINT32_C(0xe0000000),p=a&UINT32_C(0x1fffffff);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        bytes && !(p&(alignment-1u)) && p>=UINT32_C(0x0c000000) &&
        p<UINT32_C(0x0d000000) && bytes<=UINT32_C(0x0d000000)-p;
}
int kui_vmu_native_sdk_coherence_prepare(const struct kui_vmu_native_passive_config *c) {
    /* This is metadata preparation, not patch authority. Immutable discovery
     * has already admitted the complete initializer. The temporary installer
     * separately rechecks its original twelve bytes and literal bindings
     * before redirecting the wrapper's call-target literal to a cold gate. */
    if(!c || c->version!=1u || c->bytes!=sizeof(*c) ||
       c->driver.version!=KUI_VMU_NATIVE_DISCOVERY_VERSION || c->driver.bytes!=sizeof(c->driver) ||
       c->driver.family!=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 ||
       c->driver.requirements!=KUI_VMU_NATIVE_REQUIREMENTS ||
       (c->layout.version!=KUI_VMU_NATIVE_LAYOUT_VERSION &&
        c->layout.version!=KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION) || c->layout.bytes!=sizeof(c->layout) ||
       c->layout.rx_layout>KUI_VMU_NATIVE_RX_EXPLICIT48 ||
       !ram(c->driver.load_address,c->driver.image_bytes,4u) ||
       !ram(c->driver.lock_cell,5u,4u) || !ram(c->driver.manager_cell,4u,4u) ||
       !ram(c->driver.manager_initial,0x1100u,4u) ||
       !ram(c->driver.initializer,12u,2u) ||
       (c->driver.initializer&UINT32_C(0xe0000000))!=UINT32_C(0x80000000) ||
       !ram(c->layout.initializer,12u,2u) ||
       (c->layout.initializer&UINT32_C(0xe0000000))!=UINT32_C(0x80000000) ||
       c->layout.initializer<c->driver.load_address || c->driver.image_bytes<12u ||
       c->driver.initializer<c->driver.load_address ||
       c->driver.initializer-c->driver.load_address>c->driver.image_bytes-12u ||
       c->layout.initializer-c->driver.load_address>c->driver.image_bytes-12u ||
       c->layout.initializer!=c->driver.initializer+
           (c->layout.rx_layout==KUI_VMU_NATIVE_RX_EXPLICIT48?0x11cu:0u) ||
       kui_vmu_native_sdk_coherence_state[7]) return 0;
    uint32_t lock=(c->driver.lock_cell&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    kui_vmu_native_sdk_coherence_state[0]=lock;
    kui_vmu_native_sdk_coherence_state[1]=c->driver.manager_cell;
    kui_vmu_native_sdk_coherence_state[2]=c->driver.manager_initial;
    kui_vmu_native_sdk_coherence_state[3]=c->layout.initializer+12u;
    for(unsigned i=4u;i<12u;i++) kui_vmu_native_sdk_coherence_state[i]=0u;
    kui_vmu_native_sdk_coherence_state[8]=(lock+4u)|UINT32_C(0xa0000000);
    kui_vmu_native_sdk_coherence_state[9]=c->layout.rx_layout;
    kui_vmu_native_sdk_coherence_state[10]=c->layout.initializer;
    kui_vmu_native_sdk_coherence_state[7]=KUI_VMU_NATIVE_SDK_COHERENCE_READY;
    return 1;
}
