/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual receipt state machine plus the actual launch coordinator and disk
 * epoch. The inherited byte-backed receipts mock the expensive checkpoint;
 * real image/journal/write-cut coverage remains in test_vmu_history.py. */
#define main inherited_history_main
#define f_stat receipt_stat
#define f_open receipt_open
#define f_read receipt_read
#define f_write receipt_write
#define f_sync receipt_sync
#define f_close receipt_close
#include "test_vmu_history_session_fast.c"
#undef main
#undef f_stat
#undef f_open
#undef f_read
#undef f_write
#undef f_sync
#undef f_close
#include "kui/media.h"
#include "diskio.h"

static FIL *identity_file;
static uint8_t current_identity[16];
static bool identity_error,io_error;
static unsigned identity_reads;
uint32_t kui_clock_fattime(void) {return 0u;}
static uint64_t device_blocks(void *unused) {(void)unused;return 100000u;}
static int device_read(void *unused,uint32_t first,size_t count,uint8_t *out) {
    (void)unused;if(io_error)return -1;
    memset(out,0,count*512u);
    if(first==0u) {
        out[510]=0x55u;out[511]=0xaau;out[450]=0x0cu;
        put32(out+454u,2048u);put32(out+458u,100000u-2048u);
    }
    return 0;
}
static int device_write(void *unused,uint32_t first,size_t count,const uint8_t *in) {
    (void)unused;(void)first;(void)count;(void)in;return io_error?-1:0;
}
static int device_sync(void *unused) {(void)unused;return io_error?-1:0;}
static const struct kui_media_ops device={NULL,device_blocks,device_read,device_write,device_sync};
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(!identity_file && mode==FA_READ);
    assert(strstr(path,"/identity.bin"));
    if(identity_error)return FR_DISK_ERR;
    memset(file,0,sizeof(*file));file->obj.objsize=16u;identity_file=file;return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT count,UINT *got) {
    assert(identity_file==file && count==16u);++identity_reads;
    memcpy(out,current_identity,16u);*got=16u;return FR_OK;
}
FRESULT f_close(FIL *file) {assert(file==identity_file);identity_file=NULL;return FR_OK;}
int kui_vmu_history_resume_mounted(struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    struct receipt previous;assert(!load(&previous,cancel));
    int result=kui_vmu_history_session_resume(99u,&platform,out,log,cancel);
    /* The inherited checkpoint mock omits view.card; production apply sets
     * it from its exact request. Supply that existing result contract here. */
    if(result>=0 && out->known)out->card=previous.card;
    return result;
}
static void fresh(void) {
    assert(!identity_file);fixture(true);io_error=identity_error=false;identity_reads=0u;
    memcpy(current_identity,card.identity,16u);
    kui_media_set(&device);assert(!disk_initialize(0u));
    kui_vmu_launch_history_request(true);
    kui_vmu_launch_history_begin_mounted(logger,cancelled);
    assert(kui_vmu_launch_history_performed() && applies==1u && writes==1u && allocations==1u);
}
static void fallback(void) {
    struct kui_vmu_history_view view;
    assert(!begin(&view) && applies==2u && allocations==2u && writes==2u);
    assert(reason[1]==KUI_VMU_HISTORY_BEFORE_LAUNCH);
    kui_vmu_launch_history_end();
}
int main(void) {
    struct kui_vmu_history_view view;struct receipt latest;uint8_t buffer[512]={0},baseline[512];
    /* Original ordering and the new scoped ordering publish identical receipt
     * generations, with one less full checkpoint scan on the unchanged card. */
    fixture(true);assert(!kui_vmu_history_session_resume(99u,&platform,&view,logger,cancelled));
    assert(!begin(&view));assert(!load(&latest,cancelled));
    assert(applies==2u && allocations==2u && writes==2u && latest.pending);
    memcpy(baseline,files[latest.slot].wire,512u);
    fresh();assert(!begin(&view));assert(!load(&latest,cancelled));
    assert(applies==1u && allocations==2u && writes==2u && identity_reads==1u);
    assert(view.known && !view.changed && latest.pending && !memcmp(baseline,files[latest.slot].wire,512u));
    kui_vmu_launch_history_end();

    fresh();assert(disk_read(0u,buffer,1u,1u)==RES_OK);assert(disk_ioctl(0u,CTRL_SYNC,NULL)==RES_OK);
    assert(!begin(&view) && applies==1u);kui_vmu_launch_history_end();
    fresh();assert(disk_write(0u,buffer,1u,1u)==RES_OK);fallback();
    fresh();assert(disk_write(1u,buffer,1u,1u)==RES_PARERR);fallback();
    fresh();io_error=true;assert(disk_read(0u,buffer,1u,1u)==RES_ERROR);io_error=false;fallback();
    fresh();io_error=true;assert(disk_ioctl(0u,CTRL_SYNC,NULL)==RES_ERROR);io_error=false;fallback();
    fresh();kui_media_set(&device);fallback();
    fresh();current_identity[0]^=1u;fallback();
    fresh();identity_error=true;fallback();
    fresh();card.pair=2u;card.identity[0]^=1u;fallback();
    for(unsigned field=0u;field<9u;field++) {
        fresh();struct kui_vmu_game_selection different=card;
        switch(field) {
            case 0:different.pair++;break;case 1:different.product[0]^=1u;break;
            case 2:different.identity[0]^=1u;break;case 3:different.storage.cid[0]^=1u;break;
            case 4:different.storage.card_sectors++;break;case 5:different.storage.partition_first++;break;
            case 6:different.storage.partition_end--;break;case 7:different.storage.high_capacity=0u;break;
            default:different.product[11]=1;break;
        }
        memset(&view,0xa5,sizeof(view));struct kui_vmu_history_view untouched=view;
        assert(!kui_vmu_launch_history_reuse(&different,&platform,&view,cancelled));
        assert(!memcmp(&view,&untouched,sizeof(view)));kui_vmu_launch_history_end();
    }
    fresh();foreign_storage=true;assert(!kui_vmu_launch_history_reuse(&card,&platform,&view,cancelled));
    foreign_storage=false;kui_vmu_launch_history_end();
    fresh();kui_vmu_launch_history_end();fallback();
    fresh();kui_vmu_launch_history_request(false);fallback();
    fresh();kui_vmu_launch_history_begin_mounted(logger,cancelled);assert(applies==1u);
    assert(!begin(&view) && applies==1u);kui_vmu_launch_history_end();
    fresh();cancel_now=true;assert(begin(&view)==KUI_VMU_HISTORY_CANCELLED && writes==1u);
    cancel_now=false;kui_vmu_launch_history_end();
    fixture(true);history_error=KUI_VMU_HISTORY_INVALID;kui_vmu_launch_history_request(true);
    memcpy(baseline,files[0].wire,512u);kui_vmu_launch_history_begin_mounted(logger,cancelled);
    assert(kui_vmu_launch_history_performed() && !writes && !memcmp(baseline,files[0].wire,512u));
    assert(begin(&view)==KUI_VMU_HISTORY_INVALID && !writes);kui_vmu_launch_history_end();
    puts("PASS shared launch history: checkpoint2->1, identical durable receipt generations, both fresh allocation checks; write/error/reset/identity/cancel/job-end fallback");
    return 0;
}
