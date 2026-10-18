/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently authored, bounded read-only FAT32/exFAT ownership admission.
 * File contents never authorize physical ownership. No filesystem writer,
 * allocation, mount, kernel callback or card initialization is present. */
#include "kui/vmu_fs_admission.h"
#include <string.h>

#define VMU_FS_UNKNOWN UINT32_MAX
#define VMU_FS_TARGETS 259u
#define VMU_FS_UPCASE_CLUSTERS 256u
#define VMU_FS_EXFAT_SET 19u

struct vmu_fs_dir {
    uint64_t bytes;
    uint32_t first,parent,current,cluster_index,sector,slot;
    uint32_t contiguous,ended,entered,path_level;
};
struct vmu_fs_object {
    uint64_t bytes,valid,lba;
    uint32_t first,flags,attributes,offset,count,kind,checksum;
    uint32_t ascii;
    char name[256];
};
struct vmu_fs_target {uint32_t cluster,previous,role;};
/* GCC's may_alias makes the documented aligned opaque workspace an explicit
 * aliasing boundary. There is no allocation or large automatic work state. */
struct __attribute__((__may_alias__)) vmu_fs_state {
    const struct kui_vmu_fs_input *input;
    struct kui_vmu_fs_stats *stats;
    struct kui_vmu_persist_profile profile;
    uint32_t visited[KUI_VMU_FS_MAX_DIRECTORY_CLUSTERS],visited_count;
    struct vmu_fs_target targets[VMU_FS_TARGETS];
    uint32_t target_count,upcase[VMU_FS_UPCASE_CLUSTERS],upcase_count;
    uint32_t bitmap_clusters,bitmap_checksum,upcase_checksum;
    struct vmu_fs_dir dirs[KUI_VMU_FS_MAX_DEPTH];
    struct vmu_fs_object object;
    uint16_t name[260];
    uint8_t sector[512],fat[512],mirror[512],bitmap[512];
    uint8_t entries[VMU_FS_EXFAT_SET][32];
    uint64_t dir_cache,bitmap_cache;
    uint32_t fat_cache;
};
_Static_assert(sizeof(struct vmu_fs_state)<=KUI_VMU_FS_WORK_BYTES,"FS admission workspace bound");
static int vmu_fs_target_index(const struct vmu_fs_state *,uint32_t);
static int vmu_fs_system_cluster(const struct vmu_fs_state *,uint32_t);
static int vmu_fs_bitmap_bit(struct vmu_fs_state *,uint32_t);

