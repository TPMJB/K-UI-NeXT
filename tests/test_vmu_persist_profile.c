/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_persist_profile.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_vmu_persist_profile original,changed,decoded;
static uint8_t wire[4096],saved[4096];
struct source {unsigned calls,fail,corrupt_role,corrupt_block,marker,identity;};
static int read_sector(void *context,unsigned role,uint32_t block,uint8_t out[512]) {
    struct source *s=context;unsigned at=s->calls++;
    if(at==s->fail) return -1;
    for(unsigned i=0;i<512u;i++) out[i]=(uint8_t)(block*17u+i*3u+role*31u);
    if(role==1u && block==1u) {memset(out,0,512u);if(s->marker) out[511]=1u;}
    if(role==2u) {memcpy(out,original.image_id,16u);if(s->identity) out[0]^=1u;}
    if(role==s->corrupt_role && block==s->corrupt_block) out[17]^=1u;
    return 0;
}
static struct source source(void) {
    struct source s={.fail=~0u,.corrupt_role=~0u,.corrupt_block=~0u};return s;
}
static int zero(const void *p,size_t n) {
    const uint8_t *b=p;for(size_t i=0;i<n;i++) if(b[i]) return 0;return 1;
}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(n>>(i*8u));}
static void seal(void) {put32(wire+16u,0u);put32(wire+16u,kui_retail_crc32(0,wire,4096u));}
static void fixture(uint32_t fs,uint32_t spc) {
    memset(&original,0,sizeof(original));
    original.card_sectors=249999360u;original.partition_first=2048u;
    original.partition_end=249999360u;original.transport=1;original.high_capacity=1;original.flags=1;
    for(unsigned i=0;i<16u;i++) {original.cid[i]=(uint8_t)(i+1u);original.image_id[i]=(uint8_t)(i+17u);}
    memcpy(original.pair_path,"/KUI/vmu-test/p0001",20u);
    struct kui_vmu_persist_geometry *g=&original.geometry;
    g->fs_type=fs;g->sector_bytes=512;g->sectors_per_cluster=spc;g->volume_serial=0x12345678;
    g->fat_count=fs==1u?2u:1u;g->fat_mirrored=fs==1u;
    g->fat_first_lba=4096;g->fat_sectors=1024;g->data_first_lba=8192;g->cluster_count=100000;g->root_cluster=2;
    if(spc>256u) g->cluster_count=3000u;
    if(fs==2u) {g->bitmap_first_cluster=3;g->upcase_first_cluster=4;
        g->bitmap_bytes=(g->cluster_count+7u)/8u;g->upcase_bytes=128;}
    const unsigned sizes[3]={131072,1024,16},sectors[3]={256,2,1},clusters[3]={10,1000,2000};
    for(unsigned role=0;role<3u;role++) {
        struct kui_vmu_persist_file *f=&original.files[role];
        f->file_bytes=f->valid_bytes=sizes[role];f->first_cluster=clusters[role];
        f->allocation_flags=fs==1u?0u:3u;f->directory_first_cluster=5;
        f->entry_lba=g->data_first_lba+3u*spc;f->entry_offset=role*96u;
        f->entry_count=fs==1u?2u:3u;f->entry_kind=fs;f->useful_count=f->allocated_count=1;
        f->useful[0]=(struct kui_vmu_persist_extent){0,(uint32_t)g->data_first_lba+(clusters[role]-2u)*spc,sectors[role]};
        f->allocated[0]=f->useful[0];f->allocated[0].blocks=((sectors[role]+spc-1u)/spc)*spc;
    }
    assert(kui_vmu_persist_profile_validate_ownership(&original)==0);
    struct source s=source();assert(kui_vmu_persist_profile_collect_snapshot(&original,read_sector,&s)==0);
    assert(s.calls==259u);assert(kui_vmu_persist_profile_validate(&original)==0);
}
static void invalid(void) {
    assert(kui_vmu_persist_profile_validate_ownership(&changed)!=0);
    memset(wire,0xa5,sizeof(wire));assert(kui_vmu_persist_profile_encode(&changed,wire)!=0);
    assert(zero(wire,sizeof(wire)));
    struct source s=source();assert(kui_vmu_persist_profile_collect_snapshot(&changed,read_sector,&s)!=0);
    assert(s.calls==0u);assert(zero(changed.base_sha256,32u));assert(zero(changed.block_crc,sizeof(changed.block_crc)));
}
static void roundtrip(void) {
    assert(kui_vmu_persist_profile_encode(&original,wire)==0);memcpy(saved,wire,sizeof(wire));
    memset(&decoded,0xa5,sizeof(decoded));assert(kui_vmu_persist_profile_decode(&decoded,wire,sizeof(wire))==0);
    assert(kui_vmu_persist_profile_same_ownership(&original,&decoded)==1);
    assert(kui_vmu_persist_profile_encode(&decoded,wire)==0);assert(!memcmp(saved,wire,sizeof(wire)));
    struct source s=source();assert(kui_vmu_persist_profile_verify_snapshot(&decoded,read_sector,&s)==0);
    assert(s.calls==259u);
    for(unsigned role=0;role<3u;role++) {
        uint32_t lba=0;assert(kui_vmu_persist_profile_map_lba(&decoded,role,0,&lba)==0);
        assert(lba==decoded.files[role].useful[0].lba);
    }
    uint32_t out=123u;assert(kui_vmu_persist_profile_map_lba(&decoded,3u,0,&out)!=0 && !out);
    out=123;assert(kui_vmu_persist_profile_map_lba(&decoded,0u,256u,&out)!=0 && !out);
}
int main(int argc,char **argv) {
    fixture(2,256);
    if(argc==3 && !strcmp(argv[1],"--emit")) {
        assert(kui_vmu_persist_profile_encode(&original,wire)==0);
        FILE *f=fopen(argv[2],"wb");assert(f);assert(fwrite(wire,1,sizeof(wire),f)==sizeof(wire));assert(!fclose(f));return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--check")) {
        FILE *f=fopen(argv[2],"rb");if(!f) return 2;
        size_t bytes=fread(wire,1,sizeof(wire),f);int extra=fgetc(f);assert(!fclose(f));
        if(bytes!=4096u || extra!=EOF || kui_vmu_persist_profile_decode(&decoded,wire,bytes)) return 2;
        memcpy(saved,wire,sizeof(wire));assert(!kui_vmu_persist_profile_encode(&decoded,wire));
        assert(!memcmp(saved,wire,sizeof(wire)));return 0;
    }
    roundtrip();
    for(unsigned i=0;i<4096u;i++) {
        memcpy(wire,saved,sizeof(wire));wire[i]^=1u;memset(&decoded,0xa5,sizeof(decoded));
        assert(kui_vmu_persist_profile_decode(&decoded,wire,sizeof(wire))!=0);assert(zero(&decoded,sizeof(decoded)));
    }
    for(unsigned n=0;n<4096u;n++) {memset(&decoded,0xa5,sizeof(decoded));
        assert(kui_vmu_persist_profile_decode(&decoded,saved,n)!=0);assert(zero(&decoded,sizeof(decoded)));}
    assert(kui_vmu_persist_profile_decode(&decoded,saved,4097u)!=0);assert(zero(&decoded,sizeof(decoded)));
    const unsigned reserved[]={188,348,376,416,511,512+56,960+56,1408+56,2888,4095};
    for(unsigned i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++) {
        memcpy(wire,saved,sizeof(wire));wire[reserved[i]]=1;seal();
        assert(kui_vmu_persist_profile_decode(&decoded,wire,sizeof(wire))!=0);assert(zero(&decoded,sizeof(decoded)));
    }
#define BAD(edit) do {changed=original;edit;invalid();} while(0)
    BAD(changed.flags=0);BAD(changed.transport=2);BAD(changed.high_capacity=2);
    BAD(changed.card_sectors=UINT64_C(0x100000001));BAD(changed.high_capacity=0);
    BAD(memset(changed.cid,0,16));BAD(memset(changed.cid,255,16));BAD(memset(changed.image_id,0,16));
    BAD(changed.pair_path[0]='x');BAD(changed.pair_path[18]='0';changed.pair_path[17]='0';changed.pair_path[16]='0';changed.pair_path[15]='0');
    BAD(changed.pair_path[95]='x');BAD(changed.partition_end=changed.partition_first);
    BAD(changed.geometry.sector_bytes=1024);BAD(changed.geometry.sectors_per_cluster=3);
    BAD(changed.geometry.fat_count=3);BAD(changed.geometry.active_fat=1);
    BAD(changed.geometry.fat_first_lba=changed.partition_first);BAD(changed.geometry.fat_sectors=1);
    BAD(changed.geometry.data_first_lba=changed.geometry.fat_first_lba);
    BAD(changed.geometry.cluster_count=0xffffffffu);BAD(changed.geometry.root_cluster=1);
    BAD(changed.geometry.bitmap_first_cluster=changed.geometry.root_cluster);
    BAD(changed.geometry.upcase_first_cluster=changed.geometry.bitmap_first_cluster);
    BAD(changed.geometry.bitmap_bytes=1);BAD(changed.geometry.upcase_bytes=131074);
    BAD(changed.files[0].file_bytes--);BAD(changed.files[0].valid_bytes--);
    BAD(changed.files[0].entry_count=2);BAD(changed.files[0].entry_kind=1);
    BAD(changed.files[0].entry_offset=1);BAD(changed.files[0].allocation_flags=2);
    BAD(changed.files[0].useful_count=17);BAD(changed.files[1].allocated_count=3);
    BAD(changed.files[0].useful[0].lba++);BAD(changed.files[0].allocated[0].lba++);
    BAD(changed.files[1].allocated[0].blocks=2);BAD(changed.files[2].useful[1].blocks=1);
    BAD(changed.files[1].first_cluster=changed.files[0].first_cluster;
        changed.files[1].useful[0].lba=changed.files[0].useful[0].lba;
        changed.files[1].allocated[0].lba=changed.files[0].allocated[0].lba);
    BAD(changed.files[1].entry_offset=changed.files[0].entry_offset);
    BAD(changed.files[0].entry_lba=changed.files[0].useful[0].lba);
#undef BAD
    changed=original;memset(changed.base_sha256,0,32);changed.block_crc[0]^=1;changed.journal_sequence=123;
    assert(kui_vmu_persist_profile_same_ownership(&changed,&original)==1);
    assert(kui_vmu_persist_profile_validate(&changed)!=0);
    changed=original;changed.files[0].entry_offset+=32;
    assert(kui_vmu_persist_profile_validate_ownership(&changed)==0);
    assert(kui_vmu_persist_profile_same_ownership(&changed,&original)==0);
    for(unsigned fail=0;fail<259u;fail++) {
        changed=original;struct source s=source();s.fail=fail;
        assert(kui_vmu_persist_profile_collect_snapshot(&changed,read_sector,&s)==KUI_VMU_PERSIST_PROFILE_IO);
        assert(s.calls==fail+1u);assert(zero(changed.base_sha256,32));assert(zero(changed.journal_sha256,32));
        assert(zero(changed.identity_sha256,32));assert(zero(changed.marker_sha256,32));assert(zero(changed.block_crc,sizeof(changed.block_crc)));
    }
    struct source s=source();s.marker=1;changed=original;
    assert(kui_vmu_persist_profile_collect_snapshot(&changed,read_sector,&s)==KUI_VMU_PERSIST_PROFILE_MARKER);
    s=source();s.identity=1;changed=original;
    assert(kui_vmu_persist_profile_collect_snapshot(&changed,read_sector,&s)==KUI_VMU_PERSIST_PROFILE_IDENTITY);
    s=source();s.corrupt_role=0;s.corrupt_block=42;
    assert(kui_vmu_persist_profile_verify_snapshot(&original,read_sector,&s)==KUI_VMU_PERSIST_PROFILE_VERIFY);
    s=source();s.corrupt_role=1;s.corrupt_block=0;
    assert(kui_vmu_persist_profile_verify_snapshot(&original,read_sector,&s)==KUI_VMU_PERSIST_PROFILE_VERIFY);
    fixture(1,1);roundtrip();fixture(2,1);roundtrip();fixture(2,65536);roundtrip();
    puts("Persistent preparation codec PASS");return 0;
}
