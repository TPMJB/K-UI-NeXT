/* SPDX-License-Identifier: GPL-3.0-only */
/* Temporary stage only. Move the full snapshot admission and rebase out of
 * the bounded low reader; the low entry performs the final pointer publish. */
#include "kui/native_session_binding.h"
#include "kui/native_disc_set.h"
static bool native_session_low_data(uint32_t address,uint32_t bytes) {
    return !(address&3u) && bytes && address>=KUI_RETAIL_RESIDENT_ADDRESS+resident_bytes &&
        address<resident_limit && bytes<=resident_limit-address;
}
static int __attribute__((noipa)) native_session_cold_bind(uint32_t getter_entry,
    uint32_t publish_entry,const struct kui_retail_manifest *temporary,
    const struct kui_retail_manifest *owned) {
    if((getter_entry|publish_entry)&1u || getter_entry<KUI_RETAIL_RESIDENT_ADDRESS+44u ||
       publish_entry<KUI_RETAIL_RESIDENT_ADDRESS+44u ||
       getter_entry-KUI_RETAIL_RESIDENT_ADDRESS>=resident_bytes ||
       publish_entry-KUI_RETAIL_RESIDENT_ADDRESS>=resident_bytes ||
       resident_bytes<sizeof(struct kui_native_session_binding))return 0;
    kui_native_session_control getter=(void *)(uintptr_t)getter_entry;
    uint32_t descriptor=(uint32_t)getter(KUI_NATIVE_SESSION_BIND,NULL,NULL);
    if((descriptor&3u) || descriptor<KUI_RETAIL_RESIDENT_ADDRESS+44u ||
       descriptor-KUI_RETAIL_RESIDENT_ADDRESS>resident_bytes-sizeof(struct kui_native_session_binding))return 0;
    struct kui_native_session_binding binding;
    memcpy(&binding,resident_blob+(descriptor-KUI_RETAIL_RESIDENT_ADDRESS),sizeof(binding));
    if(binding.magic!=KUI_NATIVE_SESSION_BINDING_MAGIC ||
       binding.version!=KUI_NATIVE_SESSION_BINDING_VERSION || binding.bytes!=sizeof(binding) ||
       memcmp(&binding,(const void *)(uintptr_t)(descriptor|UINT32_C(0x20000000)),sizeof(binding)) ||
       !native_session_low_data(binding.region_cell,4u) ||
       !native_session_low_data(binding.manifest_cell,4u) || binding.region_cell==binding.manifest_cell ||
       !native_session_low_data(binding.firmware_snapshot,36u))return 0;
    uint32_t sr,vbr;
    __asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    __asm__ volatile("stc vbr,%0":"=r"(vbr)::"memory");
    const volatile struct kui_vmu_observe_boot_control *c=control();
    uint32_t region=native_word(binding.region_cell|UINT32_C(0x20000000));
    uint32_t current=native_word(binding.manifest_cell|UINT32_C(0x20000000));
    uint32_t retained_address=(c->lease&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    retained_address+=KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET;
    if((sr&UINT32_C(0x400000f0))!=UINT32_C(0x400000f0) ||
       c->status!=KUI_VMU_OBS_BOOT_LEASED ||
       !kui_vmu_native_background_stage_region_address(region) ||
       !kui_vmu_native_background_stage_map_address((uint32_t)(uintptr_t)temporary) ||
       !kui_vmu_native_background_map_address(c->lease,(uint32_t)(uintptr_t)owned) ||
       !kui_vmu_native_background_region_address(c->lease,retained_address) ||
       current!=(uint32_t)(uintptr_t)temporary || vbr==region-UINT32_C(0x100))return 0;
    kui_retail_async_region_type *previous=(void *)(uintptr_t)region;
    kui_retail_async_region_type *retained=(void *)(uintptr_t)retained_address;
    if(!previous->shared.service.initialized || previous->shared.service.pending || previous->shared.service.executing ||
       previous->engine.active || previous->engine.in_irq || previous->engine.hooked ||
       previous->engine.release.armed || previous->engine.release.depth ||
       previous->engine.native_loan_state || previous->shared.stream.state>1u ||
       previous->shared.port.fault || previous->engine.opened!=(uint32_t)previous->shared.port.acquired ||
       previous->engine.manifest!=temporary || previous->shared.service.tracks!=temporary->slots ||
       previous->shared.service.ops.context || previous->engine.cursor.context || previous->shared.card.device.sd.bus.ctx ||
       (previous->engine.cursor.manifest && previous->engine.cursor.manifest!=temporary) ||
       (previous->shared.stream.card && previous->shared.stream.card!=&previous->shared.card.device.sd) ||
       (previous->shared.stream.area[0] && previous->shared.stream.area[0]!=previous->area0) ||
       (previous->shared.stream.area[1] && previous->shared.stream.area[1]!=previous->area1) ||
       memcmp(temporary,owned,KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES) ||
       memcmp(previous,retained,KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES))return 0;
    retained->shared.service.tracks=owned->slots;
    retained->engine.manifest=owned;
    if(retained->engine.cursor.manifest)retained->engine.cursor.manifest=owned;
    if(retained->shared.stream.card)retained->shared.stream.card=&retained->shared.card.device.sd;
    if(retained->shared.stream.area[0])retained->shared.stream.area[0]=retained->area0;
    if(retained->shared.stream.area[1])retained->shared.stream.area[1]=retained->area1;
    kui_native_passive_publish(retained_address,retained_address+KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES);
    int (*publish)(const struct kui_retail_manifest *,const struct kui_retail_manifest *)=
        (void *)(uintptr_t)publish_entry;
    return publish(temporary,owned);
}