static uint16_t vmu_fs_u16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t vmu_fs_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t vmu_fs_u64(const uint8_t *p) {return vmu_fs_u32(p)|(uint64_t)vmu_fs_u32(p+4u)<<32;}
static int vmu_fs_zero(const void *bytes,size_t n) {
    const uint8_t *p=bytes;for(size_t i=0;i<n;i++) if(p[i]) return 0;return 1;
}
static int vmu_fs_fail(struct vmu_fs_state *s,int result,uint32_t detail) {
    if(!s->stats->result) {s->stats->result=result;s->stats->detail=detail;}
    return s->stats->result;
}
static int vmu_fs_read(struct vmu_fs_state *s,uint64_t lba,uint8_t out[512]) {
    s->stats->last_lba=(uint32_t)lba;
    if(lba>=s->input->card_sectors || lba>UINT32_MAX)
        return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,1u);
    if(s->stats->reads>=KUI_VMU_FS_MAX_READS)
        return vmu_fs_fail(s,KUI_VMU_FS_LIMIT,1u);
    ++s->stats->reads;
    if(s->input->read_sector(s->input->context,(uint32_t)lba,out))
        return vmu_fs_fail(s,KUI_VMU_FS_IO,1u);
    return 0;
}
static int vmu_fs_cluster(const struct vmu_fs_state *s,uint32_t c) {
    return c>=2u && c-2u<s->profile.geometry.cluster_count;
}
static uint64_t vmu_fs_lba(const struct vmu_fs_state *s,uint32_t c) {
    return s->profile.geometry.data_first_lba+
        (uint64_t)(c-2u)*s->profile.geometry.sectors_per_cluster;
}
static uint64_t vmu_fs_cluster_bytes(const struct vmu_fs_state *s) {
    return (uint64_t)s->profile.geometry.sectors_per_cluster*512u;
}
static uint64_t vmu_fs_clusters(const struct vmu_fs_state *s,uint64_t bytes) {
    uint64_t n=vmu_fs_cluster_bytes(s);return bytes/n+!!(bytes%n);
}
static int vmu_fs_fat_sector(struct vmu_fs_state *s,uint32_t index) {
    const struct kui_vmu_persist_geometry *g=&s->profile.geometry;
    if((uint64_t)index>=g->fat_sectors) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,2u);
    if(s->fat_cache==index) return 0;
    if(vmu_fs_read(s,g->fat_first_lba+index,s->fat)) return s->stats->result;
    if(g->fat_count==2u) {
        if(vmu_fs_read(s,g->fat_first_lba+g->fat_sectors+index,s->mirror)) return s->stats->result;
        if(memcmp(s->fat,s->mirror,512u)) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,3u);
    }
    s->fat_cache=index;return 0;
}
static int vmu_fs_fat_value(struct vmu_fs_state *s,uint32_t c,uint32_t *value) {
    if(c>=s->profile.geometry.cluster_count+2u) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,4u);
    if(vmu_fs_fat_sector(s,c/128u)) return s->stats->result;
    *value=vmu_fs_u32(s->fat+(c%128u)*4u);
    if(s->profile.geometry.fs_type==KUI_VMU_PERSIST_FAT32) *value&=UINT32_C(0x0fffffff);
    return 0;
}
static int vmu_fs_next_cluster(struct vmu_fs_state *s,uint32_t c,uint32_t *next) {
    uint32_t v;if(!vmu_fs_cluster(s,c) || vmu_fs_fat_value(s,c,&v))
        return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,5u);
    uint32_t end=s->profile.geometry.fs_type==KUI_VMU_PERSIST_FAT32?
        UINT32_C(0x0ffffff8):UINT32_C(0xfffffff8);
    if(v>=end) {*next=0;return 0;}
    if(!vmu_fs_cluster(s,v)) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,6u);
    *next=v;return 0;
}
static int vmu_fs_volume(struct vmu_fs_state *s) {
    const struct kui_vmu_fs_input *in=s->input;
    struct kui_vmu_persist_geometry *g=&s->profile.geometry;
    uint8_t *b=s->sector;
    if(vmu_fs_read(s,0u,b)) return s->stats->result;
    if(b[510]!=0x55u || b[511]!=0xaau) return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,10u);
    if(in->partition_first) {
        unsigned found=0u;
        for(unsigned i=0;i<4u;i++) {
            const uint8_t *p=b+446u+i*16u;
            if(!p[4]) {
                if(vmu_fs_u32(p+8u) || vmu_fs_u32(p+12u)) return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,11u);
                continue;
            }
            if(++found!=1u || (p[0] && p[0]!=0x80u) ||
               (p[4]!=0x0bu && p[4]!=0x0cu && p[4]!=7u) ||
               vmu_fs_u32(p+8u)!=in->partition_first ||
               vmu_fs_u32(p+12u)!=in->partition_end-in->partition_first)
                return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,12u);
        }
        if(found!=1u || vmu_fs_read(s,in->partition_first,b)) return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,13u);
    }
    if(b[510]!=0x55u || b[511]!=0xaau) return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,14u);
    g->sector_bytes=512u;
    if(!memcmp(b+3u,"EXFAT   ",8u)) {
        struct kui_vmu_fs_boot_diagnostics *boot=&s->stats->boot;
        boot->valid=1u;boot->partition_offset=vmu_fs_u64(b+64u);boot->volume_sectors=vmu_fs_u64(b+72u);
        boot->revision=vmu_fs_u16(b+104u);boot->flags=vmu_fs_u16(b+106u);
        boot->sector_shift=b[108];boot->cluster_shift=b[109];boot->fat_count=b[110];
        for(unsigned i=11u;i<64u;i++) boot->zero_or|=b[i];
        if(boot->zero_or) boot->reject_mask|=KUI_VMU_FS_BOOT_ZERO;
        if(boot->revision!=0x100u) boot->reject_mask|=KUI_VMU_FS_BOOT_REVISION;
        if(boot->sector_shift!=9u) boot->reject_mask|=KUI_VMU_FS_BOOT_SECTOR_SHIFT;
        if(boot->cluster_shift>16u) boot->reject_mask|=KUI_VMU_FS_BOOT_CLUSTER_SHIFT;
        if(boot->fat_count!=1u) boot->reject_mask|=KUI_VMU_FS_BOOT_FAT_COUNT;
        if(boot->flags&~UINT32_C(0x0008)) boot->reject_mask|=KUI_VMU_FS_BOOT_FLAGS;
        /* exFAT specification 3.1.4: zero means ignore PartitionOffset.
         * MBR selection remains independent and every map uses that start. */
        if(boot->partition_offset && boot->partition_offset!=in->partition_first)
            boot->reject_mask|=KUI_VMU_FS_BOOT_PARTITION_OFFSET;
        if(boot->volume_sectors!=in->partition_end-in->partition_first)
            boot->reject_mask|=KUI_VMU_FS_BOOT_VOLUME_LENGTH;
        if(boot->reject_mask)
            return vmu_fs_fail(s,KUI_VMU_FS_UNSUPPORTED,15u);
        g->fs_type=KUI_VMU_PERSIST_EXFAT;g->sectors_per_cluster=UINT32_C(1)<<b[109];
        g->fat_count=1u;g->fat_sectors=vmu_fs_u32(b+84u);
        g->fat_first_lba=in->partition_first+vmu_fs_u32(b+80u);
        g->data_first_lba=in->partition_first+vmu_fs_u32(b+88u);
        g->cluster_count=vmu_fs_u32(b+92u);g->root_cluster=vmu_fs_u32(b+96u);
        g->volume_serial=vmu_fs_u32(b+100u);
        if(vmu_fs_u32(b+80u)<24u || g->cluster_count<256u)
            return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,16u);
        /* Both 12-sector boot regions must independently checksum and agree
         * on stable geometry; checksum excludes flags/percent-used only. */
        uint8_t geometry[120];memcpy(geometry,b,sizeof(geometry));
        for(unsigned copy=0;copy<2u;copy++) {
            uint32_t sum=0;
            for(unsigned sector=0;sector<11u;sector++) {
                if(vmu_fs_read(s,in->partition_first+copy*12u+sector,b)) return s->stats->result;
                /* Backup VolumeFlags are explicitly stale (spec 3.1.13).
                 * The current main flags were admitted above; stable fields
                 * and both complete boot-region checksums remain mandatory. */
                if(!sector && (memcmp(b,geometry,106u) || memcmp(b+108u,geometry+108u,4u) ||
                   b[510]!=0x55u || b[511]!=0xaau ||
                   (!copy && (vmu_fs_u16(b+106u)&~UINT16_C(0x0008)))))
                    return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,17u);
                for(unsigned i=0;i<512u;i++) if(sector || (i!=106u && i!=107u && i!=112u))
                    sum=((sum&1u)?UINT32_C(0x80000000):0u)+(sum>>1)+b[i];
            }
            if(vmu_fs_read(s,in->partition_first+copy*12u+11u,b)) return s->stats->result;
            for(unsigned i=0;i<512u;i+=4u) if(vmu_fs_u32(b+i)!=sum)
                return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,18u);
        }
    } else {
        uint32_t reserved=vmu_fs_u16(b+14u),fat_sectors=vmu_fs_u32(b+36u),total=vmu_fs_u32(b+32u);
        if(vmu_fs_u16(b+11u)!=512u || !b[13] || b[13]>128u || (b[13]&(b[13]-1u)) ||
           !reserved || (b[16]!=1u && b[16]!=2u) || vmu_fs_u16(b+17u) ||
           vmu_fs_u16(b+19u) || vmu_fs_u16(b+22u) || !fat_sectors ||
           vmu_fs_u16(b+40u) || vmu_fs_u16(b+42u) ||
           vmu_fs_u32(b+28u)!=in->partition_first || total!=in->partition_end-in->partition_first)
            return vmu_fs_fail(s,KUI_VMU_FS_UNSUPPORTED,20u);
        uint64_t data=(uint64_t)reserved+(uint64_t)b[16]*fat_sectors;
        if(data>=total) return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,21u);
        g->fs_type=KUI_VMU_PERSIST_FAT32;g->sectors_per_cluster=b[13];
        g->fat_count=b[16];g->fat_mirrored=1u;g->fat_sectors=fat_sectors;
        g->fat_first_lba=in->partition_first+reserved;g->data_first_lba=in->partition_first+data;
        g->cluster_count=(uint32_t)((total-data)/b[13]);g->root_cluster=vmu_fs_u32(b+44u);
        g->volume_serial=vmu_fs_u32(b+67u);
        if(g->cluster_count<65525u) return vmu_fs_fail(s,KUI_VMU_FS_UNSUPPORTED,22u);
        uint32_t backup=vmu_fs_u16(b+50u);uint8_t geometry[79];memcpy(geometry,b+11u,sizeof(geometry));
        if(backup && backup!=0xffffu) {
            if(backup>=reserved || vmu_fs_read(s,in->partition_first+backup,b)) return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,23u);
            if(b[510]!=0x55u || b[511]!=0xaau || memcmp(geometry,b+11u,sizeof(geometry)))
                return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,24u);
        }
    }
    if(g->cluster_count>KUI_VMU_FS_MAX_FAT_ENTRIES-2u || !g->fat_sectors ||
       g->fat_sectors>KUI_VMU_FS_MAX_FAT_ENTRIES/128u)
        return vmu_fs_fail(s,KUI_VMU_FS_LIMIT,25u);
    if(g->fat_first_lba<in->partition_first || g->fat_first_lba>=g->data_first_lba ||
       g->fat_sectors*g->fat_count>g->data_first_lba-g->fat_first_lba ||
       (uint64_t)(g->cluster_count+2u)*4u>g->fat_sectors*512u ||
       g->data_first_lba>=in->partition_end ||
       (uint64_t)g->cluster_count*g->sectors_per_cluster>in->partition_end-g->data_first_lba ||
       !vmu_fs_cluster(s,g->root_cluster)) return vmu_fs_fail(s,KUI_VMU_FS_VOLUME,26u);
    uint32_t reserved1;
    if(vmu_fs_fat_value(s,1u,&reserved1)) return s->stats->result;
    if(g->fs_type==KUI_VMU_PERSIST_FAT32 &&
       (reserved1&UINT32_C(0x0c000000))!=UINT32_C(0x0c000000))
        return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,27u);
    return 0;
}

