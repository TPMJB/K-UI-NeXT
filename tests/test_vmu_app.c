/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/apps.h"
#include "kui/destination.h"
#include "kui/media.h"
#include "kui/storage.h"
#include "kui/vmu_read_bounded.h"
#include "kui/vmu_collection.h"
#include <dc/maple.h>
#include <dc/vmufs.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Real FatFs on a disposable card image; only VMU transport is simulated.
 * Restore/delete stubs model upstream allocation/commit stages. No format API. */
static uint8_t card[256][512],destination_card[256][512],original_destination[256][512];
static bool two_cards;
static maple_device_t devices[4]={{1,0,1,{MAPLE_FUNC_MEMCARD}},{1,0,1,{MAPLE_FUNC_MEMCARD}},{1,0,2,{MAPLE_FUNC_MEMCARD}},{1,0,2,{MAPLE_FUNC_MEMCARD}}};
static unsigned destination_active=2,source_vmu_writes,bulk_dir_commits;
static uint64_t reported_done;
static bool bulk_staging_checked;
static void verify_save(unsigned index);
static unsigned active,reads,locks,connects,disconnects,writes,source_slot,enumerations,raw_zero_reads,media_reads;
static bool missing,payload_seen,injected;
static unsigned vmu_writes,free_user_reads,rejection_read_count;
static bool collection_read_guard;
static unsigned collection_unowned_reads;
static uint8_t original_card[256][512];
static FILE *image;
static uint64_t image_blocks;
static const char *fault;
static const char *sentinel_path="0:/KUI/backups/vmu/A1-0001/keep.bin";
static const char sentinel[]="Existing VMU backup and capture files must remain intact";
static bool is(const char *name) {return fault && !strcmp(fault,name);}
static void log_line(const char *format,...) {
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);puts("");
}
static void put16(uint8_t *p,unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void setup(unsigned count) {
    memset(card,0,sizeof(card));memset(card[255],0x55,16);
    put16(card[255]+70,254);put16(card[255]+72,1);put16(card[255]+74,253);
    put16(card[255]+76,13);put16(card[255]+80,200);
    for(unsigned i=0;i<256;i++) put16(card[254]+2*i,0xfffc);
    for(unsigned i=0;i<count;i++) {
        uint8_t *entry=card[253-i/16]+(i%16)*32;
        entry[0]=0x33;put16(entry+2,i*2);put16(entry+24,2);
        char name[16];snprintf(name,sizeof(name),"SAVE%02u",i);memcpy(entry+4,name,strlen(name));
        put16(card[254]+i*4,i*2+1);put16(card[254]+i*4+2,0xfffa);
        for(unsigned block=0;block<2;block++) {
            for(unsigned j=0;j<512;j++) card[i*2+block][j]=(uint8_t)(i*7+block*19+j);
            memcpy(card[i*2+block],"VMUSAVE",7);
        }
    }
}
/* Keep one existing save while leaving a chosen game-prefix gap in fixtures. */
static void move_first_save(unsigned first) {
    assert(first && first+1<200);
    memmove(card[first],card[0],2*512);
    put16(card[254],0xfffc);put16(card[254]+2,0xfffc);
    put16(card[254]+first*2,first+1);put16(card[254]+(first+1)*2,0xfffa);
    put16(card[253]+2,first);
}
static uint8_t (*device_card(maple_device_t *device))[512] {
    assert(device==&devices[active] || (two_cards && device==&devices[destination_active]));
    return device==&devices[destination_active]?destination_card:card;
}
maple_device_t *maple_enum_dev(int port,int unit) {
    ++enumerations;
    if(two_cards && port==0 && unit==2) return &devices[destination_active];
    return !missing && port==(int)(source_slot/2) && unit==(int)(source_slot%2+1)?&devices[active]:NULL;
}
int vmufs_mutex_lock(void) {assert(!locks);++locks;return 0;}
int vmufs_mutex_unlock(void) {assert(locks==1);--locks;return 0;}
int kui_vmu_read_bounded(maple_device_t *device,uint16_t block,uint8_t out[512]) {
    assert(locks==1 && block<256);++reads;uint8_t (*storage)[512]=device_card(device);
    if(collection_read_guard) {
        unsigned fat=storage[255][70]|(unsigned)storage[255][71]<<8;
        unsigned dir=storage[255][74]|(unsigned)storage[255][75]<<8;
        unsigned dirs=storage[255][76]|(unsigned)storage[255][77]<<8;
        unsigned users=storage[255][80]|(unsigned)storage[255][81]<<8;
        bool metadata=block==255u || block==fat || (block<=dir && block+dirs>dir);
        bool allocated=fat<256u && block<users && (storage[fat][block*2u]!=0xfcu || storage[fat][block*2u+1u]!=0xffu);
        if(!metadata && !allocated) ++collection_unowned_reads;
    }
    if(block<200 && storage[254][block*2]==0xfc && storage[254][block*2+1]==0xff) ++free_user_reads;
    if(is("description-read-fail") && block<200) {injected=true;return -1;}
    if(is("bulk-vmu-read-fail") || (is("bulk-final-read-fail") && bulk_dir_commits && block==199 && device==&devices[destination_active]) || is("vmu-read-fail") ||
       (is("collection-capture-read-fail") && block==254 && reads>30u)) {injected=true;rejection_read_count=reads;return -1;}
    if(is("short-block")) {memcpy(out,storage[block],508);injected=true;return 0;}
    if(block==0 && is("card-image-changed") && ++raw_zero_reads==3) {storage[0][11]^=1;injected=true;}
    if(block==10 && is("card-image-removed")) {missing=true;injected=true;}
    memcpy(out,storage[block],512);
    if(is("oversize-block")) {out[512]=0;injected=true;}
    if(is("device-change") && reads==1) {active=1;injected=true;}
    if(is("contents-change") && block<200 && !injected) {card[255][48]^=1;injected=true;}
    return 0;
}
/* Transport fault model plus upstream FAT/directory publish boundaries. */
int vmu_block_write(maple_device_t *device,uint16_t block,const uint8_t *data) {
    assert(locks==1 && block<200);
    if(fault && !strncmp(fault,"bulk-",5) && !vmu_writes) {
        for(unsigned i=0;i<3;i++) verify_save(i);
        bulk_staging_checked=true;
    }
    ++vmu_writes;uint8_t (*storage)[512]=device_card(device);
    if(device!=&devices[destination_active]) ++source_vmu_writes;
    if((is("restore-write-fail") || is("copy-write-fail") || is("bulk-write-fail") || (is("bulk-write-fail-after-one") && reported_done>=1024))) {injected=true;return -1;}
    memcpy(storage[block],data,512);
    if((is("restore-data-corrupt") || is("copy-data-corrupt") || is("bulk-data-corrupt"))) {storage[block][0]^=1;injected=true;}
    if(is("restore-remove")) {missing=true;injected=true;}
    return 0;
}
int vmufs_fat_write(maple_device_t *device,vmu_root_t *root,uint16_t *fat) {
    assert(locks==1);++vmu_writes;uint8_t (*storage)[512]=device_card(device);
    if(device!=&devices[destination_active]) ++source_vmu_writes;
    if((is("restore-fat-fail") || is("delete-fat-fail") || is("copy-fat-fail") || is("bulk-fat-fail") || (is("bulk-fat-fail-after-one") && reported_done>=1024))) {injected=true;memcpy(storage[root->fat_loc]+384,(uint8_t *)fat+384,64);return -1;}
    memcpy(storage[root->fat_loc],fat,512);
    if((is("restore-fat-corrupt") || is("delete-fat-corrupt") || is("copy-fat-corrupt") || is("bulk-fat-corrupt"))) {injected=true;storage[root->fat_loc][20]^=1;}
    return 0;
}
int vmufs_dir_write(maple_device_t *device,vmu_root_t *root,vmu_dir_t *dir) {
    assert(locks==1);uint8_t (*storage)[512]=device_card(device);
    for(unsigned block=0;block<root->dir_size;block++) {
        bool dirty=false;
        for(unsigned i=0;i<16;i++) {if(dir[block*16+i].dirty) dirty=true;dir[block*16+i].dirty=0;}
        if(!dirty) continue;
        ++vmu_writes;
        if(device!=&devices[destination_active]) ++source_vmu_writes;
        if((is("restore-dir-fail") || is("delete-dir-fail") || is("copy-dir-fail") || is("bulk-dir-fail") || (is("bulk-dir-fail-after-one") && reported_done>=1024))) {injected=true;return -1;}
        memcpy(storage[root->dir_loc-block],dir+block*16,512);++bulk_dir_commits;
        if((is("restore-dir-corrupt") || is("delete-dir-corrupt") || is("copy-dir-corrupt") || is("bulk-dir-corrupt"))) {injected=true;storage[root->dir_loc-block][63]^=1;}
        if(is("restore-dir-existing-corrupt")) {injected=true;storage[root->dir_loc-block][31]^=1;}
        if(is("restore-final-corrupt") || is("copy-final-corrupt")) {injected=true;storage[199][1]^=1;}
        if(is("bulk-final-corrupt")) {injected=true;storage[198][1]^=1;}
        if(is("delete-remove")) {injected=true;missing=true;}
    }
    return 0;
}
static bool cancelled(void) {
    if(is("bulk-cancel-start") || (is("bulk-cancel-data") && vmu_writes) || (is("bulk-cancel-after-one") && reported_done>=1024)) {injected=true;return true;}
    if(is("image-cancel-read") && media_reads>=8) {injected=true;return true;}
    if(((is("restore-cancel") && vmu_writes) || (is("restore-stop-commit") && vmu_writes>=3)) || (is("delete-stop-commit") && vmu_writes) || (is("delete-cancel-backup") && payload_seen) ||
       (is("copy-cancel") && vmu_writes) || is("cancel-start") || (is("cancel-read") && reads>=4) || (is("cancel-write") && payload_seen)) {
        injected=true;return true;
    }
    return false;
}
static uint64_t blocks(void *ctx) {(void)ctx;return image_blocks;}
static bool contains_payload(const uint8_t *data,size_t count) {
    for(size_t i=0;i<count;i++) if(!memcmp(data+i*512,"VMUSAVE",7)) return true;
    return false;
}
static int read_image(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;++media_reads;
    if(fseeko(image,(off_t)block*512,SEEK_SET) || fread(data,512,count,image)!=count) return -1;
    if(contains_payload(data,count)) {
        if(is("image-read-fail")) {injected=true;return -1;}
        if(is("image-read-corrupt") && !injected) {injected=true;data[10]^=1;}
    }
    if(payload_seen && contains_payload(data,count)) {
        if(is("readback-fail") || is("delete-readback-fail") || is("copy-readback-fail") || is("bulk-readback-fail")) {injected=true;return -1;}
        if(is("readback-corrupt") || is("delete-readback-corrupt") || is("copy-readback-corrupt") || is("bulk-readback-corrupt")) {injected=true;data[0]^=1;}
    }
    return 0;
}
static int write_image(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;++writes;
    if((is("restore-sd-write-fail") || is("delete-sd-write-fail") || is("copy-sd-write-fail") || is("bulk-sd-write-fail"))) {injected=true;return -1;}
    if(contains_payload(data,count)) {
        payload_seen=true;
        if(!injected && is("bulk-source-payload-change-during-stage")) {card[4][19]^=1;original_card[4][19]^=1;injected=true;}
        if(!injected && is("bulk-destination-change-during-stage")) {destination_card[255][48]^=1;original_destination[255][48]^=1;injected=true;}
        if(!injected && is("bulk-source-removed-during-stage")) {missing=true;injected=true;}
        if(!injected && is("bulk-destination-removed-during-stage")) {two_cards=false;injected=true;}
        if(is("write-fail")) {injected=true;return -1;}
    }
    return fseeko(image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,image)!=count?-1:0;
}
static int sync_image(void *ctx) {
    (void)ctx;
    if((is("sync-fail") && payload_seen) || (is("restore-sd-sync-fail") || is("delete-sd-sync-fail") || is("copy-sd-sync-fail") || is("bulk-sd-sync-fail"))) {injected=true;return -1;}
    return fflush(image) || fsync(fileno(image))?-1:0;
}
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};
void kui_sd_set_params(unsigned sci,bool crc) {assert(sci==KUI_STORAGE_AUTO && crc);}
bool kui_sd_connect(void) {
    ++connects;
    if(is("connect-fail")) {injected=true;return false;}
    kui_media_set(&media);return true;
}
void kui_sd_disconnect(void) {++disconnects;kui_media_set(NULL);}
static void progress(const struct kui_app_status *value) {
    assert(value->done<=value->total);reported_done=value->done;
    if(value->done>=1024 && !injected) {
        if(is("bulk-source-removed-after-one")) {missing=true;injected=true;}
        if(is("bulk-destination-removed-after-one")) {two_cards=false;injected=true;}
    }
}
static void mount_image(FATFS *fs) {kui_media_set(&media);assert(kui_mount(fs,log_line));}
static void write_file(const char *path,const void *data,size_t size) {
    FIL file;UINT count;
    assert(f_open(&file,path,FA_WRITE|FA_CREATE_NEW)==FR_OK);
    assert(f_write(&file,data,(UINT)size,&count)==FR_OK && count==size);
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void equal_file(const char *path,const void *expected,size_t size) {
    FIL file;UINT got;uint8_t data[2048];
    assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==size);
    for(size_t at=0;at<size;) {
        UINT amount=(UINT)(size-at>sizeof(data)?sizeof(data):size-at);
        assert(f_read(&file,data,amount,&got)==FR_OK && got==amount);
        assert(!memcmp(data,(const uint8_t *)expected+at,amount));at+=amount;
    }
    assert(f_close(&file)==FR_OK);
}
static void verify_preserved(void) {
    equal_file(sentinel_path,sentinel,sizeof(sentinel));
    equal_file("0:/KUI/dumps/keep.bin",sentinel,sizeof(sentinel));
}
static void verify_save(unsigned index) {
    char path[160];uint8_t data[1024];memcpy(data,card[index*2],512);memcpy(data+512,card[index*2+1],512);
    snprintf(path,sizeof(path),"0:/KUI/backups/vmu/A1-0002/%03u_SAVE%02u.vms",index+1,index);
    equal_file(path,data,sizeof(data));
    snprintf(path,sizeof(path),"0:/KUI/backups/vmu/A1-0002/%03u_SAVE%02u.dir",index+1,index);
    const uint8_t *entry=card[253-index/16]+(index%16)*32;
    equal_file(path,entry,32);
    uint8_t proof[32]={0};memcpy(proof,"KUIVMU1",7);
    const uint32_t values[]={1024,kui_crc32(0,entry,32),kui_crc32(0,data,sizeof(data))};
    for(unsigned i=0;i<3;i++) for(unsigned j=0;j<4;j++) proof[8+i*4+j]=(uint8_t)(values[i]>>(j*8));
    uint32_t crc=kui_crc32(0,proof,28);
    for(unsigned j=0;j<4;j++) proof[28+j]=(uint8_t)(crc>>(j*8));
    snprintf(path,sizeof(path),"0:/KUI/backups/vmu/A1-0002/%03u_SAVE%02u.crc",index+1,index);
    equal_file(path,proof,sizeof(proof));
}
static void alter_file(const char *path) {
    FIL file;UINT n;uint8_t x=0x13;
    assert(f_open(&file,path,FA_WRITE|FA_OPEN_EXISTING)==FR_OK);
    assert(f_write(&file,&x,1,&n)==FR_OK && n==1);
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void test_restore(const char *mode) {
    const char *path="0:/KUI/backups/vmu/A1-0002/001_SAVE00.vms";
    FATFS fs;struct kui_vmu_view out;struct kui_vmu_backup_view list;
    uint8_t source_data[1024],source_entry[32];
    bool game=!strncmp(mode,"restore-game",12);
    if(game) {card[253][0]=0xcc;put16(card[253]+26,1);}
    memcpy(source_data,card[0],sizeof(source_data));memcpy(source_entry,card[253],32);
    kui_vmu_app_run(0,0,0,0,&out,log_line,cancelled,progress);assert(out.status.passed);
    kui_vmu_app_run(1,0,0,0,&out,log_line,cancelled,progress);assert(out.status.passed && !vmu_writes);
    kui_vmu_backups_run(0,&list,log_line,cancelled,progress);
    assert(list.status.passed && list.count==1 && list.total==1 && !strcmp(list.entries[0].path,path));
    if(!strcmp(mode,"restore-page")) {
        kui_vmu_backups_run(1,&list,log_line,cancelled,progress);
        assert(list.status.passed && !list.count && list.total==1);return;
    }
    setup(1);memset(card[253]+4,0,12);memcpy(card[253]+4,"OTHER_SAVE",10);card[0][10]^=0xa7;card[1][11]^=0x8b;
    if(!strcmp(mode,"restore-game")) move_first_save(2);
    if(!strcmp(mode,"restore-game-fragmented-prefix")) move_first_save(1);
    if(!strcmp(mode,"restore-game-existing")) {card[253][0]=0xcc;put16(card[253]+26,1);}
    if(!strcmp(mode,"restore-orphan")) put16(card[254]+20,0xfffa);
    if(!strcmp(mode,"restore-orphan-link")) put16(card[254]+20,199);
    if(!strcmp(mode,"restore-duplicate")) {setup(2);memcpy(card[253]+4,"OTHER_SAVE",10);memcpy(card[253]+36,card[253]+4,12);}
    if(!strcmp(mode,"restore-existing-tail")) {memset(card[253]+4,0,12);memcpy(card[253]+4,"SAVE00\0TAIL",11);}
    if(!strcmp(mode,"restore-existing")) {memset(card[253]+4,0,12);memcpy(card[253]+4,"SAVE00",6);}
    if(!strcmp(mode,"restore-capacity")) for(unsigned i=2;i<200;i++) put16(card[254]+i*2,0xfffa);
    memcpy(original_card,card,sizeof(card));
    if(!strcmp(mode,"restore-corrupt") || !strcmp(mode,"restore-bad-proof") || !strcmp(mode,"restore-bad-dir") ||
       !strcmp(mode,"restore-truncated") || !strcmp(mode,"restore-legacy")) {
        mount_image(&fs);
        if(!strcmp(mode,"restore-corrupt")) alter_file(path);
        if(!strcmp(mode,"restore-truncated")) {FIL f;assert(f_open(&f,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);assert(f_close(&f)==FR_OK);}
        if(!strcmp(mode,"restore-bad-dir")) alter_file("0:/KUI/backups/vmu/A1-0002/001_SAVE00.dir");
        if(!strcmp(mode,"restore-bad-proof")) alter_file("0:/KUI/backups/vmu/A1-0002/001_SAVE00.crc");
        if(!strcmp(mode,"restore-legacy")) assert(f_unlink("0:/KUI/backups/vmu/A1-0002/001_SAVE00.crc")==FR_OK);
        assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    if(!strcmp(mode,"restore-unpreviewed")) {
        kui_vmu_restore_run(path,0,true,&out,log_line,cancelled,progress);
        assert(!out.status.passed && !out.restore_ready && !vmu_writes);return;
    }
    kui_vmu_restore_run(!strcmp(mode,"restore-invalid-path")?"0:/KUI/backups/vmu/../SAVE.vms":path,
        0,false,&out,log_line,cancelled,progress);
    bool preview_fail=!strcmp(mode,"restore-orphan") || !strcmp(mode,"restore-orphan-link") ||
        !strcmp(mode,"restore-duplicate") || !strcmp(mode,"restore-existing-tail") || !strcmp(mode,"restore-truncated") ||
        !strcmp(mode,"restore-existing") || !strcmp(mode,"restore-capacity") ||
        !strcmp(mode,"restore-game-blocked-zero") || !strcmp(mode,"restore-game-fragmented-prefix") || !strcmp(mode,"restore-game-existing") ||
        !strcmp(mode,"restore-corrupt") || !strcmp(mode,"restore-bad-proof") || !strcmp(mode,"restore-bad-dir") ||
        !strcmp(mode,"restore-legacy") || !strcmp(mode,"restore-invalid-path");
    if(preview_fail) {
        assert(!out.status.passed && !out.restore_ready && out.status.errors && !vmu_writes);
        assert(!memcmp(card,original_card,sizeof(card)));return;
    }
    assert(out.status.passed && out.restore_ready && out.status.total==1024 && !vmu_writes);
    if(!strcmp(mode,"restore-changed-card")) card[255][48]^=1;
    if(!strcmp(mode,"restore-changed-source")) {
        mount_image(&fs);alter_file(path);assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    if(!strcmp(mode,"restore-device-change")) active=1;
    fault=mode;
    kui_vmu_restore_run(path,0,true,&out,log_line,cancelled,progress);
    bool success=!strcmp(mode,"restore-ok") || !strcmp(mode,"restore-game") || !strcmp(mode,"restore-stop-commit");
    if(success) {
        assert(out.status.passed && out.status.complete && !out.restore_ready && !out.status.errors);
        assert(out.status.done==1024 && out.free_blocks==196);
        unsigned first=game?0:199,second=game?1:198;
        assert(!memcmp(card[first],source_data,512) && !memcmp(card[second],source_data+512,512));
        if(game) assert(card[253][34]==0 && card[253][35]==0 && card[254][0]==1 && card[254][1]==0 && card[254][2]==0xfa && card[254][3]==0xff);
        assert(!memcmp(card[253]+32,source_entry,2) && !memcmp(card[253]+32+4,source_entry+4,28));
    } else {
        assert(!out.status.passed && !out.restore_ready && (out.status.errors || out.status.stopped));
        if(!strcmp(mode,"restore-changed-card") || !strcmp(mode,"restore-changed-source") ||
           !strcmp(mode,"restore-device-change") || !strcmp(mode,"restore-sd-write-fail") || !strcmp(mode,"restore-sd-sync-fail")) assert(!vmu_writes);
        if(!strcmp(mode,"restore-write-fail") || !strcmp(mode,"restore-data-corrupt") ||
           !strcmp(mode,"restore-cancel") || !strcmp(mode,"restore-remove") || !strcmp(mode,"restore-sd-write-fail")) {
            assert(!memcmp(card[254],original_card[254],512));
            assert(!memcmp(card[253],original_card[253],512));
        }
    }
    /* Existing save bytes and its directory entry remain identical through
     * every modeled failure. Metadata tearing is reported, never rolled back. */
    unsigned keep_first=!strcmp(mode,"restore-game")?2:0;
    assert(!memcmp(card[keep_first],original_card[keep_first],1024));
    if(strcmp(mode,"restore-dir-existing-corrupt")) assert(!memcmp(card[253],original_card[253],32));
    if(!strcmp(mode,"restore-remove")) assert(vmu_writes==1);
    fault=NULL;missing=false;mount_image(&fs);verify_preserved();assert(f_mount(NULL,"0:",0)==FR_OK);
}
static void test_managed(const char *mode) {
    bool copy=!strncmp(mode,"copy-",5);FATFS fs;struct kui_vmu_view out;
    uint8_t source_data[1024],source_entry[32];
    bool game=!strncmp(mode,"copy-game",9);
    if(game) {card[253][0]=0xcc;put16(card[253]+26,1);}
    memcpy(source_data,card[0],sizeof(source_data));memcpy(source_entry,card[253],32);
    if(copy) {
        memcpy(original_card,card,sizeof(card));setup(1);
        memset(card[253]+4,0,12);memcpy(card[253]+4,"OTHER_SAVE",10);card[0][10]^=0xa7;
        if(!strcmp(mode,"copy-game")) move_first_save(2);
        if(!strcmp(mode,"copy-game-fragmented-prefix")) move_first_save(1);
        if(!strcmp(mode,"copy-game-existing")) {card[253][0]=0xcc;put16(card[253]+26,1);}
        memcpy(destination_card,card,sizeof(card));memcpy(card,original_card,sizeof(card));two_cards=true;
        if(!strcmp(mode,"copy-existing")) memcpy(destination_card[253]+4,source_entry+4,12);
        if(!strcmp(mode,"copy-orphan")) put16(destination_card[254]+20,0xfffa);
        memcpy(original_destination,destination_card,sizeof(destination_card));
    }
    if(!strcmp(mode,"delete-orphan")) put16(card[254]+42,0xfffa);
    if(!strcmp(mode,"delete-nameless")) memset(card[253]+4,0,12);
    if(!strcmp(mode,"delete-nul-first")) card[253][4]=0;
    if(!strcmp(mode,"delete-invalid-protection")) card[253][1]=0x13;
    if(!strcmp(mode,"delete-duplicate")) memcpy(card[253]+36,card[253]+4,12);
    if(strcmp(mode,"delete-unlisted")) {
        kui_vmu_app_run(0,0,0,0,&out,log_line,cancelled,progress);assert(out.status.passed);
    }
    memcpy(original_card,card,sizeof(card));
    bool no_preview=!strcmp(mode,"delete-unpreviewed") || !strcmp(mode,"copy-unpreviewed");
    unsigned selected=!strcmp(mode,"delete-invalid-row")?8:0;
    unsigned target=!strcmp(mode,"copy-same-slot")?0:1;
    if(copy) kui_vmu_copy_run(0,0,selected,target,no_preview,&out,log_line,cancelled,progress);
    else kui_vmu_delete_run(0,0,selected,no_preview,&out,log_line,cancelled,progress);
    bool preview_fail=no_preview || !strcmp(mode,"delete-unlisted") || !strcmp(mode,"delete-invalid-row") ||
        !strcmp(mode,"delete-orphan") || !strcmp(mode,"delete-duplicate") || !strcmp(mode,"delete-nameless") ||
        !strcmp(mode,"delete-invalid-protection") || !strcmp(mode,"delete-nul-first") || !strcmp(mode,"copy-existing") ||
        !strcmp(mode,"copy-orphan") || !strcmp(mode,"copy-same-slot") ||
        !strcmp(mode,"copy-game-blocked-zero") || !strcmp(mode,"copy-game-fragmented-prefix") || !strcmp(mode,"copy-game-existing");
    if(preview_fail) {
        assert(!out.status.passed && !out.delete_ready && !out.copy_ready && out.status.errors && !vmu_writes && !connects);
        assert(!memcmp(card,original_card,sizeof(card)));
        if(copy) assert(!memcmp(destination_card,original_destination,sizeof(destination_card)));
        return;
    }
    assert(out.status.passed && !vmu_writes && !connects && (copy?out.copy_ready:out.delete_ready));
    if(!strcmp(mode,"delete-preview-only") || !strcmp(mode,"copy-preview-only")) return;
    if(!strcmp(mode,"delete-changed-card") || !strcmp(mode,"copy-changed-source")) card[255][48]^=1;
    if(!strcmp(mode,"delete-changed-payload") || !strcmp(mode,"copy-changed-payload")) card[0][19]^=1;
    if(!strcmp(mode,"delete-device-change")) active=1;
    if(!strcmp(mode,"copy-changed-destination")) destination_card[255][48]^=1;
    if(!strcmp(mode,"copy-removed-destination")) two_cards=false;
    fault=mode;
    if(copy) kui_vmu_copy_run(0,0,selected,target,true,&out,log_line,cancelled,progress);
    else kui_vmu_delete_run(0,0,selected,true,&out,log_line,cancelled,progress);
    assert(!out.delete_ready && !out.copy_ready);
    bool success=!strcmp(mode,"delete-ok") || !strcmp(mode,"delete-stop-commit") || !strcmp(mode,"delete-restore") || !strcmp(mode,"copy-ok") || !strcmp(mode,"copy-game");
    if(success) {
        assert(out.status.passed && out.status.complete && !out.status.errors && !out.status.stopped);
        assert(out.status.done==1024 && out.free_blocks==(copy?196:182));
        if(copy) {
            unsigned first=game?0:199,second=game?1:198;
            assert(!memcmp(destination_card[first],source_data,512) && !memcmp(destination_card[second],source_data+512,512));
            if(game) assert(destination_card[253][34]==0 && destination_card[253][35]==0 && destination_card[254][0]==1 && destination_card[254][1]==0 && destination_card[254][2]==0xfa && destination_card[254][3]==0xff);
            assert(!memcmp(destination_card[253]+36,source_entry+4,28));
        } else {
            uint8_t empty[32]={0};assert(!memcmp(card[253],empty,32));
            assert(card[254][0]==0xfc && card[254][1]==0xff && card[254][2]==0xfc && card[254][3]==0xff);
        }
    } else {
        assert(!out.status.passed && (out.status.errors || out.status.stopped));
        if(strstr(mode,"changed-") || strstr(mode,"device-change") || strstr(mode,"removed-destination") ||
           strstr(mode,"sd-write") || strstr(mode,"sd-sync") || strstr(mode,"readback-") || strstr(mode,"cancel-backup")) assert(!vmu_writes);
    }
    /* No source payload is ever erased; no unrelated payload/metadata is
     * intentionally changed. Injected physical directory corruption is an
     * explicit unverified outcome, not a rollback promise. */
    if(strcmp(mode,"delete-changed-payload") && strcmp(mode,"copy-changed-payload")) assert(!memcmp(card[0],source_data,sizeof(source_data)));
    assert(!memcmp(card[2],original_card[2],198*512));
    if(copy && strcmp(mode,"copy-changed-source") && strcmp(mode,"copy-changed-payload")) assert(!memcmp(card,original_card,sizeof(card)));
    if(copy) {unsigned keep_first=!strcmp(mode,"copy-game")?2:0;assert(!memcmp(destination_card[keep_first],original_destination[keep_first],2*512));}
    if(!vmu_writes && strcmp(mode,"delete-changed-card") && strcmp(mode,"delete-changed-payload"))
        assert(!memcmp(card,original_card,sizeof(card)) || copy);
    bool backed=out.backup_path[0]!=0;char backup[KUI_VMU_BACKUP_PATH_CAP];strcpy(backup,out.backup_path);
    fault=NULL;missing=false;mount_image(&fs);verify_preserved();
    if(backed) equal_file(backup,source_data,sizeof(source_data));
    assert(f_mount(NULL,"0:",0)==FR_OK);
    if(!strcmp(mode,"delete-restore")) {
        kui_vmu_restore_run(backup,0,false,&out,log_line,cancelled,progress);assert(out.status.passed && out.restore_ready);
        kui_vmu_restore_run(backup,0,true,&out,log_line,cancelled,progress);assert(out.status.passed && out.free_blocks==180);
        assert(!memcmp(card[199],source_data,512) && !memcmp(card[198],source_data+512,512));
    }
}
/* Bulk fixtures and fault injection exercise the independent batch API, not
 * a loop around the single-save preview. Every preview must be SD/read-only. */
static unsigned get16(const uint8_t *bytes) {return bytes[0]|((unsigned)bytes[1]<<8);}
static void verify_bulk_copy(unsigned index) {
    const uint8_t *source=card[253-index/16]+index%16*32;
    const uint8_t *destination=destination_card[253-(index+1)/16]+(index+1)%16*32;
    assert(!memcmp(source,destination,2) && !memcmp(source+4,destination+4,28));
    unsigned block=get16(destination+2);
    for(unsigned part=0;part<2;part++) {
        assert(block<200 && !memcmp(destination_card[block],card[index*2+part],512));
        block=get16(destination_card[254]+block*2);
    }
    assert(block==0xfffa);
}
static void test_bulk_copy(const char *mode) {
    FATFS fs;struct kui_vmu_view out;
    setup(!strcmp(mode,"bulk-empty-source")?0:3);
    bool game=!strncmp(mode,"bulk-game",9);
    if(game) {card[253][0]=0xcc;put16(card[253]+26,1);}
    memcpy(original_card,card,sizeof(card));
    unsigned destination_count=!strcmp(mode,"bulk-capacity")?98:!strcmp(mode,"bulk-directory-full")?16:1;
    setup(destination_count);
    for(unsigned i=0;i<destination_count;i++) {
        uint8_t *entry=card[253-i/16]+i%16*32;char name[16];
        memset(entry+4,0,12);snprintf(name,sizeof(name),"KEEP%02u",i);memcpy(entry+4,name,strlen(name));
    }
    card[0][10]^=0xa7;
    if(!strcmp(mode,"bulk-game")) move_first_save(2);
    if(!strcmp(mode,"bulk-game-fragmented-prefix")) move_first_save(1);
    if(!strcmp(mode,"bulk-game-existing")) {card[253][0]=0xcc;put16(card[253]+26,1);}
    if(!strcmp(mode,"bulk-directory-full")) put16(card[255]+76,1);
    memcpy(destination_card,card,sizeof(card));memcpy(card,original_card,sizeof(card));two_cards=true;
    if(!strcmp(mode,"bulk-conflict-last")) memcpy(destination_card[253]+4,card[253]+68,12);
    if(!strcmp(mode,"bulk-source-cycle")) put16(card[254],0);
    if(!strcmp(mode,"bulk-source-shared")) put16(card[253]+34,0);
    if(!strcmp(mode,"bulk-source-orphan")) put16(card[254]+40,0xfffa);
    if(!strcmp(mode,"bulk-destination-orphan")) put16(destination_card[254]+40,0xfffa);
    if(!strcmp(mode,"bulk-source-duplicate")) memcpy(card[253]+36,card[253]+4,12);
    if(!strcmp(mode,"bulk-destination-duplicate")) {
        memcpy(destination_card[253]+32,destination_card[253],32);
        put16(destination_card[253]+34,2);put16(destination_card[254]+4,3);put16(destination_card[254]+6,0xfffa);
    }
    if(!strcmp(mode,"bulk-source-nameless")) memset(card[253]+68,0,12);
    if(!strcmp(mode,"bulk-source-protection")) card[253][65]=0x13;
    if(!strcmp(mode,"bulk-source-removed")) missing=true;
    if(!strcmp(mode,"bulk-destination-removed")) two_cards=false;
    memcpy(original_card,card,sizeof(card));memcpy(original_destination,destination_card,sizeof(destination_card));
    unsigned source=!strcmp(mode,"bulk-invalid-source")?8:0;
    unsigned destination=!strcmp(mode,"bulk-invalid-destination")?8:!strcmp(mode,"bulk-same-slot")?0:1;
    bool unpreviewed=!strcmp(mode,"bulk-unpreviewed");
    if(!strcmp(mode,"bulk-vmu-read-fail")) fault=mode;
    unsigned writes_before=writes,connects_before=connects;
    kui_vmu_copy_all_run(source,destination,unpreviewed,&out,log_line,cancelled,progress);
    bool preview_fail=unpreviewed || !strcmp(mode,"bulk-same-slot") || !strcmp(mode,"bulk-invalid-source") ||
        !strcmp(mode,"bulk-invalid-destination") || !strcmp(mode,"bulk-conflict-last") || !strcmp(mode,"bulk-capacity") ||
        !strcmp(mode,"bulk-directory-full") || !strcmp(mode,"bulk-source-cycle") || !strcmp(mode,"bulk-source-shared") ||
        !strcmp(mode,"bulk-source-orphan") || !strcmp(mode,"bulk-destination-orphan") || !strcmp(mode,"bulk-source-duplicate") ||
        !strcmp(mode,"bulk-destination-duplicate") || !strcmp(mode,"bulk-source-nameless") || !strcmp(mode,"bulk-source-protection") ||
        !strcmp(mode,"bulk-source-removed") || !strcmp(mode,"bulk-destination-removed") || !strcmp(mode,"bulk-vmu-read-fail") || !strcmp(mode,"bulk-empty-source") ||
        !strcmp(mode,"bulk-game-blocked-zero") || !strcmp(mode,"bulk-game-fragmented-prefix") || !strcmp(mode,"bulk-game-existing");
    assert(!vmu_writes && writes==writes_before && connects==connects_before && !source_vmu_writes);
    assert(!memcmp(card,original_card,sizeof(card)) && !memcmp(destination_card,original_destination,sizeof(destination_card)));
    if(preview_fail) {
        assert(!out.status.passed && !out.copy_ready && out.status.errors && !out.bulk_completed && !out.bulk_unverified);return;
    }
    assert(out.status.passed && out.copy_ready && out.bulk_total==3 && !out.bulk_completed && out.bulk_remaining==3);
    assert(!out.bulk_unverified && out.status.total==3072 && !out.status.done);
    if(!strcmp(mode,"bulk-preview-only")) return;
    if(!strcmp(mode,"bulk-changed-source")) card[255][48]^=1;
    if(!strcmp(mode,"bulk-changed-payload-last")) card[4][19]^=1;
    if(!strcmp(mode,"bulk-changed-destination")) destination_card[255][48]^=1;
    if(!strcmp(mode,"bulk-source-device-change")) active=1;
    if(!strcmp(mode,"bulk-destination-device-change")) destination_active=3;
    if(!strcmp(mode,"bulk-source-removed-before-commit")) missing=true;
    if(!strcmp(mode,"bulk-destination-removed-before-commit")) two_cards=false;
    memcpy(original_card,card,sizeof(card));memcpy(original_destination,destination_card,sizeof(destination_card));
    fault=mode;reported_done=0;
    kui_vmu_copy_all_run(0,1,true,&out,log_line,cancelled,progress);
    bool success=!strcmp(mode,"bulk-ok") || !strcmp(mode,"bulk-game");
    unsigned expected_completed=success?3:strstr(mode,"after-one")?1:0;
    bool metadata_unverified=!strcmp(mode,"bulk-fat-fail") || !strcmp(mode,"bulk-fat-corrupt") ||
        !strcmp(mode,"bulk-dir-fail") || !strcmp(mode,"bulk-dir-corrupt") || !strcmp(mode,"bulk-final-corrupt") ||
        !strcmp(mode,"bulk-final-read-fail") || !strcmp(mode,"bulk-fat-fail-after-one") || !strcmp(mode,"bulk-dir-fail-after-one");
    assert(out.status.passed==success && !out.copy_ready && !out.restore_ready);
    assert(out.bulk_total==3 && out.bulk_completed==expected_completed && out.bulk_remaining==3-expected_completed);
    assert(out.bulk_unverified==metadata_unverified && out.status.done==(uint64_t)expected_completed*1024 && out.status.total==3072);
    assert(out.status.complete==success);
    if(!success) assert(out.status.errors || out.status.stopped);
    if(!strcmp(mode,"bulk-cancel-start") || !strcmp(mode,"bulk-cancel-data") || !strcmp(mode,"bulk-cancel-after-one")) assert(out.status.stopped);
    assert(!source_vmu_writes && !memcmp(card,original_card,sizeof(card)));
    unsigned keep_first=!strcmp(mode,"bulk-game")?2:0;
    assert(!memcmp(destination_card[keep_first],original_destination[keep_first],1024) && !memcmp(destination_card[253],original_destination[253],32));
    for(unsigned i=0;i<expected_completed;i++) verify_bulk_copy(i);
    if(success && game) assert(destination_card[253][34]==0 && destination_card[253][35]==0 && destination_card[254][0]==1 && destination_card[254][1]==0 && destination_card[254][2]==0xfa && destination_card[254][3]==0xff);
    if(success) assert(out.free_blocks==192);
    if(strstr(mode,"changed-") || strstr(mode,"device-change") || strstr(mode,"removed-before-commit") ||
       strstr(mode,"during-stage") || !strcmp(mode,"bulk-cancel-start") || strstr(mode,"sd-write") || strstr(mode,"sd-sync") || strstr(mode,"readback-")) {
        assert(!vmu_writes && !memcmp(destination_card,original_destination,sizeof(destination_card)));
    }
    if(metadata_unverified) {
        unsigned metadata_writes=strstr(mode,"fat-")?3:4;
        assert(injected && vmu_writes==expected_completed*4+metadata_writes);
    }
    /* A stopped or failed batch consumes its preview, preventing blind retry. */
    if(!success) {
        fault=NULL;missing=false;two_cards=true;
        unsigned before=vmu_writes;kui_vmu_copy_all_run(0,1,true,&out,log_line,cancelled,progress);
        assert(!out.status.passed && !out.copy_ready && out.status.errors && vmu_writes==before);
    }
    fault=NULL;missing=false;two_cards=true;mount_image(&fs);verify_preserved();
    /* Entire source batch is durably staged before any destination write. */
    if(vmu_writes) {assert(bulk_staging_checked);for(unsigned i=0;i<3;i++) verify_save(i);}
    assert(f_mount(NULL,"0:",0)==FR_OK);
}
static void test_import_publish(const char *mode) {
    FATFS fs;struct kui_vmu_view out;uint8_t entry[32],data[1024];
    memcpy(entry,card[253],32);memcpy(data,card[0],sizeof(data));
    size_t length=sizeof(data);
    if(!strcmp(mode,"publish-type")) entry[0]=0x17;
    if(!strcmp(mode,"publish-protection")) entry[1]=0x13;
    if(!strcmp(mode,"publish-first-block")) put16(entry+2,0xffff);
    if(!strcmp(mode,"publish-nameless")) memset(entry+4,0,12);
    if(!strcmp(mode,"publish-header")) put16(entry+26,2);
    if(!strcmp(mode,"publish-dirty")) entry[28]=1;
    if(!strcmp(mode,"publish-reserved")) entry[31]=1;
    if(!strcmp(mode,"publish-count")) put16(entry+24,1);
    if(!strcmp(mode,"publish-zero")) length=0;
    if(!strcmp(mode,"publish-short")) length=513;
    if(!strcmp(mode,"publish-large")) length=131072;
    bool invalid=!strcmp(mode,"publish-type") || !strcmp(mode,"publish-protection") ||
        !strcmp(mode,"publish-nameless") || !strcmp(mode,"publish-header") || !strcmp(mode,"publish-dirty") ||
        !strcmp(mode,"publish-reserved") || !strcmp(mode,"publish-count") || !strcmp(mode,"publish-zero") ||
        !strcmp(mode,"publish-short") || !strcmp(mode,"publish-large") || !strcmp(mode,"publish-null-entry") || !strcmp(mode,"publish-null-data");
    uint8_t original_entry[32];memcpy(original_entry,entry,sizeof(original_entry));
    mount_image(&fs);unsigned before=writes;
    fault=mode;
    if(!strcmp(mode,"publish-write-fail")) fault="write-fail";
    if(!strcmp(mode,"publish-sync-fail")) fault="sync-fail";
    if(!strcmp(mode,"publish-readback-fail")) fault="readback-fail";
    if(!strcmp(mode,"publish-readback-corrupt")) fault="readback-corrupt";
    if(!strcmp(mode,"publish-cancel-start")) fault="cancel-start";
    if(!strcmp(mode,"publish-cancel-write")) fault="cancel-write";
    bool result=kui_vmu_import_publish(!strcmp(mode,"publish-null-entry")?NULL:entry,
        !strcmp(mode,"publish-null-data")?NULL:data,length,&out,log_line,cancelled,progress);
    bool success=!strcmp(mode,"publish-ok") || !strcmp(mode,"publish-first-block");
    assert(result==success && out.status.passed==success && out.status.complete==success);
    assert(!reads && !enumerations && !vmu_writes && !connects && !disconnects && !locks);
    assert(!memcmp(entry,original_entry,sizeof(entry)));assert(!memcmp(data,card[0],sizeof(data)));
    if(invalid || !strcmp(mode,"publish-cancel-start")) assert(writes==before);
    if(!success) assert(out.status.errors || out.status.stopped);
    fault=NULL;
    if(!success) {assert(f_mount(NULL,"0:",0)==FR_OK);mount_image(&fs);}
    verify_preserved();
    FILINFO info;
    if(success) {
        assert(!strcmp(out.backup_path,"0:/KUI/backups/vmu/A1-0002/001_SAVE00.vms"));
        assert(out.status.done==1024 && out.status.total==1024);
        if(!strcmp(mode,"publish-first-block")) {
            equal_file("0:/KUI/backups/vmu/A1-0002/001_SAVE00.dir",entry,sizeof(entry));
            equal_file("0:/KUI/backups/vmu/A1-0002/001_SAVE00.vms",data,sizeof(data));
        } else verify_save(0);
        assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)==FR_OK);
    } else assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)!=FR_OK);
    assert(f_mount(NULL,"0:",0)==FR_OK);
    if(success) {
        setup(0);
        kui_vmu_restore_run("0:/KUI/backups/vmu/A1-0002/001_SAVE00.vms",0,false,&out,log_line,cancelled,progress);
        assert(out.status.passed && out.restore_ready && !vmu_writes);
        kui_vmu_restore_run("0:/KUI/backups/vmu/A1-0002/001_SAVE00.vms",0,true,&out,log_line,cancelled,progress);
        assert(out.status.passed && out.status.complete && out.status.done==1024);
        assert(!memcmp(card[199],data,512) && !memcmp(card[198],data+512,512));
        assert(!memcmp(card[253],entry,2) && !memcmp(card[253]+4,entry+4,28));
        assert(card[253][2]==199 && card[253][3]==0); /* Imported location is ignored; new allocation is used. */
    }
}
static const char *fixture="0:/KUI/backups/vmu/A1-0001/fixture.vmd";
static void description_header(uint8_t *bytes,const char *description) {
    memset(bytes,0,512);memset(bytes,' ',48);
    memcpy(bytes,"Short title",11);memcpy(bytes+16,description,strlen(description));
    memcpy(bytes+48,"TEST_APP",8);put16(bytes+72,64);
}
static void test_description_rows(const char *mode) {
    FATFS fs;struct kui_vmu_view out;setup(2);
    description_header(card[0],"  Long save title  ");
    description_header(card[2],""); /* Long field blank: use the short field. */
    bool fragmented=strstr(mode,"fragmented")!=NULL;
    if(fragmented) {
        /* Header block 1 follows FAT 0 -> 190; physical block 1 is unrelated. */
        description_header(card[1],"Wrong physical block");
        description_header(card[190],"Fragmented title");
        put16(card[253]+26,1);put16(card[254],190);put16(card[254]+2,0xfffc);put16(card[254]+380,0xfffa);
    }
    bool malformed=!strcmp(mode,"description-malformed");
    if(malformed) {put16(card[0]+64,4);put16(card[2]+72,2048);}
    memcpy(original_card,card,sizeof(card));
    bool image_mode=strstr(mode,"image")!=NULL;
    if(image_mode) {
        mount_image(&fs);write_file(fixture,card,sizeof(card));assert(f_mount(NULL,"0:",0)==FR_OK);
        reads=enumerations=0;unsigned before=writes;
        kui_vmu_image_run(fixture,0,0,0,&out,log_line,cancelled,progress);
        assert(!reads && !enumerations && writes==before);
    } else {
        if(!strcmp(mode,"description-read-fail")) fault=mode;
        kui_vmu_app_run(0,0,0,0,&out,log_line,cancelled,progress);
        assert(!connects && !writes);
    }
    assert(out.status.passed && out.status.complete && !out.status.errors && !out.status.stopped);
    assert(out.count==2 && out.total==2 && out.free_blocks==196 && !locks && !vmu_writes && !free_user_reads);
    assert(!strcmp(out.entries[0].name,"SAVE00") && out.entries[0].bytes==1024);
    if(malformed || is("description-read-fail")) {
        for(unsigned i=0;i<out.count;i++) assert(!out.entries[i].description[0] && !out.entries[i].app_id[0]);
    } else {
        assert(!strcmp(out.entries[0].description,fragmented?"Fragmented title":"Long save title"));
        assert(!strcmp(out.entries[0].app_id,"TEST_APP"));
        assert(!strcmp(out.entries[1].description,"Short title"));
    }
    assert(!memcmp(card,original_card,sizeof(card)));
    if(is("description-read-fail")) {
        /* The optional read failure must retain the checked listing identity. */
        assert(injected);fault=NULL;kui_vmu_app_run(1,0,0,0,&out,log_line,cancelled,progress);
        assert(out.status.passed && !out.status.errors && !vmu_writes && !locks);
        mount_image(&fs);verify_save(0);verify_preserved();assert(f_mount(NULL,"0:",0)==FR_OK);
    }
}
static void test_card_image(const char *mode) {
    FATFS fs;struct kui_vmu_view out;
    if(!strcmp(mode,"card-image-empty")) setup(0);
    memcpy(original_card,card,sizeof(card));
    kui_vmu_app_run(0,0,0,0,&out,log_line,cancelled,progress);assert(out.status.passed);
    fault=mode;
    if(!strcmp(mode,"card-image-read-fail")) fault="vmu-read-fail";
    if(!strcmp(mode,"card-image-write-fail")) fault="write-fail";
    if(!strcmp(mode,"card-image-sync-fail")) fault="sync-fail";
    if(!strcmp(mode,"card-image-readback-fail")) fault="readback-fail";
    if(!strcmp(mode,"card-image-readback-corrupt")) fault="readback-corrupt";
    if(!strcmp(mode,"card-image-cancel")) fault="cancel-read";
    kui_vmu_app_run(3,0,0,0,&out,log_line,cancelled,progress);
    bool success=!strcmp(mode,"card-image") || !strcmp(mode,"card-image-empty");
    assert(out.status.passed==success && !vmu_writes && !locks);
    if(!success) assert(out.status.errors || out.status.stopped);
    if(success) {
        assert(out.status.done==131072 && out.status.total==131072);
        assert(!strcmp(out.backup_path,"0:/KUI/backups/vmu/A1-0002/card.vmd"));
    }
    if(strcmp(mode,"card-image-changed")) assert(!memcmp(card,original_card,sizeof(card)));
    fault=NULL;missing=false;mount_image(&fs);FILINFO info;
    if(success) {
        equal_file("0:/KUI/backups/vmu/A1-0002/card.vmd",original_card,sizeof(original_card));
        FIL proof;UINT got;uint8_t bytes[32];
        assert(f_open(&proof,"0:/KUI/backups/vmu/A1-0002/card.vmd.crc",FA_READ)==FR_OK);
        assert(f_read(&proof,bytes,sizeof(bytes),&got)==FR_OK && got==32);
        assert(!memcmp(bytes,"KUIVMD1",8));assert(f_close(&proof)==FR_OK);
        assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)==FR_OK);
    } else assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)!=FR_OK);
    verify_preserved();assert(f_mount(NULL,"0:",0)==FR_OK);
}
static bool image_changed_midway;
static void image_progress(const struct kui_app_status *value) {
    progress(value);
    if((is("image-change-during") || is("image-partial")) && !image_changed_midway &&
       strstr(value->message,is("image-partial")?"Extracting 2/":"Extracting 1/")) {
        alter_file(fixture);image_changed_midway=true;injected=true;
    }
}
static void test_image_tools(const char *mode) {
    FATFS fs;struct kui_vmu_view out;struct kui_vmu_backup_view backups;
    if(!strcmp(mode,"image-uppercase")) fixture="0:/KUI/backups/vmu/A1-0001/FIXTURE.VMD";
    /* A mini-game with a displaced header, protection and timestamp proves
     * extraction retains the original metadata rather than manufacturing it. */
    card[253][0]=0xcc;card[253][1]=0xff;put16(card[253]+26,1);
    for(unsigned i=16;i<24;i++) card[253][i]=(uint8_t)(0x20+i);
    if(!strcmp(mode,"image-empty")) setup(0);
    if(!strcmp(mode,"image-cycle")) put16(card[254],0);
    if(!strcmp(mode,"image-range")) put16(card[254],200);
    if(!strcmp(mode,"image-shared")) put16(card[253]+34,0);
    if(!strcmp(mode,"image-short-chain")) put16(card[254]+2,2);
    if(!strcmp(mode,"image-layout")) put16(card[255]+76,65535);
    if(!strcmp(mode,"image-root")) card[255][3]=0;
    if(!strcmp(mode,"image-header")) put16(card[253]+26,2);
    if(!strcmp(mode,"image-dirty")) card[253][28]=1;
    if(!strcmp(mode,"image-protection")) card[253][1]=3;
    if(!strcmp(mode,"image-nameless")) card[253][4]=0;
    memcpy(original_card,card,sizeof(card));
    mount_image(&fs);size_t size=sizeof(card);
    if(!strcmp(mode,"image-truncated")) --size;
    write_file(fixture,card,size);
    if(!strcmp(mode,"image-proof-good") || !strcmp(mode,"image-proof-bad") || !strcmp(mode,"image-proof-mismatch")) {
        uint8_t proof[32]={0};memcpy(proof,"KUIVMD1",7);
        const uint32_t values[]={131072,kui_crc32(0,card,sizeof(card))};
        for(unsigned i=0;i<2;i++) for(unsigned j=0;j<4;j++) proof[8+i*4+j]=(uint8_t)(values[i]>>(j*8));
        uint32_t crc=kui_crc32(0,proof,28);for(unsigned j=0;j<4;j++) proof[28+j]=(uint8_t)(crc>>(j*8));
        if(!strcmp(mode,"image-proof-bad")) proof[28]^=1;
        write_file("0:/KUI/backups/vmu/A1-0001/fixture.vmd.crc",proof,sizeof(proof));
        if(!strcmp(mode,"image-proof-mismatch")) alter_file(fixture);
    }
    if(!strcmp(mode,"image-oversized")) {
        FIL file;UINT n;assert(f_open(&file,fixture,FA_WRITE|FA_OPEN_EXISTING)==FR_OK);
        assert(f_lseek(&file,sizeof(card))==FR_OK);assert(f_write(&file,"X",1,&n)==FR_OK && n==1);
        assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
    }
    assert(f_mount(NULL,"0:",0)==FR_OK);payload_seen=false;reads=enumerations=media_reads=0;
    unsigned before=writes;
    if(!strcmp(mode,"image-unlisted")) {
        kui_vmu_image_run(fixture,0,0,1,&out,log_line,cancelled,image_progress);
        assert(!out.status.passed && out.status.errors && writes==before && !reads && !enumerations && !vmu_writes);return;
    }
    fault=mode;kui_vmu_image_run(fixture,!strcmp(mode,"image-page") || !strcmp(mode,"image-read-fail")?1:0,0,0,&out,log_line,cancelled,image_progress);
    bool malformed=!strcmp(mode,"image-cycle") || !strcmp(mode,"image-range") || !strcmp(mode,"image-shared") ||
        !strcmp(mode,"image-short-chain") || !strcmp(mode,"image-layout") || !strcmp(mode,"image-root") ||
        !strcmp(mode,"image-header") || !strcmp(mode,"image-dirty") || !strcmp(mode,"image-protection") ||
        !strcmp(mode,"image-nameless") || !strcmp(mode,"image-truncated") || !strcmp(mode,"image-oversized") ||
        !strcmp(mode,"image-read-fail") || !strcmp(mode,"image-read-corrupt") || !strcmp(mode,"image-cancel-read") ||
        !strcmp(mode,"image-proof-bad") || !strcmp(mode,"image-proof-mismatch");
    assert(out.status.passed==!malformed && writes==before && !reads && !enumerations && !vmu_writes && !locks);
    if(malformed) {
        assert(out.status.errors || out.status.stopped);
        if(!strcmp(mode,"image-read-fail")) assert(out.page==1);
        fault=NULL;return;
    }
    assert(out.present && out.total==(!strcmp(mode,"image-empty")?0u:10u));
    if(!strcmp(mode,"image-page")) assert(out.page==1 && out.count==2 && !strcmp(out.entries[0].name,"SAVE08"));
    if(!strcmp(mode,"image-browse") || !strcmp(mode,"image-page") || !strcmp(mode,"image-empty") || !strcmp(mode,"image-proof-good") || !strcmp(mode,"image-uppercase")) {
        fault=NULL;kui_vmu_backups_run(0,&backups,log_line,cancelled,progress);
        assert(backups.status.passed && backups.total==1 && !strcmp(backups.entries[0].path,fixture));return;
    }
    if(!strcmp(mode,"image-stale")) {fault=NULL;mount_image(&fs);alter_file(fixture);assert(f_mount(NULL,"0:",0)==FR_OK);}
    fault=mode;
    if(!strcmp(mode,"image-write-fail")) fault="write-fail";
    if(!strcmp(mode,"image-sync-fail")) fault="sync-fail";
    if(!strcmp(mode,"image-readback-fail")) fault="readback-fail";
    if(!strcmp(mode,"image-readback-corrupt")) fault="readback-corrupt";
    if(!strcmp(mode,"image-cancel-start")) fault="cancel-start";
    if(!strcmp(mode,"image-cancel-write")) fault="cancel-write";
    kui_vmu_image_run(!strcmp(mode,"image-invalid-path")?"0:/KUI/backups/vmu/A1-0001/../fixture.vmd":fixture,
        !strcmp(mode,"image-selected-page")?1:!strcmp(mode,"image-invalid-page")?UINT32_MAX:0,
        !strcmp(mode,"image-invalid-row")?8:0,!strcmp(mode,"image-extract-all") || !strcmp(mode,"image-partial")?2:1,
        &out,log_line,cancelled,image_progress);
    bool success=!strcmp(mode,"image-extract-selected") || !strcmp(mode,"image-selected-page") || !strcmp(mode,"image-extract-all");
    assert(out.status.passed==success && !reads && !enumerations && !vmu_writes && !locks);
    assert(!memcmp(card,original_card,sizeof(card)));
    if(!success) assert(out.status.errors || out.status.stopped);
    fault=NULL;mount_image(&fs);verify_preserved();FILINFO info;
    if(success) {
        if(!strcmp(mode,"image-extract-all")) for(unsigned i=0;i<10;i++) verify_save(i);
        else verify_save(!strcmp(mode,"image-selected-page")?8:0);
        assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)==FR_OK);
        assert(out.status.done==out.status.total);
        /* Existing restore verifies the same bundle's metadata and CRC proof. */
    } else {
        assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)!=FR_OK);
        if(!strcmp(mode,"image-partial")) {
            verify_save(0);assert(f_stat("0:/KUI/backups/vmu/A1-0002/001_SAVE00.crc",&info)==FR_OK);
            assert(f_stat("0:/KUI/backups/vmu/A1-0002/002_SAVE01.crc",&info)!=FR_OK);
        }
    }
    if(strcmp(mode,"image-stale") && strcmp(mode,"image-change-during") && strcmp(mode,"image-partial"))
        equal_file(fixture,original_card,sizeof(original_card));
    assert(f_mount(NULL,"0:",0)==FR_OK);
    if(success) {
        const char *bundle=!strcmp(mode,"image-selected-page")?"0:/KUI/backups/vmu/A1-0002/009_SAVE08.vms":
            "0:/KUI/backups/vmu/A1-0002/001_SAVE00.vms";
        unsigned index=!strcmp(mode,"image-selected-page")?8:0;
        uint8_t expected_data[1024],expected_entry[32];
        memcpy(expected_data,original_card[index*2],sizeof(expected_data));
        memcpy(expected_entry,original_card[253-index/16]+(index%16)*32,32);
        setup(0);missing=false;
        kui_vmu_restore_run(bundle,0,false,&out,log_line,cancelled,progress);
        assert(out.status.passed && out.restore_ready && !vmu_writes);
        kui_vmu_restore_run(bundle,0,true,&out,log_line,cancelled,progress);
        assert(out.status.passed && out.status.complete && vmu_writes);
        unsigned first=expected_entry[0]==0xcc?0:199,second=expected_entry[0]==0xcc?1:198;
        assert(!memcmp(card[first],expected_data,512) && !memcmp(card[second],expected_data+512,512));
        assert(!memcmp(card[253],expected_entry,2) && !memcmp(card[253]+4,expected_entry+4,28));
        /* Roundtrip uses existing restore; image tools above proved zero physical calls. */
    }
}

