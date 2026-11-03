/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_cards.h"
#include "kui/disc_launcher.h"
#include "kui/games_storage.h"
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_library.h"
#include "kui/vmu_collection.h"
#include "kui/vmu_import_plan.h"
#include "kui/vmu_import_match.h"
#include "kui/vmu_backing.h"
#include "kui/media.h"
#include "kui/hash.h"
#include "kui/clock.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct linked_cards {
    struct kui_vmu_game_candidates_view candidates;
    struct kui_vmu_game_candidate_target targets[KUI_VMU_GAME_CANDIDATES_PER_GAME];
    unsigned count,default_pair;
    bool dedicated_fallback;
};
static struct kui_disc_cards_view reviewed;
static uint64_t tokens;
static uint8_t reviewed_links[32];
static struct {
    bool valid,labelled;
    enum kui_disc_cards_op op;
    uint8_t signature[32],source_id[16],source_hash[32],identity[16];
    char source_path[KUI_VMU_BACKUP_PATH_CAP];
    struct kui_vmu_game_candidates_storage storage;
    unsigned pair;
} pending;
struct operation {
    struct kui_disc_cards_view *out;
    kui_log_fn log;kui_cancel_fn cancel;kui_app_progress_fn progress;
};
static void quiet(const char *format,...) {(void)format;}
static bool never_cancel(void) {return false;}
static void status(struct operation *s,const char *format,...) {
    va_list args;va_start(args,format);
    vsnprintf(s->out->status.message,sizeof(s->out->status.message),format,args);va_end(args);
    if(s->progress) s->progress(&s->out->status);
}
static bool fail(struct operation *s,const char *message) {
    ++s->out->status.errors;s->out->identified=false;
    s->out->status.complete=true;s->out->status.passed=false;
    status(s,"%s",message);s->log("Disc VMU cards: %s",message);return false;
}
static bool stopped(struct operation *s) {
    if(!s->cancel()) return false;
    s->out->status.stopped=true;s->out->identified=false;
    s->out->status.complete=true;s->out->status.passed=false;
    status(s,"Disc card operation stopped; saved cards are retained");return true;
}
static bool pin(struct kui_vmu_game_candidates_storage *out) {
    struct kui_vmu_sd_identity id={0};const struct kui_volume *volume=kui_media_volume();
    if(!volume || !volume->count || !kui_sd_vmu_profile_identity(&id) || !id.sectors ||
       (uint64_t)volume->start+volume->count>id.sectors) return false;
    memset(out,0,sizeof(*out));memcpy(out->cid,id.cid,16u);
    out->card_sectors=id.sectors;out->high_capacity=id.high_capacity?1u:0u;
    out->partition_first=volume->start;out->partition_end=(uint64_t)volume->start+volume->count;return true;
}
static bool same_pin(const struct kui_vmu_game_candidates_storage *a,
    const struct kui_vmu_game_candidates_storage *b) {
    return !memcmp(a->cid,b->cid,16u) && a->card_sectors==b->card_sectors &&
        a->high_capacity==b->high_capacity && a->partition_first==b->partition_first &&
        a->partition_end==b->partition_end;
}
static bool current_storage(struct operation *s) {
    struct kui_vmu_game_candidates_storage now;
    return (pin(&now) && same_pin(&now,&s->out->storage)) ||
        fail(s,"SD card or partition changed; refresh the disc cards");
}
static void pair_path(char path[KUI_VMU_BACKUP_PATH_CAP],unsigned pair,const char *leaf) {
    snprintf(path,KUI_VMU_BACKUP_PATH_CAP,KUI_VMU_GAME_CARD_ROOT "/p%04u/%s",pair,leaf);
}
static unsigned path_pair(const char *path) {
    static const char prefix[]=KUI_VMU_GAME_CARD_ROOT "/p";
    if(!path || strncmp(path,prefix,sizeof(prefix)-1u)) return 0u;
    const char *at=path+sizeof(prefix)-1u;unsigned n=0;
    for(unsigned i=0;i<4u;i++) {if(at[i]<'0'||at[i]>'9')return 0u;n=n*10u+(unsigned)(at[i]-'0');}
    return n && !strcmp(at+4,"/card.vmd")?n:0u;
}
static bool exact_file(const char *path,void *out,size_t bytes) {
    FIL file;UINT got=0;if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    bool ok=f_size(&file)==bytes && f_read(&file,out,(UINT)bytes,&got)==FR_OK && got==bytes;
    if(f_close(&file)!=FR_OK)ok=false;
    return ok;
}
static bool source_identity(const char *path,uint8_t identity[16]) {
    if(!kui_vmu_library_path(path))return false;
    const char *slash=strrchr(path,'/');char peer[KUI_VMU_BACKUP_PATH_CAP];
    int n=snprintf(peer,sizeof(peer),"%.*s/identity.bin",(int)(slash-path),path);
    return n>0 && n<(int)sizeof(peer) && exact_file(peer,identity,16u);
}
static int image_block(void *context,uint16_t block,void *out) {
    if(block>=256u)return -1;
    memcpy(out,(uint8_t *)context+(size_t)block*512u,512u);return 0;
}
static void digest(const void *data,size_t bytes,uint8_t sha[32]) {
    struct kui_sha256 work;kui_sha256_init(&work);kui_sha256_update(&work,data,bytes);kui_sha256_digest(&work,sha);
}
static bool image_row(struct operation *s,struct kui_disc_cards_row *row,uint8_t *image,
    struct kui_vmu_image_workspace *work) {
    uint8_t before[16],after[16];struct kui_vmu_image_summary summary;
    if(!source_identity(row->path,before) || memcmp(before,row->identity,16u) ||
       kui_vmu_library_read(row->path,image,s->log,s->cancel)!=KUI_VMU_LIBRARY_OK ||
       !source_identity(row->path,after) || memcmp(before,after,16u))
        return stopped(s)?false:fail(s,"The selected card is unavailable or changed; refresh its list");
    const struct kui_vmu_image_io io={image,image_block,NULL,NULL};
    if(kui_vmu_image_validate(&io,KUI_VMU_IMAGE_BYTES,work,&summary)!=KUI_VMU_IMAGE_OK)
        return fail(s,"The selected card image could not be checked");
    row->files=summary.files;row->free_blocks=summary.free_blocks;row->readable=true;
    return true;
}
static bool target_row(struct operation *s,const struct kui_vmu_game_candidate_target *target,
    struct kui_disc_cards_row *row,uint8_t *image,struct kui_vmu_image_workspace *work) {
    memset(row,0,sizeof(*row));row->pair=target->pair;row->active=row->linked=true;
    memcpy(row->identity,target->identity,16u);pair_path(row->path,target->pair,"card.vmd");
    if(!image_row(s,row,image,work))return false;
    char label[49]={0};
    if(kui_vmu_library_label(row->path,row->identity,label,s->cancel)!=KUI_VMU_LIBRARY_OK)
        return stopped(s)?false:fail(s,"The linked card name could not be checked");
    if(label[0])snprintf(row->label,sizeof(row->label),"%s",label);
    else snprintf(row->label,sizeof(row->label),"%.44s / p%04u",s->out->metadata.title[0]?
        s->out->metadata.title:s->out->product,(unsigned)target->pair);
    return true;
}
/* The candidate catalogue is authoritative for a covered product. Only its
 * ABSENT result permits the established v1/dedicated fallback. */
