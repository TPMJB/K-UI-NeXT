/* SPDX-License-Identifier: GPL-3.0-only */
/* The SDK normalizer can leave dirty cached data for its P2 lock metadata.
 * A cold initializer gateway retires the pointer and flag lines before TAS.
 * This temporary installer admits the exact entry replay and private state;
 * it never cleans a line after the SDK has already acquired its lock. */
static bool __attribute__((noipa)) native_sdk_coherence_window_valid(void) {
    uint32_t at=passive_layout.initializer;
    uint32_t load=native_driver.load_address;
    uint32_t first=at&UINT32_C(0x1fffffff),begin=load&UINT32_C(0x1fffffff);
    if(native_driver.image_bytes!=exec_bytes ||
       (passive_layout.version!=KUI_VMU_NATIVE_LAYOUT_VERSION &&
        passive_layout.version!=KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION) ||
       passive_layout.bytes!=sizeof(passive_layout) || (at&1u) ||
       first<begin || first-begin>exec_bytes || exec_bytes-(first-begin)<12u)
        return false;
    const uint16_t *op=(const void *)(uintptr_t)(first|UINT32_C(0x80000000));
    static const uint16_t uniform[6]={0x2fe6u,0xde3eu,0xd33eu,0x9071u,0x6233u,0x2fd6u};
    static const uint16_t variable[6]={0x2fe6u,0x2fd6u,0x2fc6u,0x2fb6u,0x2fa6u,0x2f96u};
    if(passive_layout.rx_layout==KUI_VMU_NATIVE_RX_EXPLICIT48)
        return first==(native_driver.initializer&UINT32_C(0x1fffffff))+0x11cu &&
            !memcmp(op,variable,sizeof(variable));
    if(passive_layout.rx_layout!=KUI_VMU_NATIVE_RX_UNIFORM48 ||
       first!=(native_driver.initializer&UINT32_C(0x1fffffff)) ||
       memcmp(op,uniform,sizeof(uniform))) return false;
    uint32_t cell=((at+6u)&~3u)+4u*(op[1]&255u);
    uint32_t object=((at+8u)&~3u)+4u*(op[2]&255u);
    uint32_t offset=at+10u+2u*(op[3]&255u);
    uint32_t end=begin+exec_bytes;
    if((cell&UINT32_C(0x1fffffff))<begin || (object&UINT32_C(0x1fffffff))<begin ||
       (offset&UINT32_C(0x1fffffff))<begin ||
       (cell&UINT32_C(0x1fffffff))>end-4u ||
       (object&UINT32_C(0x1fffffff))>end-4u ||
       (offset&UINT32_C(0x1fffffff))>end-2u) return false;
    return native_word(cell)==native_driver.manager_cell &&
        native_word(object)==native_driver.manager_initial &&
        *(const uint16_t *)(uintptr_t)offset==0x0fc4u;
}

/* The exact wrapper already loads the low initializer through a typed
 * PC-relative function-pointer literal. Redirect that data cell, preserving
 * the original initializer entry and every possible direct branch into it. */
