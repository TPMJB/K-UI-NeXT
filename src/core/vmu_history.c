/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_history.h"
#include "kui/vmu_library.h"
#include "kui/vmu_game_card_service.h"
#include "kui/clock.h"
#include "kui/hash.h"
#include "kui/destination.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCAN_LIMIT 256u
enum {OK=KUI_VMU_HISTORY_OK,IO=KUI_VMU_HISTORY_IO,BAD=KUI_VMU_HISTORY_INVALID,
      STOP=KUI_VMU_HISTORY_CANCELLED,STALE=KUI_VMU_HISTORY_STALE,FULL=KUI_VMU_HISTORY_FULL};
struct history_scan {
    struct kui_vmu_history_record records[SCAN_LIMIT];
    uint64_t maximum;
    unsigned count,damaged;
};
static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static bool nonzero(const void *data,size_t bytes) {
    const uint8_t *p=data;while(bytes--)if(*p++)return true;return false;
}
static bool same_storage(const struct kui_vmu_game_candidates_storage *a,
    const struct kui_vmu_game_candidates_storage *b) {
    return !memcmp(a->cid,b->cid,16u) && a->card_sectors==b->card_sectors &&
        a->partition_first==b->partition_first && a->partition_end==b->partition_end &&
        a->high_capacity==b->high_capacity;
}
static bool storage_matches(const struct kui_vmu_history_platform *p,
    const struct kui_vmu_game_selection *card) {
    struct kui_vmu_game_candidates_storage now={0};
    return p && p->storage && p->storage(p->context,&now) && same_storage(&now,&card->storage);
}
static bool card_valid(const struct kui_vmu_game_selection *card) {
    char key[12];
    return card && card->selected && card->pair && card->pair<=9999u &&
        nonzero(card->identity,16u) && memchr(card->product,0,12u) &&
        kui_vmu_game_card_key(card->product,key) && !memcmp(key,card->product,12u) &&
        nonzero(card->storage.cid,16u) && card->storage.card_sectors &&
        card->storage.partition_first<card->storage.partition_end &&
        card->storage.partition_end<=card->storage.card_sectors && card->storage.high_capacity<=1u;
}
static bool path_for(char out[192],const char *key,uint64_t sequence,const char *leaf) {
    int n=sequence?snprintf(out,192,KUI_VMU_HISTORY_ROOT "/%s/h%016" PRIx64 "%s%s",
        key,sequence,leaf?"/":"",leaf?leaf:""):
        snprintf(out,192,KUI_VMU_HISTORY_ROOT "/%s",key);
    return n>0 && n<192;
}
static uint64_t folder_sequence(const char *name) {
    if(!name || strlen(name)!=17u || name[0]!='h')return 0;
    uint64_t n=0;
    for(unsigned i=1;i<17u;i++) {
        unsigned char c=(unsigned char)name[i];unsigned v;
        if(c>='0' && c<='9')v=c-'0';else if(c>='a' && c<='f')v=c-'a'+10u;else return 0;
        n=(n<<4)|v;
    }
    return n;
}
static int exact(const char *path,void *out,size_t bytes,const void *compare,kui_cancel_fn cancel) {
    if(stopped(cancel))return STOP;
    FIL file;FRESULT r=f_open(&file,path,FA_READ);
    if(r!=FR_OK)return r==FR_NO_FILE || r==FR_NO_PATH?BAD:IO;
    int result=f_size(&file)==bytes?OK:BAD;uint8_t block[512];
    for(size_t at=0;result==OK && at<bytes;) {
        if(stopped(cancel)){result=STOP;break;}
        UINT n=(UINT)(bytes-at<sizeof(block)?bytes-at:sizeof(block)),got=0;
        if(f_read(&file,block,n,&got)!=FR_OK || got!=n){result=IO;break;}
        if(compare && memcmp(block,(const uint8_t *)compare+at,n)){result=BAD;break;}
        if(out)memcpy((uint8_t *)out+at,block,n);
        at+=n;
    }
    if(f_close(&file)!=FR_OK)result=IO;
    return result;
}
static int write_new(const char *path,const void *data,size_t bytes,kui_cancel_fn cancel) {
    if(stopped(cancel))return STOP;
    FIL file;if(f_open(&file,path,FA_CREATE_NEW|FA_WRITE)!=FR_OK)return IO;
    int result=OK;
    for(size_t at=0;at<bytes;) {
        if(stopped(cancel)){result=STOP;break;}
        UINT n=(UINT)(bytes-at>4096u?4096u:bytes-at),put=0;
        if(f_write(&file,(const uint8_t *)data+at,n,&put)!=FR_OK || put!=n){result=IO;break;}
        at+=n;
    }
    if(result==OK && f_sync(&file)!=FR_OK)result=IO;
    if(f_close(&file)!=FR_OK)result=IO;
    return result?result:exact(path,NULL,bytes,data,cancel);
}
static int image_read(void *context,uint16_t block,void *out) {
    if(block>=256u)return -1;
    memcpy(out,(const uint8_t *)context+(size_t)block*512u,512u);return 0;
}
static int image_digest(uint8_t *image,uint8_t sha[32],uint32_t *crc) {
    struct kui_vmu_image_workspace *work=malloc(sizeof(*work));
    if(!work)return IO;
    struct kui_vmu_image_summary summary;
    const struct kui_vmu_image_io io={image,image_read,NULL,NULL};
    int result=kui_vmu_image_validate(&io,KUI_VMU_IMAGE_BYTES,work,&summary)?BAD:OK;
    free(work);if(result)return result;
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,image,KUI_VMU_IMAGE_BYTES);
    kui_sha256_digest(&hash,sha);*crc=kui_crc32(0,image,KUI_VMU_IMAGE_BYTES);return OK;
}
static int verify(const char *key,uint64_t sequence,uint8_t *image,
    struct kui_vmu_history_record *record,kui_cancel_fn cancel) {
    char path[192];uint8_t wire[512],again[512],sha[32];uint32_t crc;
    if(!path_for(path,key,sequence,"record.bin"))return BAD;
    int result=exact(path,wire,sizeof(wire),NULL,cancel);
    if(result)return result;
    if(!kui_vmu_history_record_decode(wire,sizeof(wire),record) ||
       record->sequence!=sequence || strcmp(record->product,key))return BAD;
    path_for(path,key,sequence,"card.vmd");
    if((result=exact(path,image,KUI_VMU_IMAGE_BYTES,NULL,cancel)) ||
       (result=exact(path,NULL,KUI_VMU_IMAGE_BYTES,image,cancel)) ||
       (result=image_digest(image,sha,&crc)))return result;
    if(crc!=record->image_crc32 || memcmp(sha,record->sha256,32u))return BAD;
    path_for(path,key,sequence,"record.bin");
    result=exact(path,again,sizeof(again),wire,cancel);return result;
}
static int scan(const char *key,struct history_scan *out,uint8_t *image,kui_cancel_fn cancel) {
    memset(out,0,sizeof(*out));char path[192];path_for(path,key,0,NULL);
    DIR dir;FILINFO info;FRESULT r=f_opendir(&dir,path);
    if(r==FR_NO_FILE || r==FR_NO_PATH)return OK;
    if(r!=FR_OK)return IO;
    uint64_t ids[SCAN_LIMIT];unsigned count=0,visited=0;int result=OK;
    while((r=f_readdir(&dir,&info))==FR_OK && info.fname[0]) {
        if(stopped(cancel)){result=STOP;break;}
        if(++visited>SCAN_LIMIT){result=FULL;break;}
        uint64_t id=folder_sequence(info.fname);
        if(!id || !(info.fattrib&AM_DIR))continue;
        if(id>out->maximum)out->maximum=id;
        ids[count++]=id;
    }
    if(r!=FR_OK || f_closedir(&dir)!=FR_OK)return IO;
    for(unsigned i=0;!result && i<count;i++) {
        struct kui_vmu_history_record record;
        int current=verify(key,ids[i],image,&record,cancel);
        if(current==BAD){++out->damaged;continue;}
        if(current){result=current;break;}
        unsigned at=out->count;
        while(at && out->records[at-1u].sequence<record.sequence){out->records[at]=out->records[at-1u];--at;}
        out->records[at]=record;++out->count;
    }
    return result;
}
/* Candidate-v2 is authoritative. Only its absence allows the existing v1
 * explicit assignment, then a dedicated game.bin binding, to select a card. */