static bool linked_load(struct operation *s,struct linked_cards *cards,
    struct kui_vmu_game_card_work *work) {
    memset(cards,0,sizeof(*cards));
    int r=kui_vmu_game_candidates_list(s->out->product,&s->out->storage,&cards->candidates,s->log,s->cancel);
    if(r!=KUI_VMU_GAME_CANDIDATES_OK && r!=KUI_VMU_GAME_CANDIDATES_ABSENT)
        return stopped(s)?false:fail(s,"Disc card links could not be checked; inspect VMU tools");
    s->out->candidates_sequence=cards->candidates.sequence;
    if(r==KUI_VMU_GAME_CANDIDATES_OK) {
        cards->count=cards->candidates.count;
        memcpy(cards->targets,cards->candidates.targets,cards->count*sizeof(cards->targets[0]));
        if(cards->candidates.default_index!=KUI_VMU_GAME_CANDIDATES_NO_DEFAULT)
            cards->default_pair=cards->targets[cards->candidates.default_index].pair;
        return true;
    }
    struct kui_vmu_game_card_choice choice;
    r=kui_vmu_game_card_assignment(s->out->product,work,&choice,s->cancel);
    if(r==KUI_VMU_GAME_CARD_READY) {
        cards->count=1u;cards->targets[0].pair=choice.pair;
        char path[KUI_VMU_BACKUP_PATH_CAP];pair_path(path,choice.pair,"identity.bin");
        if(!exact_file(path,cards->targets[0].identity,16u))return fail(s,"The old disc card identity is unavailable");
        cards->default_pair=choice.pair;return true;
    }
    if(r!=KUI_VMU_GAME_CARD_UNBOUND)return stopped(s)?false:fail(s,choice.problem[0]?choice.problem:"Old disc card assignment could not be checked");
    DIR dir;FILINFO info;r=f_opendir(&dir,KUI_VMU_GAME_CARD_ROOT);
    if(r==FR_NO_FILE || r==FR_NO_PATH)return true;
    if(r!=FR_OK)return fail(s,"Cannot read stored disc card folders");
    bool ok=true;unsigned visited=0;
    while((r=f_readdir(&dir,&info))==FR_OK && info.fname[0]) {
        if(stopped(s)) {ok=false;break;}
        if(++visited>20000u) {ok=fail(s,"Disc card folder limit reached");break;}
        unsigned pair=kui_vmu_game_card_pair_number(info.fname);
        if(!pair || !(info.fattrib&AM_DIR))continue;
        char path[KUI_VMU_BACKUP_PATH_CAP],key[12];uint8_t binding[64];
        pair_path(path,pair,"game.bin");
        FILINFO found;FRESULT present=f_stat(path,&found);
        if(present==FR_NO_FILE || present==FR_NO_PATH)continue;
        if(present!=FR_OK) {ok=fail(s,"Cannot read a stored disc binding");break;}
        if(!exact_file(path,binding,sizeof(binding)) ||
           !kui_vmu_game_card_binding_decode(binding,sizeof(binding),key) || strcmp(key,s->out->product))continue;
        if(cards->count) {ok=fail(s,"Several old cards claim this disc; inspect VMU tools");break;}
        cards->count=1u;cards->targets[0].pair=pair;pair_path(path,pair,"identity.bin");
        cards->dedicated_fallback=true;
        if(!exact_file(path,cards->targets[0].identity,16u)) {ok=fail(s,"The stored disc card identity is unavailable");break;}
        cards->default_pair=pair;
    }
    if(f_closedir(&dir)!=FR_OK || r!=FR_OK)ok=fail(s,"Cannot finish reading disc bindings");
    return ok;
}
static void linked_hash(const struct linked_cards *cards,uint8_t out[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,&cards->count,sizeof(cards->count));
    kui_sha256_update(&hash,&cards->default_pair,sizeof(cards->default_pair));
    kui_sha256_update(&hash,cards->targets,cards->count*sizeof(cards->targets[0]));
    kui_sha256_digest(&hash,out);
}
static bool same_linked(const struct linked_cards *a,const struct kui_disc_cards_view *view) {
    uint8_t hash[32];linked_hash(a,hash);
    return a->candidates.sequence==view->candidates_sequence && !memcmp(hash,reviewed_links,32u);
}
static const struct kui_disc_cards_row *reviewed_row(const struct kui_disc_cards_request *request) {
    for(unsigned i=0;i<reviewed.count;i++) {
        const struct kui_disc_cards_row *row=reviewed.rows+i;
        if(row->readable && row->pair==request->pair && !strcmp(row->path,request->path) &&
           !memcmp(row->identity,request->identity,16u))return row;
    }
    return NULL;
}
static void row_copy(struct kui_disc_cards_row *out,const struct kui_vmu_backup_entry *source) {
    memset(out,0,sizeof(*out));memcpy(out->path,source->path,sizeof(out->path));
    memcpy(out->identity,source->identity,16u);out->pair=path_pair(out->path);
    out->active=source->active;out->readable=source->readable;out->free_blocks=source->free_blocks;
    memcpy(out->contents,source->contents,sizeof(out->contents));
    snprintf(out->label,sizeof(out->label),"%s",source->label[0]?source->label:source->name);
}
static bool suggest(struct operation *s,struct kui_disc_cards_row *row,uint8_t *image,
    struct kui_vmu_image_workspace *work,struct kui_vmu_import_plan *plan,
    struct kui_vmu_import_matches *matches) {
    if(!image_row(s,row,image,work))return false;
    if(kui_vmu_import_plan_inspect(image,KUI_VMU_IMAGE_BYTES,NULL,NULL,work,plan)!=KUI_VMU_IMAGE_OK)return true;
    struct kui_vmu_import_match_game game={.native_eligible=true};
    snprintf(game.title,sizeof(game.title),"%s",s->out->metadata.title);
    memcpy(game.product,s->out->product,sizeof(game.product));
    if(kui_vmu_import_match(plan,&game,1u,matches))return true;
    for(unsigned i=0;i<matches->group_count;i++)
        if(matches->groups[i].state==KUI_VMU_IMPORT_MATCH_AUTO) {row->suggested=true;break;}
    return true;
}
static bool listing(struct operation *s,enum kui_disc_cards_stage stage,unsigned page,
    const struct linked_cards *cards,uint8_t *image,struct kui_vmu_game_card_work *work) {
    s->out->stage=stage;s->out->page=page;s->out->count=0;s->out->linked_count=cards->count;
    s->out->default_pair=cards->default_pair;
    if(stage==KUI_DISC_CARDS_LINKED_STAGE) {
        s->out->total=cards->count;
        if(page>UINT_MAX/KUI_DISC_CARDS_ROWS || (page && page*KUI_DISC_CARDS_ROWS>=cards->count))
            return fail(s,"Disc card page changed; refresh the list");
        for(unsigned i=page*KUI_DISC_CARDS_ROWS;i<cards->count && s->out->count<KUI_DISC_CARDS_ROWS;i++) {
            struct kui_disc_cards_row *row=s->out->rows+s->out->count;
            if(!target_row(s,cards->targets+i,row,image,&work->image))return false;
            row->is_default=row->pair==cards->default_pair;++s->out->count;
        }
        return true;
    }
    struct kui_vmu_backup_view *view=malloc(sizeof(*view));
    struct kui_vmu_import_plan *plan=malloc(sizeof(*plan));
    struct kui_vmu_import_matches *matches=malloc(sizeof(*matches));
    if(!view || !plan || !matches) {free(view);free(plan);free(matches);return fail(s,"Not enough RAM to list imported cards");}
    bool ok=kui_vmu_library_list(page,view,s->log,s->cancel)==KUI_VMU_LIBRARY_OK;
    if(!ok && !stopped(s))fail(s,view->status.message[0]?view->status.message:"Imported card list unavailable");
    if(ok && (view->count>KUI_DISC_CARDS_ROWS || view->page!=page))ok=fail(s,"Imported card page changed; refresh its list");
    if(ok) {
        s->out->total=view->total;
        for(unsigned i=0;ok && i<view->count;i++) {
            struct kui_disc_cards_row *row=s->out->rows+s->out->count++;row_copy(row,view->entries+i);
            row->link_copies=!row->active;
            if(row->readable && row->active) {
                char marker[KUI_VMU_BACKUP_PATH_CAP];FILINFO info;pair_path(marker,row->pair,"import.bin");
                FRESULT r=f_stat(marker,&info);
                if(r!=FR_OK && r!=FR_NO_FILE && r!=FR_NO_PATH) {ok=fail(s,"Cannot inspect an active card marker");break;}
                row->link_copies=r!=FR_OK;
            }
            for(unsigned j=0;j<cards->count;j++) if(row->active && row->pair==cards->targets[j].pair &&
                !memcmp(row->identity,cards->targets[j].identity,16u)) row->linked=true;
            if(row->readable)ok=suggest(s,row,image,&work->image,plan,matches);
        }
    }
    free(matches);free(plan);free(view);return ok;
}
static bool pending_same(const struct kui_disc_cards_request *request,const struct kui_disc_cards_view *view) {
    return pending.valid && pending.op==request->op && !memcmp(pending.signature,view->signature,32u) &&
        same_pin(&pending.storage,&view->storage) && (request->op==KUI_DISC_CARDS_CREATE ||
        (!strcmp(pending.source_path,request->path) && !memcmp(pending.source_id,request->identity,16u)));
}
static void salt(uint8_t out[16]) {
    static uint32_t calls;struct kui_datetime now;int64_t seconds=0;
    if(kui_clock_now(&now))(void)kui_clock_to_seconds(&now,&seconds);
    uint32_t words[4]={(uint32_t)seconds,(uint32_t)((uint64_t)seconds>>32),++calls,0x44495343u};
    memcpy(out,words,16u);
}
static bool prepare_target(struct operation *s,const struct kui_disc_cards_request *request,
    const struct kui_disc_cards_row *source,uint8_t *image,struct kui_vmu_game_card_work *work,
    struct kui_vmu_game_assignment *entry) {
    memset(entry,0,sizeof(*entry));memcpy(entry->key,s->out->product,sizeof(entry->key));
    bool copy=request->op==KUI_DISC_CARDS_CREATE;
    uint8_t hash[32]={0};
    if(source) {
        struct kui_disc_cards_row checked=*source;
        if(!image_row(s,&checked,image,&work->image))return false;
        digest(image,KUI_VMU_IMAGE_BYTES,hash);
        char marker[KUI_VMU_BACKUP_PATH_CAP];FILINFO info;
        if(source->active) {
            pair_path(marker,source->pair,"import.bin");FRESULT r=f_stat(marker,&info);
            if(r!=FR_OK && r!=FR_NO_FILE && r!=FR_NO_PATH)return fail(s,"Cannot inspect the selected active card");
            copy=r!=FR_OK;
        } else copy=true;
        if(copy!=source->link_copies)return fail(s,"The selected card's sharing state changed; refresh before linking");
        if(!copy) {entry->pair=source->pair;memcpy(entry->identity,source->identity,16u);return true;}
    } else {
        for(unsigned b=0;b<KUI_VMU_IMAGE_BLOCKS;b++)
            if(kui_vmu_image_blank_block((uint16_t)b,image+(size_t)b*512u))return fail(s,"Cannot format the new virtual card");
        digest(image,KUI_VMU_IMAGE_BYTES,hash);
    }
    if(pending_same(request,s->out)) {
        if(source && memcmp(hash,pending.source_hash,32u))return fail(s,"The source card changed after its copy; inspect the retained copy before retry");
        struct kui_disc_cards_row retained={.pair=pending.pair,.active=true};
        pair_path(retained.path,pending.pair,"card.vmd");memcpy(retained.identity,pending.identity,16u);
        if(!image_row(s,&retained,image,&work->image))return false;
        digest(image,KUI_VMU_IMAGE_BYTES,hash);
        if(memcmp(hash,pending.source_hash,32u))return fail(s,"The retained new card changed; inspect it before linking");
        entry->pair=pending.pair;memcpy(entry->identity,pending.identity,16u);
    } else {
        struct kui_vmu_game_card_choice choice;uint8_t nonce[16],id[16];salt(nonce);
        if(!current_storage(s) || stopped(s))return false;
        int r=kui_vmu_game_card_import_publish(image,KUI_VMU_IMAGE_BYTES,nonce,work,&choice,id,s->log,s->cancel);
        if(r!=KUI_VMU_GAME_CARD_READY)return stopped(s)?false:fail(s,choice.problem[0]?choice.problem:"New playable card could not be published");
        memset(&pending,0,sizeof(pending));pending.valid=true;pending.op=request->op;pending.pair=choice.pair;
        memcpy(pending.identity,id,16u);memcpy(pending.signature,s->out->signature,32u);pending.storage=s->out->storage;
        if(source) {
            memcpy(pending.source_id,request->identity,16u);
            memcpy(pending.source_path,request->path,sizeof(pending.source_path));
        }
        memcpy(pending.source_hash,hash,32u);entry->pair=choice.pair;memcpy(entry->identity,id,16u);
    }
    if(!pending.labelled) {
        char path[KUI_VMU_BACKUP_PATH_CAP],name[49];pair_path(path,entry->pair,"card.vmd");
        snprintf(name,sizeof(name),"%.36s / %s",s->out->metadata.title[0]?s->out->metadata.title:s->out->product,
            source?"Imported":"New card");
        for(char *p=name;*p;p++)if((unsigned char)*p<32u || (unsigned char)*p>126u)*p='?';
        if(!current_storage(s) || stopped(s))return false;
        if(kui_vmu_library_rename(path,name,entry->identity,s->log,s->cancel)!=KUI_VMU_LIBRARY_OK)
            return stopped(s)?false:fail(s,"The new card is retained; its name could not be saved, retry to finish");
        pending.labelled=true;
    }
    s->out->created=true;return current_storage(s);
}
static bool mutate(struct operation *s,const struct kui_disc_cards_request *request,
    struct linked_cards *cards,uint8_t *image,struct kui_vmu_game_card_work *work) {
    const struct kui_disc_cards_row *source=NULL;
    if(request->op==KUI_DISC_CARDS_LINK || request->op==KUI_DISC_CARDS_DEFAULT) {
        source=reviewed_row(request);
        if(!source || (request->op==KUI_DISC_CARDS_LINK && reviewed.stage!=KUI_DISC_CARDS_IMPORTS_STAGE) ||
           (request->op==KUI_DISC_CARDS_DEFAULT && !source->linked))
            return fail(s,"The selected card is stale; refresh the disc card list");
    }
    if(stopped(s) || !current_storage(s))return false;
    struct kui_vmu_game_assignment entries[KUI_VMU_GAME_CANDIDATES_PER_GAME+1u];unsigned count=0;
    /* Carry legacy dedicated cards into the first candidate publication. */
    for(unsigned i=0;i<cards->count;i++) {
        memset(entries+count,0,sizeof(entries[0]));memcpy(entries[count].key,s->out->product,12u);
        entries[count].pair=cards->targets[i].pair;memcpy(entries[count++].identity,cards->targets[i].identity,16u);
    }
    bool adding=request->op==KUI_DISC_CARDS_LINK || request->op==KUI_DISC_CARDS_CREATE;
    if(adding) {
        struct kui_vmu_game_candidates_plan plan={.additional=1u};memcpy(plan.key,s->out->product,12u);
        if(cards->dedicated_fallback)plan.additional+=cards->count;
        if(source && source->linked)plan.additional=0u;
        if(kui_vmu_game_candidates_preflight(&plan,1u,&s->out->storage,s->log,s->cancel)!=KUI_VMU_GAME_CANDIDATES_OK)
            return stopped(s)?false:fail(s,"Disc card links cannot be extended; check capacity and old cards");
        if(kui_vmu_library_check_allocation(s->cancel)!=KUI_VMU_LIBRARY_OK)
            return stopped(s)?false:fail(s,"Virtual card allocation could not be checked");
        if(!prepare_target(s,request,source,image,work,entries+count))return false;
        bool exists=false;for(unsigned i=0;i<count;i++)if(entries[i].pair==entries[count].pair &&
            !memcmp(entries[i].identity,entries[count].identity,16u))exists=true;
        if(!exists)++count;
    }
    if(!count)return fail(s,"This disc has no linked card; create or link one first");
    if(stopped(s) || !current_storage(s))return false;
    const struct kui_vmu_game_assignment *selected=NULL;
    unsigned wanted=request->op==KUI_DISC_CARDS_DEFAULT?source->pair:
        adding?cards->default_pair:0u;
    if(wanted) for(unsigned i=0;i<count;i++)if(entries[i].pair==wanted)selected=entries+i;
    int r=selected?kui_vmu_game_candidates_apply_choose(entries,count,selected,&s->out->storage,s->log,s->cancel):
        kui_vmu_game_candidates_apply(entries,count,&s->out->storage,s->log,s->cancel);
    if(r!=KUI_VMU_GAME_CANDIDATES_OK)return stopped(s)?false:fail(s,"Card retained; its disc link could not be saved, refresh and retry");
    if(!current_storage(s) || !linked_load(s,cards,work))return false;
    if(request->op==KUI_DISC_CARDS_ASK) {
        r=kui_vmu_game_candidates_set_default(s->out->product,NULL,cards->candidates.sequence,
            &s->out->storage,s->log,s->cancel);
        if(r!=KUI_VMU_GAME_CANDIDATES_OK)return stopped(s)?false:fail(s,"The disc card default could not be saved; refresh and retry");
        if(!current_storage(s) || !linked_load(s,cards,work))return false;
    }
    return true;
}
void kui_disc_cards_run(const struct kui_disc_cards_request *request,
    struct kui_disc_cards_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out)return;
    struct kui_disc_cards_request input;
    if(request) {input=*request;request=&input;}
    memset(out,0,sizeof(*out));struct operation s={out,log?log:quiet,cancel?cancel:never_cancel,progress};
    if(request)out->generation=request->generation;
    if(!request || (unsigned)request->op>KUI_DISC_CARDS_ASK) {fail(&s,"Invalid disc card request");return;}
    if((request->op==KUI_DISC_CARDS_LINK || request->op==KUI_DISC_CARDS_DEFAULT) &&
       !memchr(request->path,0,sizeof(request->path))) {fail(&s,"Invalid disc card path");return;}
    if(stopped(&s))return;
    /* The worker has released held SD/music I/O before this optical call. */
    if(!kui_disc_launcher_metadata(&out->metadata,out->signature,&out->status,s.log,s.cancel,s.progress))return;
    if(!out->metadata.ip_valid || !out->metadata.boot_valid ||
       !kui_vmu_game_card_key(out->metadata.product,out->product)) {fail(&s,"Inserted disc has no usable game release ID");return;}
    bool identify=request->op==KUI_DISC_CARDS_IDENTIFY;
    if(!identify && (!reviewed.identified || request->token!=reviewed.token ||
       !memchr(request->product,0,sizeof(request->product)) || strcmp(request->product,reviewed.product) ||
       memcmp(request->signature,reviewed.signature,32u) || strcmp(out->product,reviewed.product) ||
       memcmp(out->signature,reviewed.signature,32u))) {fail(&s,"Inserted disc changed; identify it again before changing cards");return;}
    out->status=(struct kui_app_status){0};status(&s,"Checking this disc's cards on SD...");
    bool opened=kui_games_storage_open(s.log)==KUI_GAMES_STORAGE_OK,ok=opened;
    struct linked_cards *cards=malloc(sizeof(*cards));
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));uint8_t *image=malloc(KUI_VMU_IMAGE_BYTES);
    if(!opened)fail(&s,"Cannot open the SD card for disc cards");
    if(ok && (!cards || !work || !image))ok=fail(&s,"Not enough RAM to manage disc cards");
    if(ok && !pin(&out->storage))ok=fail(&s,"Cannot verify the SD card holding these virtual cards");
    if(ok && !identify && (!same_pin(&request->storage,&reviewed.storage) || !same_pin(&out->storage,&reviewed.storage)))
        ok=fail(&s,"SD card or partition changed; identify the disc cards again");
    if(ok)ok=linked_load(&s,cards,work);
    bool mutation=request->op>=KUI_DISC_CARDS_LINK;
    if(ok && mutation && (request->candidates_sequence!=reviewed.candidates_sequence || !same_linked(cards,&reviewed)))
        ok=fail(&s,"Disc card links changed; refresh before changing them");
    if(ok && mutation)ok=mutate(&s,request,cards,image,work);
    enum kui_disc_cards_stage stage=request->op==KUI_DISC_CARDS_IMPORTS?KUI_DISC_CARDS_IMPORTS_STAGE:KUI_DISC_CARDS_LINKED_STAGE;
    if(ok)ok=listing(&s,stage,mutation?0u:request->page,cards,image,work);
    if(ok)ok=current_storage(&s) && !stopped(&s);
    if(opened && !kui_games_storage_release())ok=fail(&s,"Could not release the SD card; refresh to confirm saved links");
    uint8_t links[32];bool completed_pending=false;
    if(ok) {
        linked_hash(cards,links);
        if(mutation && pending.valid && !memcmp(pending.signature,out->signature,32u) &&
           same_pin(&pending.storage,&out->storage))
            for(unsigned i=0;i<cards->count;i++)
                if(cards->targets[i].pair==pending.pair && !memcmp(cards->targets[i].identity,pending.identity,16u))
                    completed_pending=true;
    }
    free(image);free(work);free(cards);
    out->status.complete=true;out->status.passed=ok;out->identified=ok;
    if(ok) {
        if(++tokens==0u)++tokens;
        out->token=tokens;
        if(mutation)status(&s,request->op==KUI_DISC_CARDS_ASK?"Disc card default cleared; multiple cards ask at launch":
            request->op==KUI_DISC_CARDS_DEFAULT?"Default card saved for this disc":
            out->created?"New playable card linked to this disc":"Imported card linked to this disc");
        else status(&s,stage==KUI_DISC_CARDS_IMPORTS_STAGE?"Choose a card to link; matching saves are marked":"Disc cards checked");
        reviewed=*out;memcpy(reviewed_links,links,32u);if(completed_pending)pending.valid=false;
    }
    if(progress)progress(&out->status);
}
