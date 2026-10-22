/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_persist_profile.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include <string.h>

static const uint8_t persist_profile_marker_sha[32]={
    0x07,0x6a,0x27,0xc7,0x9e,0x5a,0xce,0x2a,0x3d,0x47,0xf9,0xdd,0x2e,0x83,0xe4,0xff,
    0x6e,0xa8,0x87,0x2b,0x3c,0x22,0x18,0xf6,0x6c,0x92,0xb8,0x9b,0x55,0xf3,0x65,0x60};
static const uint32_t persist_profile_file_bytes[3]={131072u,1024u,16u};
static const uint32_t persist_profile_file_sectors[3]={256u,2u,1u};
static const uint32_t persist_profile_file_limits[3]={16u,2u,1u};
static int persist_profile_zero(const void *buffer,size_t bytes) {
    const uint8_t *p=buffer;for(size_t i=0;i<bytes;i++) if(p[i]) return 0;return 1;
}
static uint32_t persist_profile_get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t persist_profile_get64(const uint8_t *p) {
    return persist_profile_get32(p)|(uint64_t)persist_profile_get32(p+4u)<<32;
}
static void persist_profile_put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static void persist_profile_put64(uint8_t *p,uint64_t value) {
    persist_profile_put32(p,(uint32_t)value);persist_profile_put32(p+4u,(uint32_t)(value>>32));
}
static uint32_t persist_profile_wire_crc(const uint8_t *wire) {
    const uint8_t empty[4]={0};uint32_t crc=kui_retail_crc32(0,wire,16u);
    crc=kui_retail_crc32(crc,empty,4u);
    return kui_retail_crc32(crc,wire+20u,4096u-20u);
}
static int persist_profile_cluster(const struct kui_vmu_persist_geometry *g,uint32_t cluster) {
    return cluster>=2u && (uint64_t)cluster<(uint64_t)g->cluster_count+2u;
}
static int persist_profile_path(const char path[96]) {
    static const char persist_profile_prefix[]="/KUI/vmu-test/p";
    if(memcmp(path,persist_profile_prefix,15u)) return 0;
    unsigned number=0;
    for(unsigned i=15u;i<19u;i++) {
        if(path[i]<'0' || path[i]>'9') return 0;
        number=number*10u+(unsigned)(path[i]-'0');
    }
    return number && persist_profile_zero(path+19u,96u-19u);
}
static int persist_profile_geometry(const struct kui_vmu_persist_profile *p) {
    const struct kui_vmu_persist_geometry *g=&p->geometry;
    uint32_t spc=g->sectors_per_cluster;
    if((g->fs_type!=KUI_VMU_PERSIST_FAT32 && g->fs_type!=KUI_VMU_PERSIST_EXFAT) ||
       g->sector_bytes!=512u || !spc || (spc&(spc-1u)) || spc>65536u ||
       !g->cluster_count || g->cluster_count>UINT32_C(0xfffffff5) ||
       !persist_profile_cluster(g,g->root_cluster) || !g->fat_count || g->fat_count>2u ||
       g->active_fat>=g->fat_count || g->fat_mirrored>1u ||
       (g->fat_mirrored && g->active_fat) || !g->fat_sectors ||
       g->fat_first_lba<=p->partition_first || g->fat_first_lba>=p->partition_end ||
       g->fat_sectors>(p->partition_end-g->fat_first_lba)/g->fat_count ||
       g->data_first_lba<g->fat_first_lba+g->fat_sectors*g->fat_count ||
       g->data_first_lba>=p->partition_end ||
       (uint64_t)g->cluster_count*spc>p->partition_end-g->data_first_lba ||
       ((uint64_t)g->cluster_count+2u)*4u>g->fat_sectors*512u)
        return KUI_VMU_PERSIST_PROFILE_MAPPING;
    if(g->fs_type==KUI_VMU_PERSIST_FAT32) {
        if(spc>128u || g->cluster_count<65525u || g->cluster_count>UINT32_C(0x0ffffff5) ||
           g->bitmap_first_cluster || g->upcase_first_cluster || g->bitmap_bytes || g->upcase_bytes)
            return KUI_VMU_PERSIST_PROFILE_FORMAT;
    } else if(g->fat_mirrored || !persist_profile_cluster(g,g->bitmap_first_cluster) ||
              !persist_profile_cluster(g,g->upcase_first_cluster) ||
              g->bitmap_first_cluster==g->upcase_first_cluster ||
              g->bitmap_first_cluster==g->root_cluster || g->upcase_first_cluster==g->root_cluster ||
              g->bitmap_bytes<((uint64_t)g->cluster_count+7u)/8u ||
              g->bitmap_bytes>(uint64_t)g->cluster_count*spc*512u ||
              !g->upcase_bytes || g->upcase_bytes>131072u || (g->upcase_bytes&1u))
        return KUI_VMU_PERSIST_PROFILE_FORMAT;
    return 0;
}
static int persist_profile_map(const struct kui_vmu_persist_profile *p,
    const struct kui_vmu_persist_extent map[16],uint32_t count,uint32_t limit,
    uint32_t total,int allocated) {
    const struct kui_vmu_persist_geometry *g=&p->geometry;
    uint64_t heap_end=g->data_first_lba+(uint64_t)g->cluster_count*g->sectors_per_cluster;
    if(!count || count>limit) return KUI_VMU_PERSIST_PROFILE_MAPPING;
    uint32_t logical=0;
    for(unsigned i=0;i<count;i++) {
        const struct kui_vmu_persist_extent *e=&map[i];uint64_t end=(uint64_t)e->lba+e->blocks;
        if(e->logical_block!=logical || !e->blocks || e->blocks>total-logical ||
           e->lba<g->data_first_lba || end>heap_end ||
           (allocated && (((uint64_t)e->lba-g->data_first_lba)%g->sectors_per_cluster ||
                          e->blocks%g->sectors_per_cluster))) return KUI_VMU_PERSIST_PROFILE_MAPPING;
        for(unsigned j=0;j<i;j++)
            if((uint64_t)e->lba<(uint64_t)map[j].lba+map[j].blocks && map[j].lba<end)
                return KUI_VMU_PERSIST_PROFILE_MAPPING;
        if(i && (uint64_t)map[i-1u].lba+map[i-1u].blocks==e->lba)
            return KUI_VMU_PERSIST_PROFILE_MAPPING;
        logical+=e->blocks;
    }
    if(logical!=total || !persist_profile_zero(map+count,(16u-count)*sizeof(*map)))
        return KUI_VMU_PERSIST_PROFILE_MAPPING;
    return 0;
}
static int persist_profile_contains(const struct kui_vmu_persist_file *f,uint64_t lba) {
    for(unsigned i=0;i<f->allocated_count;i++)
        if(lba>=f->allocated[i].lba && lba<(uint64_t)f->allocated[i].lba+f->allocated[i].blocks)
            return 1;
    return 0;
}
int kui_vmu_persist_profile_validate_ownership(const struct kui_vmu_persist_profile *p) {
    if(!p) return KUI_VMU_PERSIST_PROFILE_ARGUMENT;
    if(p->transport!=1u || p->high_capacity>1u || p->flags!=KUI_VMU_PERSIST_PROFILE_PREP_ONLY ||
       !persist_profile_path(p->pair_path)) return KUI_VMU_PERSIST_PROFILE_FORMAT;
    if(!p->card_sectors || p->card_sectors>(UINT64_C(1)<<32) ||
       (!p->high_capacity && p->card_sectors>(UINT64_C(1)<<23)) ||
       p->partition_first>=p->partition_end || p->partition_end>p->card_sectors ||
       persist_profile_zero(p->cid,16u) || persist_profile_zero(p->image_id,16u))
        return KUI_VMU_PERSIST_PROFILE_IDENTITY;
    unsigned ones=0;for(unsigned i=0;i<16u;i++) ones+=p->cid[i]==255u;
    if(ones==16u) return KUI_VMU_PERSIST_PROFILE_IDENTITY;
    int status=persist_profile_geometry(p);if(status) return status;
    const struct kui_vmu_persist_geometry *g=&p->geometry;
    for(unsigned role=0;role<3u;role++) {
        const struct kui_vmu_persist_file *f=&p->files[role];
        uint32_t useful=persist_profile_file_sectors[role];
        uint32_t allocated=((useful+g->sectors_per_cluster-1u)/g->sectors_per_cluster)*g->sectors_per_cluster;
        if(f->file_bytes!=persist_profile_file_bytes[role] || f->valid_bytes!=f->file_bytes ||
           !persist_profile_cluster(g,f->first_cluster) || !persist_profile_cluster(g,f->directory_first_cluster) ||
           f->entry_lba<g->data_first_lba ||
           f->entry_lba>=g->data_first_lba+(uint64_t)g->cluster_count*g->sectors_per_cluster ||
           f->entry_offset>480u || (f->entry_offset&31u) || f->entry_kind!=g->fs_type ||
           !f->entry_count || f->entry_count>(g->fs_type==KUI_VMU_PERSIST_FAT32?21u:19u) ||
           (g->fs_type==KUI_VMU_PERSIST_EXFAT && f->entry_count<3u) ||
           (g->fs_type==KUI_VMU_PERSIST_FAT32?f->allocation_flags!=0u:
                (f->allocation_flags!=1u && f->allocation_flags!=3u)))
            return KUI_VMU_PERSIST_PROFILE_FORMAT;
        status=persist_profile_map(p,f->useful,f->useful_count,persist_profile_file_limits[role],useful,0);
        if(status) return status;
        status=persist_profile_map(p,f->allocated,f->allocated_count,persist_profile_file_limits[role],allocated,1);
        if(status) return status;
        if(f->allocated[0].lba!=g->data_first_lba+(uint64_t)(f->first_cluster-2u)*g->sectors_per_cluster)
            return KUI_VMU_PERSIST_PROFILE_MAPPING;
        unsigned count=0;uint32_t left=useful;
        for(unsigned i=0;i<f->allocated_count && left;i++) {
            const struct kui_vmu_persist_extent *a=&f->allocated[i],*u=&f->useful[count];
            uint32_t n=a->blocks<left?a->blocks:left;
            if(count>=f->useful_count || u->logical_block!=a->logical_block || u->lba!=a->lba || u->blocks!=n)
                return KUI_VMU_PERSIST_PROFILE_MAPPING;
            ++count;left-=n;
        }
        if(left || count!=f->useful_count) return KUI_VMU_PERSIST_PROFILE_MAPPING;
    }
    for(unsigned role=0;role<3u;role++) {
        const struct kui_vmu_persist_file *f=&p->files[role];
        uint32_t metadata[3]={g->root_cluster,g->bitmap_first_cluster,g->upcase_first_cluster};
        for(unsigned m=0;m<3u;m++) if(metadata[m] &&
            persist_profile_contains(f,g->data_first_lba+(uint64_t)(metadata[m]-2u)*g->sectors_per_cluster))
            return KUI_VMU_PERSIST_PROFILE_MAPPING;
        for(unsigned other=0;other<3u;other++) {
            const struct kui_vmu_persist_file *o=&p->files[other];
            if(persist_profile_contains(f,o->entry_lba) || persist_profile_contains(f,
                g->data_first_lba+(uint64_t)(o->directory_first_cluster-2u)*g->sectors_per_cluster))
                return KUI_VMU_PERSIST_PROFILE_MAPPING;
            if(other>=role) continue;
            if(f->entry_lba==o->entry_lba && f->entry_offset==o->entry_offset)
                return KUI_VMU_PERSIST_PROFILE_MAPPING;
            for(unsigned i=0;i<f->allocated_count;i++) for(unsigned j=0;j<o->allocated_count;j++)
                if((uint64_t)f->allocated[i].lba<(uint64_t)o->allocated[j].lba+o->allocated[j].blocks &&
                   (uint64_t)o->allocated[j].lba<(uint64_t)f->allocated[i].lba+f->allocated[i].blocks)
                    return KUI_VMU_PERSIST_PROFILE_MAPPING;
        }
    }
    return 0;
}
int kui_vmu_persist_profile_validate(const struct kui_vmu_persist_profile *p) {
    int status=kui_vmu_persist_profile_validate_ownership(p);if(status) return status;
    if(p->journal_sequence || persist_profile_zero(p->base_sha256,32u) ||
       persist_profile_zero(p->journal_sha256,32u) ||
       memcmp(p->marker_sha256,persist_profile_marker_sha,32u)) return KUI_VMU_PERSIST_PROFILE_FORMAT;
    struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);
    kui_sha256_update(&hash,p->image_id,16u);kui_sha256_digest(&hash,digest);
    return memcmp(digest,p->identity_sha256,32u)?KUI_VMU_PERSIST_PROFILE_IDENTITY:0;
}
int kui_vmu_persist_profile_encode(const struct kui_vmu_persist_profile *p,uint8_t wire[4096]) {
    if(!wire) return KUI_VMU_PERSIST_PROFILE_ARGUMENT;
    memset(wire,0,4096u);int status=kui_vmu_persist_profile_validate(p);if(status) return status;
    memcpy(wire,"KUIVMUP2",8u);persist_profile_put32(wire+8u,2u);persist_profile_put32(wire+12u,4096u);
    persist_profile_put32(wire+20u,p->transport);persist_profile_put32(wire+24u,p->high_capacity);
    persist_profile_put32(wire+28u,p->flags);persist_profile_put64(wire+32u,p->card_sectors);
    persist_profile_put64(wire+40u,p->partition_first);persist_profile_put64(wire+48u,p->partition_end);
    memcpy(wire+56u,p->cid,16u);memcpy(wire+72u,p->image_id,16u);
    memcpy(wire+88u,p->base_sha256,32u);memcpy(wire+120u,p->journal_sha256,32u);
    memcpy(wire+152u,p->identity_sha256,32u);persist_profile_put32(wire+184u,19u);
    memcpy(wire+192u,p->pair_path,96u);
    const struct kui_vmu_persist_geometry *g=&p->geometry;
    uint32_t words[6]={g->fs_type,g->sector_bytes,g->sectors_per_cluster,g->volume_serial,g->fat_count,g->active_fat};
    for(unsigned i=0;i<6u;i++) persist_profile_put32(wire+288u+4u*i,words[i]);
    persist_profile_put64(wire+312u,g->fat_first_lba);persist_profile_put64(wire+320u,g->fat_sectors);
    persist_profile_put64(wire+328u,g->data_first_lba);persist_profile_put32(wire+336u,g->cluster_count);
    persist_profile_put32(wire+340u,g->root_cluster);persist_profile_put32(wire+344u,g->fat_mirrored);
    persist_profile_put32(wire+352u,g->bitmap_first_cluster);persist_profile_put32(wire+356u,g->upcase_first_cluster);
    persist_profile_put64(wire+360u,g->bitmap_bytes);persist_profile_put64(wire+368u,g->upcase_bytes);
    memcpy(wire+384u,p->marker_sha256,32u);
    for(unsigned role=0;role<3u;role++) {
        const struct kui_vmu_persist_file *f=&p->files[role];uint8_t *w=wire+512u+448u*role;
        persist_profile_put64(w,f->file_bytes);persist_profile_put32(w+8u,f->first_cluster);
        persist_profile_put32(w+12u,f->allocation_flags);persist_profile_put32(w+16u,f->directory_first_cluster);
        persist_profile_put32(w+20u,f->entry_offset);persist_profile_put64(w+24u,f->entry_lba);
        persist_profile_put32(w+32u,f->entry_count);persist_profile_put32(w+36u,f->entry_kind);
        persist_profile_put32(w+40u,f->useful_count);persist_profile_put32(w+44u,f->allocated_count);
        persist_profile_put64(w+48u,f->valid_bytes);
        for(unsigned kind=0;kind<2u;kind++) for(unsigned i=0;i<16u;i++) {
            const struct kui_vmu_persist_extent *e=kind?&f->allocated[i]:&f->useful[i];
            uint8_t *out=w+64u+kind*192u+i*12u;
            persist_profile_put32(out,e->logical_block);persist_profile_put32(out+4u,e->lba);
            persist_profile_put32(out+8u,e->blocks);
        }
    }
    for(unsigned i=0;i<256u;i++) persist_profile_put32(wire+1856u+i*4u,p->block_crc[i]);
    persist_profile_put64(wire+2880u,p->journal_sequence);
    persist_profile_put32(wire+16u,persist_profile_wire_crc(wire));return 0;
}
int kui_vmu_persist_profile_decode(struct kui_vmu_persist_profile *p,const uint8_t *wire,size_t bytes) {
    if(!p) return KUI_VMU_PERSIST_PROFILE_ARGUMENT;
    memset(p,0,sizeof(*p));if(!wire || bytes!=4096u) return KUI_VMU_PERSIST_PROFILE_ARGUMENT;
    if(memcmp(wire,"KUIVMUP2",8u) || persist_profile_get32(wire+8u)!=2u ||
       persist_profile_get32(wire+12u)!=4096u) return KUI_VMU_PERSIST_PROFILE_WIRE;
    if(persist_profile_get32(wire+16u)!=persist_profile_wire_crc(wire)) return KUI_VMU_PERSIST_PROFILE_CRC;
    if(persist_profile_get32(wire+184u)!=19u || !persist_profile_zero(wire+188u,4u) ||
       !persist_profile_zero(wire+348u,4u) || !persist_profile_zero(wire+376u,8u) ||
       !persist_profile_zero(wire+416u,96u) || !persist_profile_zero(wire+2888u,1208u))
        return KUI_VMU_PERSIST_PROFILE_FORMAT;
    p->transport=persist_profile_get32(wire+20u);p->high_capacity=persist_profile_get32(wire+24u);
    p->flags=persist_profile_get32(wire+28u);p->card_sectors=persist_profile_get64(wire+32u);
    p->partition_first=persist_profile_get64(wire+40u);p->partition_end=persist_profile_get64(wire+48u);
    memcpy(p->cid,wire+56u,16u);memcpy(p->image_id,wire+72u,16u);memcpy(p->base_sha256,wire+88u,32u);
    memcpy(p->journal_sha256,wire+120u,32u);memcpy(p->identity_sha256,wire+152u,32u);
    memcpy(p->pair_path,wire+192u,96u);struct kui_vmu_persist_geometry *g=&p->geometry;
    g->fs_type=persist_profile_get32(wire+288u);g->sector_bytes=persist_profile_get32(wire+292u);
    g->sectors_per_cluster=persist_profile_get32(wire+296u);g->volume_serial=persist_profile_get32(wire+300u);
    g->fat_count=persist_profile_get32(wire+304u);g->active_fat=persist_profile_get32(wire+308u);
    g->fat_first_lba=persist_profile_get64(wire+312u);g->fat_sectors=persist_profile_get64(wire+320u);
    g->data_first_lba=persist_profile_get64(wire+328u);g->cluster_count=persist_profile_get32(wire+336u);
    g->root_cluster=persist_profile_get32(wire+340u);g->fat_mirrored=persist_profile_get32(wire+344u);
    g->bitmap_first_cluster=persist_profile_get32(wire+352u);g->upcase_first_cluster=persist_profile_get32(wire+356u);
    g->bitmap_bytes=persist_profile_get64(wire+360u);g->upcase_bytes=persist_profile_get64(wire+368u);
    memcpy(p->marker_sha256,wire+384u,32u);
    for(unsigned role=0;role<3u;role++) {
        struct kui_vmu_persist_file *f=&p->files[role];const uint8_t *w=wire+512u+448u*role;
        if(!persist_profile_zero(w+56u,8u)) {memset(p,0,sizeof(*p));return KUI_VMU_PERSIST_PROFILE_FORMAT;}
        f->file_bytes=persist_profile_get64(w);f->first_cluster=persist_profile_get32(w+8u);
        f->allocation_flags=persist_profile_get32(w+12u);f->directory_first_cluster=persist_profile_get32(w+16u);
        f->entry_offset=persist_profile_get32(w+20u);f->entry_lba=persist_profile_get64(w+24u);
        f->entry_count=persist_profile_get32(w+32u);f->entry_kind=persist_profile_get32(w+36u);
        f->useful_count=persist_profile_get32(w+40u);f->allocated_count=persist_profile_get32(w+44u);
        f->valid_bytes=persist_profile_get64(w+48u);
        for(unsigned kind=0;kind<2u;kind++) for(unsigned i=0;i<16u;i++) {
            struct kui_vmu_persist_extent *e=kind?&f->allocated[i]:&f->useful[i];
            const uint8_t *in=w+64u+kind*192u+i*12u;
            e->logical_block=persist_profile_get32(in);e->lba=persist_profile_get32(in+4u);
            e->blocks=persist_profile_get32(in+8u);
        }
    }
    for(unsigned i=0;i<256u;i++) p->block_crc[i]=persist_profile_get32(wire+1856u+i*4u);
    p->journal_sequence=persist_profile_get64(wire+2880u);
    int status=kui_vmu_persist_profile_validate(p);if(status) memset(p,0,sizeof(*p));return status;
}
int kui_vmu_persist_profile_same_ownership(const struct kui_vmu_persist_profile *a,
    const struct kui_vmu_persist_profile *b) {
    if(kui_vmu_persist_profile_validate_ownership(a) || kui_vmu_persist_profile_validate_ownership(b)) return 0;
    if(a->card_sectors!=b->card_sectors || a->partition_first!=b->partition_first ||
       a->partition_end!=b->partition_end || a->transport!=b->transport || a->high_capacity!=b->high_capacity ||
       a->flags!=b->flags || memcmp(a->cid,b->cid,16u) || memcmp(a->image_id,b->image_id,16u) ||
       memcmp(a->pair_path,b->pair_path,96u)) return 0;
    const struct kui_vmu_persist_geometry *x=&a->geometry,*y=&b->geometry;
    if(x->fs_type!=y->fs_type || x->sector_bytes!=y->sector_bytes || x->sectors_per_cluster!=y->sectors_per_cluster ||
       x->volume_serial!=y->volume_serial || x->fat_count!=y->fat_count || x->active_fat!=y->active_fat ||
       x->fat_first_lba!=y->fat_first_lba || x->fat_sectors!=y->fat_sectors || x->data_first_lba!=y->data_first_lba ||
       x->cluster_count!=y->cluster_count || x->root_cluster!=y->root_cluster || x->fat_mirrored!=y->fat_mirrored ||
       x->bitmap_first_cluster!=y->bitmap_first_cluster || x->upcase_first_cluster!=y->upcase_first_cluster ||
       x->bitmap_bytes!=y->bitmap_bytes || x->upcase_bytes!=y->upcase_bytes) return 0;
    for(unsigned role=0;role<3u;role++) {
        const struct kui_vmu_persist_file *f=&a->files[role],*g=&b->files[role];
        if(f->file_bytes!=g->file_bytes || f->valid_bytes!=g->valid_bytes || f->first_cluster!=g->first_cluster ||
           f->allocation_flags!=g->allocation_flags || f->directory_first_cluster!=g->directory_first_cluster ||
           f->entry_offset!=g->entry_offset || f->entry_lba!=g->entry_lba || f->entry_count!=g->entry_count ||
           f->entry_kind!=g->entry_kind || f->useful_count!=g->useful_count || f->allocated_count!=g->allocated_count ||
           memcmp(f->useful,g->useful,sizeof(f->useful)) || memcmp(f->allocated,g->allocated,sizeof(f->allocated))) return 0;
    }
    return 1;
}
int kui_vmu_persist_profile_map_lba(const struct kui_vmu_persist_profile *p,unsigned role,
    uint32_t logical,uint32_t *out) {
    if(!out) return KUI_VMU_PERSIST_PROFILE_ARGUMENT;
    *out=0u;int status=kui_vmu_persist_profile_validate_ownership(p);if(status) return status;
    if(role>=3u || logical>=persist_profile_file_sectors[role]) return KUI_VMU_PERSIST_PROFILE_MAPPING;
    const struct kui_vmu_persist_file *f=&p->files[role];
    for(unsigned i=0;i<f->useful_count;i++) if(logical>=f->useful[i].logical_block &&
        logical-f->useful[i].logical_block<f->useful[i].blocks) {
        *out=f->useful[i].lba+logical-f->useful[i].logical_block;return 0;
    }
    return KUI_VMU_PERSIST_PROFILE_MAPPING;
}
static void persist_profile_clear_snapshot(struct kui_vmu_persist_profile *p) {
    memset(p->base_sha256,0,32u);memset(p->journal_sha256,0,32u);
    memset(p->identity_sha256,0,32u);memset(p->marker_sha256,0,32u);
    memset(p->block_crc,0,sizeof(p->block_crc));p->journal_sequence=0;
}
static int persist_profile_snapshot(const struct kui_vmu_persist_profile *p,
    struct kui_vmu_persist_profile *output,kui_vmu_persist_profile_read read,void *context) {
    int status=output?kui_vmu_persist_profile_validate_ownership(p):kui_vmu_persist_profile_validate(p);
    if(status) return status;
    if(!read) return KUI_VMU_PERSIST_PROFILE_ARGUMENT;
    struct kui_sha256 hash;uint8_t block[512],digest[32];
    for(unsigned role=0;role<3u;role++) {
        kui_sha256_init(&hash);
        for(uint32_t logical=0;logical<persist_profile_file_sectors[role];logical++) {
            if(read(context,role,logical,block)) return KUI_VMU_PERSIST_PROFILE_IO;
            if(role==KUI_VMU_PERSIST_JOURNAL && logical==1u && !persist_profile_zero(block,512u))
                return KUI_VMU_PERSIST_PROFILE_MARKER;
            if(role==KUI_VMU_PERSIST_IDENTITY && memcmp(block,p->image_id,16u))
                return KUI_VMU_PERSIST_PROFILE_IDENTITY;
            if(role==KUI_VMU_PERSIST_BASE) {
                uint32_t crc=kui_retail_crc32(0,block,512u);
                if(output) output->block_crc[logical]=crc;
                else if(p->block_crc[logical]!=crc) return KUI_VMU_PERSIST_PROFILE_VERIFY;
            }
            kui_sha256_update(&hash,block,role==KUI_VMU_PERSIST_IDENTITY?16u:512u);
        }
        kui_sha256_digest(&hash,digest);
        const uint8_t *expected=role==0u?p->base_sha256:role==1u?p->journal_sha256:p->identity_sha256;
        uint8_t *target=output?(role==0u?output->base_sha256:role==1u?output->journal_sha256:output->identity_sha256):NULL;
        if(target) memcpy(target,digest,32u);
        else if(memcmp(digest,expected,32u)) return KUI_VMU_PERSIST_PROFILE_VERIFY;
    }
    if(output) memcpy(output->marker_sha256,persist_profile_marker_sha,32u);
    return 0;
}
int kui_vmu_persist_profile_collect_snapshot(struct kui_vmu_persist_profile *p,
    kui_vmu_persist_profile_read read,void *context) {
    if(!p) return KUI_VMU_PERSIST_PROFILE_ARGUMENT;
    persist_profile_clear_snapshot(p);int status=persist_profile_snapshot(p,p,read,context);
    if(status) persist_profile_clear_snapshot(p);
    return status;
}
int kui_vmu_persist_profile_verify_snapshot(const struct kui_vmu_persist_profile *p,
    kui_vmu_persist_profile_read read,void *context) {
    return persist_profile_snapshot(p,NULL,read,context);
}
