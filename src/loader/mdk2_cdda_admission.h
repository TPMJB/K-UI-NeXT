/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MDK2_CDDA_ADMISSION_H
#define KUI_MDK2_CDDA_ADMISSION_H
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"
#include "kui/mdk2_cdda_layout.h"
#define KUI_MDK2_BOOT_BYTES 750804u
#define KUI_MDK2_BOOT_CRC UINT32_C(0x682d90e3)
#define KUI_MDK2_IP_CRC UINT32_C(0x34122049)
struct kui_mdk2_track_identity { uint32_t start,end,control; };
/* Descriptor geometry, independent of card fragmentation and filename case.
 * All audio must be mapped; the 128-slot MDK2 reader refuses an oversized map. */
static const struct kui_mdk2_track_identity kui_mdk2_tracks[31]={
    {0u,14451u,4u},
    {14601u,15127u,0u},
    {45000u,52140u,4u},
    {52290u,74215u,0u},
    {74215u,94719u,0u},
    {94719u,115029u,0u},
    {115029u,134578u,0u},
    {134578u,154620u,0u},
    {154620u,174965u,0u},
    {174965u,193871u,0u},
    {193871u,213463u,0u},
    {213463u,235118u,0u},
    {235118u,253694u,0u},
    {253694u,278376u,0u},
    {278376u,298017u,0u},
    {298017u,314109u,0u},
    {314109u,330973u,0u},
    {330973u,349474u,0u},
    {349474u,368207u,0u},
    {368207u,373047u,0u},
    {373047u,377949u,0u},
    {377949u,382628u,0u},
    {382628u,387412u,0u},
    {387412u,396388u,0u},
    {396388u,400888u,0u},
    {400888u,402548u,0u},
    {402548u,416575u,0u},
    {416575u,420052u,0u},
    {420052u,424954u,0u},
    {424954u,429348u,0u},
    {429498u,549150u,4u},
};
static inline int kui_mdk2_cdda_manifest_admit(const struct kui_retail_manifest *m) {
    if(!m || m->storage_transport!=KUI_STORAGE_SCI ||
       m->reader!=KUI_RETAIL_READER_STANDARD || m->track_count!=31u ||
       !m->extent_count || m->extent_count>KUI_MDK2_CDDA_MANIFEST_SLOTS-31u ||
       m->session_lba!=45000u ||
       m->boot_lba!=548633u || m->boot_bytes!=KUI_MDK2_BOOT_BYTES ||
       m->ip_crc32!=KUI_MDK2_IP_CRC ||
       (m->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       ((m->flags&KUI_RETAIL_IMAGE_BOOT_CRC) && m->boot_crc32!=KUI_MDK2_BOOT_CRC))
        return 0;
    for(unsigned t=0;t<31u;t++) {
        const struct kui_retail_track *track=&m->slots[t].track;
        if(track->start_lba!=kui_mdk2_tracks[t].start ||
           track->end_lba!=kui_mdk2_tracks[t].end ||
           kui_retail_track_control(track)!=kui_mdk2_tracks[t].control ||
           kui_retail_track_sector_bytes(track)!=2352u ||
           (track->control&KUI_RETAIL_TRACK_MODE2) ||
           kui_retail_track_file_offset(track)!=0u || !track->extent_count)
            return 0;
    }
    return 1;
}
#endif
