/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_game_info.h"
#include "kui/vmu_game_assign.h"
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_emulation_settings.h"
#include "kui/vmu_library.h"
#include "kui/vmu_backing.h"
#include "kui/games_storage.h"
#include "kui/media.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INFO_SCAN_LIMIT 20000u
static const char *const assignment_paths[2]={KUI_VMU_GAME_CARD_ROOT "/assign-a.bin",KUI_VMU_GAME_CARD_ROOT "/assign-b.bin"};
static void unavailable(struct kui_vmu_game_info *out,const char *notice) {
    out->known=false;snprintf(out->label,sizeof(out->label),"Unavailable");
    snprintf(out->notice,sizeof(out->notice),"%s",notice);
}
static bool stopped(struct kui_vmu_game_info *out,kui_cancel_fn cancel) {
    if(!cancel || !cancel()) return false;
    unavailable(out,"Card information cancelled; reopen game options to retry.");return true;
}
/* Missing records and malformed lengths are distinct from actual I/O errors.
 * Two-slot callers may ignore a torn record, but never a failed storage read. */
static bool record(const char *path,void *data,size_t bytes,bool *present,bool *valid) {
    FILINFO info;*present=false;*valid=false;
    FRESULT r=f_stat(path,&info);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return true;
    if(r!=FR_OK) return false;
    *present=true;
    if((info.fattrib&AM_DIR) || info.fsize!=bytes) return true;
    FIL file;UINT got=0;
    if(f_open(&file,path,FA_READ)!=FR_OK) return false;
    bool sized=f_size(&file)==bytes;
    bool ok=!sized || (f_read(&file,data,(UINT)bytes,&got)==FR_OK && got==bytes);
    if(f_close(&file)!=FR_OK) ok=false;
    *valid=sized && ok;return ok;
}
static bool required_record(const char *path,void *data,size_t bytes) {
    bool present,valid;
    return record(path,data,bytes,&present,&valid) && present && valid;
}
static bool assignments(struct kui_vmu_game_assign_table *chosen,struct kui_vmu_game_info *out,
    kui_cancel_fn cancel) {
    uint8_t *wire=malloc(KUI_VMU_GAME_ASSIGN_WIRE_BYTES);
    struct kui_vmu_game_assign_table *candidate=malloc(sizeof(*candidate));
    if(!wire || !candidate) {free(wire);free(candidate);unavailable(out,"Not enough memory to inspect virtual VMU assignments.");return false;}
    bool any=false,found=false,ok=true;memset(chosen,0,sizeof(*chosen));
    for(unsigned i=0;i<2u;i++) {
        if(stopped(out,cancel)) {ok=false;break;}
        bool present,valid;
        if(!record(assignment_paths[i],wire,KUI_VMU_GAME_ASSIGN_WIRE_BYTES,&present,&valid)) {
            unavailable(out,"Cannot read virtual VMU assignments; check the SD card.");ok=false;break;
        }
        any|=present;
        if(!valid || !kui_vmu_game_assign_decode(wire,KUI_VMU_GAME_ASSIGN_WIRE_BYTES,candidate)) continue;
        if(found && chosen->sequence==candidate->sequence && memcmp(chosen,candidate,sizeof(*chosen))) {
            unavailable(out,"Virtual VMU assignment records disagree; inspect VMU tools.");ok=false;break;
        }
        if(!found || candidate->sequence>chosen->sequence) {*chosen=*candidate;found=true;}
    }
    free(wire);free(candidate);
    if(ok && any && !found) {unavailable(out,"Virtual VMU assignments are damaged; inspect VMU tools.");ok=false;}
    return ok;
}
static bool card_metadata(unsigned pair,const uint8_t *expected,bool imported,
    uint8_t identity[16],char image_path[KUI_VMU_BACKUP_PATH_CAP]) {
    char path[KUI_VMU_BACKUP_PATH_CAP];bool nonzero=false;
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/identity.bin",pair);
    if(!required_record(path,identity,16u) || (expected && memcmp(identity,expected,16u))) return false;
    for(unsigned i=0;i<16u;i++) nonzero|=identity[i]!=0;
    if(!nonzero) return false;
    static const char *const leaves[2]={"card.vmd","card.journal"};
    static const unsigned sizes[2]={KUI_VMU_IMAGE_BYTES,1024u};
    for(unsigned i=0;i<2u;i++) {
        FILINFO info;snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/%s",pair,leaves[i]);
        if(f_stat(path,&info)!=FR_OK || (info.fattrib&AM_DIR) || info.fsize!=sizes[i]) return false;
    }
    if(imported) {
        uint8_t marker[64],wanted[64]={0};
        memcpy(wanted,"KUIVMUI1",8u);memcpy(wanted+16u,identity,16u);
        uint32_t crc=kui_crc32(0,wanted,60u);
        for(unsigned i=0;i<4u;i++) wanted[60u+i]=(uint8_t)(crc>>(8u*i));
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/import.bin",pair);
        if(!required_record(path,marker,sizeof(marker)) || memcmp(marker,wanted,sizeof(marker))) return false;
    }
    snprintf(image_path,KUI_VMU_BACKUP_PATH_CAP,KUI_VMU_GAME_CARD_ROOT "/p%04u/card.vmd",pair);
    return true;
}
static bool named_card(unsigned pair,const uint8_t *expected,bool imported,
    struct kui_vmu_game_info *out,kui_cancel_fn cancel) {
    uint8_t identity[16];char path[KUI_VMU_BACKUP_PATH_CAP],name[49]={0};
    if(!card_metadata(pair,expected,imported,identity,path)) {
        unavailable(out,"The selected virtual VMU is missing or its identity changed.");return false;
    }
    if(stopped(out,cancel)) return false;
    if(kui_vmu_library_label(path,identity,name,cancel)!=KUI_VMU_LIBRARY_OK) {
        unavailable(out,"Cannot verify the virtual VMU name; inspect VMU tools.");return false;
    }
    if(name[0]) snprintf(out->label,sizeof(out->label),"%s",name);
    else if(imported) snprintf(out->label,sizeof(out->label),"Shared virtual VMU");
    else snprintf(out->label,sizeof(out->label),"Game card %s",out->product);
    if(out->count>=KUI_VMU_GAME_INFO_CARDS_MAX) {
        unavailable(out,"Too many virtual VMUs for this game.");return false;
    }
    struct kui_vmu_game_info_card *card=&out->cards[out->count++];
    card->pair=pair;memcpy(card->identity,identity,16u);
    snprintf(card->label,sizeof(card->label),"%s",out->label);
    snprintf(out->notice,sizeof(out->notice),"Compatibility is checked at launch.");out->known=true;return true;
}
static bool dedicated_card(struct kui_vmu_game_info *out,kui_cancel_fn cancel) {
    uint16_t *pairs=malloc(KUI_VMU_GAME_CARD_PAIRS_MAX*sizeof(*pairs));
    if(!pairs) {unavailable(out,"Not enough memory to list virtual VMUs.");return false;}
    unsigned count=0,visited=0;DIR directory;FILINFO info;FRESULT r=f_opendir(&directory,KUI_VMU_GAME_CARD_ROOT);
    if(r==FR_NO_FILE || r==FR_NO_PATH) {free(pairs);return true;}
    if(r!=FR_OK) {free(pairs);unavailable(out,"Cannot open the virtual VMU folder.");return false;}
    bool ok=true;
    while((r=f_readdir(&directory,&info))==FR_OK && info.fname[0]) {
        if(stopped(out,cancel)) {ok=false;break;}
        if(++visited>INFO_SCAN_LIMIT) {unavailable(out,"Virtual VMU folder exceeds the scan limit.");ok=false;break;}
        unsigned pair=kui_vmu_game_card_pair_number(info.fname);
        if(pair && (info.fattrib&AM_DIR)) {
            if(count==KUI_VMU_GAME_CARD_PAIRS_MAX) {unavailable(out,"Too many virtual VMU folders.");ok=false;break;}
            pairs[count++]=(uint16_t)pair;
        }
    }
    if(f_closedir(&directory)!=FR_OK || r!=FR_OK) {unavailable(out,"Cannot finish reading the virtual VMU folder.");ok=false;}
    unsigned found=0;
    for(unsigned i=0;ok && i<count;i++) {
        if(stopped(out,cancel)) {ok=false;break;}
        uint8_t binding[KUI_VMU_GAME_CARD_BINDING_BYTES];char key[KUI_VMU_GAME_CARD_KEY_BYTES],path[KUI_VMU_BACKUP_PATH_CAP];
        bool present,valid;snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/game.bin",pairs[i]);
        if(!record(path,binding,sizeof(binding),&present,&valid)) {unavailable(out,"Cannot read a virtual VMU game binding.");ok=false;break;}
        if(!present || !valid || !kui_vmu_game_card_binding_decode(binding,sizeof(binding),key) || strcmp(key,out->product)) continue;
        if(found) {unavailable(out,"Several virtual VMUs claim this game; inspect VMU tools.");ok=false;break;}
        found=pairs[i];
    }
    free(pairs);
    if(ok && found) ok=named_card(found,NULL,false,out,cancel);
    return ok;
}
static bool same_storage(const struct kui_vmu_sd_identity *before,const struct kui_volume *volume) {
    struct kui_vmu_sd_identity now;const struct kui_volume *current=kui_media_volume();
    return current && current->start==volume->start && current->count==volume->count &&
        kui_sd_vmu_profile_identity(&now) && !memcmp(now.cid,before->cid,16u) &&
        now.sectors==before->sectors && now.high_capacity==before->high_capacity;
}
static bool storage_pin(struct kui_vmu_game_candidates_storage *pin) {
    struct kui_vmu_sd_identity id;const struct kui_volume *v=kui_media_volume();
    if(!pin || !v || !v->count ||
       !kui_sd_vmu_profile_identity(&id) || !id.sectors) return false;
    memset(pin,0,sizeof(*pin));memcpy(pin->cid,id.cid,16u);
    pin->card_sectors=id.sectors;pin->high_capacity=id.high_capacity?1u:0u;
    pin->partition_first=v->start;pin->partition_end=(uint64_t)v->start+v->count;return true;
}
static bool same_pin(const struct kui_vmu_game_candidates_storage *a,
    const struct kui_vmu_game_candidates_storage *b) {
    return !memcmp(a->cid,b->cid,16u) && a->card_sectors==b->card_sectors &&
        a->partition_first==b->partition_first && a->partition_end==b->partition_end &&
        a->high_capacity==b->high_capacity;
}
/* ABSENT alone permits the old one-card assignment/binding rules. A present
 * candidate set is authoritative, including its deliberate lack of default. */
