/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently constructed sparse exFAT image. No formatter/parser shares
 * implementation with admission, and no write callback exists. */
#include "kui/vmu_fs_admission.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FS_CLUSTERS 1024u
#define FS_SPC 8u
#define FS_FAT 24u
#define FS_FAT_SECTORS 9u
#define FS_HEAP 40u
#define FS_SECTORS (FS_HEAP+FS_CLUSTERS*FS_SPC)

static struct {
    struct kui_vmu_fs_input input;
    struct kui_vmu_fs_workspace work;
    struct kui_vmu_persist_profile profile;
    uint8_t boot[24][512],dirs[4][512],bitmap[512],upcase[512],identity[512];
    uint32_t fat[FS_CLUSTERS+2u];
    unsigned calls,fail_at;
    uint32_t partition;
    unsigned generated_files,generated_chain_clusters,generated_root_clusters;
} disk;
static void p16(uint8_t *p,unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void p32(uint8_t *p,uint32_t value) {p16(p,value&65535u);p16(p+2u,value>>16);}
static void p64(uint8_t *p,uint64_t value) {p32(p,(uint32_t)value);p32(p+4u,(uint32_t)(value>>32));}
static uint32_t lba(unsigned c) {return FS_HEAP+(c-2u)*FS_SPC;}
static unsigned upper(unsigned c) {return c>='a' && c<='z'?c-32u:c;}
static void allocated(unsigned c,int value) {
    unsigned byte=(c-2u)/8u,bit=(c-2u)%8u;
    if(value) disk.bitmap[byte]|=(uint8_t)(1u<<bit);else disk.bitmap[byte]&=(uint8_t)~(1u<<bit);
}
static void entry_checksum(uint8_t *p) {
    uint16_t sum=0;unsigned bytes=((unsigned)p[1]+1u)*32u;
    for(unsigned i=0;i<bytes;i++) if(i!=2u && i!=3u)
        sum=(uint16_t)(((sum&1u)?0x8000u:0u)+(sum>>1)+p[i]);
    p16(p+2u,sum);
}
static void file_entry(uint8_t *p,const char *name,unsigned attributes,unsigned first,
    uint64_t bytes,unsigned flags) {
    size_t n=strlen(name);assert(n && n<=15u);memset(p,0,96u);
    p[0]=0x85u;p[1]=2u;p16(p+4u,attributes);
    p[32]=0xc0u;p[33]=(uint8_t)flags;p[35]=(uint8_t)n;
    uint16_t hash=0;
    for(unsigned i=0;i<n;i++) {
        unsigned c=upper((unsigned char)name[i]);
        hash=(uint16_t)(((hash&1u)?0x8000u:0u)+(hash>>1)+c);
        hash=(uint16_t)(((hash&1u)?0x8000u:0u)+(hash>>1));
        p16(p+66u+i*2u,(unsigned char)name[i]);
    }
    p16(p+36u,hash);p64(p+40u,bytes);p32(p+52u,first);p64(p+56u,bytes);p[64]=0xc1u;
    entry_checksum(p);
}
static int read_sector(void *context,uint32_t sector,uint8_t out[512]) {
    assert(context==&disk);++disk.calls;
    if(disk.fail_at && disk.calls==disk.fail_at) return -1;
    memset(out,0,512u);
    if(!sector && disk.partition) {
        out[446u+4u]=7u;p32(out+446u+8u,disk.partition);p32(out+446u+12u,FS_SECTORS);
        p16(out+510u,0xaa55u);return 0;
    }
    assert(sector>=disk.partition && sector-disk.partition<FS_SECTORS);sector-=disk.partition;
    if(sector<24u) memcpy(out,disk.boot[sector],512u);
    else if(sector>=FS_FAT && sector<FS_FAT+FS_FAT_SECTORS) {
        unsigned start=(sector-FS_FAT)*128u;
        for(unsigned i=0;i<128u && start+i<FS_CLUSTERS+2u;i++) p32(out+i*4u,disk.fat[start+i]);
    } else {
        if(disk.generated_files) {
            unsigned cluster=(sector-FS_HEAP)/FS_SPC+2u,part=(sector-FS_HEAP)%FS_SPC;
            unsigned logical=UINT32_MAX;
            if(cluster==4u) logical=part;
            else if(cluster>=880u && cluster<880u+disk.generated_root_clusters-1u)
                logical=(cluster-880u+1u)*FS_SPC+part;
            if(logical!=UINT32_MAX) {
                for(unsigned i=0;i<16u;i++) {
                    unsigned slot=logical*16u+i;
                    if(slot<5u) memcpy(out+i*32u,disk.dirs[0]+slot*32u,32u);
                    else if((slot-5u)/3u<disk.generated_files) {
                        uint8_t entry[96];char name[16];unsigned n=(slot-5u)/3u;
                        snprintf(name,sizeof(name),"f%05u",n);
                        file_entry(entry,name,32u,100u,(uint64_t)disk.generated_chain_clusters*4096u,1u);
                        memcpy(out+i*32u,entry+((slot-5u)%3u)*32u,32u);
                    }
                }
                return 0;
            }
        }
        if(sector==lba(2u)) memcpy(out,disk.bitmap,512u);
        if(sector==lba(3u)) memcpy(out,disk.upcase,512u);
        for(unsigned i=0;i<4u;i++) if(sector==lba(4u+i)) memcpy(out,disk.dirs[i],512u);
        if(sector==lba(61u)) memcpy(out,disk.identity,512u);
    }
    return 0;
}
static void boot_checksum(void) {
    uint32_t sum=0;
    for(unsigned sector=0;sector<11u;sector++) for(unsigned i=0;i<512u;i++)
        if(sector || (i!=106u && i!=107u && i!=112u))
            sum=((sum&1u)?UINT32_C(0x80000000):0u)+(sum>>1)+disk.boot[sector][i];
    for(unsigned i=0;i<512u;i+=4u) p32(disk.boot[11]+i,sum);
    memcpy(disk.boot+12u,disk.boot,12u*512u);
}
static void fresh(void) {
    memset(&disk,0,sizeof(disk));
    disk.input=(struct kui_vmu_fs_input){.read_sector=read_sector,.context=&disk,
        .card_sectors=FS_SECTORS,.partition_end=FS_SECTORS,.high_capacity=1,
        .pair_path="/KUI/vmu-test/p0001"};
    for(unsigned i=0;i<16u;i++) {disk.input.cid[i]=(uint8_t)(0x21u+i);disk.input.image_id[i]=(uint8_t)(0x71u+i);}
    memcpy(disk.identity,disk.input.image_id,16u);
    uint8_t *b=disk.boot[0];b[0]=0xebu;b[1]=0x76u;b[2]=0x90u;memcpy(b+3u,"EXFAT   ",8u);
    p64(b+72u,FS_SECTORS);p32(b+80u,FS_FAT);p32(b+84u,FS_FAT_SECTORS);
    p32(b+88u,FS_HEAP);p32(b+92u,FS_CLUSTERS);p32(b+96u,4u);p32(b+100u,0x12345678u);
    p16(b+104u,0x100u);b[108]=9u;b[109]=3u;b[110]=1u;b[111]=0x80u;p16(b+510u,0xaa55u);
    for(unsigned i=1;i<=8u;i++) p32(disk.boot[i]+508u,UINT32_C(0xaa550000));
    boot_checksum();
    disk.fat[0]=UINT32_C(0xfffffff8);disk.fat[1]=UINT32_MAX;
    disk.fat[2]=disk.fat[3]=disk.fat[4]=UINT32_MAX;
    /* A complete compressed upcase table with the required ASCII mappings. */
    for(unsigned i=0;i<128u;i++) p16(disk.upcase+i*2u,upper(i));
    p16(disk.upcase+256u,0xffffu);p16(disk.upcase+258u,65536u-128u);
    uint32_t sum=0;for(unsigned i=0;i<260u;i++) sum=((sum&1u)?UINT32_C(0x80000000):0u)+(sum>>1)+disk.upcase[i];
    disk.dirs[0][0]=0x81u;p32(disk.dirs[0]+20u,2u);p64(disk.dirs[0]+24u,128u);
    disk.dirs[0][32u]=0x82u;p32(disk.dirs[0]+36u,sum);p32(disk.dirs[0]+52u,3u);p64(disk.dirs[0]+56u,260u);
    file_entry(disk.dirs[0]+64u,"KUI",16u,5u,4096u,3u);
    file_entry(disk.dirs[1],"vmu-test",16u,6u,4096u,3u);
    file_entry(disk.dirs[2],"p0001",16u,7u,4096u,3u);
    file_entry(disk.dirs[3],"card.vmd",32u,20u,131072u,3u);
    file_entry(disk.dirs[3]+96u,"card.journal",32u,60u,1024u,3u);
    file_entry(disk.dirs[3]+192u,"identity.bin",32u,61u,16u,3u);
    for(unsigned i=2u;i<=7u;i++) allocated(i,1);
    for(unsigned i=20u;i<52u;i++) allocated(i,1);
    allocated(60u,1);allocated(61u,1);
}
static int collect(void) {return kui_vmu_fs_collect(&disk.input,&disk.profile,&disk.work);}
static void refusal(void) {
    memset(&disk.profile,0xa5,sizeof(disk.profile));int result=collect();
    assert(result && result==disk.work.stats.result);
    const uint8_t *p=(const uint8_t *)&disk.profile;
    for(size_t i=0;i<sizeof(disk.profile);i++) assert(!p[i]);
    assert(disk.calls<=KUI_VMU_FS_MAX_READS);
}
static void valid_contiguous_and_fresh_comparison(void) {
    fresh();assert(collect()==0);
    assert(disk.profile.geometry.fs_type==KUI_VMU_PERSIST_EXFAT && disk.profile.geometry.bitmap_first_cluster==2u);
    assert(disk.profile.files[0].useful[0].blocks==256u && disk.profile.files[0].allocated[0].blocks==256u);
    assert(disk.profile.files[1].useful[0].blocks==2u && disk.profile.files[1].allocated[0].blocks==8u);
    assert(disk.profile.files[2].file_bytes==16u && disk.profile.files[2].allocated[0].blocks==8u);
    struct kui_vmu_persist_profile expected=disk.profile;
    memset(expected.base_sha256,0x42,sizeof(expected.base_sha256));expected.journal_sequence=99u;
    assert(kui_vmu_fs_admit(&disk.input,&expected,&disk.work)==0); /* Content provenance is not authority. */
    file_entry(disk.dirs[3]+96u,"card.journal",32u,62u,1024u,3u);allocated(62u,1);
    assert(kui_vmu_fs_admit(&disk.input,&expected,&disk.work)==KUI_VMU_FS_CHANGED);
    /* NoFatChain uses no FAT data; its old entries cannot manufacture links. */
    fresh();disk.fat[20u]=60u;disk.fat[21u]=UINT32_C(0xabcdef01);assert(collect()==0);
}
static void fragmented_map_and_exact_tail(void) {
    fresh();file_entry(disk.dirs[3],"card.vmd",32u,20u,131072u,1u);
    for(unsigned i=20u;i<35u;i++) disk.fat[i]=i+1u;
    disk.fat[35u]=100u;
    for(unsigned i=100u;i<115u;i++) {disk.fat[i]=i+1u;allocated(i,1);}
    allocated(115u,1);disk.fat[115u]=UINT32_MAX;
    assert(collect()==0 && disk.profile.files[0].allocated_count==2u);
    disk.fat[120u]=100u;assert(collect()==0); /* Inactive exFAT word, not a live owner. */
    fresh();file_entry(disk.dirs[3]+96u,"card.journal",32u,60u,1024u,1u);
    disk.fat[60u]=62u;disk.fat[62u]=UINT32_MAX;allocated(62u,1);refusal();
}
static void crosslinks_bitmap_metadata_and_attributes(void) {
    fresh();file_entry(disk.dirs[0]+160u,"alias",32u,59u,8192u,3u);allocated(59u,1);refusal();
    fresh();file_entry(disk.dirs[0]+160u,"alias",32u,21u,16u,1u);refusal();
    fresh();disk.fat[100u]=60u;assert(collect()==0); /* Free cluster's old word. */
    for(unsigned cluster=2u;cluster<=7u;cluster++) {fresh();allocated(cluster,0);refusal();}
    fresh();allocated(60u,0);refusal();
    fresh();p64(disk.dirs[3]+96u+40u,1023u);entry_checksum(disk.dirs[3]+96u);refusal();
    fresh();disk.dirs[3][96u+4u]|=1u;entry_checksum(disk.dirs[3]+96u);refusal();
    fresh();disk.dirs[3][96u+4u]|=4u;entry_checksum(disk.dirs[3]+96u);refusal();
    fresh();disk.identity[7u]^=1u;refusal();
    fresh();disk.upcase[194u]^=1u;refusal();
    fresh();struct kui_vmu_fs_range range={lba(60u)+7u,1u};
    disk.input.forbidden=&range;disk.input.forbidden_count=1u;refusal();
}
static void malformed_sets_boot_and_io(void) {
    fresh();disk.dirs[3][2u]^=1u;refusal();
    fresh();disk.dirs[0][160u]=0xa0u;refusal();
    fresh();disk.dirs[3][32u]=0xc2u;entry_checksum(disk.dirs[3]);refusal();
    fresh();file_entry(disk.dirs[0]+160u,"cycle",16u,4u,4096u,3u);refusal();
    fresh();disk.boot[0][106u]=4u;boot_checksum();refusal();
    fresh();disk.boot[12u][100u]^=1u;refusal();
    fresh();disk.boot[11u][23u]^=1u;refusal();
    fresh();p32(disk.boot[0]+92u,KUI_VMU_FS_MAX_FAT_ENTRIES);boot_checksum();refusal();
    fresh();assert(collect()==0);unsigned calls=disk.calls;
    for(unsigned failure=1u;failure<=calls;failure++) {fresh();disk.fail_at=failure;refusal();}
}
static void exfat_partition_offset_and_stale_backup_flags(void) {
    for(unsigned exact=0;exact<2u;exact++) {
        fresh();disk.partition=2048u;disk.input.partition_first=2048u;
        disk.input.partition_end=disk.input.card_sectors=2048u+FS_SECTORS;
        p64(disk.boot[0]+64u,exact?2048u:0u);boot_checksum();
        assert(collect()==0);
        assert(disk.work.stats.boot.valid && !disk.work.stats.boot.reject_mask);
        assert(disk.work.stats.boot.partition_offset==(exact?2048u:0u));
        assert(disk.work.stats.boot.volume_sectors==FS_SECTORS && disk.work.stats.boot.flags==0u);
        assert(disk.profile.partition_first==2048u && disk.profile.files[0].useful[0].lba==2048u+lba(20u));
    }
    fresh();disk.partition=2048u;disk.input.partition_first=2048u;
    disk.input.partition_end=disk.input.card_sectors=2048u+FS_SECTORS;
    p64(disk.boot[0]+64u,4096u);boot_checksum();refusal();
    assert(disk.work.stats.boot.reject_mask==KUI_VMU_FS_BOOT_PARTITION_OFFSET);
    assert(disk.work.stats.boot.partition_offset==4096u && disk.work.stats.detail==15u);
    /* Flags do not contribute to boot checksums. Backup flags are stale even
     * when their dirty/media/active/reserved bits differ from the clean main. */
    fresh();disk.boot[12u][106u]=7u;disk.boot[12u][107u]=0x80u;
    assert(collect()==0 && !disk.work.stats.boot.flags && !disk.work.stats.boot.reject_mask);
    const unsigned flags[]={1u,3u,4u,6u,0x10u,0x8000u};
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
        fresh();p16(disk.boot[0]+106u,flags[i]);refusal();
        assert(disk.work.stats.boot.reject_mask==KUI_VMU_FS_BOOT_FLAGS);
        assert(disk.work.stats.boot.flags==flags[i] && disk.work.stats.detail==15u);
    }
    fresh();p64(disk.boot[0]+72u,FS_SECTORS-1u);boot_checksum();refusal();
    assert(disk.work.stats.boot.reject_mask==KUI_VMU_FS_BOOT_VOLUME_LENGTH);
    fresh();disk.boot[12u][100u]^=1u;refusal();assert(disk.work.stats.detail==17u);
    fresh();disk.boot[11u][3u]^=1u;refusal();assert(disk.work.stats.detail==18u);
    fresh();disk.boot[0][11u]=0x40u;refusal();
    assert(disk.work.stats.boot.reject_mask==KUI_VMU_FS_BOOT_ZERO && disk.work.stats.boot.zero_or==0x40u);
}
static void exfat_dirty_status_preserved_with_full_ownership(void) {
    const unsigned flags[]={0u,2u,8u,10u};
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
        fresh();assert(!collect());struct kui_vmu_persist_profile expected=disk.profile;
        p16(disk.boot[0]+106u,flags[i]);uint8_t before[sizeof(disk.boot)];
        memcpy(before,disk.boot,sizeof(before));
        assert(!kui_vmu_fs_admit(&disk.input,&expected,&disk.work));
        assert(disk.work.stats.boot.flags==flags[i] && !disk.work.stats.boot.reject_mask);
        assert(!memcmp(before,disk.boot,sizeof(before)));
    }
    /* A dirty status cannot hide malformed live metadata, missing bitmap
     * ownership, a live crosslink or a failed boot-region checksum. */
    for(unsigned damage=0;damage<4u;damage++) {
        fresh();p16(disk.boot[0]+106u,2u);
        if(damage==0u) disk.dirs[0][160u]=0xc1u;
        if(damage==1u) allocated(60u,0);
        if(damage==2u) {file_entry(disk.dirs[0]+160u,"alias",32u,59u,8192u,3u);allocated(59u,1);}
        if(damage==3u) disk.boot[11u][3u]^=1u;
        uint8_t before[sizeof(disk.boot)];memcpy(before,disk.boot,sizeof(before));
        refusal();assert(disk.work.stats.boot.flags==2u && !disk.work.stats.boot.reject_mask);
        assert(!memcmp(before,disk.boot,sizeof(before)));
    }
}
static void positive_live_fat_stream_ownership(void) {
    fresh();disk.fat[100u]=60u;assert(collect()==0 && !disk.work.stats.link.valid);
    fresh();allocated(100u,1);disk.fat[100u]=60u;assert(collect()==0); /* Allocated orphan has no live stream. */
    fresh();allocated(100u,1);allocated(101u,1);disk.fat[100u]=60u;
    file_entry(disk.dirs[0]+160u,"contiguous",32u,100u,8192u,3u);
    assert(collect()==0); /* NoFatChain's FAT contents are undefined. */
    fresh();allocated(100u,1);disk.fat[100u]=60u;
    file_entry(disk.dirs[0]+160u,"live",32u,100u,8192u,1u);refusal();
    assert(disk.work.stats.detail==110u && disk.work.stats.link.valid);
    assert(disk.work.stats.link.source_cluster==100u && disk.work.stats.link.target_cluster==60u);
    assert(disk.work.stats.link.target_role==KUI_VMU_PERSIST_JOURNAL && disk.work.stats.link.source_allocated==1u);
    assert(disk.work.stats.link.raw_word==60u && disk.work.stats.link.word_valid==1u);
    assert(disk.work.stats.link.fat_lba==FS_FAT && disk.work.stats.link.decision==KUI_VMU_FS_LINK_TARGET);
    fresh();disk.fat[100u]=60u;
    file_entry(disk.dirs[0]+160u,"live",32u,100u,8192u,1u);refusal();
    assert(disk.work.stats.detail==70u && disk.work.stats.link.source_allocated==0u);
    assert(!disk.work.stats.link.word_valid && disk.work.stats.link.decision==KUI_VMU_FS_LINK_BITMAP_FREE);
    fresh();allocated(99u,1);disk.fat[99u]=100u;disk.fat[100u]=60u;
    file_entry(disk.dirs[0]+160u,"middlefree",32u,99u,12288u,1u);refusal();
    assert(disk.work.stats.detail==70u && disk.work.stats.link.source_cluster==100u);
    assert(!disk.work.stats.link.source_allocated && !disk.work.stats.link.word_valid);
    /* A NoFatChain claim must never hide a distinct live chained file
     * reaching that same cluster and then the journal. */
    fresh();for(unsigned i=99u;i<=101u;i++) allocated(i,1);
    file_entry(disk.dirs[0]+160u,"contiguous",32u,100u,8192u,3u);
    file_entry(disk.dirs[0]+256u,"live",32u,99u,12288u,1u);
    disk.fat[99u]=100u;disk.fat[100u]=60u;refusal();assert(disk.work.stats.detail==110u);
    fresh();file_entry(disk.dirs[0]+160u,"fragmented",32u,100u,12288u,1u);
    allocated(100u,1);allocated(102u,1);allocated(104u,1);
    disk.fat[100u]=102u;disk.fat[102u]=104u;disk.fat[104u]=UINT32_MAX;
    assert(collect()==0 && disk.work.stats.chain_steps==3u);
    fresh();file_entry(disk.dirs[0]+160u,"empty",32u,0u,0u,1u);assert(collect()==0);
    file_entry(disk.dirs[0]+160u,"empty",32u,100u,0u,1u);refusal();
    for(unsigned kind=0;kind<3u;kind++) {
        fresh();allocated(100u,1);allocated(101u,1);
        file_entry(disk.dirs[0]+160u,"badchain",32u,100u,kind==0u?8192u:kind==1u?4096u:12288u,1u);
        disk.fat[100u]=kind==0u?UINT32_MAX:101u;
        disk.fat[101u]=kind==2u?100u:UINT32_MAX;
        refusal();assert(disk.work.stats.link.decision==KUI_VMU_FS_LINK_MALFORMED);
    }
}
static void positive_live_chain_budgets(void) {
    for(unsigned random=0;random<2u;random++) {
        fresh();disk.generated_files=random?200u:6000u;
        disk.generated_chain_clusters=random?736u:768u;
        unsigned slots=5u+disk.generated_files*3u+1u;
        disk.generated_root_clusters=((slots+15u)/16u+FS_SPC-1u)/FS_SPC;
        disk.fat[4u]=880u;
        for(unsigned i=0;i<disk.generated_root_clusters-1u;i++) {
            unsigned cluster=880u+i;allocated(cluster,1);
            disk.fat[cluster]=i+1u<disk.generated_root_clusters-1u?cluster+1u:UINT32_MAX;
        }
        if(random) {
            /* Alternate distant FAT sectors for a real read-budget refusal. */
            for(unsigned i=0;i<368u;i++) {
                allocated(100u+i,1);allocated(500u+i,1);
                disk.fat[100u+i]=500u+i;disk.fat[500u+i]=i+1u<368u?101u+i:UINT32_MAX;
            }
        } else for(unsigned i=0;i<768u;i++) {
            allocated(100u+i,1);disk.fat[100u+i]=i+1u<768u?101u+i:UINT32_MAX;
        }
        refusal();assert(disk.work.stats.result==KUI_VMU_FS_LIMIT);
        if(random) {
            assert(disk.work.stats.detail==1u && disk.work.stats.reads==KUI_VMU_FS_MAX_READS);
            assert(disk.work.stats.chain_steps<KUI_VMU_FS_MAX_CHAIN_STEPS);
        } else {
            assert(disk.work.stats.detail==131u && disk.work.stats.chain_steps==KUI_VMU_FS_MAX_CHAIN_STEPS);
            assert(disk.work.stats.link.decision==KUI_VMU_FS_LINK_LIMIT);
        }
    }
}
int main(void) {
    valid_contiguous_and_fresh_comparison();fragmented_map_and_exact_tail();
    crosslinks_bitmap_metadata_and_attributes();malformed_sets_boot_and_io();
    exfat_partition_offset_and_stale_backup_flags();
    exfat_dirty_status_preserved_with_full_ownership();
    positive_live_fat_stream_ownership();positive_live_chain_budgets();
    puts("VMU filesystem admission: exFAT live maps, full allocation, bitmap/directory ownership, crosslinks, metadata and I/O refusals passed");
    return 0;
}
