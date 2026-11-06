/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_gin.h"
__asm__(".global __kui_smash_ce_gin_adoption\n.set __kui_smash_ce_gin_adoption,1\n");
static struct kui_smash_ce_gin_seed smash_ce_gin_saved;
static uint32_t smash_ce_gin_ip_crc;
static uint8_t smash_ce_gin_source_ip[KUI_RETAIL_IP_BYTES] __attribute__((aligned(32)));
static uint32_t smash_ce_gin_retained_ip_crc(void) {
    /* Preserve the live original IP header/folder verbatim. The following
     * shim is replaced deliberately; the retired outer handoff after it is
     * replaced with authenticated original BOOT1 bytes before continuation. */
    return kui_retail_crc32(0u,(const void *)(uintptr_t)UINT32_C(0x8c008000),UINT32_C(0x100));
}

/* These named boundaries are deliberately retained for linked-code tests.
 * They adopt an actual original GIN result, rather than reconstruct BOOT2. */
static void __attribute__((noipa)) smash_ce_gin_begin(void) {
    memcpy(&smash_ce_gin_saved,(const void *)(uintptr_t)
        (KUI_RETAIL_EXEC_ADDRESS+KUI_SMASH_CE_GIN_OFFSET),sizeof(smash_ce_gin_saved));
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    if(!kui_smash_ce_gin_valid(&smash_ce_gin_saved) ||
       smash_ce_gin_saved.profile!=SMASH_CE_CHILD_PROFILE ||
       (sr&UINT32_C(0x700080f0))!=(smash_ce_gin_saved.frame[4]&UINT32_C(0x700080f0)))
        stopped("SMASH CE NEEDS ORIGINAL GIN CONTINUATION",sr);
    /* This is the live original IP header, including fields we do not
     * interpret, rather than a replacement header read from disk. */
    smash_ce_gin_ip_crc=smash_ce_gin_retained_ip_crc();
#if KUI_SMASH_CE_RETURN
    /* GIN retains the collection's current scanout, which need not be the
     * framebuffer0/640/RGB565 layout used by our terminal renderer. Select
     * its already authenticated K-UI mode in RAM while adoption is quiet;
     * neither successful adoption nor ordinary reads repaint the game.
     * Snapshot the volatile descriptor once so the checked bytes are the
     * same bytes copied into the low reader and later helper fault state. */
    struct kui_smash_phase_video_seed video;
    memcpy(&video,(const void *)&smash_ce_return_video_seed,sizeof(video));
    if(!kui_smash_phase_video_valid(&video))
        stopped("SMASH CE DIAGNOSTIC VIDEO REFUSED",0u);
    memcpy(display.regs,video.regs,sizeof(display.regs));
#endif
}

static void __attribute__((noipa)) smash_ce_gin_restore(void *out) {
    /* The outer handoff reclaimed retired BOOT1 at 8300..c000. Restore its
     * exact source bytes as well; leaving that handoff there would still
     * differ from the accepted original GIN route. Read into high temporary
     * RAM and validate the entire source IP, never over the live header,
     * BOOT2, stack or GIN frame. The original executable is not reread. */
    read_sectors(manifest.session_lba,16u,KUI_GAME_SECTOR_MODE1,smash_ce_gin_source_ip);
    uint32_t ip_crc=kui_retail_crc32(0u,smash_ce_gin_source_ip,sizeof(smash_ce_gin_source_ip));
    if(manifest.ip_crc32!=KUI_SMASH_NATIVE_IP_CRC32 || ip_crc!=manifest.ip_crc32)
        stopped("ORIGINAL CE IP SOURCE CHANGED",ip_crc);
    const void *source=(const void *)(uintptr_t)KUI_SMASH_CE_GIN_SAMPLE_P1;
    uint8_t digest[32];struct kui_sha256 hash;
    uint32_t crc=kui_retail_crc32(0u,source,KUI_SMASH_CE_GIN_SAMPLE_BYTES);
    kui_sha256_init(&hash);kui_sha256_update(&hash,source,KUI_SMASH_CE_GIN_SAMPLE_BYTES);
    kui_sha256_digest(&hash,digest);
    if(manifest.boot_bytes!=KUI_SMASH_CE_GIN_SAMPLE_BYTES ||
       crc!=KUI_SMASH_CE_SAMPLE_CRC32 ||
       !kui_smash_ce_child_owner(KUI_SMASH_CE_GIN_SAMPLE_BYTES,digest))
        stopped("ORIGINAL GIN SAMPLE SOURCE CHANGED",crc);
    memcpy(out,source,KUI_SMASH_CE_GIN_SAMPLE_BYTES);
    source_boot_crc=crc;
}

static void __attribute__((noipa,noreturn)) smash_ce_gin_adopt(void) {
    if(!kui_smash_ce_gin_valid(&smash_ce_gin_saved) ||
       memcmp((const void *)(uintptr_t)UINT32_C(0x8c0080f0),smash_ce_prepared.ip_tail,16u))
        stopped("ORIGINAL GIN FOLDER CHANGED",smash_ce_gin_saved.profile);
    install_resident();
    vc2_ce_vmu_late_publish();
    memcpy((void *)(uintptr_t)UINT32_C(0x8c008300),smash_ce_gin_source_ip+UINT32_C(0x300),UINT32_C(0x3d00));
    if(memcmp((const void *)(uintptr_t)UINT32_C(0x8c008300),
        smash_ce_gin_source_ip+UINT32_C(0x300),UINT32_C(0x3d00)))
        stopped("ORIGINAL CE BOOT1 COPY CHANGED",0u);
    uint32_t crc=smash_ce_gin_retained_ip_crc();
    if(crc!=smash_ce_gin_ip_crc)
        stopped("ORIGINAL GIN IP STATE CHANGED",crc);
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    if((sr&UINT32_C(0x700080f0))!=(smash_ce_gin_saved.frame[4]&UINT32_C(0x700080f0)))
        stopped("ORIGINAL GIN CPU BANK CHANGED",sr);
    memcpy((void *)(uintptr_t)KUI_SMASH_CE_GIN_FRAME_P1,
        smash_ce_gin_saved.frame,sizeof(smash_ce_gin_saved.frame));
    if(memcmp((const void *)(uintptr_t)KUI_SMASH_CE_GIN_FRAME_P1,
        smash_ce_gin_saved.frame,sizeof(smash_ce_gin_saved.frame)))
        stopped("ORIGINAL GIN FRAME COPY CHANGED",0u);
    void (*resume)(const uint32_t *,uint32_t)=
        (void (*)(const uint32_t *,uint32_t))(uintptr_t)
        ((uintptr_t)kui_smash_ce_gin_resume|UINT32_C(0x20000000));
    resume((const uint32_t *)(uintptr_t)KUI_SMASH_CE_GIN_FRAME_P1,smash_ce_gin_saved.ccr);
    __builtin_unreachable();
}