static int candidate_cards(struct kui_vmu_game_info *out,kui_log_fn log,kui_cancel_fn cancel) {
    struct kui_vmu_game_candidates_view view;
    int result=kui_vmu_game_candidates_list(out->product,&out->storage,&view,log,cancel);
    if(result==KUI_VMU_GAME_CANDIDATES_ABSENT) return 0;
    if(result!=KUI_VMU_GAME_CANDIDATES_OK || !view.present || !view.count ||
       view.count>KUI_VMU_GAME_INFO_CARDS_MAX || !memchr(view.key,0,sizeof(view.key)) || strcmp(view.key,out->product) ||
       !same_pin(&view.storage,&out->storage) ||
       (view.default_index!=KUI_VMU_GAME_CANDIDATES_NO_DEFAULT && view.default_index>=view.count)) {
        unavailable(out,result==KUI_VMU_GAME_CANDIDATES_CANCELLED?
            "Card information cancelled; reopen game options to retry.":
            "Virtual VMU choices are unavailable; inspect VMU tools.");return -1;
    }
    out->candidates_sequence=view.sequence;
    for(unsigned i=0;i<view.count;i++) {
        const struct kui_vmu_game_candidate_target *target=&view.targets[i];
        if(!named_card(target->pair,target->identity,true,out,cancel)) return -1;
    }
    if(view.default_index!=KUI_VMU_GAME_CANDIDATES_NO_DEFAULT) {
        out->default_pair=out->cards[view.default_index].pair;
        snprintf(out->label,sizeof(out->label),"%s",out->cards[view.default_index].label);
        snprintf(out->notice,sizeof(out->notice),"Default virtual VMU. Choose another card for one launch in game options.");
    } else if(out->count>1u) {
        out->choice_required=true;snprintf(out->label,sizeof(out->label),"%u available cards",(unsigned)out->count);
        snprintf(out->notice,sizeof(out->notice),"Choose a virtual VMU before launch. A saved default is optional.");
    } else snprintf(out->notice,sizeof(out->notice),"The only candidate is selected at launch; no default is saved.");
    return 1;
}
void kui_vmu_game_info_run(const char *product,bool native_route,struct kui_vmu_game_info *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return;
    memset(out,0,sizeof(*out));unavailable(out,"Card information has not been checked.");
    (void)kui_vmu_game_card_key(product,out->product);
    if(stopped(out,cancel)) return;
    if(!native_route) {
        snprintf(out->label,sizeof(out->label),"Physical / loader specific");
        snprintf(out->notice,sizeof(out->notice),"Virtual VMU is not provided by this native route.");out->known=true;return;
    }
    bool opened=kui_games_storage_open(log)==KUI_GAMES_STORAGE_OK;
    if(!opened) {unavailable(out,"Cannot open the SD card; reopen game options to retry.");return;}
    bool ok=kui_vmu_emulation_settings_load(&out->enabled,log);
    if(!ok) unavailable(out,"Virtual VMU preference is unavailable; reload it in VMU tools.");
    if(ok && !out->enabled) {
        snprintf(out->label,sizeof(out->label),"Physical VMUs");
        snprintf(out->notice,sizeof(out->notice),"Virtual VMU is Off.");out->known=true;
    } else if(ok) {
        struct kui_vmu_sd_identity storage={0};struct kui_volume volume={0};const struct kui_volume *current=kui_media_volume();
        ok=current && current->count && kui_sd_vmu_profile_identity(&storage) && storage.sectors;
        if(ok) {volume=*current;ok=storage_pin(&out->storage);}
        else unavailable(out,"Cannot verify the SD card holding the virtual VMUs.");
        struct kui_vmu_game_assign_table *table=ok?malloc(sizeof(*table)):NULL;
        if(ok && !table) {unavailable(out,"Not enough memory to inspect virtual VMU assignments.");ok=false;}
        if(ok && !out->product[0]) {
            snprintf(out->label,sizeof(out->label),"Selected at launch");
            snprintf(out->notice,sizeof(out->notice),"No usable game ID; the installed card is checked at launch.");out->known=true;
        } else if(ok) {
            int candidates=candidate_cards(out,log,cancel);
            if(candidates<0) ok=false;
            else if(candidates==0 && assignments(table,out,cancel)) {
            const struct kui_vmu_game_assignment *entry=kui_vmu_game_assign_find(table,out->product);
            if(entry) {ok=named_card(entry->pair,entry->identity,true,out,cancel);out->default_pair=entry->pair;}
            else {
                ok=dedicated_card(out,cancel);
                if(ok && !out->known) {
                    snprintf(out->label,sizeof(out->label),"Automatic game card");
                    snprintf(out->notice,sizeof(out->notice),"Created when a supported game launches. Compatibility is checked at launch.");out->known=true;
                }
            }
            } else if(candidates==0) ok=false;
        }
        free(table);
        if(ok && !same_storage(&storage,&volume)) {unavailable(out,"SD card or partition changed; reopen game options.");ok=false;}
    }
    if(stopped(out,cancel)) ok=false;
    if(!kui_games_storage_release()) {unavailable(out,"Could not release the SD card; reopen game options.");ok=false;}
    if(!ok) out->known=false;
}
bool kui_vmu_game_info_default(const struct kui_vmu_game_selection *selection,bool clear,
    struct kui_vmu_game_info *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return false;
    memset(out,0,sizeof(*out));unavailable(out,"Virtual VMU default was not changed.");
    char key[12];
    if(!selection || !selection->selected || !memchr(selection->product,0,sizeof(selection->product)) ||
       !kui_vmu_game_card_key(selection->product,key) ||
       memcmp(key,selection->product,sizeof(key))) return false;
    memcpy(out->product,key,sizeof(key));
    if(stopped(out,cancel) || kui_games_storage_open(log)!=KUI_GAMES_STORAGE_OK) return false;
    struct kui_vmu_game_candidates_storage current;
    bool ok=storage_pin(&current) && same_pin(&current,&selection->storage);
    if(!ok) unavailable(out,"SD card or partition changed; reopen game options.");
    if(ok) {
        struct kui_vmu_game_candidate_target target={.pair=selection->pair};
        memcpy(target.identity,selection->identity,16u);
        int result=kui_vmu_game_candidates_set_default(key,clear?NULL:&target,
            selection->candidates_sequence,&current,log,cancel);
        ok=result==KUI_VMU_GAME_CANDIDATES_OK;
        if(!ok) unavailable(out,result==KUI_VMU_GAME_CANDIDATES_STALE?
            "Virtual VMU choices changed; reopen game options before saving a default.":
            "Virtual VMU default could not be saved; reopen game options.");
    }
    if(ok) {
        struct kui_vmu_game_candidates_storage after;
        ok=storage_pin(&after) && same_pin(&after,&selection->storage);
        if(!ok) unavailable(out,"SD card or partition changed; reopen game options.");
    }
    if(!kui_games_storage_release()) {unavailable(out,"Could not release the SD card; reopen game options.");ok=false;}
    if(ok) {kui_vmu_game_info_run(key,true,out,log,cancel);ok=out->known && out->enabled;}
    return ok;
}