static int vmu_fs_visit_cluster(struct vmu_fs_state *s,uint32_t c) {
    s->stats->last_cluster=c;
    if(!vmu_fs_cluster(s,c)) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,30u);
    if(vmu_fs_target_index(s,c)>=0 || vmu_fs_system_cluster(s,c))
        return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,28u);
    if(s->profile.geometry.bitmap_first_cluster && vmu_fs_bitmap_bit(s,c)) return s->stats->result;
    if(s->visited_count>=KUI_VMU_FS_MAX_DIRECTORY_CLUSTERS ||
       s->stats->directory_clusters>=KUI_VMU_FS_MAX_DIRECTORY_CLUSTERS)
        return vmu_fs_fail(s,KUI_VMU_FS_LIMIT,31u);
    for(unsigned i=0;i<s->visited_count;i++) if(s->visited[i]==c)
        return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,32u);
    s->visited[s->visited_count++]=c;++s->stats->directory_clusters;return 0;
}
static int vmu_fs_open_dir(struct vmu_fs_state *s,struct vmu_fs_dir *d,
    uint32_t first,uint32_t parent,uint64_t bytes,uint32_t contiguous) {
    if(!vmu_fs_cluster(s,first) || (bytes &&
       (bytes%vmu_fs_cluster_bytes(s) || vmu_fs_clusters(s,bytes)>s->profile.geometry.cluster_count)) ||
       (contiguous && (!bytes || vmu_fs_clusters(s,bytes)>s->profile.geometry.cluster_count-(first-2u))))
        return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,33u);
    memset(d,0,sizeof(*d));d->first=d->current=first;d->parent=parent;
    d->bytes=bytes;d->contiguous=contiguous;return 0;
}
static int vmu_fs_dir_advance(struct vmu_fs_state *s,struct vmu_fs_dir *d) {
    uint32_t next;
    if(d->contiguous) next=(uint64_t)d->cluster_index+1u<vmu_fs_clusters(s,d->bytes)?d->current+1u:0u;
    else if(vmu_fs_next_cluster(s,d->current,&next)) return s->stats->result;
    if(d->bytes && (!!next != ((uint64_t)d->cluster_index+1u<vmu_fs_clusters(s,d->bytes))))
        return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,34u);
    if(!next) {d->ended=1u;return 0;}
    d->current=next;++d->cluster_index;d->entered=0u;d->sector=d->slot=0u;return 0;
}
/* A zero directory terminator ends live entries, not allocated metadata.
 * Finish through EOC so hidden tail clusters remain protected and cycles fail. */
static int vmu_fs_finish_dir(struct vmu_fs_state *s,struct vmu_fs_dir *d) {
    while(!d->ended) {
        if(!d->entered) {
            if(vmu_fs_visit_cluster(s,d->current)) return s->stats->result;
            d->entered=1u;
        }
        if(vmu_fs_dir_advance(s,d)) return s->stats->result;
    }
    return 0;
}
/* 1 slot,0 EOC,negative refusal. The slot is copied because subsequent FAT/
 * directory reads can replace any cache. Every step and actual read is bounded. */
