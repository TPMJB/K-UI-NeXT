/* SPDX-License-Identifier: GPL-3.0-only */
/* Production receipt state machine with byte-backed receipt files. Real
 * history image/ownership/write-cut tests remain in test_vmu_history.c. */
#include "../src/core/vmu_history_session.c"
#include <assert.h>

struct file_slot {uint8_t wire[512];size_t bytes;bool present;};
static struct file_slot files[2];static FIL *active;static unsigned active_slot;
static unsigned applies,allocations,writes,reason[8],applied_pair[8];
static int history_error,allocation_error,write_limit;
static bool cancel_now,foreign_storage,sync_failure,readback_failure;
static struct kui_vmu_game_selection card;
static struct kui_vmu_game_candidates_storage storage;
static const struct kui_vmu_history_platform platform;
static unsigned slot_for(const char *path) {assert(!strcmp(path,paths[0]) || !strcmp(path,paths[1]));return strcmp(path,paths[0])?1u:0u;}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    unsigned slot=slot_for(path);memset(info,0,sizeof(*info));info->fsize=files[slot].bytes;
    return files[slot].present?FR_OK:FR_NO_FILE;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(!active);unsigned slot=slot_for(path);
    if(mode==FA_READ && !files[slot].present)return FR_NO_FILE;
    if(mode&FA_WRITE){files[slot].present=true;files[slot].bytes=0u;}
    memset(file,0,sizeof(*file));file->obj.objsize=files[slot].bytes;file->flag=mode;
    active=file;active_slot=slot;return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT want,UINT *got) {
    assert(active==file && want<=files[active_slot].bytes);*got=want;
    memcpy(out,files[active_slot].wire,want);
    if(readback_failure && writes && active_slot==1u)((uint8_t *)out)[0]^=1u;
    return FR_OK;
}
FRESULT f_write(FIL *file,const void *in,UINT want,UINT *put) {
    assert(active==file && want==512u);++writes;
    *put=write_limit<0?want:(UINT)write_limit;
    memcpy(files[active_slot].wire,in,*put);files[active_slot].bytes=*put;file->obj.objsize=*put;
    return *put==want?FR_OK:FR_DISK_ERR;
}
FRESULT f_sync(FIL *file) {assert(active==file);return sync_failure?FR_DISK_ERR:FR_OK;}
FRESULT f_close(FIL *file) {assert(active==file);active=NULL;return FR_OK;}
int kui_vmu_library_check_allocation(kui_cancel_fn cancel) {assert(cancel);++allocations;return allocation_error;}
bool kui_destination_mkdirs(const char *path,kui_log_fn log) {assert(log && !strcmp(path,KUI_VMU_HISTORY_ROOT+2));return true;}
int kui_vmu_history_apply(const struct kui_vmu_history_request *request,enum kui_vmu_history_reason why,
    uint64_t seconds,const struct kui_vmu_history_platform *p,struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    assert(request->action==KUI_VMU_HISTORY_CHECKPOINT && seconds && p==&platform && log && cancel && applies<8u);
    reason[applies]=(unsigned)why;applied_pair[applies]=request->card.pair;++applies;
    memset(out,0,sizeof(*out));out->known=true;out->changed=true;out->count=1u;
    out->entries[0].reason=(uint32_t)why;return history_error;
}
static bool identity(void *context,struct kui_vmu_game_candidates_storage *out) {
    assert(context==&storage);*out=storage;if(foreign_storage)out->cid[0]^=1u;return true;
}
static void salt(void *context,uint8_t out[16]) {(void)context;memset(out,7,16u);}
static const struct kui_vmu_history_platform platform={&storage,identity,salt};
static bool cancelled(void) {return cancel_now;}
static void logger(const char *format,...) {(void)format;}
static void fixture(bool pending) {
    assert(!active);memset(files,0,sizeof(files));memset(&card,0,sizeof(card));
    storage=(struct kui_vmu_game_candidates_storage){.card_sectors=100000u,.partition_first=2048u,.partition_end=100000u,.high_capacity=1u};
    memset(storage.cid,0x21,sizeof(storage.cid));card.storage=storage;card.selected=true;card.pair=1u;
    memset(card.identity,0x31,sizeof(card.identity));assert(kui_vmu_game_card_key("GENERIC-1",card.product));
    files[0].present=true;files[0].bytes=512u;encode(files[0].wire,1u,pending?&card:NULL);
    applies=allocations=writes=0u;history_error=allocation_error=0;write_limit=-1;
    cancel_now=foreign_storage=sync_failure=readback_failure=false;
}
static int begin(struct kui_vmu_history_view *out) {return kui_vmu_history_session_begin(&card,100u,&platform,out,logger,cancelled);}
int main(void) {
    struct kui_vmu_history_view view;struct receipt latest;uint8_t retained[512];
    fixture(true);memcpy(retained,files[0].wire,512u);assert(!begin(&view));
    assert(applies==1u && reason[0]==KUI_VMU_HISTORY_AFTER_RETURN && allocations==1u && writes==1u);
    assert(view.known && !view.changed && view.entries[0].reason==KUI_VMU_HISTORY_AFTER_RETURN);
    assert(!memcmp(retained,files[0].wire,512u) && !load(&latest,cancelled) && latest.pending && latest.sequence==2u);
    assert(!kui_vmu_history_session_resume(101u,&platform,&view,logger,cancelled));
    unsigned done=writes;assert(kui_vmu_history_session_resume(102u,&platform,&view,logger,cancelled)==KUI_VMU_HISTORY_UNCHANGED && writes==done);
    fixture(false);assert(!begin(&view));assert(applies==1u && reason[0]==KUI_VMU_HISTORY_BEFORE_LAUNCH && allocations==1u && writes==1u);
    fixture(true);struct kui_vmu_game_selection prior=card;prior.pair=2u;prior.identity[0]^=1u;encode(files[0].wire,1u,&prior);
    assert(!begin(&view));assert(applies==2u && allocations==2u && writes==2u);
    assert(reason[0]==KUI_VMU_HISTORY_AFTER_RETURN && applied_pair[0]==2u && reason[1]==KUI_VMU_HISTORY_BEFORE_LAUNCH && applied_pair[1]==1u);
    /* Exact identity/storage equality is mandatory; there is no product or
     * game-title shortcut into the one-checkpoint path. */
    fixture(true);prior=card;assert(same_card(&prior,&card));
    for(unsigned field=0u;field<9u;field++) {
        prior=card;
        switch(field) {
            case 0:prior.pair++;break;case 1:prior.product[0]^=1u;break;case 2:prior.identity[0]^=1u;break;
            case 3:prior.storage.cid[0]^=1u;break;case 4:prior.storage.card_sectors++;break;
            case 5:prior.storage.partition_first++;break;case 6:prior.storage.partition_end--;break;
            case 7:prior.storage.high_capacity=0u;break;default:prior.product[11]=1;break;
        }
        assert(!same_card(&prior,&card));
    }
    const int cuts[]={0,1,255,511,512};
    for(unsigned i=0;i<sizeof(cuts)/sizeof(cuts[0]);i++) {
        fixture(true);memcpy(retained,files[0].wire,512u);write_limit=cuts[i];
        int result=begin(&view);assert((result==0)==(cuts[i]==512));
        assert(!memcmp(retained,files[0].wire,512u) && !load(&latest,cancelled) && latest.pending);
        assert(latest.sequence==(cuts[i]==512?2u:1u));
    }
    fixture(true);sync_failure=true;assert(begin(&view)<0);assert(!load(&latest,cancelled) && latest.pending);
    fixture(true);readback_failure=true;assert(begin(&view)<0);readback_failure=false;assert(!load(&latest,cancelled) && latest.pending);
    fixture(true);allocation_error=KUI_VMU_LIBRARY_INVALID;assert(begin(&view)<0 && !writes && applies==1u);
    fixture(true);history_error=KUI_VMU_HISTORY_INVALID;assert(begin(&view)<0 && !writes && !allocations);
    fixture(true);foreign_storage=true;assert(begin(&view)==KUI_VMU_HISTORY_STALE && !writes && !applies);
    fixture(true);cancel_now=true;assert(begin(&view)==KUI_VMU_HISTORY_CANCELLED && !writes && !applies);
    fixture(true);files[0].wire[508]^=1u;assert(begin(&view)==KUI_VMU_HISTORY_INVALID && !writes && !applies);
    fixture(true);encode(files[0].wire,UINT64_MAX,&card);assert(begin(&view)==KUI_VMU_HISTORY_FULL && !writes);
    puts("Common history session: same-card checkpoint calls 2->1, receipt allocation/publication calls 2->1; durability guards OK");
    return 0;
}
