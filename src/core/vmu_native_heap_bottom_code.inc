/* SPDX-License-Identifier: GPL-3.0-only */
/* Cold-only structural contract for the existing tail32 SDK allocator.
 * Before retaining its first chunk, the installer temporarily calls the
 * original allocator twice and then releases the upper placeholder. Bind
 * every instruction in both complete entry bodies, not just the displaced
 * entry windows. Decoded pointer literals retain the discovered owner roles.
 * This grants no allocation, release, placement or lifetime authority alone.
 *
 * The allocator's +0x42 mask word shares an unreferenced padding halfword
 * at +0x44 (observed as0000 orffff). Fixed branches skip the literal pool;
 * that padding has no executed consumer. All seven pointer words are checked
 * separately below, including the shared initializer/dispatch literal roles.
 * Reads preserve the caller's original alias and use aligned32-bit words. */
static const uint16_t heap_bottom_allocate_words[103]={
    0x2fe6u,0x7ffcu,0x0002u,0x931cu,0x4009u,0x4009u,0xc90fu,0x2f01u,
    0x0002u,0x2039u,0xcbf0u,0x400eu,0xde0fu,0x6543u,0x751fu,0x67f1u,
    0x61e2u,0x4509u,0x4509u,0x6613u,0x6462u,0x4501u,0x677du,0x7501u,
    0x5241u,0x3252u,0x8b2cu,0x5141u,0x3150u,0x8b12u,0x6242u,0xa018u,
    0x2622u,0xff0fu,0x0000u,0x0000u,0x0000u,0x0000u,0x0000u,0x0000u,
    0x0000u,0x0000u,0x0000u,0x0000u,0x0000u,0x0000u,0x0000u,0x0000u,
    0x0000u,0x3158u,0x6313u,0x4308u,0x4308u,0x1411u,0x4300u,0x343cu,
    0x1451u,0x6073u,0x9382u,0x0202u,0xc90fu,0x2e62u,0x4008u,0x2239u,
    0x4008u,0x202bu,0x400eu,0x6043u,0x7020u,0x7f04u,0x000bu,0x6ef6u,
    0x3410u,0x8b0cu,0x0302u,0x6073u,0x9270u,0xc90fu,0x4008u,0x2329u,
    0x4008u,0x203bu,0x400eu,0xe000u,0x7f04u,0x000bu,0x6ef6u,0x6643u,
    0xafbeu,0x6442u,0x7f04u,0x000bu,0x6ef6u,0xd030u,0x6102u,0x5311u,
    0x432bu,0x0009u,0xd22eu,0x6322u,0x5131u,0x412bu,0x0009u,
};

static const uint16_t heap_bottom_release_words[77]={
    0x0002u,0x9354u,0x4009u,0x4009u,0xc90fu,0x6703u,0x0002u,0x2039u,
    0xcbf0u,0x400eu,0x2448u,0x8935u,0xd326u,0x6543u,0x75e0u,0xa008u,
    0x6432u,0x6642u,0x3462u,0x8b03u,0x3546u,0x8907u,0x3562u,0x8b05u,
    0x6442u,0x3546u,0x8bf5u,0x6242u,0x3522u,0x89f2u,0x5151u,0x6342u,
    0x4108u,0x4108u,0x4100u,0x315cu,0x3130u,0x8b08u,0x6142u,0x5251u,
    0x5311u,0x323cu,0x1521u,0x6142u,0x6312u,0xa002u,0x2532u,0x6142u,
    0x2512u,0x5341u,0x4308u,0x4308u,0x4300u,0x334cu,0x3350u,0x8b06u,
    0x5241u,0x5351u,0x323cu,0x1421u,0x6152u,0xa001u,0x2412u,0x2452u,
    0xd20cu,0x2242u,0x0302u,0x607du,0x9211u,0xc90fu,0x4008u,0x2329u,
    0x4008u,0x203bu,0x400eu,0x000bu,0x0009u,
};

static bool heap_bottom_code_span(const struct kui_vmu_native_heap *h,
    uint32_t address,uint32_t bytes) {
    uint32_t at=phys(address),begin=phys(h->load);
    return span(address,bytes) && at>=begin && at-begin<=h->image_bytes &&
        bytes<=h->image_bytes-(at-begin);
}
static bool heap_bottom_halfword(kui_vmu_native_heap_read read,void *context,
    uint32_t address,uint16_t expected) {
    uint32_t value;
    return read(context,address&~UINT32_C(3),&value) &&
        (uint16_t)(value>>((address&2u)*8u))==expected;
}
bool kui_vmu_native_heap_bottom_code_valid(const struct kui_vmu_native_heap *h,
    kui_vmu_native_heap_read read,void *context) {
    if(!h || !read || !metadata(h) || (h->initializer&3u) ||
       !heap_bottom_code_span(h,h->allocate,0xceu) ||
       !heap_bottom_code_span(h,h->release,0xb8u)) return false;
    for(unsigned i=0u;i<sizeof(heap_bottom_allocate_words)/sizeof(heap_bottom_allocate_words[0]);i++) {
        unsigned offset=2u*i;
        if(offset==0x44u || (offset>=0x46u && offset<0x62u)) continue;
        if(!heap_bottom_halfword(read,context,h->allocate+offset,
            heap_bottom_allocate_words[i])) return false;
    }
    const uint32_t roles[7]={h->begin_cell,h->begin_copy_cell,h->bytes_cell,
        h->sentinel,h->cursor_cell,h->dispatch_cell,h->dispatch_table};
    for(unsigned i=0u;i<7u;i++)
        if(!read_is(read,context,h->allocate+0x46u+4u*i,roles[i])) return false;
    for(unsigned i=0u;i<sizeof(heap_bottom_release_words)/sizeof(heap_bottom_release_words[0]);i++)
        if(!heap_bottom_halfword(read,context,h->release+2u*i,
            heap_bottom_release_words[i])) return false;
    /* Exact PC-relative consumers are already pinned by the complete bodies:
     * allocate+06 -> allocate+42; allocate+74/+98 -> release+AE;
     * release+02/+88 -> release+AE; release+18/+80 -> release+B4.
     * The two fixed dispatch thunks at the end of the allocator body also
     * reference release+B0; they cannot introduce another mutable target. */
    return heap_bottom_halfword(read,context,h->release+0xaeu,0xff0fu) &&
        read_is(read,context,h->release+0xb0u,h->dispatch_cell) &&
        read_is(read,context,h->release+0xb4u,h->cursor_cell);
}
