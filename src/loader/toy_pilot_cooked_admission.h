/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_COOKED_ADMISSION_H
#define KUI_TOY_PILOT_COOKED_ADMISSION_H
#include "toy_pilot_admission.h"
#include "kui/retail_observe.h"
#include "kui/toy_loader_data_pipeline.h"

#ifndef KUI_TOY_PILOT_COOKED_DATA_PROFILE
#define KUI_TOY_PILOT_COOKED_DATA_PROFILE 0
#endif
#if KUI_TOY_PILOT_COOKED_DATA_PROFILE < 0 || KUI_TOY_PILOT_COOKED_DATA_PROFILE > 1
#error Cooked Toy DATA profile must be 0 or 1
#endif
#if KUI_TOY_PILOT_COOKED_DATA_PROFILE && KUI_TOY_PILOT_DATA_PIPELINE_PROFILE != 2
#error Cooked Toy DATA requires the exact admitted AP pipeline
#endif

#if KUI_TOY_PILOT_COOKED_DATA_PROFILE
/* Canonical output of tools/gdi_optimize.py for this exact 15-track layout.
 * The descriptor changes format and filenames; it cannot keep the raw CRC.
 * Loaded IP, full executable SHA and driver identity remain mandatory. */
#define KUI_TOY_COOKED_GDI_CRC UINT32_C(0xb7100f9e)
static inline int kui_toy_pilot_cooked_manifest_admit(const struct kui_retail_manifest *m) {
    static const uint32_t starts[15]={0u,7257u,45000u,201900u,219712u,234091u,
        255599u,272017u,282799u,303092u,323352u,341314u,356690u,374201u,377422u};
    static const uint32_t ends[15]={7107u,7753u,201750u,219712u,234091u,
        255599u,272017u,282799u,303092u,323352u,341314u,356690u,374201u,377272u,549150u};
    if(!m || kui_retail_manifest_validate(m)!=KUI_GAME_OK ||
       m->gdi_crc32!=KUI_TOY_COOKED_GDI_CRC || m->track_count!=15u ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_STANDARD ||
       m->flags!=KUI_RETAIL_IMAGE_BOOT_CRC || m->session_lba!=45000u ||
       m->boot_lba!=548634u || m->boot_bytes!=TOY_BOOT_BYTES ||
       m->boot_crc32!=TOY_BOOT_CRC || m->ip_crc32!=UINT32_C(0x38ba2868) ||
       !m->extent_count || m->extent_count>KUI_RETAIL_OBSERVE_SLOTS-15u) return 0;
    for(unsigned i=0;i<15u;i++) {
        const struct kui_retail_track *t=&m->slots[i].track;
        uint32_t data=i==0u || i==2u || i==14u;
        uint32_t control=data?(4u|KUI_RETAIL_TRACK_COOKED):0u;
        if(t->start_lba!=starts[i] || t->end_lba!=ends[i] || t->control!=control ||
           kui_retail_track_sector_bytes(t)!=(data?2048u:2352u) ||
           kui_retail_track_file_offset(t) || !t->extent_count) return 0;
    }
    return 1;
}
static inline uint32_t kui_toy_pilot_cooked_launch_refusal(const struct kui_retail_manifest *m) {
    uint32_t reason=m && m->reader!=KUI_RETAIL_READER_STANDARD?
        KUI_TOY_PILOT_LAUNCH_READER:0u;
    if(!kui_toy_pilot_cooked_manifest_admit(m)) reason|=KUI_TOY_PILOT_LAUNCH_FORMAT;
    return reason;
}
#endif
#endif
