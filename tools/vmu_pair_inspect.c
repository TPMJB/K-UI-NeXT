/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only diagnosis of an owner's virtual card pair, copied off the SD card.
 * The production store replays the journal into an in-memory copy laid out at
 * the profile's own card sectors, then the production validator checks the
 * resulting image. The input files are only read. Usage:
 *   vmu_pair_inspect PROFILE card.vmd card.journal [EFFECTIVE_IMAGE_OUT] */
#include "kui/vmu_persist_profile.h"
#include "kui/vmu_store.h"
#include "kui/vmu_image.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct device {
    const struct kui_vmu_persist_profile *profile;
    uint8_t image[KUI_VMU_IMAGE_BYTES],journal[2u*512u];
    unsigned writes,syncs;
};
/* Only the pair's own mapped sectors exist; anything else is an error. */
static uint8_t *sector(struct device *d,uint64_t lba) {
    for(unsigned role=0;role<2u;role++) {
        const struct kui_vmu_persist_file *f=&d->profile->files[role];
        for(uint32_t i=0;i<f->useful_count;i++) {
            const struct kui_vmu_persist_extent *e=&f->useful[i];
            if(lba>=e->lba && lba-e->lba<e->blocks)
                return (role?d->journal:d->image)+512u*(e->logical_block+(uint32_t)(lba-e->lba));
        }
    }
    return NULL;
}
static int read_sector(void *context,uint64_t lba,void *out) {
    uint8_t *at=sector(context,lba);if(!at) return -1;
    memcpy(out,at,512u);return 0;
}
static int write_sector(void *context,uint64_t lba,const void *in) {
    struct device *d=context;uint8_t *at=sector(d,lba);if(!at) return -1;
    memcpy(at,in,512u);++d->writes;return 0;
}
static int sync_device(void *context) {++((struct device *)context)->syncs;return 0;}
static int image_read(void *context,uint16_t block,void *out) {
    return kui_vmu_store_read(context,block,out)?-1:0;
}
static bool load(const char *path,void *out,size_t bytes) {
    FILE *f=fopen(path,"rb");if(!f) return false;
    size_t got=fread(out,1,bytes,f);int extra=fgetc(f);fclose(f);
    return got==bytes && extra==EOF;
}
int main(int argc,char **argv) {
    if(argc!=4 && argc!=5) {fprintf(stderr,"Usage: %s PROFILE card.vmd card.journal [OUT]\n",argv[0]);return 2;}
    static uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES];
    static struct kui_vmu_persist_profile profile;static struct device device;
    if(!load(argv[1],wire,sizeof(wire)) || kui_vmu_persist_profile_decode(&profile,wire,sizeof(wire))) {
        fprintf(stderr,"Profile unreadable or not a canonical KUIVMUP2 record\n");return 2;
    }
    device.profile=&profile;
    if(!load(argv[2],device.image,sizeof(device.image)) || !load(argv[3],device.journal,sizeof(device.journal))) {
        fprintf(stderr,"card.vmd must be 131072 bytes and card.journal 1024 bytes\n");return 2;
    }
    static uint8_t before[KUI_VMU_IMAGE_BYTES];memcpy(before,device.image,sizeof(before));
    struct kui_vmu_store_extent image[KUI_VMU_STORE_MAX_EXTENTS],journal[KUI_VMU_STORE_MAX_EXTENTS];
    for(uint32_t i=0;i<profile.files[0].useful_count;i++)
        image[i]=(struct kui_vmu_store_extent){profile.files[0].useful[i].lba,profile.files[0].useful[i].blocks};
    for(uint32_t i=0;i<profile.files[1].useful_count;i++)
        journal[i]=(struct kui_vmu_store_extent){profile.files[1].useful[i].lba,profile.files[1].useful[i].blocks};
    struct kui_vmu_store_config config={.card_sectors=profile.card_sectors,.image_extents=image,
        .image_extent_count=profile.files[0].useful_count,.journal_extents=journal,
        .journal_extent_count=profile.files[1].useful_count};
    memcpy(config.image_id,profile.image_id,16u);
    static struct kui_vmu_store store;
    const struct kui_vmu_store_io io={&device,read_sector,write_sector,sync_device};
    int opened=kui_vmu_store_open(&store,&io,&config);
    int changed=-1;
    for(unsigned b=0;b<256u;b++) if(memcmp(before+512u*b,device.image+512u*b,512u)) {changed=(int)b;break;}
    int valid=-99;static struct kui_vmu_image_workspace work;struct kui_vmu_image_summary summary={0};
    if(!opened) {
        const struct kui_vmu_image_io image_io={.context=&store,.read_block=image_read};
        valid=kui_vmu_image_validate(&image_io,KUI_VMU_IMAGE_BYTES,&work,&summary);
    }
    if(argc==5 && !opened) {
        FILE *f=fopen(argv[4],"wb");
        if(!f || fwrite(device.image,1,sizeof(device.image),f)!=sizeof(device.image) || fclose(f)) return 2;
    }
    printf("{\"store_open\":%d,\"journal_sequence\":%llu,\"replay_writes\":%u,\"first_changed_block\":%d,"
        "\"validate\":%d,\"files\":%u,\"game_files\":%u,\"used_blocks\":%u,\"free_blocks\":%u}\n",
        opened,(unsigned long long)store.sequence,device.writes,changed,valid,
        summary.files,summary.game_files,summary.used_blocks,summary.free_blocks);
    return opened || valid;
}