static int vmu_fs_slot(struct vmu_fs_state *s,struct vmu_fs_dir *d,uint8_t out[32],
    uint64_t *lba,uint32_t *offset) {
    if(d->ended) return 0;
    if(d->slot==16u) {d->slot=0u;++d->sector;}
    if(d->sector==s->profile.geometry.sectors_per_cluster) {
        if(vmu_fs_dir_advance(s,d)) return s->stats->result;
        if(d->ended) return 0;
    }
    if(!d->entered) {
        if(vmu_fs_visit_cluster(s,d->current)) return s->stats->result;
        d->entered=1u;
    }
    uint64_t sector=vmu_fs_lba(s,d->current)+d->sector;
    if(sector!=s->dir_cache) {
        if(s->stats->directory_sectors>=KUI_VMU_FS_MAX_DIRECTORY_SECTORS)
            return vmu_fs_fail(s,KUI_VMU_FS_LIMIT,35u);
        ++s->stats->directory_sectors;
        if(vmu_fs_read(s,sector,s->sector)) return s->stats->result;
        s->dir_cache=sector;
    }
    if(s->stats->entries>=KUI_VMU_FS_MAX_ENTRIES) return vmu_fs_fail(s,KUI_VMU_FS_LIMIT,36u);
    ++s->stats->entries;*lba=sector;*offset=d->slot*32u;
    memcpy(out,s->sector+d->slot++*32u,32u);return 1;
}
static unsigned vmu_fs_upper(unsigned c) {return c>='a' && c<='z'?c-32u:c;}
static int vmu_fs_name_equal(const char *a,const char *b) {
    for(unsigned i=0;i<256u;i++) {
        if(vmu_fs_upper((unsigned char)a[i])!=vmu_fs_upper((unsigned char)b[i])) return 0;
        if(!a[i]) return 1;
    }
    return 0;
}
static uint8_t vmu_fs_sfn_checksum(const uint8_t *p) {
    uint8_t sum=0;for(unsigned i=0;i<11u;i++) sum=(uint8_t)(((sum&1u)?0x80u:0u)+(sum>>1)+p[i]);return sum;
}
static int vmu_fs_bad_sfn(unsigned c) {
    static const char vmu_fs_forbidden[]="\"*+,./:;<=>?[\\]|";
    for(unsigned i=0;vmu_fs_forbidden[i];i++) if(c==(unsigned char)vmu_fs_forbidden[i]) return 1;
    return 0;
}
static int vmu_fs_utf16_name(struct vmu_fs_state *s,unsigned count) {
    struct vmu_fs_object *o=&s->object;unsigned length=0u,terminated=0u;
    o->ascii=1u;
    for(unsigned i=0;i<count;i++) {
        unsigned c=s->name[i];
        if(!c) {terminated=1u;continue;}
        if(c==0xffffu) {if(!terminated) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,37u);continue;}
        if(terminated || length>=255u) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,38u);
        if(c<32u || c>=127u) o->ascii=0u;
        o->name[length++]=(char)(c<127u?c:'?');
    }
    if(!length) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,39u);
    o->name[length]=0;return 0;
}
static int vmu_fs_fat_object(struct vmu_fs_state *s,struct vmu_fs_dir *d) {
    static const uint8_t vmu_fs_lfn_offsets[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    struct vmu_fs_object *o=&s->object;unsigned sequence=0u,total=0u,checksum=0u;
    for(;;) {
        uint8_t *p=s->entries[0];uint64_t lba;uint32_t offset;
        int result=vmu_fs_slot(s,d,p,&lba,&offset);if(result<=0) {
            if(sequence || total) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,40u);
            return result;
        }
        if(!p[0]) {
            if(sequence || total) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,41u);
            return vmu_fs_finish_dir(s,d);
        }
        if(p[0]==0xe5u) {sequence=total=0u;continue;}
        if(p[11]==15u) {
            unsigned ordinal=p[0]&31u;
            if(p[0]&0x40u) {
                if(sequence || total || !ordinal || ordinal>20u || (p[0]&0xa0u))
                    return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,42u);
                total=sequence=ordinal;checksum=p[13];
                for(unsigned i=0;i<260u;i++) s->name[i]=0xffffu;
            }
            if(!sequence || ordinal!=sequence || (p[0]&0xa0u) || p[12] ||
               vmu_fs_u16(p+26u) || p[13]!=checksum)
                return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,43u);
            for(unsigned i=0;i<13u;i++) s->name[(ordinal-1u)*13u+i]=vmu_fs_u16(p+vmu_fs_lfn_offsets[i]);
            --sequence;continue;
        }
        if((p[11]&0xc0u) || sequence || (total && checksum!=vmu_fs_sfn_checksum(p)))
            return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,44u);
        if(p[11]&8u) {
            if(total || (p[11]&16u)) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,45u);
            continue;
        }
        /* SFN fields have trailing padding only. Treating an embedded gap
         * as a terminator would turn a different raw name into our target. */
        unsigned dot=p[0]=='.' && (p[1]==' ' || p[1]=='.');
        if(dot) for(unsigned i=p[1]=='.'?2u:1u;i<11u;i++) if(p[i]!=' ') dot=0u;
        for(unsigned field=0;field<2u;field++) {
            unsigned begin=field?8u:0u,end=field?11u:8u,padding=0u;
            for(unsigned i=begin;i<end;i++) {
                unsigned c=p[i];
                if(c==' ') {padding=1u;continue;}
                if(padding || (c<32u && !(i==0u && c==5u)) || c==127u ||
                   (!dot && vmu_fs_bad_sfn(c)))
                    return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,66u);
            }
        }
        if(p[12]&~0x18u) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,67u);
        memset(o,0,sizeof(*o));o->kind=KUI_VMU_PERSIST_FAT32;o->attributes=p[11];
        o->first=((uint32_t)vmu_fs_u16(p+20u)<<16)|vmu_fs_u16(p+26u);
        o->bytes=o->valid=vmu_fs_u32(p+28u);o->lba=lba;o->offset=offset;o->count=total+1u;
        if(total) {if(vmu_fs_utf16_name(s,total*13u)) return s->stats->result;}
        else {
            unsigned n=0u;o->ascii=1u;
            for(unsigned i=0;i<8u && p[i]!=' ';i++) {
                if(p[i]<32u || p[i]>=127u) o->ascii=0u;
                o->name[n++]=(char)p[i];
            }
            if(p[8]!=' ') {
                o->name[n++]='.';
                for(unsigned i=8u;i<11u && p[i]!=' ';i++) {
                    if(p[i]<32u || p[i]>=127u) o->ascii=0u;
                    o->name[n++]=(char)p[i];
                }
            }
            o->name[n]=0;
        }
        if(!strcmp(o->name,".") || !strcmp(o->name,"..")) {
            uint32_t wanted=o->name[1]?d->parent:d->first;
            if(!(o->attributes&16u) || o->bytes || (o->first!=wanted &&
               !(o->name[1] && !o->first && wanted==s->profile.geometry.root_cluster)))
                return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,46u);
            total=0u;continue;
        }
        if((o->bytes || (o->attributes&16u)) && !vmu_fs_cluster(s,o->first))
            return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,47u);
        if((o->attributes&16u) && o->bytes) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,48u);
        if(o->first && !vmu_fs_cluster(s,o->first)) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,49u);
        return 1;
    }
}
static int vmu_fs_exfat_object(struct vmu_fs_state *s,struct vmu_fs_dir *d) {
    struct vmu_fs_object *o=&s->object;
    for(;;) {
        uint64_t lba;uint32_t offset;uint8_t *p=s->entries[0];
        int result=vmu_fs_slot(s,d,p,&lba,&offset);if(result<=0) return result;
        if(!p[0]) return vmu_fs_finish_dir(s,d);
        if(!(p[0]&0x80u)) continue;
        memset(o,0,sizeof(*o));o->kind=p[0];o->lba=lba;o->offset=offset;o->count=1u;
        if(p[0]==0x81u || p[0]==0x82u) {
            if(d->first!=s->profile.geometry.root_cluster || p[1] ||
               !vmu_fs_zero(p+8u,12u)) return vmu_fs_fail(s,KUI_VMU_FS_UNSUPPORTED,50u);
            o->first=vmu_fs_u32(p+20u);o->bytes=o->valid=vmu_fs_u64(p+24u);
            o->checksum=vmu_fs_u32(p+4u);
            if(!vmu_fs_cluster(s,o->first) || !o->bytes) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,51u);
            return 1;
        }
        if(p[0]==0x83u) {if(d->first!=s->profile.geometry.root_cluster || p[1]>11u)
                return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,52u);
            continue;
        }
        if(p[0]!=0x85u) return vmu_fs_fail(s,KUI_VMU_FS_UNSUPPORTED,53u);
        unsigned secondary=p[1];uint16_t checksum=vmu_fs_u16(p+2u);
        if(secondary<2u || secondary>=VMU_FS_EXFAT_SET || !vmu_fs_zero(p+6u,2u) ||
           (vmu_fs_u16(p+4u)&~UINT16_C(0x0037))) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,54u);
        o->attributes=vmu_fs_u16(p+4u);o->count=secondary+1u;
        uint16_t actual=0;
        for(unsigned i=0;i<=secondary;i++) {
            if(i) {
                result=vmu_fs_slot(s,d,s->entries[i],&lba,&offset);
                if(result!=1) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,55u);
            }
            for(unsigned j=0;j<32u;j++) if(i || (j!=2u && j!=3u))
                actual=(uint16_t)(((actual&1u)?0x8000u:0u)+(actual>>1)+s->entries[i][j]);
        }
        if(actual!=checksum) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,56u);
        const uint8_t *stream=s->entries[1];unsigned name_length=stream[3];
        if(stream[0]!=0xc0u || (stream[1]!=1u && stream[1]!=3u) || !name_length ||
           secondary!=1u+(name_length+14u)/15u || stream[2] ||
           !vmu_fs_zero(stream+6u,2u) || !vmu_fs_zero(stream+16u,4u))
            return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,57u);
        o->flags=stream[1];o->first=vmu_fs_u32(stream+20u);
        o->bytes=vmu_fs_u64(stream+24u);o->valid=vmu_fs_u64(stream+8u);
        if(o->valid>o->bytes || (o->bytes && !vmu_fs_cluster(s,o->first)) ||
           (!o->bytes && o->first) || (o->attributes&16u && (!o->bytes || o->valid!=o->bytes)))
            return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,58u);
        if(vmu_fs_clusters(s,o->bytes)>s->profile.geometry.cluster_count ||
           ((o->flags&2u) && vmu_fs_clusters(s,o->bytes)>s->profile.geometry.cluster_count-(o->first-2u)))
            return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,59u);
        o->ascii=1u;uint16_t name_hash=0;
        for(unsigned i=0;i<name_length;i++) {
            const uint8_t *entry=s->entries[2u+i/15u];
            if(entry[0]!=0xc1u || entry[1]) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,60u);
            unsigned c=vmu_fs_u16(entry+2u+(i%15u)*2u);
            if(!c || c==0xffffu) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,61u);
            if(c<32u || c>=127u) o->ascii=0u;
            o->name[i]=(char)(c<127u?c:'?');unsigned upper=vmu_fs_upper(c);
            name_hash=(uint16_t)(((name_hash&1u)?0x8000u:0u)+(name_hash>>1)+(upper&255u));
            name_hash=(uint16_t)(((name_hash&1u)?0x8000u:0u)+(name_hash>>1)+(upper>>8));
        }
        o->name[name_length]=0;
        if(o->ascii && name_hash!=vmu_fs_u16(stream+4u)) return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,62u);
        for(unsigned i=name_length;i<(secondary-1u)*15u;i++)
            if(vmu_fs_u16(s->entries[2u+i/15u]+2u+(i%15u)*2u))
                return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,63u);
        o->kind=KUI_VMU_PERSIST_EXFAT;return 1;
    }
}
static int vmu_fs_object_next(struct vmu_fs_state *s,struct vmu_fs_dir *d) {
    return s->profile.geometry.fs_type==KUI_VMU_PERSIST_FAT32?
        vmu_fs_fat_object(s,d):vmu_fs_exfat_object(s,d);
}

