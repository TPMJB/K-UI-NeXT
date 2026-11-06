/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_stage_cache.h"
#include <string.h>

void kui_vc2_ce_vmu_stage_cache_invalidate(struct kui_vc2_ce_vmu_stage_cache *c) {
    if(c) {
        c->base_valid=0u;c->fat_blocks[0]=c->fat_blocks[1]=0u;c->mode=0u;
        memset(c->memo_valid,0,sizeof(c->memo_valid));
        memset(c->memo_fat_blocks,0,sizeof(c->memo_fat_blocks));
    }
}
static unsigned vc2_stage_memo_index(uint32_t lba) {
    return (lba^(lba>>8)^(lba>>16)^(lba>>24))&(KUI_VC2_STAGE_MEMO_BLOCKS-1u);
}
static int vc2_stage_memo_get(struct kui_vc2_ce_vmu_stage_cache *c,uint32_t lba,void *out) {
    unsigned i=vc2_stage_memo_index(lba);
    if(!(c->memo_valid[i/32u]&(UINT32_C(1)<<(i%32u))) || c->memo_lba[i]!=lba)return 0;
    ++c->hits;memcpy(out,c->base[i],512u);return 1;
}
static void vc2_stage_memo_put(struct kui_vc2_ce_vmu_stage_cache *c,uint32_t lba,const void *in) {
    unsigned i=vc2_stage_memo_index(lba);
    memcpy(c->base[i],in,512u);c->memo_lba[i]=lba;
    c->memo_valid[i/32u]|=UINT32_C(1)<<(i%32u);
}
/* Native metadata admission does not use the CE FAT windows. Reuse those
 * 32 KiB as four verified 16-sector windows; the 256 metadata tags keep their
 * accepted mapping and capacity. Mode transitions invalidate both uses. */