/* Production collection admission uses a complete standard card, whereas the
 * older physical-only fixture intentionally leaves system FAT bytes opaque. */
static void collection_setup(unsigned count) {
    setup(count);
    for(unsigned b=200;b<256;b++)
        put16(card[254]+b*2,b<241?0xfffc:b>241 && b<=253?b-1:0xfffa);
    for(unsigned b=count*2;b<200;b++) memset(card[b],0x9c,512);
}
static void test_collection_adapter(const char *mode) {
    struct kui_vmu_view source,out;struct kui_vmu_backup_view catalog;FATFS fs;
    bool all=strcmp(mode,"collection-selected-page")!=0;
    unsigned page=all?0u:1u,selected=all?0u:1u;
    collection_setup(10);
    if(!strcmp(mode,"collection-game")) {card[253][0]=0xcc;put16(card[253]+26,1);}
    if(!strcmp(mode,"collection-orphan")) put16(card[254]+100*2,0xfffa);
    if(!strcmp(mode,"collection-system-fat")) put16(card[254]+250*2,0xfffc);
    if(!strcmp(mode,"collection-root-geometry")) card[255][16]=2;
    if(!strcmp(mode,"collection-sort-flag")) card[255][0x4f]=1;
    if(!strcmp(mode,"collection-user-240")) put16(card[255]+80,240);
    if(!strcmp(mode,"collection-relocated-metadata")) {
        memcpy(card[240],card[254],512);put16(card[255]+70,240);
        memcpy(card[230],card[253],512);memset(card[253],0,512);put16(card[255]+74,230);
    }
    if(!strcmp(mode,"collection-inactive-padding")) memset(card[253]+11u*32u+29u,0x9c,3u);
    if(!strcmp(mode,"collection-active-reserved")) card[253][31]=0x9c;
    if(!strcmp(mode,"collection-control-name")) card[253][4]=1;
    if(!strcmp(mode,"collection-over-capacity")) {
        collection_setup(0);put16(card[255]+80,240);
        card[253][0]=0x33;memcpy(card[253]+4,"LARGE",5);put16(card[253]+24,201);
        for(unsigned b=0;b<201;b++) put16(card[254]+b*2,b==200?0xfffa:b+1u);
    }
    collection_read_guard=true;
    memcpy(original_card,card,sizeof(card));
    if(strcmp(mode,"collection-unlisted") && strcmp(mode,"collection-unpreviewed") && strcmp(mode,"collection-invalid-slot")) {
        kui_vmu_app_run(0,0,page,0,&source,log_line,cancelled,progress);assert(source.status.passed);
    }
    reads=free_user_reads=0;
    if(!strcmp(mode,"collection-unpreviewed")) {
        kui_vmu_collection_copy_run(0,page,selected,all,true,&out,log_line,cancelled,progress);
        assert(!out.status.passed && out.status.errors && !reads && !connects && !writes && !locks);return;
    }
    if(!strcmp(mode,"collection-short-block")) fault="short-block";
    if(!strcmp(mode,"collection-oversize-block")) fault="oversize-block";
    if(!strcmp(mode,"collection-read-fail")) fault="vmu-read-fail";
    if(!strcmp(mode,"collection-capture-read-fail")) fault=mode;
    if(!strcmp(mode,"collection-cancel-read")) fault="cancel-read";
    if(!strcmp(mode,"collection-invalid-page")) {page=UINT32_MAX;all=false;}
    if(!strcmp(mode,"collection-invalid-row")) {selected=8u;all=false;}
    kui_vmu_collection_copy_run(!strcmp(mode,"collection-invalid-slot")?8u:0u,page,selected,all,false,&out,log_line,cancelled,progress);
    bool preview_bad=!strcmp(mode,"collection-unlisted") || !strcmp(mode,"collection-invalid-slot") ||
        !strcmp(mode,"collection-short-block") || !strcmp(mode,"collection-oversize-block") ||
        !strcmp(mode,"collection-read-fail") || !strcmp(mode,"collection-cancel-read") ||
        !strcmp(mode,"collection-invalid-page") || !strcmp(mode,"collection-invalid-row") ||
        !strcmp(mode,"collection-orphan") || !strcmp(mode,"collection-active-reserved") ||
        !strcmp(mode,"collection-control-name") || !strcmp(mode,"collection-over-capacity") ||
        !strcmp(mode,"collection-capture-read-fail");
    assert(!connects && !writes && !vmu_writes && !locks && !free_user_reads && !collection_unowned_reads);
    if(preview_bad) {
        assert(!out.status.passed && !out.copy_ready && (out.status.errors || out.status.stopped));
        if(!strcmp(mode,"collection-orphan"))
            assert(strstr(out.status.message,"unowned allocated blocks"));
        if(!strcmp(mode,"collection-active-reserved"))
            assert(strstr(out.status.message,"Source entry 0 rejected:") && strstr(out.status.message,"0000009c"));
        if(!strcmp(mode,"collection-control-name"))
            assert(strstr(out.status.message,"Source entry 0 rejected: invalid or duplicate filename"));
        if(!strcmp(mode,"collection-over-capacity"))
            assert(strstr(out.status.message,"need 201 blocks"));
        if(!strcmp(mode,"collection-capture-read-fail")) {
            assert(injected && reads==rejection_read_count && out.status.errors==1u);
            assert(!strcmp(out.status.message,"VMU block read failed"));
        }
        assert(!memcmp(card,original_card,sizeof(card)));return;
    }
    assert(out.status.passed && out.status.complete && out.copy_ready && !out.status.errors && !out.status.stopped);
    assert(out.slot==0 && out.page==page && out.bulk_total==(all?10u:1u) && out.bulk_remaining==out.bulk_total && !out.bulk_completed);
    assert(out.free_blocks==(all?180u:198u) && out.status.total==(all?10240u:1024u));
    if(!all) assert(out.count==1 && !strcmp(out.entries[0].name,"SAVE09"));
    if(!strcmp(mode,"collection-preview-only")) return;
    if(!strcmp(mode,"collection-changed-payload")) {card[0][13]^=1;memcpy(original_card,card,sizeof(card));}
    if(!strcmp(mode,"collection-changed-metadata")) {card[255][48]^=1;memcpy(original_card,card,sizeof(card));}
    if(!strcmp(mode,"collection-device-changed")) active=1;
    if(!strcmp(mode,"collection-source-removed")) missing=true;
    if(!strcmp(mode,"collection-cancel-publish")) fault="cancel-write";
    if(!strcmp(mode,"collection-write-fail")) fault="write-fail";
    if(!strcmp(mode,"collection-sync-fail")) fault="sync-fail";
    if(!strcmp(mode,"collection-readback-corrupt")) fault="readback-corrupt";
    kui_vmu_collection_copy_run(0,page,selected,all,true,&out,log_line,cancelled,progress);
    bool success=!strcmp(mode,"collection-all") || !strcmp(mode,"collection-selected-page") || !strcmp(mode,"collection-game") ||
        !strcmp(mode,"collection-commit-twice") || !strcmp(mode,"collection-system-fat") ||
        !strcmp(mode,"collection-root-geometry") || !strcmp(mode,"collection-user-240") ||
        !strcmp(mode,"collection-relocated-metadata") || !strcmp(mode,"collection-inactive-padding") ||
        !strcmp(mode,"collection-sort-flag");
    assert(out.status.passed==success && !out.copy_ready && !vmu_writes && !source_vmu_writes && !locks && !free_user_reads && !collection_unowned_reads);
    assert(!memcmp(card,original_card,sizeof(card)));
    if(!success) {
        assert(out.status.errors || out.status.stopped);
        if(!strcmp(mode,"collection-changed-payload") || !strcmp(mode,"collection-changed-metadata") ||
           !strcmp(mode,"collection-device-changed") || !strcmp(mode,"collection-source-removed")) assert(!connects && !writes);
        else assert(injected);
        return;
    }
    assert(out.bulk_completed==(all?10u:1u) && !out.bulk_remaining && out.status.done==out.status.total);
    assert(!strcmp(out.backup_path,"0:/KUI/vmu/cards/v0001/card.vmd"));
    assert(strstr(out.status.message,"activation pending"));
    unsigned before_writes=writes,before_reads=reads,before_enumerations=enumerations;
    if(!strcmp(mode,"collection-commit-twice")) {
        kui_vmu_collection_copy_run(0,page,selected,all,true,&out,log_line,cancelled,progress);
        assert(!out.status.passed && out.status.errors && reads==before_reads && writes==before_writes);
    }
    kui_vmu_collection_run(0,&catalog,log_line,cancelled,progress);
    assert(catalog.status.passed && catalog.count==1u && catalog.total==1u);
    assert(!strcmp(catalog.entries[0].path,"0:/KUI/vmu/cards/v0001/card.vmd"));
    assert(catalog.entries[0].bytes==(all?10240u:1024u));
    assert(reads==before_reads && enumerations==before_enumerations && writes==before_writes && !vmu_writes);
    mount_image(&fs);
    struct kui_vmu_collection_card verified;
    assert(!kui_vmu_collection_verify(catalog.entries[0].path,&verified,log_line,cancelled));
    assert(verified.summary.files==(all?10u:1u) && verified.summary.free_blocks==(all?180u:198u));
    uint8_t *captured=malloc(131072u);assert(captured);FIL file;UINT got;
    assert(f_open(&file,catalog.entries[0].path,FA_READ)==FR_OK && f_size(&file)==131072u);
    assert(f_read(&file,captured,131072u,&got)==FR_OK && got==131072u && f_close(&file)==FR_OK);
    bool used[200]={false};unsigned dir=card[255][74]|(unsigned)card[255][75]<<8;
    unsigned fat=card[255][70]|(unsigned)card[255][71]<<8;
    for(unsigned i=0;i<(all?10u:1u);i++) {
        const uint8_t *entry=captured+(253u-i/16u)*512u+(i%16u)*32u;
        unsigned original=all?i:9u;const uint8_t *source_entry=card[dir-original/16u]+(original%16u)*32u;
        assert(!memcmp(entry,source_entry,2u) && !memcmp(entry+4,source_entry+4,28u));
        unsigned target=entry[2]|(unsigned)entry[3]<<8,physical=source_entry[2]|(unsigned)source_entry[3]<<8;
        for(unsigned n=0;n<2u;n++) {
            assert(target<200u && !used[target]);used[target]=true;
            assert(!memcmp(captured+target*512u,card[physical],512u));
            target=captured[254u*512u+target*2u]|(unsigned)captured[254u*512u+target*2u+1u]<<8;
            physical=card[fat][physical*2u]|(unsigned)card[fat][physical*2u+1u]<<8;
        }
        assert(target==0xfffau && physical==0xfffau);
    }
    for(unsigned b=0;b<200;b++) if(!used[b]) for(unsigned j=0;j<512;j++) assert(!captured[b*512u+j]);
    for(unsigned i=all?10u:1u;i<208u;i++) {
        const uint8_t *entry=captured+(253u-i/16u)*512u+(i%16u)*32u;
        for(unsigned j=0;j<32u;j++) assert(!entry[j]);
    }
    free(captured);verify_preserved();assert(f_mount(NULL,"0:",0)==FR_OK);
    kui_vmu_image_run(catalog.entries[0].path,0,0,0,&out,log_line,cancelled,progress);
    assert(out.status.passed && out.present && out.total==(all?10u:1u));
    assert(reads==before_reads && enumerations==before_enumerations && writes==before_writes && !vmu_writes && !locks);
}

