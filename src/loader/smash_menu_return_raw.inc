/* SPDX-License-Identifier: GPL-3.0-only */
/* Retired-BOOT1 P2 entry, called on the return gate's private stack with
 * IMASK15 after its worker has drained and retired. No GD cursor is moved.
 * Each consecutive physical run keeps one CMD18 session; the original SD
 * implementation verifies every block CRC16 before returning its bytes. */
uint32_t __attribute__((noinline,noclone,externally_visible,section(".smash_return_raw")))
kui_smash_menu_return_read_card(uint32_t lba,uint8_t out[512],uint32_t count) {
    const struct kui_retail_manifest *m=RETAIL_MANIFEST;
#ifdef KUI_SMASH_RETURN_RAW_HOST_TEST
    uint32_t sr=kui_smash_return_raw_test_sr();
#else
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
#endif
    if((sr&UINT32_C(0xf0))!=UINT32_C(0xf0) || kui_retail_hook_active ||
       !service.initialized || service.pending || service.executing || card.stream.active ||
       !card.device.sd.ready || card.device.sd.slow || !card.device.sd.blocks ||
       kui_sci_sd_faulted() || !out || !count || count>4096u ||
       (uintptr_t)out>UINTPTR_MAX-count*512u ||
       (uint32_t)(lba-(uint32_t)m->partition_start)>
           (uint32_t)(m->partition_end-m->partition_start-1u) ||
       count-1u>(uint32_t)(m->partition_end-m->partition_start-1u)-
           (uint32_t)(lba-(uint32_t)m->partition_start) ||
       (uint64_t)lba+count>m->card_sectors ||
       (uint64_t)lba+count>card.device.sd.blocks)return KUI_LOADER_SD_NOT_READY;
    kui_retail_hook_active=1u;
    enum kui_loader_sd_result result=kui_retail_storage_acquire(&card);
    if(result!=KUI_LOADER_SD_OK) {
        if(result==KUI_LOADER_SD_NOT_READY || result==KUI_LOADER_SD_UNSUPPORTED)
            kui_retail_hook_active=0u;
        else card.device.sd.ready=false;
        return result;
    }
    for(uint32_t i=0;i<count;i++) {
        result=kui_retail_storage_read_run(&card,lba+i,count-i,out+i*512u);
        if(result!=KUI_LOADER_SD_OK)break;
    }
    enum kui_loader_sd_result stopped=kui_retail_storage_stop(&card);
    if(stopped!=KUI_LOADER_SD_OK || !kui_sci_sd_healthy()) {
        /* Never release or reacquire an uncertain producer. The existing
         * card-ready bit and GD ownership guard retain sticky quarantine. */
        card.device.sd.ready=false;return KUI_LOADER_SD_TIMEOUT;
    }
    kui_retail_storage_release(&card);
    if(kui_sci_sd_faulted()) {
        card.device.sd.ready=false;return KUI_LOADER_SD_TIMEOUT;
    }
    kui_retail_hook_active=0u;
    return result;
}