static uint32_t *vc2_stage_memo_fat_lba(struct kui_vc2_ce_vmu_stage_cache *c,unsigned i) {
    return i<2u?&c->fat_lba[i]:&c->fat_blocks[i-2u];
}
static uint32_t vc2_stage_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t vc2_stage_u64(const uint8_t *p) {
    return vc2_stage_u32(p)|(uint64_t)vc2_stage_u32(p+4u)<<32;
}
static int vc2_stage_exfat_boot(const struct kui_vmu_persist_profile *p,const uint8_t b[512]) {
    uint64_t offset=vc2_stage_u64(b+64u),volume=vc2_stage_u64(b+72u);
    uint32_t fat=vc2_stage_u32(b+80u),sectors=vc2_stage_u32(b+84u),heap=vc2_stage_u32(b+88u);
    return p->partition_end>=p->partition_first && volume>=24u &&
        volume==p->partition_end-p->partition_first && (!offset || offset==p->partition_first) &&
        !memcmp(b+3u,"EXFAT   ",8u) && b[510]==0x55u && b[511]==0xaau &&
        b[108]==9u && b[110]==1u && fat>=24u && sectors &&
        (uint64_t)fat+sectors<=heap && heap<volume;
}
static int vc2_stage_run(struct kui_vc2_ce_vmu_stage_cache *c,
    const struct kui_vmu_persist_profile *p,uint64_t lba,uint32_t count,void *out,
    kui_vc2_stage_read_run read,void *context) {
    if(!count || count>KUI_VC2_STAGE_READ_BLOCKS || lba>UINT32_MAX ||
       lba>=p->card_sectors || count>p->card_sectors-lba ||
       (uint64_t)count>UINT64_C(0x100000000)-lba)return -1;
    ++c->commands;c->sectors+=count;
    return read(context,(uint32_t)lba,count,out);
}
static int vc2_stage_base_index(const struct kui_vmu_persist_profile *p,uint64_t lba) {
    const struct kui_vmu_persist_file *f=&p->files[KUI_VMU_PERSIST_BASE];
    if(!f->useful_count || f->useful_count>KUI_VMU_PERSIST_PROFILE_MAX_EXTENTS)return -1;
    for(uint32_t i=0u;i<f->useful_count;i++) {
        const struct kui_vmu_persist_extent *e=&f->useful[i];
        if(lba>=e->lba && lba-e->lba<e->blocks &&
           e->logical_block<256u && lba-e->lba<256u-e->logical_block)
            return (int)(e->logical_block+(uint32_t)(lba-e->lba));
    }
    return -1;
}
static int vc2_stage_base_load(struct kui_vc2_ce_vmu_stage_cache *c,
    const struct kui_vmu_persist_profile *p,kui_vc2_stage_read_run read,void *context) {
    const struct kui_vmu_persist_file *f=&p->files[KUI_VMU_PERSIST_BASE];
    c->base_valid=0u;uint32_t done=0u;
    for(uint32_t i=0u;i<f->useful_count;i++) {
        const struct kui_vmu_persist_extent *e=&f->useful[i];
        if(e->logical_block!=done || !e->blocks || e->blocks>256u-done)return -1;
        for(uint32_t at=0u;at<e->blocks;) {
            uint32_t count=e->blocks-at;if(count>KUI_VC2_STAGE_READ_BLOCKS)count=KUI_VC2_STAGE_READ_BLOCKS;
            if(vc2_stage_run(c,p,(uint64_t)e->lba+at,count,c->base[done+at],read,context))return -1;
            at+=count;
        }
        done+=e->blocks;
    }
    if(done!=256u)return -1;
    ++c->base_loads;c->base_valid=1u;return 0;
}
static int vc2_stage_read(struct kui_vc2_ce_vmu_stage_cache *c,
    const struct kui_vmu_persist_profile *p,bool admission,bool fat_windows,bool base_snapshot,
    uint64_t lba,void *out,kui_vc2_stage_read_run read,void *context) {
    if(!c || !p || !out || !read || (admission && base_snapshot) ||
       lba>=p->card_sectors || lba>UINT32_MAX)return -1;
    uint32_t mode=1u|(admission?2u:0u)|(base_snapshot?4u:0u)|(fat_windows?8u:0u);
    if(c->mode!=mode) {kui_vc2_ce_vmu_stage_cache_invalidate(c);c->mode=mode;}
    ++c->requests;
    int block=base_snapshot?vc2_stage_base_index(p,lba):-1;
    if(block>=0) {
        if(c->base_valid)++c->hits;
        else if(vc2_stage_base_load(c,p,read,context))return -1;
        memcpy(out,c->base[(unsigned)block],512u);return 0;
    }
    if(admission && vc2_stage_memo_get(c,(uint32_t)lba,out))return 0;
    const struct kui_vmu_persist_geometry *g=&p->geometry;
    if(fat_windows && g->fat_count>=1u && g->fat_count<=2u && g->fat_sectors &&
       lba>=g->fat_first_lba && lba-g->fat_first_lba<g->fat_sectors*g->fat_count) {
        uint64_t delta=lba-g->fat_first_lba;
        unsigned copy=(unsigned)(delta/g->fat_sectors);
        uint32_t sector=(uint32_t)(delta%g->fat_sectors);
        uint64_t begin=g->fat_first_lba+copy*g->fat_sectors+
            sector-sector%KUI_VC2_STAGE_FAT_BLOCKS;
        /* Fill the aligned window, clipped at the exact FAT copy boundary. */
        uint32_t count=KUI_VC2_STAGE_FAT_BLOCKS;
        uint64_t available=g->fat_sectors-sector+sector%KUI_VC2_STAGE_FAT_BLOCKS;
        if(available<count)count=(uint32_t)available;
        if(c->fat_blocks[copy] && c->fat_lba[copy]==begin)++c->hits;
        else {
            c->fat_blocks[copy]=0u;
            if(vc2_stage_run(c,p,begin,count,c->fat[copy],read,context))return -1;
            c->fat_lba[copy]=(uint32_t)begin;c->fat_blocks[copy]=count;
            for(uint32_t i=0u;i<count;i++)
                vc2_stage_memo_put(c,(uint32_t)begin+i,c->fat[copy][i]);
        }
        memcpy(out,c->fat[copy][(unsigned)(lba-begin)],512u);return 0;
    }
    if(vc2_stage_run(c,p,lba,1u,c->single,read,context))return -1;
    if(admission) {
        /* Select read-ahead only from a freshly checked live VBR. The
         * scanner independently checks every boot byte and checksum. */
        if(lba==p->partition_first && vc2_stage_exfat_boot(p,c->single)) {
            /* The boot receiver overlaps native memo windows. Drop their
             * tags before even a failed 23-sector fill can touch the bytes. */
            c->fat_blocks[0]=0u;memset(c->memo_fat_blocks,0,sizeof(c->memo_fat_blocks));
            if(vc2_stage_run(c,p,lba+1u,23u,c->fat[0],read,context))return -1;
            for(uint32_t i=0u;i<23u;i++)
                vc2_stage_memo_put(c,(uint32_t)lba+1u+i,c->fat[0][i]);
        }
        vc2_stage_memo_put(c,(uint32_t)lba,c->single);
    }
    if(out!=c->single)memcpy(out,c->single,512u);
    return 0;
}
int kui_vc2_ce_vmu_stage_cache_read(struct kui_vc2_ce_vmu_stage_cache *c,
    const struct kui_vmu_persist_profile *p,bool fat_windows,bool base_snapshot,
    uint64_t lba,void *out,kui_vc2_stage_read_run read,void *context) {
    return vc2_stage_read(c,p,fat_windows,fat_windows,base_snapshot,lba,out,read,context);
}
int kui_vc2_ce_vmu_stage_cache_metadata_read(struct kui_vc2_ce_vmu_stage_cache *c,
    const struct kui_vmu_persist_profile *p,bool admission,uint64_t lba,void *out,
    kui_vc2_stage_read_run read,void *context) {
    return vc2_stage_read(c,p,admission,false,false,lba,out,read,context);
}
int kui_vc2_ce_vmu_stage_cache_admission_peek(struct kui_vc2_ce_vmu_stage_cache *c,
    const struct kui_vmu_persist_profile *p,uint64_t lba,void *out) {
    if(!c || !p || !out || lba>=p->card_sectors || lba>UINT32_MAX)return -1;
    if(c->mode!=3u)return 0;
    for(unsigned i=0u;i<4u;i++) {
        uint32_t first=*vc2_stage_memo_fat_lba(c,i),count=c->memo_fat_blocks[i];
        if(count>16u || (count && ((uint64_t)first>=p->card_sectors ||
           count>p->card_sectors-first || (uint64_t)count>UINT64_C(0x100000000)-first)))return -1;
        if(count && lba>=first && lba-first<count) {
            ++c->hits;++c->requests;memcpy(out,(const uint8_t *)c->fat+(i*16u+lba-first)*512u,512u);return 1;
        }
    }
    int hit=vc2_stage_memo_get(c,(uint32_t)lba,out);
    if(hit)++c->requests;
    return hit;
}
int kui_vc2_ce_vmu_stage_cache_admission_store(struct kui_vc2_ce_vmu_stage_cache *c,
    const struct kui_vmu_persist_profile *p,uint32_t lba,uint32_t count,const void *data) {
    if(!c || !p || !data || c->mode!=3u ||
       !count || count>KUI_VC2_STAGE_READ_BLOCKS || lba>=p->card_sectors ||
       count>p->card_sectors-lba || (uint64_t)count>UINT64_C(0x100000000)-lba)return -1;
    for(uint32_t at=0u;at<count;) {
        uint32_t run=count-at;if(run>16u)run=16u;
        unsigned i;
        for(i=0u;i<4u;i++)if(!c->memo_fat_blocks[i] || *vc2_stage_memo_fat_lba(c,i)==lba+at)break;
        /* base_valid is unused in metadata-admission mode. Its low two
         * bits select the next victim; validation resets it before use. */
        if(i==4u){i=c->base_valid&3u;c->base_valid=(i+1u)&3u;}
        memcpy((uint8_t *)c->fat+i*16u*512u,(const uint8_t *)data+at*512u,run*512u);
        *vc2_stage_memo_fat_lba(c,i)=lba+at;c->memo_fat_blocks[i]=run;at+=run;
    }
    return 0;
}