static int vmu_fs_bitmap_bit(struct vmu_fs_state *s,uint32_t cluster) {
    if(s->profile.geometry.fs_type!=KUI_VMU_PERSIST_EXFAT) return 0;
    uint32_t byte=(cluster-2u)/8u;
    uint64_t lba=vmu_fs_lba(s,s->profile.geometry.bitmap_first_cluster)+byte/512u;
    if(s->bitmap_cache!=lba) {
        if(vmu_fs_read(s,lba,s->bitmap)) return s->stats->result;
        s->bitmap_cache=lba;
    }
    if(!(s->bitmap[byte%512u]&(1u<<((cluster-2u)%8u))))
        return vmu_fs_fail(s,KUI_VMU_FS_ALLOCATION,70u);
    return 0;
}
static int vmu_fs_system_cluster(const struct vmu_fs_state *s,uint32_t c) {
    const struct kui_vmu_persist_geometry *g=&s->profile.geometry;
    if(g->fs_type!=KUI_VMU_PERSIST_EXFAT) return 0;
    if(c>=g->bitmap_first_cluster && c-g->bitmap_first_cluster<s->bitmap_clusters) return 1;
    for(unsigned i=0;i<s->upcase_count;i++) if(s->upcase[i]==c) return 1;
    return 0;
}
static int vmu_fs_system(struct vmu_fs_state *s) {
    struct kui_vmu_persist_geometry *g=&s->profile.geometry;
    if(g->fs_type!=KUI_VMU_PERSIST_EXFAT) return 0;
    struct vmu_fs_dir *d=&s->dirs[0];
    if(vmu_fs_open_dir(s,d,g->root_cluster,g->root_cluster,0,0)) return s->stats->result;
    s->visited_count=0u;unsigned bitmap_count=0u,upcase_count=0u;int result;
    while((result=vmu_fs_object_next(s,d))==1) {
        const struct vmu_fs_object *o=&s->object;
        if(o->kind==0x81u) {
            if(++bitmap_count>1u || o->checksum || o->bytes!=(g->cluster_count+7u)/8u)
                return vmu_fs_fail(s,KUI_VMU_FS_UNSUPPORTED,71u);
            g->bitmap_first_cluster=o->first;g->bitmap_bytes=o->bytes;
        } else if(o->kind==0x82u) {
            if(++upcase_count>1u || (o->bytes&1u) || o->bytes>131072u)
                return vmu_fs_fail(s,KUI_VMU_FS_UNSUPPORTED,72u);
            g->upcase_first_cluster=o->first;g->upcase_bytes=o->bytes;s->upcase_checksum=o->checksum;
        }
    }
    if(result || bitmap_count!=1u || upcase_count!=1u)
        return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,73u);
    s->bitmap_clusters=(uint32_t)vmu_fs_clusters(s,g->bitmap_bytes);
    uint32_t c=g->bitmap_first_cluster;
    for(unsigned i=0;i<s->bitmap_clusters;i++) {
        uint32_t next;
        if(c!=g->bitmap_first_cluster+i || !vmu_fs_cluster(s,c) ||
           vmu_fs_next_cluster(s,c,&next) || (!!next!=(i+1u<s->bitmap_clusters)))
            return vmu_fs_fail(s,KUI_VMU_FS_UNSUPPORTED,74u);
        for(unsigned j=0;j<s->visited_count;j++) if(s->visited[j]==c)
            return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,75u);
        c=next;
    }
    c=g->upcase_first_cluster;unsigned clusters=(unsigned)vmu_fs_clusters(s,g->upcase_bytes);
    if(clusters>VMU_FS_UPCASE_CLUSTERS) return vmu_fs_fail(s,KUI_VMU_FS_LIMIT,76u);
    uint64_t remaining=g->upcase_bytes;uint32_t checksum=0,codepoint=0,escape=0;
    for(unsigned i=0;i<clusters;i++) {
        uint32_t next;
        if(!vmu_fs_cluster(s,c) || (c>=g->bitmap_first_cluster &&
           c-g->bitmap_first_cluster<s->bitmap_clusters))
            return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,77u);
        for(unsigned j=0;j<i;j++) if(s->upcase[j]==c) return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,78u);
        for(unsigned j=0;j<s->visited_count;j++) if(s->visited[j]==c)
            return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,79u);
        s->upcase[s->upcase_count++]=c;
        for(unsigned sector=0;sector<g->sectors_per_cluster && remaining;sector++) {
            if(vmu_fs_read(s,vmu_fs_lba(s,c)+sector,s->sector)) return s->stats->result;
            unsigned bytes=remaining<512u?(unsigned)remaining:512u;
            for(unsigned j=0;j<bytes;j++) checksum=((checksum&1u)?UINT32_C(0x80000000):0u)+(checksum>>1)+s->sector[j];
            for(unsigned j=0;j<bytes;j+=2u) {
                unsigned value=vmu_fs_u16(s->sector+j);
                if(escape) {
                    if(!value || value>65536u-codepoint ||
                       (codepoint<=122u && codepoint+value>97u))
                        return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,64u);
                    codepoint+=value;escape=0;
                } else if(value==0xffffu && codepoint!=65535u) escape=1u;
                else {
                    if(codepoint>=65536u || (codepoint<128u && value!=vmu_fs_upper(codepoint)))
                        return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,65u);
                    ++codepoint;
                }
            }
            remaining-=bytes;
        }
        if(vmu_fs_next_cluster(s,c,&next) || (!!next!=(i+1u<clusters)))
            return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,80u);
        c=next;
    }
    if(checksum!=s->upcase_checksum || escape || codepoint!=65536u)
        return vmu_fs_fail(s,KUI_VMU_FS_CORRUPT,81u);
    for(unsigned i=0;i<s->bitmap_clusters;i++)
        if(vmu_fs_bitmap_bit(s,g->bitmap_first_cluster+i)) return s->stats->result;
    for(unsigned i=0;i<s->upcase_count;i++) if(vmu_fs_bitmap_bit(s,s->upcase[i])) return s->stats->result;
    s->dir_cache=UINT64_MAX;return 0;
}
static int vmu_fs_overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) {
    return n && m && a<b+m && b<a+n;
}
static int vmu_fs_target_index(const struct vmu_fs_state *s,uint32_t cluster) {
    unsigned begin=0u,end=s->target_count;
    while(begin<end) {
        unsigned middle=begin+(end-begin)/2u;
        if(s->targets[middle].cluster==cluster) return (int)middle;
        if(s->targets[middle].cluster<cluster) begin=middle+1u;else end=middle;
    }
    return -1;
}
static int vmu_fs_add_extent(struct vmu_fs_state *s,struct kui_vmu_persist_extent *map,
    uint32_t *count,unsigned limit,uint32_t logical,uint32_t lba,uint32_t blocks) {
    if(*count && (uint64_t)map[*count-1u].lba+map[*count-1u].blocks==lba) {
        map[*count-1u].blocks+=blocks;return 0;
    }
    if(*count>=limit) return vmu_fs_fail(s,KUI_VMU_FS_LIMIT,82u);
    map[*count]=(struct kui_vmu_persist_extent){logical,lba,blocks};++*count;return 0;
}
static int vmu_fs_file(struct vmu_fs_state *s,const struct vmu_fs_object *o,
    uint32_t parent,unsigned role) {
    static const uint32_t vmu_fs_sizes[3]={131072u,1024u,16u};
    static const uint32_t vmu_fs_limits[3]={16u,2u,1u};
    struct kui_vmu_persist_file *file=&s->profile.files[role];
    s->stats->last_role=role;
    if(file->first_cluster) return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,83u);
    if(o->bytes!=vmu_fs_sizes[role] || o->valid!=o->bytes ||
       (o->attributes&UINT32_C(0x1c)) || (role!=KUI_VMU_PERSIST_IDENTITY && (o->attributes&1u)))
        return vmu_fs_fail(s,KUI_VMU_FS_ALLOCATION,84u);
    file->file_bytes=o->bytes;file->valid_bytes=o->valid;file->first_cluster=o->first;
    file->allocation_flags=o->flags;file->directory_first_cluster=parent;
    file->entry_lba=o->lba;file->entry_offset=o->offset;file->entry_count=o->count;
    file->entry_kind=s->profile.geometry.fs_type;
    unsigned clusters=(unsigned)vmu_fs_clusters(s,o->bytes),spc=s->profile.geometry.sectors_per_cluster;
    uint32_t useful=(vmu_fs_sizes[role]+511u)/512u,c=o->first,previous=0;
    for(unsigned i=0;i<clusters;i++) {
        if(!vmu_fs_cluster(s,c) || s->target_count>=VMU_FS_TARGETS)
            return vmu_fs_fail(s,KUI_VMU_FS_ALLOCATION,85u);
        if(vmu_fs_target_index(s,c)>=0 || vmu_fs_system_cluster(s,c))
            return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,86u);
        uint64_t lba=vmu_fs_lba(s,c);
        for(size_t j=0;j<s->input->forbidden_count;j++)
            if(vmu_fs_overlap(lba,spc,s->input->forbidden[j].lba,s->input->forbidden[j].blocks))
                return vmu_fs_fail(s,KUI_VMU_FS_PROTECTED,87u);
        if(vmu_fs_bitmap_bit(s,c)) return s->stats->result;
        unsigned at=s->target_count++;
        while(at && s->targets[at-1u].cluster>c) {s->targets[at]=s->targets[at-1u];--at;}
        s->targets[at]=(struct vmu_fs_target){c,previous,role};
        ++s->stats->target_clusters;
        uint32_t logical=i*spc;
        if(vmu_fs_add_extent(s,file->allocated,&file->allocated_count,vmu_fs_limits[role],logical,(uint32_t)lba,spc))
            return s->stats->result;
        if(logical<useful) {
            uint32_t n=useful-logical;if(n>spc) n=spc;
            if(vmu_fs_add_extent(s,file->useful,&file->useful_count,vmu_fs_limits[role],logical,(uint32_t)lba,n))
                return s->stats->result;
        }
        uint32_t next;
        if(o->flags&2u) next=i+1u<clusters?c+1u:0u;
        else if(vmu_fs_next_cluster(s,c,&next)) return s->stats->result;
        if(!!next!=(i+1u<clusters)) return vmu_fs_fail(s,KUI_VMU_FS_ALLOCATION,88u);
        previous=c;c=next;
    }
    return 0;
}
static int vmu_fs_path(struct vmu_fs_state *s) {
    const char *components[3]={"KUI","vmu-test",s->profile.pair_path+14u};
    uint32_t first=s->profile.geometry.root_cluster,parent=first,flags=0;
    uint64_t bytes=0;
    for(unsigned level=0;level<3u;level++) {
        struct vmu_fs_dir *d=&s->dirs[0];
        uint64_t found_bytes=0;uint32_t found_first=0,found_flags=0;
        unsigned matches=0;s->visited_count=0u;
        if(vmu_fs_open_dir(s,d,first,parent,bytes,flags&2u)) return s->stats->result;
        int result;
        while((result=vmu_fs_object_next(s,d))==1) {
            const struct vmu_fs_object *o=&s->object;
            if(o->ascii && vmu_fs_name_equal(o->name,components[level])) {
                if(++matches>1u || !(o->attributes&16u) || o->kind!=s->profile.geometry.fs_type)
                    return vmu_fs_fail(s,KUI_VMU_FS_PATH,90u);
                found_first=o->first;found_bytes=o->bytes;found_flags=o->flags;
            }
        }
        if(result || matches!=1u) return vmu_fs_fail(s,KUI_VMU_FS_PATH,91u+level);
        parent=first;first=found_first;bytes=found_bytes;flags=found_flags;
    }
    struct vmu_fs_dir *d=&s->dirs[0];s->visited_count=0u;
    if(vmu_fs_open_dir(s,d,first,parent,bytes,flags&2u)) return s->stats->result;
    static const char *const vmu_fs_leaves[3]={"card.vmd","card.journal","identity.bin"};
    int result;
    while((result=vmu_fs_object_next(s,d))==1) {
        const struct vmu_fs_object *o=&s->object;
        if(!o->ascii || o->kind!=s->profile.geometry.fs_type) continue;
        for(unsigned role=0;role<3u;role++) if(vmu_fs_name_equal(o->name,vmu_fs_leaves[role]))
            if(vmu_fs_file(s,o,first,role)) return s->stats->result;
    }
    if(result) return s->stats->result;
    for(unsigned role=0;role<3u;role++) if(!s->profile.files[role].first_cluster)
        return vmu_fs_fail(s,KUI_VMU_FS_PATH,95u+role);
    uint64_t identity=s->profile.files[KUI_VMU_PERSIST_IDENTITY].useful[0].lba;
    if(vmu_fs_read(s,identity,s->sector)) return s->stats->result;
    s->dir_cache=UINT64_MAX;
    if(memcmp(s->sector,s->input->image_id,16u)) return vmu_fs_fail(s,KUI_VMU_FS_IDENTITY,98u);
    return 0;
}

