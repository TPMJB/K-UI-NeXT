/* SPDX-License-Identifier: GPL-3.0-only */
/* Included only by the dedicated native session-control installer. */
#include "kui/native_return.h"
static uint32_t native_session_stage_word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void __attribute__((noipa)) native_session_stage_configure(
    struct kui_vmu_native_passive_config *cfg) {
    const uint8_t *wire=kui_native_session_descriptor;
    if(!kui_native_session_wire_valid(wire,KUI_NATIVE_SESSION_WIRE_BYTES,true))
        native_report(8u,0x70u);
    if(manifest.reader!=KUI_RETAIL_READER_ASYNC || manifest.storage_transport!=KUI_STORAGE_SCI ||
       (manifest.flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)))
        native_report(8u,0x73u);
    for(uint32_t track=0u;track<manifest.track_count;++track)
        if(!kui_retail_track_control(&manifest.slots[track].track) &&
           manifest.slots[track].track.end_lba>manifest.session_lba)native_report(8u,0x73u);
    uint32_t flags=native_session_stage_word(wire+KUI_NATIVE_SESSION_FLAGS_OFFSET);
    if(flags&KUI_NATIVE_SESSION_FLAG_RETURN) {
        static struct kui_native_return_plan return_plan;
        if(!kui_native_return_decode(&return_plan,wire+KUI_NATIVE_SESSION_RETURN_OFFSET,
                KUI_NATIVE_SESSION_RETURN_BYTES) ||
           return_plan.card_sectors!=manifest.card_sectors ||
           return_plan.partition_first!=manifest.partition_start ||
           return_plan.partition_end!=manifest.partition_end)
            native_report(8u,0x74u);
    }
    if(flags&KUI_NATIVE_SESSION_FLAG_DISCS) {
        const uint32_t count=native_session_stage_word(wire+KUI_NATIVE_SESSION_COUNT_OFFSET);
        const uint32_t selected=native_session_stage_word(wire+KUI_NATIVE_SESSION_SELECTED_OFFSET);
        /* Full decoding belongs to temporary stage memory, never the low
         * reader stack or a later unowned game allocation. The worker repeats
         * admission into its own compact map records during arm. */
        static struct kui_retail_manifest candidate;
        for(uint32_t disc=0;disc<count;++disc) {
            const uint8_t *record=wire+KUI_NATIVE_SESSION_MAP_OFFSET+disc*KUI_NATIVE_SESSION_MAP_BYTES;
            if(kui_retail_manifest_decode(record,&candidate)!=KUI_GAME_OK ||
               candidate.track_count+candidate.extent_count>KUI_RETAIL_ASYNC_SLOTS ||
               candidate.storage_transport!=KUI_STORAGE_SCI ||
               candidate.reader!=manifest.reader ||
               (candidate.flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
               candidate.card_sectors!=manifest.card_sectors ||
               candidate.partition_start!=manifest.partition_start ||
               candidate.partition_end!=manifest.partition_end)
                native_report(8u,0x71u);
            for(uint32_t track=0u;track<candidate.track_count;++track)
                if(!kui_retail_track_control(&candidate.slots[track].track) &&
                   candidate.slots[track].track.end_lba>candidate.session_lba)native_report(8u,0x71u);
            if(disc==selected) {
                size_t used=offsetof(struct kui_retail_manifest,slots)+
                    (candidate.track_count+candidate.extent_count)*sizeof(candidate.slots[0]);
                if(memcmp(&candidate,&manifest,used))native_report(8u,0x72u);
            }
        }
    }
    cfg->session_wire=(uint32_t)(uintptr_t)wire;
    cfg->session_wire_bytes=KUI_NATIVE_SESSION_WIRE_BYTES;
    cfg->session_control=KUI_NATIVE_SESSION_CONTROL;
}
