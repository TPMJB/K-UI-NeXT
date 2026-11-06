/* SPDX-License-Identifier: GPL-3.0-only */
/* Terminal only. Retain the complete gateway trace and all engine/shared
 * state without spending constrained low code on per-field format strings.
 * The old manifest is no longer used after this irreversible stop. */
static void native_ocram_diagnostic_report(const char *label,uint32_t caller,uint32_t sp) {
#if KUI_VC2_TEXTURE_PROBE
    /* Before high-helper admission no callback is safe. Keep a bounded low
     * refusal page; successful admission uses the dedicated high payload probe. */
    (void)label;(void)caller;(void)sp;
    retail_display_restore(&display);
    retail_display_line("VC2 PROBE EARLY STOP");
    for(unsigned i=0u;i<16u;i+=4u)
        retail_display_values("",(const uint32_t *)(const void *)(kui_native_ocram_trace.entry+i),4u);
    retail_display_line("PHOTO PAGE THEN POWER OFF");
#else
    (void)label;(void)caller;(void)sp;
    volatile uint32_t *out=(volatile void *)RETAIL_MANIFEST;
    _Static_assert(sizeof(kui_retail_async_region_type)==1600u,"Late diagnostic region ABI");
    _Static_assert(sizeof(*RETAIL_MANIFEST)>=168u*4u,"Late diagnostic overlay");
    out[165]=RETAIL_MANIFEST->boot_lba;
    out[166]=RETAIL_MANIFEST->boot_bytes;
    out[167]=RETAIL_MANIFEST->gdi_crc32;
    for(unsigned i=0u;i<37u;i++)out[i]=((volatile uint32_t *)&kui_native_ocram_trace)[i];
    memcpy((void *)(out+37u),&kui_retail_async_region.engine,512u);
    unsigned page=0u;
    for(;;) {
        uint32_t row[5];
        retail_display_restore(&display);
#if !KUI_SMASH_CE_RETURN
        retail_display_line("VC2 STOP");
#endif
        row[0]=page+1u;row[1]=out[15];row[2]=out[3];row[3]=out[14];
        retail_display_values("PG WHY ADDR VAL",row,4u);
        for(unsigned at=page*56u;at<(page+1u)*56u;at+=4u) {
            row[0]=at;
            for(unsigned j=0u;j<4u;j++)row[j+1u]=out[at+j];
            retail_display_values("",row,5u);
        }
#if !KUI_SMASH_CE_RETURN
        retail_display_line("PHOTO 3 POWER OFF");
#endif
        retail_display_pause(480u);
        if(++page==3u)page=0u;
    }
#endif
}