static bool native_sdk_coherence_span(uint32_t at,uint32_t bytes,unsigned alignment) {
    uint32_t area=at&UINT32_C(0xe0000000),p=at&UINT32_C(0x1fffffff);
    uint32_t begin=native_driver.load_address&UINT32_C(0x1fffffff);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        bytes && !(p&(alignment-1u)) && p>=begin && p-begin<=exec_bytes &&
        bytes<=exec_bytes-(p-begin);
}
static bool native_sdk_coherence_overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    a&=UINT32_C(0x1fffffff);b&=UINT32_C(0x1fffffff);
    return an && bn && a<b+bn && b<a+an;
}
static bool native_sdk_coherence_literal(uint32_t pc,unsigned reg,uint32_t *slot,uint32_t *value) {
    if(!native_sdk_coherence_span(pc,2u,2u)) return false;
    uint16_t op=*(const uint16_t *)(uintptr_t)pc;
    if((op&0xff00u)!=(0xd000u|(reg<<8))) return false;
    uint32_t at=((pc+4u)&~3u)+4u*(op&255u);
    if(!native_sdk_coherence_span(at,4u,4u)) return false;
    if(slot) *slot=at;
    if(value) *value=native_word(at);
    return true;
}
static bool __attribute__((noipa)) native_sdk_coherence_slot(uint32_t *out) {
    if(!out || !native_sdk_coherence_window_valid()) return false;
    bool variable=passive_layout.rx_layout==KUI_VMU_NATIVE_RX_EXPLICIT48;
    uint32_t wrapper=passive_layout.initializer_wrapper;
    unsigned length=variable?56u:52u;
    if(!native_sdk_coherence_span(wrapper,length,2u)) return false;
    const uint16_t *op=(const void *)(uintptr_t)wrapper;
    static const uint16_t uniform[26]={0x4f22,0x7ff4,0x0002,0,0x2f42,0x4009,0x1f51,0x4009,
        0xc90f,0x1f02,0x0002,0x2039,0xcbf0,0x400e,0,0,0x6242,0x223b,0,0x2422,0,0,
        0x66f2,0x6573,0x430b,0x54f1};
    static const uint16_t alternate[28]={0x4f22,0x7fec,0x0002,0,0x1f43,0x4009,0x1f52,0x4009,
        0x1f61,0xc90f,0x2f72,0x1f04,0x0002,0x2039,0xcbf0,0x400e,0,0,0x6242,0x223b,0,
        0x2422,0x55f2,0,0x67f2,0x56f1,0x430b,0x54f3};
    const uint16_t *expected=variable?alternate:uniform;
    for(unsigned i=0;i<length/2u;i++) if(expected[i] && op[i]!=expected[i]) return false;
    if((op[3]&0xff00u)!=0x9300u) return false;
    uint32_t mask_at=wrapper+10u+2u*(op[3]&255u);
    if(!native_sdk_coherence_span(mask_at,2u,2u) ||
       *(const uint16_t *)(uintptr_t)mask_at!=0xff0fu) return false;
    unsigned lock_at=variable?32u:28u,call_at=variable?46u:42u;
    unsigned bsr_at=variable?40u:36u;
    if((op[bsr_at/2u]&0xf000u)!=0xb000u) return false;
    uint32_t value,slot;
    if(!native_sdk_coherence_literal(wrapper+lock_at,4u,NULL,&value) ||
       value!=native_driver.lock_cell ||
       !native_sdk_coherence_literal(wrapper+lock_at+2u,3u,NULL,&value) ||
       value!=UINT32_C(0xa0000000) ||
       (!variable && (!native_sdk_coherence_literal(wrapper+40u,7u,NULL,&value) || value!=0xc000u)) ||
       !native_sdk_coherence_literal(wrapper+call_at,3u,&slot,&value) ||
       !native_sdk_coherence_span(value,12u,2u) ||
       (value&UINT32_C(0x1fffffff))!=(passive_layout.initializer&UINT32_C(0x1fffffff))) return false;
    /* A function-pointer literal cannot alias any code window or another
     * retained SDK input/control field. Absolute aliases compare physically. */
    const uint32_t blocked[][2]={{native_driver.submit,12u},{native_heap.initializer,12u},
        {native_heap.release,20u},{passive_layout.initializer,12u},{wrapper,length},
        {native_driver.lock_cell,5u},{native_driver.manager_cell,4u},{native_driver.manager_initial,0x1100u},
        {passive_layout.gun_flag,4u},{passive_layout.timer1_callback,4u},{passive_layout.timer2_callback,4u},
        {passive_layout.input_array,passive_layout.inputs*4u},{passive_layout.index_array,passive_layout.inputs*4u},
        {passive_layout.tx_base,0x6000u}};
    for(unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);i++)
        if(native_sdk_coherence_overlap(slot,4u,blocked[i][0],blocked[i][1])) return false;
    if(native_exits.count>KUI_VMU_NATIVE_EXIT_ENTRIES) return false;
    for(unsigned i=0;i<native_exits.count;i++)
        if(native_sdk_coherence_overlap(slot,4u,native_exits.entries[i],12u)) return false;
    for(unsigned i=0;i<KUI_VMU_NATIVE_RX_SLOTS;i++)
        if(passive_layout.rx_slots[i] && native_sdk_coherence_overlap(slot,4u,passive_layout.rx_slots[i],1024u)) return false;
    *out=slot;return true;
}
static bool __attribute__((noipa)) native_sdk_coherence_cached_admit(void) {
    uint32_t slot;return native_sdk_coherence_slot(&slot);
}

static bool native_sdk_cold_entry(uint32_t entry,uint32_t dest,uint32_t bytes) {
    if((entry&UINT32_C(0xe0000000))!=UINT32_C(0xa0000000)) return false;
#if KUI_VMU_NATIVE_CACHED_CODE
    entry-=UINT32_C(0x20000000);
#endif
    return passive_entry(entry,dest,bytes);
}

static int __attribute__((noipa)) native_sdk_coherence_install(
    const struct kui_vmu_native_passive_exports *e,uint32_t dest,uint32_t bytes) {
    if(!e || bytes<80u+40u || e->bss_begin!=dest+bytes ||
       e->bss_end<e->bss_begin || e->bss_end>e->stack_bottom ||
       !native_sdk_coherence_window_valid()) return -1;
    uint32_t slot;
    if(!native_sdk_coherence_slot(&slot)) return -1;
    uint32_t table=(e->trace_exports&UINT32_C(0x1fffffff))|UINT32_C(0xa0000000);
    if((table&3u) || table<dest+80u || table>dest+bytes-40u) return -1;
    const uint32_t *t=(const void *)(uintptr_t)table;
    if(t[0]!=UINT32_C(0x4b545231) || t[1]!=2u || t[2]!=40u ||
       !native_sdk_cold_entry(t[7],dest,bytes) ||
       !native_sdk_cold_entry(t[8],dest,bytes) || (t[9]&3u) ||
       (t[9]&UINT32_C(0xe0000000))!=UINT32_C(0xa0000000) ||
       t[9]<e->bss_begin || t[9]>e->bss_end || e->bss_end-t[9]<48u)
        return -1;
    for(unsigned i=3u;i<7u;i++) if(!passive_entry(t[i],dest,bytes)) return -1;
    const volatile uint32_t *s=(const void *)(uintptr_t)t[9];
    uint32_t init=(passive_layout.initializer&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    uint32_t lock=(native_driver.lock_cell&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    unsigned kind=passive_layout.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48?0u:1u;
    if(s[0]!=lock || s[1]!=native_driver.manager_cell ||
       s[2]!=native_driver.manager_initial || s[3]!=init+12u ||
       s[4] || s[5] || s[6] || s[7]!=UINT32_C(0x4e434f31) ||
       s[8]!=((lock+4u)|UINT32_C(0xa0000000)) || s[9]!=kind || s[10]!=init || s[11])
        return -1;
    *(volatile uint32_t *)(uintptr_t)slot=t[7u+kind];
    kui_native_passive_publish(slot,slot+4u);
    return native_word(slot|UINT32_C(0x20000000))==t[7u+kind]?1:-2;
}
