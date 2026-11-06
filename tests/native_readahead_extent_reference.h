/* SPDX-License-Identifier: GPL-3.0-only */
/* Frozen public validator from 242169041894. This independent old arithmetic
 * is the differential oracle; never update it along with the implementation. */
/* Validated maps permit one shared512B container boundary between adjacent
 * tracks, with disjoint byte slices. This does not authorize crossing either
 * track's mapped extent while speculatively filling the cache. */
static bool reference_shared_boundary(const struct kui_retail_manifest *m,uint32_t ai,uint32_t bi,
    uint32_t aowner,uint32_t bowner) {
    if(aowner+1u!=bowner && bowner+1u!=aowner)return false;
    const struct kui_retail_extent *a=&m->slots[ai].extent,*b=&m->slots[bi].extent;
    if(aowner>bowner) {
        const struct kui_retail_extent *swap=a;a=b;b=swap;
        uint32_t owner=aowner;aowner=bowner;bowner=owner;
    }
    const struct kui_retail_track *ta=&m->slots[aowner].track,*tb=&m->slots[bowner].track;
    uint64_t first=a->card_lba>b->card_lba?a->card_lba:b->card_lba;
    uint64_t last=(uint64_t)a->card_lba+a->blocks;
    if(last>(uint64_t)b->card_lba+b->blocks)last=(uint64_t)b->card_lba+b->blocks;
    uint64_t end=(uint64_t)kui_retail_track_file_offset(ta)+
        (uint64_t)(ta->end_lba-ta->start_lba)*kui_retail_track_sector_bytes(ta);
    uint32_t begin=kui_retail_track_file_offset(tb);
    return last-first==1u && (end&511u) && begin && (end&511u)<=begin &&
        (uint64_t)a->file_block+first-a->card_lba==(end-1u)/512u &&
        (uint64_t)b->file_block+first-b->card_lba==0;
}
uint32_t reference_extent_end(const struct kui_retail_manifest *m,uint32_t lba) {
    const uint32_t slots=KUI_RETAIL_MANIFEST_SLOTS<KUI_RETAIL_ASYNC_SLOTS?
        KUI_RETAIL_MANIFEST_SLOTS:KUI_RETAIL_ASYNC_SLOTS;
    if(!m || !m->track_count || m->track_count>slots ||
       m->track_count>KUI_RETAIL_IMAGE_TRACKS || !m->extent_count ||
       m->extent_count>slots-m->track_count || !m->card_sectors ||
       m->card_sectors>UINT64_C(0x100000000) ||
       m->partition_start>=m->partition_end || m->partition_end>m->card_sectors)return 0;
    uint32_t used=m->track_count+m->extent_count,next=m->track_count,bound=0;
    uint8_t owners[KUI_RETAIL_ASYNC_SLOTS];
    for(uint32_t i=0;i<m->track_count;i++) {
        const struct kui_retail_track *t=&m->slots[i].track;
        if(t->start_lba>=t->end_lba || t->end_lba>KUI_GAME_LBA_LIMIT ||
           (i && m->slots[i-1u].track.end_lba>t->start_lba) || (t->control&3u) ||
           (!(t->control&4u) && (t->control&(KUI_RETAIL_TRACK_COOKED|KUI_RETAIL_TRACK_MODE2|KUI_RETAIL_TRACK_2336))) ||
           ((t->control&KUI_RETAIL_TRACK_COOKED) && (t->control&(KUI_RETAIL_TRACK_2336|KUI_RETAIL_TRACK_2448))) ||
           ((t->control&KUI_RETAIL_TRACK_2336) && (!(t->control&KUI_RETAIL_TRACK_MODE2) || (t->control&KUI_RETAIL_TRACK_2448))) ||
           kui_retail_track_first_extent(t)!=next || t->extent_count>used-next ||
           (!t->extent_count && kui_retail_track_control(t)==4u))return 0;
        uint64_t bytes=(uint64_t)kui_retail_track_file_offset(t)+
            (uint64_t)(t->end_lba-t->start_lba)*kui_retail_track_sector_bytes(t);
        uint64_t blocks=(bytes+511u)/512u;
        uint32_t file=0;
        if(blocks>UINT32_MAX)return 0;
        for(uint32_t j=0;j<t->extent_count;j++) {
            uint32_t index=next+j;
            const struct kui_retail_extent *e=&m->slots[index].extent;
            uint64_t end=(uint64_t)e->card_lba+e->blocks;
            if(e->file_block!=file || !e->blocks || file>blocks || e->blocks>blocks-file ||
               e->card_lba<m->partition_start || end>m->partition_end || end>UINT32_MAX)return 0;
            owners[index]=(uint8_t)i;
            if(lba>=e->card_lba && lba-e->card_lba<e->blocks && (!bound || end<bound))bound=(uint32_t)end;
            file+=e->blocks;
        }
        if(t->extent_count && file!=blocks)return 0;
        next+=t->extent_count;
    }
    if(next!=used)return 0;
    for(uint32_t i=m->track_count;i<used;i++) {
        const struct kui_retail_extent *a=&m->slots[i].extent;
        for(uint32_t j=m->track_count;j<i;j++) {
            const struct kui_retail_extent *b=&m->slots[j].extent;
            if((uint64_t)a->card_lba<(uint64_t)b->card_lba+b->blocks &&
               (uint64_t)b->card_lba<(uint64_t)a->card_lba+a->blocks &&
               !reference_shared_boundary(m,i,j,owners[i],owners[j]))return 0;
        }
    }
    return bound;
}