int main(int argc,char **argv) {
    assert(argc==3);image=fopen(argv[1],"r+b");assert(image);
    struct stat st;assert(!fstat(fileno(image),&st));image_blocks=(uint64_t)st.st_size/512;
    const char *mode=argv[2];FATFS fs;setup(10);
    if(!strcmp(mode,"seed")) {
        mount_image(&fs);assert(kui_destination_mkdirs("/KUI/backups/vmu/A1-0001",log_line));
        assert(kui_destination_mkdirs("/KUI/dumps",log_line));
        write_file(sentinel_path,sentinel,sizeof(sentinel));write_file("0:/KUI/dumps/keep.bin",sentinel,sizeof(sentinel));
        assert(f_mount(NULL,"0:",0)==FR_OK);fclose(image);puts("PASS VMU seed");return 0;
    }
    if(!strncmp(mode,"description-",12)) {
        test_description_rows(mode);assert(!locks);fclose(image);printf("PASS VMU %s\n",mode);return 0;
    }
    if(!strncmp(mode,"collection-",11)) {
        test_collection_adapter(mode);assert(!locks);fclose(image);printf("PASS VMU %s\n",mode);return 0;
    }
    if(!strncmp(mode,"card-image",10) || !strncmp(mode,"image-",6)) {
        if(!strncmp(mode,"card-image",10)) test_card_image(mode);else test_image_tools(mode);
        assert(!locks);fclose(image);printf("PASS VMU %s\n",mode);return 0;
    }
    if(!strncmp(mode,"publish-",8)) {
        test_import_publish(mode);assert(!locks);fclose(image);printf("PASS VMU %s\n",mode);return 0;
    }
    if(!strncmp(mode,"bulk-",5)) {
        test_bulk_copy(mode);assert(!locks);fclose(image);printf("PASS VMU %s\n",mode);return 0;
    }
    if(!strncmp(mode,"copy-game-source-",17)) {
        struct kui_vmu_view source;card[253][0]=0xcc;put16(card[253]+26,1);
        if(!strcmp(mode,"copy-game-source-nonzero")) move_first_save(20);
        if(!strcmp(mode,"copy-game-source-fragmented")) {
            memcpy(card[20],card[1],512);put16(card[254],20);put16(card[254]+2,0xfffc);put16(card[254]+40,0xfffa);
        }
        if(!strcmp(mode,"copy-game-source-multiple")) {card[253][32]=0xcc;put16(card[253]+58,1);}
        memcpy(original_card,card,sizeof(card));
        kui_vmu_app_run(0,0,0,0,&source,log_line,cancelled,progress);
        assert(!source.status.passed && source.status.errors && !connects && !writes && !vmu_writes && !locks);
        assert(!memcmp(card,original_card,sizeof(card)));
        fclose(image);printf("PASS VMU %s\n",mode);return 0;
    }
    if(!strncmp(mode,"delete-",7) || !strncmp(mode,"copy-",5)) {
        test_managed(mode);assert(!locks);fclose(image);printf("PASS VMU %s\n",mode);return 0;
    }
    if(!strncmp(mode,"restore-",8)) {
        test_restore(mode);assert(!locks);fclose(image);printf("PASS VMU %s\n",mode);return 0;
    }
    struct kui_vmu_view out;
    if(!strcmp(mode,"slot-d2")) {source_slot=7;devices[0].port=3;devices[0].unit=2;}
    if(!strcmp(mode,"missing")) missing=true;
    if(!strcmp(mode,"empty")) setup(0);
    if(!strcmp(mode,"cycle")) put16(card[254],0);
    if(!strcmp(mode,"range")) put16(card[254],240);
    if(!strcmp(mode,"root-layout")) put16(card[255]+76,65535);
    if(!strcmp(mode,"duplicate-chain")) put16(card[253]+32+2,0);
    if(!strcmp(mode,"short-chain")) put16(card[254]+2,3);
    if(!strcmp(mode,"bad-header")) put16(card[253]+26,2);
    bool list_only=!strcmp(mode,"missing") || !strcmp(mode,"empty") || !strcmp(mode,"list") ||
        !strcmp(mode,"slot-d2") || !strcmp(mode,"invalid-slot") ||
        !strcmp(mode,"cycle") || !strcmp(mode,"range") || !strcmp(mode,"root-layout") ||
        !strcmp(mode,"duplicate-chain") || !strcmp(mode,"short-chain") || !strcmp(mode,"bad-header") ||
        !strcmp(mode,"vmu-read-fail") || !strcmp(mode,"short-block") || !strcmp(mode,"oversize-block") ||
        !strcmp(mode,"device-change") || !strcmp(mode,"cancel-start") || !strcmp(mode,"cancel-read");
    if(list_only) {
        fault=mode;kui_vmu_app_run(0,!strcmp(mode,"invalid-slot")?8:source_slot,1,0,&out,log_line,cancelled,progress);
        assert(!connects && !writes && !locks);
        if(!strcmp(mode,"list") || !strcmp(mode,"slot-d2")) {
            assert(out.status.passed && out.present && out.total==10 && out.free_blocks==180 && out.count==2 && out.page==1);
            assert(!strcmp(out.entries[0].name,"SAVE08") && out.entries[0].bytes==1024);
        } else if(!strcmp(mode,"empty")) assert(out.status.passed && out.present && !out.count && out.free_blocks==200);
        else {assert(!out.status.passed && !out.status.complete);assert(out.status.errors || out.status.stopped);}
    } else {
        if(!strcmp(mode,"unsafe-name")) memset(card[253]+4,'/',12);
        if(!strcmp(mode,"game-save")) {card[253][0]=0xcc;put16(card[253]+26,1);}
        if(strcmp(mode,"unlisted")) {
            kui_vmu_app_run(0,0,0,0,&out,log_line,cancelled,progress);assert(out.status.passed);
        }
        if(!strcmp(mode,"stale-list")) card[253][4]='X';
        fault=mode;reads=0;
        kui_vmu_app_run(!strcmp(mode,"backup-all")?2:1,0,!strcmp(mode,"invalid-page")?UINT32_MAX:!strcmp(mode,"selected-page")?1:0,
            !strcmp(mode,"invalid-row")?8:0,&out,log_line,cancelled,progress);
        assert(!locks && connects==disconnects+(!strcmp(mode,"connect-fail")?1u:0u));
        bool success=!strcmp(mode,"backup-all") || !strcmp(mode,"backup-selected") || !strcmp(mode,"selected-page") ||
            !strcmp(mode,"unsafe-name") || !strcmp(mode,"game-save");
        if(success) {
            assert(out.status.complete && out.status.passed && !out.status.errors && !out.status.stopped);
            assert(out.status.done==out.status.total && out.status.done==(!strcmp(mode,"backup-all")?10240u:1024u));
        } else assert(!out.status.passed && (out.status.errors || out.status.stopped));
        if(!strcmp(mode,"stale-list") || !strcmp(mode,"invalid-row") || !strcmp(mode,"invalid-page") || !strcmp(mode,"unlisted")) assert(!connects && !writes);
        if(!strcmp(mode,"stale-list")) {
            /* Repeating Backup must not bypass the required refreshed view. */
            kui_vmu_app_run(1,0,0,0,&out,log_line,cancelled,progress);
            assert(!out.status.passed && out.status.errors && !connects && !writes);
        }
        if(!strcmp(mode,"contents-change")) assert(injected && !payload_seen);
        if(!strcmp(mode,"write-fail") || !strcmp(mode,"sync-fail") || !strcmp(mode,"readback-fail") ||
           !strcmp(mode,"readback-corrupt") || !strcmp(mode,"cancel-write") || !strcmp(mode,"connect-fail")) assert(injected);
        fault=NULL;mount_image(&fs);
        if(success) {
            if(!strcmp(mode,"unsafe-name")) {
                char base[17],path[160];memcpy(base,"001_",4);memset(base+4,'_',12);base[16]=0;
                uint8_t bytes[1024];memcpy(bytes,card[0],512);memcpy(bytes+512,card[1],512);
                snprintf(path,sizeof(path),"0:/KUI/backups/vmu/A1-0002/%s.vms",base);equal_file(path,bytes,sizeof(bytes));
                snprintf(path,sizeof(path),"0:/KUI/backups/vmu/A1-0002/%s.dir",base);equal_file(path,card[253],32);
            } else if(!strcmp(mode,"backup-all")) for(unsigned i=0;i<10;i++) verify_save(i);
            else verify_save(!strcmp(mode,"selected-page")?8:0);
            FILINFO info;assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)==FR_OK);
        } else {
            FILINFO info;assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)!=FR_OK);
            assert(f_stat("0:/KUI/backups/vmu/A1-0002/001_SAVE00.vms",&info)!=FR_OK);
        }
        verify_preserved();assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    fault=NULL;assert(!fclose(image));printf("PASS VMU %s\n",mode);return 0;
}