static int vmu_fs_namespace(struct vmu_fs_state *s) {
    unsigned depth=1u,owners[3]={0,0,0};s->visited_count=0u;
    if(vmu_fs_open_dir(s,&s->dirs[0],s->profile.geometry.root_cluster,
        s->profile.geometry.root_cluster,0,0)) return s->stats->result;
    while(depth) {
        struct vmu_fs_dir *d=&s->dirs[depth-1u];
        int result=vmu_fs_object_next(s,d);
        if(result<0) return s->stats->result;
        if(!result) {--depth;continue;}
        const struct vmu_fs_object *o=&s->object;
        if(o->kind==0x81u || o->kind==0x82u) continue; /* Previously admitted system records. */
        int index=vmu_fs_target_index(s,o->first),owner=-1;
        if(index>=0) {
            unsigned role=s->targets[index].role;
            const struct kui_vmu_persist_file *f=&s->profile.files[role];
            if(o->first!=f->first_cluster || o->lba!=f->entry_lba || o->offset!=f->entry_offset ||
               d->first!=f->directory_first_cluster || o->count!=f->entry_count ||
               o->bytes!=f->file_bytes || o->valid!=f->valid_bytes || o->flags!=f->allocation_flags ||
               (o->attributes&0x1cu) || (role!=KUI_VMU_PERSIST_IDENTITY && (o->attributes&1u)) ||
               ++owners[role]!=1u)
                return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,100u);
            owner=(int)role;
        }
        if(o->first && vmu_fs_system_cluster(s,o->first))
            return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,101u);
        if((o->flags&2u) && o->bytes) {
            uint64_t count=vmu_fs_clusters(s,o->bytes);
            for(unsigned i=0;i<s->target_count;i++)
                if(s->targets[i].cluster>=o->first && s->targets[i].cluster-o->first<count &&
                   (owner<0 || s->targets[i].role!=(unsigned)owner))
                    return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,102u);
            const struct kui_vmu_persist_geometry *g=&s->profile.geometry;
            if(vmu_fs_overlap(o->first,count,g->bitmap_first_cluster,s->bitmap_clusters))
                return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,103u);
            for(unsigned i=0;i<s->upcase_count;i++)
                if(s->upcase[i]>=o->first && s->upcase[i]-o->first<count)
                    return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,104u);
        }
        if(o->attributes&16u) {
            if(depth>=KUI_VMU_FS_MAX_DEPTH) return vmu_fs_fail(s,KUI_VMU_FS_LIMIT,105u);
            if(vmu_fs_open_dir(s,&s->dirs[depth],o->first,d->first,o->bytes,o->flags&2u))
                return s->stats->result;
            ++depth;
        }
    }
    for(unsigned role=0;role<3u;role++) if(owners[role]!=1u)
        return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,106u+role);
    return 0;
}
static int vmu_fs_incoming(struct vmu_fs_state *s) {
    const struct kui_vmu_persist_geometry *g=&s->profile.geometry;
    for(uint32_t sector=0;sector<g->fat_sectors;sector++) {
        if(vmu_fs_fat_sector(s,sector)) return s->stats->result;
        for(unsigned i=0;i<128u;i++) {
            uint32_t cluster=sector*128u+i;
            if(cluster<2u || cluster>=g->cluster_count+2u) continue;
            ++s->stats->fat_entries;
            uint32_t value=vmu_fs_u32(s->fat+i*4u);
            if(g->fs_type==KUI_VMU_PERSIST_FAT32) value&=UINT32_C(0x0fffffff);
            int target=vmu_fs_target_index(s,value);if(target<0) continue;
            int source=vmu_fs_target_index(s,cluster);
            /* NoFatChain objects never interpret their own FAT entries. A
             * stale word there is not an active link. Unexpected external
             * links conservatively refuse, including stale unused entries. */
            if(source>=0 && (s->profile.files[s->targets[source].role].allocation_flags&2u)) continue;
            if(source<0 || s->targets[target].previous!=cluster ||
               s->targets[source].role!=s->targets[target].role)
                return vmu_fs_fail(s,KUI_VMU_FS_CROSSLINK,110u);
        }
    }
    return 0;
}
static int vmu_fs_pointer_overlap(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>UINTPTR_MAX-n || y>UINTPTR_MAX-m || (x<y+m && y<x+n);
}
static int vmu_fs_input_valid(const struct kui_vmu_fs_input *in) {
    if(!in || !in->read_sector || !in->pair_path || !in->card_sectors ||
       in->card_sectors>(UINT64_C(1)<<32) || in->high_capacity>1u ||
       (!in->high_capacity && in->card_sectors>(UINT64_C(1)<<23)) ||
       in->partition_first>=in->partition_end || in->partition_end>in->card_sectors ||
       in->partition_end-in->partition_first>UINT32_MAX ||
       in->forbidden_count>256u || (in->forbidden_count && !in->forbidden) ||
       vmu_fs_zero(in->cid,16u) || vmu_fs_zero(in->image_id,16u)) return 0;
    unsigned ones=0;for(unsigned i=0;i<16u;i++) ones+=in->cid[i]==0xffu;if(ones==16u) return 0;
    static const char vmu_fs_prefix[]="/KUI/vmu-test/p";
    for(unsigned i=0;i<15u;i++) if(in->pair_path[i]!=vmu_fs_prefix[i]) return 0;
    unsigned number=0;
    for(unsigned i=15u;i<19u;i++) {
        if(in->pair_path[i]<'0' || in->pair_path[i]>'9') return 0;
        number=number*10u+(unsigned)(in->pair_path[i]-'0');
    }
    if(!number || in->pair_path[19]) return 0;
    for(size_t i=0;i<in->forbidden_count;i++) {
        const struct kui_vmu_fs_range *r=&in->forbidden[i];
        if(!r->blocks || r->lba>=in->card_sectors || r->blocks>in->card_sectors-r->lba) return 0;
    }
    return 1;
}
static int vmu_fs_scan(const struct kui_vmu_fs_input *in,struct kui_vmu_fs_workspace *work) {
    struct vmu_fs_state *s=(struct vmu_fs_state *)(void *)work->private_work.bytes;
    memset(work,0,sizeof(*work));s->input=in;s->stats=&work->stats;
    s->dir_cache=s->bitmap_cache=UINT64_MAX;s->fat_cache=VMU_FS_UNKNOWN;
    struct kui_vmu_persist_profile *p=&s->profile;
    p->card_sectors=in->card_sectors;p->partition_first=in->partition_first;p->partition_end=in->partition_end;
    p->transport=KUI_VMU_PERSIST_PROFILE_TRANSPORT;p->high_capacity=in->high_capacity;
    p->flags=KUI_VMU_PERSIST_PROFILE_PREP_ONLY;
    memcpy(p->cid,in->cid,16u);memcpy(p->image_id,in->image_id,16u);memcpy(p->pair_path,in->pair_path,20u);
    int result=vmu_fs_volume(s);
    if(!result) result=vmu_fs_system(s);
    if(!result) result=vmu_fs_path(s);
    if(!result) result=vmu_fs_namespace(s);
    if(!result) result=vmu_fs_incoming(s);
    if(!result && kui_vmu_persist_profile_validate_ownership(p)) result=vmu_fs_fail(s,KUI_VMU_FS_ALLOCATION,120u);
    work->stats.result=result;return result;
}
int kui_vmu_fs_collect(const struct kui_vmu_fs_input *in,
    struct kui_vmu_persist_profile *out,struct kui_vmu_fs_workspace *work) {
    if(!out || !work) return KUI_VMU_FS_ARGUMENT;
    if(vmu_fs_pointer_overlap(out,sizeof(*out),work,sizeof(*work)) ||
       (in && (vmu_fs_pointer_overlap(in,sizeof(*in),out,sizeof(*out)) ||
        vmu_fs_pointer_overlap(in,sizeof(*in),work,sizeof(*work))))) return KUI_VMU_FS_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!vmu_fs_input_valid(in)) {memset(work,0,sizeof(*work));work->stats.result=KUI_VMU_FS_ARGUMENT;return KUI_VMU_FS_ARGUMENT;}
    int result=vmu_fs_scan(in,work);
    if(!result) {
        const struct vmu_fs_state *s=(const struct vmu_fs_state *)(const void *)work->private_work.bytes;
        memcpy(out,&s->profile,sizeof(*out));
    }
    return result;
}
int kui_vmu_fs_admit(const struct kui_vmu_fs_input *in,
    const struct kui_vmu_persist_profile *expected,struct kui_vmu_fs_workspace *work) {
    if(!expected || !work || !vmu_fs_input_valid(in) ||
       vmu_fs_pointer_overlap(expected,sizeof(*expected),work,sizeof(*work)) ||
       vmu_fs_pointer_overlap(in,sizeof(*in),work,sizeof(*work))) return KUI_VMU_FS_ARGUMENT;
    if(kui_vmu_persist_profile_validate_ownership(expected)) return KUI_VMU_FS_ARGUMENT;
    int result=vmu_fs_scan(in,work);
    if(!result) {
        const struct vmu_fs_state *s=(const struct vmu_fs_state *)(const void *)work->private_work.bytes;
        if(!kui_vmu_persist_profile_same_ownership(expected,&s->profile)) {
            work->stats.result=KUI_VMU_FS_CHANGED;work->stats.detail=121u;result=KUI_VMU_FS_CHANGED;
        }
    }
    return result;
}
