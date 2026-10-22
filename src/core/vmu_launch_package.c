/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_launch_package.h"
#include "kui/vmu_silver_observe_admission.h"
#include "kui/vmu_toy_observe_admission.h"
#include <string.h>
static uint32_t launch_le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
bool kui_vmu_launch_silver_candidate(const struct kui_retail_manifest *m) {
    static const char product[16]="T15108N";
    return m && !memcmp(m->product,product,sizeof(product));
}
bool kui_vmu_launch_toy_candidate(const struct kui_retail_manifest *m) {
    return m && m->gdi_crc32==KUI_VMU_LAUNCH_TOY_GDI_CRC;
}
unsigned kui_vmu_launch_profile_candidate(const struct kui_retail_manifest *m) {
    if(kui_vmu_launch_silver_candidate(m)) return KUI_VMU_LAUNCH_CAP_PROFILE;
    if(kui_vmu_launch_toy_candidate(m)) return KUI_VMU_LAUNCH_TOY_CAP_PROFILE;
    return 0u;
}
static bool launch_capability_profile(const void *capability,size_t bytes,unsigned profile) {
    static const uint32_t silver[14]={KUI_VMU_LAUNCH_CAP_VERSION,KUI_VMU_LAUNCH_CAP_BYTES,
        KUI_VMU_LAUNCH_CAP_PROFILE,KUI_VMU_LAUNCH_CAP_MODE,KUI_VMU_LAUNCH_CAP_FLAGS,
        KUI_VMU_LAUNCH_GDI_CRC,KUI_VMU_LAUNCH_IP_CRC,KUI_VMU_LAUNCH_EXEC_CRC,
        KUI_VMU_LAUNCH_EXEC_BYTES,KUI_VMU_LAUNCH_SESSION_LBA,KUI_VMU_LAUNCH_BOOT_LBA,0,0,0};
    static const uint32_t toy[14]={KUI_VMU_LAUNCH_CAP_VERSION,KUI_VMU_LAUNCH_CAP_BYTES,
        KUI_VMU_LAUNCH_TOY_CAP_PROFILE,KUI_VMU_LAUNCH_CAP_MODE,KUI_VMU_LAUNCH_CAP_FLAGS,
        KUI_VMU_LAUNCH_TOY_GDI_CRC,KUI_VMU_LAUNCH_TOY_IP_CRC,KUI_VMU_LAUNCH_TOY_EXEC_CRC,
        KUI_VMU_LAUNCH_TOY_EXEC_BYTES,KUI_VMU_LAUNCH_TOY_SESSION_LBA,KUI_VMU_LAUNCH_TOY_BOOT_LBA,0,0,0};
    const uint32_t *words=profile==KUI_VMU_LAUNCH_CAP_PROFILE?silver:
        profile==KUI_VMU_LAUNCH_TOY_CAP_PROFILE?toy:NULL;
    const uint8_t *p=capability;
    if(!words || !p || bytes!=KUI_VMU_LAUNCH_CAP_BYTES || memcmp(p,KUI_VMU_LAUNCH_CAP_MAGIC,8u)) return false;
    for(unsigned i=0;i<14u;i++) if(launch_le32(p+8u+4u*i)!=words[i]) return false;
    return true;
}
bool kui_vmu_launch_capability_valid(const void *capability,size_t bytes) {
    return launch_capability_profile(capability,bytes,KUI_VMU_LAUNCH_CAP_PROFILE) ||
        launch_capability_profile(capability,bytes,KUI_VMU_LAUNCH_TOY_CAP_PROFILE);
}
enum kui_vmu_launch_route kui_vmu_launch_package_route(const struct kui_retail_manifest *m,
    uint32_t requested_options,bool original_raw_gdi,bool patches,bool optional_present,
    const void *capability,size_t bytes) {
    unsigned profile=kui_vmu_launch_profile_candidate(m);
    if(!optional_present || !profile) return KUI_VMU_LAUNCH_ORDINARY;
    if(requested_options || !original_raw_gdi || patches) return KUI_VMU_LAUNCH_SETTINGS;
    bool silver=profile==KUI_VMU_LAUNCH_CAP_PROFILE;
    if(kui_retail_manifest_validate(m)!=KUI_GAME_OK ||
       !(silver?kui_vmu_silver_observe_manifest(m):kui_vmu_toy_observe_manifest(m)) ||
       m->flags!=KUI_RETAIL_IMAGE_BOOT_CRC ||
       m->ip_crc32!=(silver?KUI_VMU_LAUNCH_IP_CRC:KUI_VMU_LAUNCH_TOY_IP_CRC) ||
       m->boot_crc32!=(silver?KUI_VMU_LAUNCH_EXEC_CRC:KUI_VMU_LAUNCH_TOY_EXEC_CRC))
        return KUI_VMU_LAUNCH_MANIFEST;
    if(!launch_capability_profile(capability,bytes,profile)) return KUI_VMU_LAUNCH_CAPABILITY;
    return silver?KUI_VMU_LAUNCH_SILVER:KUI_VMU_LAUNCH_TOY;
}
