/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_capsule.h"
#include <string.h>

static bool overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    return an && bn && (uint64_t)a<(uint64_t)b+bn && (uint64_t)b<(uint64_t)a+an;
}
static bool runs_valid(const struct kui_smash_menu_capsule_runs *r) {
    return r && r->blocks && r->first_lba && r->first_blocks && r->first_blocks<=r->blocks &&
        (!!r->second_lba==(r->first_blocks<r->blocks)) &&
        (uint64_t)r->first_lba+r->first_blocks<=UINT64_C(0x100000000) &&
        (uint64_t)r->second_lba+r->blocks-r->first_blocks<=UINT64_C(0x100000000) &&
        (!r->second_lba || !overlap(r->first_lba,r->first_blocks,r->second_lba,r->blocks-r->first_blocks));
}
bool kui_smash_menu_capsule_runs_disjoint(const struct kui_smash_menu_capsule_runs *a,
    const struct kui_smash_menu_capsule_runs *b) {
    if(!runs_valid(a) || !runs_valid(b))return false;
    const uint32_t al[2]={a->first_lba,a->second_lba},an[2]={a->first_blocks,a->blocks-a->first_blocks};
    const uint32_t bl[2]={b->first_lba,b->second_lba},bn[2]={b->first_blocks,b->blocks-b->first_blocks};
    for(unsigned i=0u;i<2u;++i)for(unsigned j=0u;j<2u;++j)
        if(overlap(al[i],an[i],bl[j],bn[j]))return false;
    return true;
}
bool kui_smash_menu_capsule_map(const uint32_t *table,size_t words,
    uint32_t data_sector,uint32_t sectors_per_cluster,uint32_t cluster_limit,
    uint32_t volume_first,uint32_t volume_blocks,uint32_t file_blocks,
    struct kui_smash_menu_capsule_runs *out) {
    if(out)memset(out,0,sizeof(*out));
    if(!out || !table || words<4u || (words&1u) || words>UINT32_MAX ||
       table[0]!=words || table[words-1u] || !file_blocks || !volume_blocks ||
       (uint64_t)volume_first+volume_blocks>UINT64_C(0x100000000) ||
       !data_sector || data_sector>=volume_blocks || !sectors_per_cluster ||
       (sectors_per_cluster&(sectors_per_cluster-1u)) || cluster_limit<=2u ||
       (uint64_t)data_sector+(uint64_t)(cluster_limit-2u)*sectors_per_cluster>volume_blocks)return false;
    struct kui_smash_menu_capsule_runs result={0};
    uint32_t lba[2]={0u,0u},blocks[2]={0u,0u},done=0u;unsigned runs=0u;
    for(size_t at=1u;at+1u<words;at+=2u) {
        uint32_t clusters=table[at],first=table[at+1u];
        if(!clusters || first<2u || first>=cluster_limit || clusters>cluster_limit-first)return false;
        uint64_t sector=(uint64_t)data_sector+(uint64_t)(first-2u)*sectors_per_cluster;
        uint64_t count=(uint64_t)clusters*sectors_per_cluster;
        if(sector<data_sector || sector>=volume_blocks || count>volume_blocks-sector)return false;
        if(done==file_blocks)continue;
        if(count>file_blocks-done)count=file_blocks-done;
        uint64_t absolute=(uint64_t)volume_first+sector;
        if(absolute>UINT32_MAX || absolute+count>(uint64_t)volume_first+volume_blocks)return false;
        if(runs && absolute==(uint64_t)lba[runs-1u]+blocks[runs-1u])blocks[runs-1u]+=(uint32_t)count;
        else {
            if(runs==2u)return false;
            lba[runs]=(uint32_t)absolute;blocks[runs]=(uint32_t)count;++runs;
        }
        done+=(uint32_t)count;
    }
    if(done!=file_blocks || !runs ||
       (runs==2u && overlap(lba[0],blocks[0],lba[1],blocks[1])))return false;
    result=(struct kui_smash_menu_capsule_runs){lba[0],blocks[0],runs==2u?lba[1]:0u,file_blocks};
    *out=result;return true;
}
bool kui_smash_menu_capsule_disjoint(const struct kui_smash_menu_capsule_runs *runs,
    const struct kui_retail_manifest *map,const struct kui_vmu_persist_profile *profile) {
    if(!runs_valid(runs) || !map || !profile ||
       map->track_count>KUI_RETAIL_MANIFEST_SLOTS ||
       map->extent_count>KUI_RETAIL_MANIFEST_SLOTS-map->track_count)return false;
    const uint32_t lba[2]={runs->first_lba,runs->second_lba};
    const uint32_t blocks[2]={runs->first_blocks,runs->blocks-runs->first_blocks};
    for(unsigned part=0u;part<2u;++part) {
        if(!blocks[part])continue;
        if(lba[part]<map->partition_start || (uint64_t)lba[part]+blocks[part]>map->partition_end)return false;
        for(uint32_t i=0u;i<map->extent_count;++i) {
            const struct kui_retail_extent *e=&map->slots[map->track_count+i].extent;
            if(overlap(lba[part],blocks[part],e->card_lba,e->blocks))return false;
        }
        for(unsigned role=0u;role<KUI_VMU_PERSIST_PROFILE_FILES;++role) {
            const struct kui_vmu_persist_file *file=&profile->files[role];
            if(file->allocated_count>KUI_VMU_PERSIST_PROFILE_MAX_EXTENTS)return false;
            for(uint32_t i=0u;i<file->allocated_count;++i)
                if(overlap(lba[part],blocks[part],file->allocated[i].lba,file->allocated[i].blocks))return false;
        }
    }
    return true;
}
