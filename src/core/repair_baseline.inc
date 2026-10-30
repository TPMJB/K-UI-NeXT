/* SPDX-License-Identifier: GPL-3.0-only */
/* Derived, bounded scan state only. The durable checkpoint CRC/SHA and original/
 * candidate evidence remain authority; this bitmap never authorizes a patch. */
static bool repair_baseline_start(struct recovery_copy *s) {
    s->baseline_offsets[0]=0;
    for(unsigned i=0;i<s->source.plan.count;++i) {
        uint32_t count=s->source.plan.tracks[i].end-s->source.plan.tracks[i].start;
        if(count>720000u-s->baseline_offsets[i])return fail(s,"Repair baseline exceeds admitted disc bounds");
        s->baseline_offsets[i+1]=s->baseline_offsets[i]+count;
    }
    size_t bytes=(s->baseline_offsets[s->source.plan.count]+7u)/8u;
    free(s->baseline_targets);s->baseline_targets=calloc(bytes?bytes:1u,1);
    if(!s->baseline_targets)return fail(s,"Cannot allocate bounded repair scan map");
    s->baseline_collect=true;s->baseline_ready=false;s->baseline_partial_valid=false;return true;
}
static bool repair_baseline_scan(struct recovery_copy *s,unsigned ti,uint64_t offset,const uint8_t *data,unsigned bytes) {
    if(!s->baseline_collect)return true;
    if(ti>=s->source.plan.count || offset%KUI_RAW_BYTES || bytes%KUI_RAW_BYTES ||
       offset/KUI_RAW_BYTES+bytes/KUI_RAW_BYTES>s->source.plan.tracks[ti].end-s->source.plan.tracks[ti].start)
        return fail(s,"Repair baseline chunk has invalid raw-sector ranges");
    if(s->source.plan.tracks[ti].control!=4)return true;
    uint32_t sector=(uint32_t)(offset/KUI_RAW_BYTES);
    for(unsigned at=0;at<bytes;at+=KUI_RAW_BYTES,++sector) {
        if(cancelled(s))return false;
        if(check_sector(s,data+at,s->source.plan.tracks[ti].start+sector)) {
            uint32_t index=s->baseline_offsets[ti]+sector;s->baseline_targets[index>>3]|=(uint8_t)(1u<<(index&7u));
        }
    }
    return true;
}
static bool repair_baseline_target(struct recovery_copy *s,unsigned ti,uint32_t sector) {
    if(!s->baseline_ready)return true;
    uint32_t index=s->baseline_offsets[ti]+sector;
    return (s->baseline_targets[index>>3]&(1u<<(index&7u)))!=0;
}
static void repair_baseline_hash(struct recovery_copy *s,unsigned ti,uint32_t sectors,uint32_t crc,const struct kui_sha256 *hash) {
    uint32_t count=s->source.plan.tracks[ti].end-s->source.plan.tracks[ti].start;
    if(!s->in_place || !sectors || sectors>=count)return;
    s->baseline_partial_hash=*hash;s->baseline_partial_track=ti;
    s->baseline_partial_sectors=sectors;s->baseline_partial_crc=crc;s->baseline_partial_valid=true;
}
static bool repair_baseline_load(struct recovery_copy *s,unsigned ti,struct kui_sha256 *hash,uint32_t *crc,bool *hash_known) {
    *hash_known=true;
    if(!s->committed[ti]) {kui_sha256_init(hash);*crc=0;return true;}
    if(s->baseline_partial_valid && s->baseline_partial_track==ti &&
       s->baseline_partial_sectors==s->committed[ti] && s->baseline_partial_crc==s->crc[ti]) {
        *hash=s->baseline_partial_hash;*crc=s->crc[ti];return true;
    }
    /* A patch changes SHA's non-composable running state. Keep CRC-only
     * intermediate checkpoints for this already checked prefix instead of
     * rereading healthy bytes solely to reconstruct SHA. The final full saved
     * read establishes the new SHA before any successful publication. */
    kui_sha256_init(hash);*crc=s->crc[ti];*hash_known=false;return true;
}