static int member(const struct kui_vmu_game_selection *card,bool *dedicated,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(dedicated)*dedicated=false;
    struct kui_vmu_game_candidates_view *view=malloc(sizeof(*view));
    if(!view)return IO;
    int r=kui_vmu_game_candidates_list(card->product,&card->storage,view,log,cancel);
    bool found=false;
    if(r==KUI_VMU_GAME_CANDIDATES_OK)for(unsigned i=0;i<view->count;i++)
        found|=view->targets[i].pair==card->pair && !memcmp(view->targets[i].identity,card->identity,16u);
    free(view);
    if(r==KUI_VMU_GAME_CANDIDATES_OK)return found?OK:STALE;
    if(r!=KUI_VMU_GAME_CANDIDATES_ABSENT)return r==KUI_VMU_GAME_CANDIDATES_CANCELLED?STOP:BAD;
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    if(!work)return IO;
    struct kui_vmu_game_card_choice choice;
    r=kui_vmu_game_card_assignment(card->product,work,&choice,cancel);
    free(work);
    if(r==KUI_VMU_GAME_CARD_READY)return choice.pair==card->pair?OK:STALE;
    if(r!=KUI_VMU_GAME_CARD_UNBOUND)return r==KUI_VMU_GAME_CARD_CANCELLED?STOP:BAD;
    char path[96],key[12];uint8_t binding[64];
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/game.bin",(unsigned)card->pair);
    r=exact(path,binding,sizeof(binding),NULL,cancel);
    if(r)return r;
    if(!kui_vmu_game_card_binding_decode(binding,sizeof(binding),key) || strcmp(key,card->product))return STALE;
    if(dedicated)*dedicated=true;
    return OK;
}
static int identity(const struct kui_vmu_game_selection *card,kui_cancel_fn cancel) {
    char path[96];uint8_t id[16];
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/identity.bin",(unsigned)card->pair);
    int r=exact(path,id,sizeof(id),card->identity,cancel);return r==BAD?STALE:r;
}
static int current_image(const struct kui_vmu_game_selection *card,uint8_t *image,
    kui_log_fn log,kui_cancel_fn cancel) {
    int result=identity(card,cancel);if(result)return result;
    char path[96];snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.vmd",(unsigned)card->pair);
    result=kui_vmu_library_read(path,image,log,cancel);
    if(result)return result==KUI_VMU_LIBRARY_CANCELLED?STOP:result==KUI_VMU_LIBRARY_IO?IO:BAD;
    return identity(card,cancel);
}
static void fill(const struct history_scan *scan,struct kui_vmu_history_view *out) {
    out->count=scan->count<KUI_VMU_HISTORY_KEEP?scan->count:KUI_VMU_HISTORY_KEEP;
    out->damaged=scan->damaged;
    for(unsigned i=0;i<out->count;i++) {
        const struct kui_vmu_history_record *r=scan->records+i;struct kui_vmu_history_entry *entry=out->entries+i;
        entry->sequence=r->sequence;entry->timestamp_seconds=r->timestamp_seconds;
        entry->reason=r->reason;entry->source_pair=r->source_pair;memcpy(entry->sha256,r->sha256,32u);
        struct kui_datetime date;
        const char *reason=r->reason==KUI_VMU_HISTORY_MANUAL?"Manual":
            r->reason==KUI_VMU_HISTORY_BEFORE_LAUNCH?"Before play":"Previous session";
        if(r->timestamp_seconds && r->timestamp_seconds<=INT64_MAX &&
           kui_clock_from_seconds((int64_t)r->timestamp_seconds,&date))
            snprintf(entry->label,sizeof(entry->label),"%04u-%02u-%02u %02u:%02u  %s",date.year,date.month,date.day,date.hour,date.minute,reason);
        else snprintf(entry->label,sizeof(entry->label),"Date unknown  %s  #%.8" PRIx64,reason,r->sequence);
    }
    out->known=true;
}
static int allocation(kui_cancel_fn cancel) {
    int r=kui_vmu_library_check_allocation(cancel);
    return !r?OK:r==KUI_VMU_LIBRARY_CANCELLED?STOP:r==KUI_VMU_LIBRARY_IO?IO:BAD;
}
int kui_vmu_history_allocation_mounted(kui_cancel_fn cancel) {return allocation(cancel);}
static bool same_record(const struct kui_vmu_history_record *a,const struct kui_vmu_history_record *b) {
    return a->sequence==b->sequence && a->timestamp_seconds==b->timestamp_seconds &&
        a->source_pair==b->source_pair && a->reason==b->reason && a->image_crc32==b->image_crc32 &&
        !memcmp(a->source_identity,b->source_identity,16u) && !memcmp(a->sha256,b->sha256,32u) &&
        !memcmp(a->product,b->product,12u);
}
static int prune(const char *key,const struct history_scan *s,const struct kui_vmu_history_platform *p,
    const struct kui_vmu_game_selection *card,uint8_t *image,kui_cancel_fn cancel) {
    for(unsigned i=KUI_VMU_HISTORY_KEEP;i<s->count;i++) {
        if(stopped(cancel))return STOP;
        if(!storage_matches(p,card))return STALE;
        struct kui_vmu_history_record record;
        int r=verify(key,s->records[i].sequence,image,&record,cancel);
        if(r || !same_record(&record,&s->records[i]))return r?r:STALE;
        char folder[192],path[192];path_for(folder,key,record.sequence,NULL);
        DIR dir;FILINFO info;if(f_opendir(&dir,folder)!=FR_OK)return IO;
        bool owned=true;FRESULT fr;
        while((fr=f_readdir(&dir,&info))==FR_OK && info.fname[0])
            if((info.fattrib&AM_DIR) || (strcmp(info.fname,"card.vmd") && strcmp(info.fname,"record.bin")))owned=false;
        if(f_closedir(&dir)!=FR_OK || fr!=FR_OK)return IO;
        if(!owned)return BAD;
        /* Remove publication first. A cut leaves a hidden orphan, while the
         * newly verified checkpoint and every retained predecessor survive. */
        path_for(path,key,record.sequence,"record.bin");if(f_unlink(path)!=FR_OK)return IO;
        path_for(path,key,record.sequence,"card.vmd");if(f_unlink(path)!=FR_OK)return IO;
        if(f_unlink(folder)!=FR_OK)return IO;
    }
    return OK;
}
static int checkpoint(const struct kui_vmu_history_request *request,enum kui_vmu_history_reason reason,
    uint64_t seconds,const struct kui_vmu_history_platform *platform,struct history_scan *s,
    uint8_t *image,struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    const struct kui_vmu_game_selection *card=&request->card;
    int r=current_image(card,image,log,cancel);if(r)return r;
    struct kui_vmu_history_record record={.timestamp_seconds=seconds,.source_pair=card->pair,.reason=reason};
    memcpy(record.product,card->product,12u);memcpy(record.source_identity,card->identity,16u);
    if((r=image_digest(image,record.sha256,&record.image_crc32)))return r;
    if(s->count && !memcmp(record.sha256,s->records[0].sha256,32u)) {
        if(s->count>KUI_VMU_HISTORY_KEEP) {
            if((r=allocation(cancel)) || !storage_matches(platform,card))return r?r:STALE;
            if((r=prune(card->product,s,platform,card,image,cancel)) ||
               (r=scan(card->product,s,image,cancel)))return r;
        }
        return KUI_VMU_HISTORY_UNCHANGED;
    }
    if(s->maximum==UINT64_MAX)return FULL;
    if((r=allocation(cancel)) || !storage_matches(platform,card))return r?r:STALE;
    /* Repair a prior interrupted retention pass before allocating another
     * image, bounding completed history to eight plus one uncertain publish. */
    if((r=prune(card->product,s,platform,card,image,cancel)))return r;
    /* Pruning used the scratch image; renew the recovered source afterwards. */
    if((r=current_image(card,image,log,cancel)))return r;
    char parent[192];path_for(parent,card->product,0,NULL);
    if(!kui_destination_mkdirs(parent+2,log))return IO;
    record.sequence=s->maximum+1u;char path[192];path_for(path,card->product,record.sequence,NULL);
    if(f_mkdir(path)!=FR_OK)return IO;
    path_for(path,card->product,record.sequence,"card.vmd");
    if((r=write_new(path,image,KUI_VMU_IMAGE_BYTES,cancel)))return r;
    if(!storage_matches(platform,card) || (r=identity(card,cancel)))return r?r:STALE;
    uint8_t wire[512];if(!kui_vmu_history_record_encode(&record,wire))return BAD;
    if(stopped(cancel))return STOP;
    path_for(path,card->product,record.sequence,"record.bin");
    if((r=write_new(path,wire,sizeof(wire),NULL)))return r;
    struct kui_vmu_history_record verified;
    if((r=verify(card->product,record.sequence,image,&verified,NULL)))return r;
    out->changed=true;
    if((r=scan(card->product,s,image,NULL)))return r;
    if((r=prune(card->product,s,platform,card,image,NULL)))return r;
    return scan(card->product,s,image,NULL);
}
static int restore(const struct kui_vmu_history_request *request,
    const struct kui_vmu_history_platform *platform,uint8_t *image,bool dedicated,struct kui_vmu_history_view *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    /* Publish the legacy source as an explicit candidate in the same
     * generation that hides game.bin fallback, preserving reselection and any
     * pending session receipt's exact source identity. */
    unsigned count=dedicated?2u:1u;
    struct kui_vmu_game_candidates_plan plan={.additional=count};memcpy(plan.key,request->card.product,12u);
    int r=kui_vmu_game_candidates_preflight(&plan,1u,&request->card.storage,log,cancel);
    if(r!=KUI_VMU_GAME_CANDIDATES_OK)return r==KUI_VMU_GAME_CANDIDATES_CANCELLED?STOP:BAD;
    if((r=allocation(cancel)) || !storage_matches(platform,&request->card))return r?r:STALE;
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    if(!work)return IO;
    struct kui_vmu_game_assignment assignments[2]={0};
    if(dedicated) {
        memcpy(assignments[0].key,request->card.product,12u);assignments[0].pair=request->card.pair;
        memcpy(assignments[0].identity,request->card.identity,16u);
    }
    for(unsigned i=count-1u;!r && i<count;i++) {
        struct kui_vmu_game_card_choice choice;uint8_t salt[16];platform->salt(platform->context,salt);
        struct kui_vmu_game_assignment *entry=assignments+i;
        int imported=kui_vmu_game_card_import_publish(image,KUI_VMU_IMAGE_BYTES,
            salt,work,&choice,entry->identity,log,cancel);
        if(imported!=KUI_VMU_GAME_CARD_READY){r=imported==KUI_VMU_GAME_CARD_CANCELLED?STOP:IO;break;}
        memcpy(entry->key,request->card.product,12u);entry->pair=choice.pair;
        if(!storage_matches(platform,&request->card))r=STALE;
        if(!r)r=identity(&request->card,cancel);
    }
    free(work);if(r)return r;
    const struct kui_vmu_game_assignment *selected=assignments+count-1u;
    r=kui_vmu_game_candidates_apply_choose(assignments,count,selected,&request->card.storage,log,cancel);
    if(r!=KUI_VMU_GAME_CANDIDATES_OK)return r==KUI_VMU_GAME_CANDIDATES_CANCELLED?STOP:IO;
    out->card=request->card;out->card.pair=selected->pair;memcpy(out->card.identity,selected->identity,16u);
    struct kui_vmu_game_candidates_view *view=malloc(sizeof(*view));
    if(!view)return IO;
    r=kui_vmu_game_candidates_list(request->card.product,&request->card.storage,view,log,NULL);
    if(!r)out->card.candidates_sequence=view->sequence;
    free(view);if(r)return IO;
    out->changed=true;out->restored=true;return OK;
}
static int export_image(const struct kui_vmu_history_request *request,
    const struct kui_vmu_history_record *record,uint8_t *image,
    const struct kui_vmu_history_platform *platform,struct kui_vmu_history_view *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    int r=allocation(cancel);if(r)return r;
    if(!kui_destination_mkdirs(KUI_VMU_HISTORY_EXPORT_ROOT+2,log))return IO;
    char image_path[192],part[192],metadata[192],metadata_part[192];FILINFO info;
    unsigned number;
    for(number=1;number<=9999u;number++) {
        snprintf(image_path,sizeof(image_path),KUI_VMU_HISTORY_EXPORT_ROOT "/%s-e%04u.vmd",request->card.product,number);
        snprintf(part,sizeof(part),KUI_VMU_HISTORY_EXPORT_ROOT "/%s-e%04u.vmd.part",request->card.product,number);
        snprintf(metadata,sizeof(metadata),KUI_VMU_HISTORY_EXPORT_ROOT "/%s-e%04u.txt",request->card.product,number);
        snprintf(metadata_part,sizeof(metadata_part),KUI_VMU_HISTORY_EXPORT_ROOT "/%s-e%04u.txt.part",request->card.product,number);
        const char *paths[]={image_path,part,metadata,metadata_part};bool absent=true;
        for(unsigned i=0;i<4u;i++) {
            FRESULT fr=f_stat(paths[i],&info);
            if(fr==FR_OK)absent=false;else if(fr!=FR_NO_FILE && fr!=FR_NO_PATH)return IO;
        }
        if(absent)break;
    }
    if(number>9999u)return FULL;
    uint8_t sha[32];uint32_t crc;if((r=image_digest(image,sha,&crc)))return r;
    char hash[65],text[512];kui_hex(sha,32u,hash);
    int length=snprintf(text,sizeof(text),"K-UI verified virtual VMU export\nProduct: %s\nSource pair: p%04u\nHistory sequence: %" PRIu64 "\nLocal RTC seconds: %" PRIu64 "\nImage bytes: %u\nSHA256: %s\nCRC32: %08" PRIx32 "\nRecovered block journal included; this is not a whole-save transaction guarantee.\nOriginal active card and history remain unchanged.\n",
        request->card.product,(unsigned)(record?record->source_pair:request->card.pair),record?record->sequence:0u,
        record?record->timestamp_seconds:0u,KUI_VMU_IMAGE_BYTES,hash,crc);
    if(length<0 || (size_t)length>=sizeof(text))return BAD;
    if((r=write_new(part,image,KUI_VMU_IMAGE_BYTES,cancel)) ||
       (r=write_new(metadata_part,text,(size_t)length,cancel)))return r;
    if(stopped(cancel))return STOP;
    if(!storage_matches(platform,&request->card))return STALE;
    if(f_rename(metadata_part,metadata)!=FR_OK || f_rename(part,image_path)!=FR_OK)return IO;
    if((r=exact(image_path,NULL,KUI_VMU_IMAGE_BYTES,image,NULL)))return r;
    snprintf(out->export_path,sizeof(out->export_path),"%s",image_path);out->exported=true;return OK;
}
bool kui_vmu_history_choose(const struct kui_vmu_history_view *view,unsigned index,
    enum kui_vmu_history_action action,struct kui_vmu_history_request *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!view || !view->known || view->count>KUI_VMU_HISTORY_KEEP || index>=view->count ||
       (action!=KUI_VMU_HISTORY_RESTORE && action!=KUI_VMU_HISTORY_EXPORT) || !card_valid(&view->card))return false;
    out->action=action;out->card=view->card;out->sequence=view->entries[index].sequence;
    memcpy(out->sha256,view->entries[index].sha256,32u);return out->sequence && nonzero(out->sha256,32u);
}
int kui_vmu_history_apply(const struct kui_vmu_history_request *request,
    enum kui_vmu_history_reason reason,uint64_t seconds,const struct kui_vmu_history_platform *platform,
    struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out)return KUI_VMU_HISTORY_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!request || !card_valid(&request->card) || !platform || !platform->storage || !platform->salt || !log ||
       request->action<KUI_VMU_HISTORY_LIST || request->action>KUI_VMU_HISTORY_EXPORT ||
       reason<KUI_VMU_HISTORY_MANUAL || reason>KUI_VMU_HISTORY_AFTER_RETURN)
        return KUI_VMU_HISTORY_ARGUMENT;
    out->card=request->card;
    if(stopped(cancel))return STOP;
    if(!storage_matches(platform,&request->card))return STALE;
    int result=identity(&request->card,cancel);if(result)return result;
    bool dedicated=false;
    if((result=member(&request->card,&dedicated,log,cancel)))return result;
    struct history_scan *s=malloc(sizeof(*s));uint8_t *image=malloc(KUI_VMU_IMAGE_BYTES);
    if(!s || !image){free(s);free(image);return IO;}
    result=scan(request->card.product,s,image,cancel);
    if(!result && request->action==KUI_VMU_HISTORY_CHECKPOINT)
        result=checkpoint(request,reason,seconds,platform,s,image,out,log,cancel);
    if(!result && (request->action==KUI_VMU_HISTORY_RESTORE || request->action==KUI_VMU_HISTORY_EXPORT)) {
        struct kui_vmu_history_record record,*selected=NULL;
        if(request->sequence) {
            result=verify(request->card.product,request->sequence,image,&record,cancel);
            if(!result && memcmp(record.sha256,request->sha256,32u))result=STALE;
            selected=&record;
        } else if(request->action==KUI_VMU_HISTORY_RESTORE)result=KUI_VMU_HISTORY_ARGUMENT;
        else result=current_image(&request->card,image,log,cancel);
        if(!result)result=request->action==KUI_VMU_HISTORY_RESTORE?
            restore(request,platform,image,dedicated,out,log,cancel):export_image(request,selected,image,platform,out,log,cancel);
    }
    if(result>=0 && !storage_matches(platform,&request->card))result=STALE;
    if(result>=0) {
        fill(s,out);
        snprintf(out->notice,sizeof(out->notice),"%s",out->restored?"Version restored to a new card and selected. Original card kept.":
            out->exported?"Verified VMD and metadata exported to KUI/exports/saves.":
            result==KUI_VMU_HISTORY_UNCHANGED?"No new checkpoint: card data is unchanged.":
            out->changed?"Checkpoint verified. The newest eight versions are retained.":
            s->damaged?"History loaded; incomplete or damaged versions were omitted.":"Newest verified checkpoints. Original cards are preserved.");
    } else snprintf(out->notice,sizeof(out->notice),"%s",out->changed?
        "A new version may be complete; refresh history before retrying.":
        result==STOP?"History operation stopped. Original card kept.":
        result==STALE?"Card or storage identity changed; refresh before retrying.":
        result==FULL?"History scan or sequence limit reached; inspect history files.":
        "History operation not confirmed. Original card kept; refresh before retrying.");
    free(image);free(s);return result;
}
