/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_wizard.h"
#include "kui/vmu_image_merge.h"
#include "kui/vmu_game_card_service.h"
#include "kui/games_storage.h"
#include "kui/clock.h"
#include "kui/vmu_backing.h"
#include "kui/vmu_library.h"
#include "kui/media.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIZARD_SCAN_ENTRIES 2000u
#define WIZARD_SCAN_DEPTH 3u
struct wizard_card {
    uint8_t *image;
    struct kui_vmu_wizard_card summary;
    uint8_t identity[16];
};
struct wizard_game {
    char title[129],product[12];
    unsigned card; /* Zero is unselected, otherwise staged card index + 1. */
    bool supported;
};
struct wizard_session {
    struct wizard_card cards[KUI_VMU_WIZARD_CARDS];
    struct wizard_game *games;
    unsigned card_count,game_count,sources,card,page,slot,omitted_folders,unrecognized;
    enum kui_vmu_wizard_stage stage;
    bool catalogued,publication_started,existing_card,copy_on_commit,view_only;
    unsigned source_pair;
    uint8_t source_identity[16];
    bool storage_pinned;
    struct kui_vmu_sd_identity storage;
    struct kui_volume volume;
    char root[KUI_DEST_ROOT_CAP];
};
static struct wizard_session wizard,suspended;
static bool suspended_valid;
struct operation {
    struct kui_vmu_wizard_view *out;
    kui_log_fn log;
    kui_cancel_fn cancel;
    kui_app_progress_fn progress;
};
static bool stop(struct operation *s) {
    if(!s->cancel || !s->cancel()) return false;
    s->out->status.stopped=true;
    snprintf(s->out->status.message,sizeof(s->out->status.message),"Import stopped; captured saves are retained");
    return true;
}
static void notice(struct operation *s,const char *format,...) {
    va_list args;va_start(args,format);
    vsnprintf(s->out->status.message,sizeof(s->out->status.message),format,args);
    va_end(args);
    if(s->progress) s->progress(&s->out->status);
}
static bool fail(struct operation *s,const char *message) {
    ++s->out->status.errors;notice(s,"%s",message);
    if(s->log) s->log("VMU wizard: %s",message);
    return false;
}
static void discard_session(struct wizard_session *session) {
    for(unsigned i=0;i<session->card_count;i++) free(session->cards[i].image);
    free(session->games);memset(session,0,sizeof(*session));
}
static void reset(void) {
    discard_session(&wizard);discard_session(&suspended);suspended_valid=false;
}
static void counts(void) {
    for(unsigned i=0;i<wizard.card_count;i++) wizard.cards[i].summary.games=0;
    for(unsigned i=0;i<wizard.game_count;i++)
        if(wizard.games[i].card && wizard.games[i].card<=wizard.card_count)
            ++wizard.cards[wizard.games[i].card-1].summary.games;
}
static unsigned selected_count(void) {
    unsigned count=0;
    for(unsigned i=0;i<wizard.game_count;i++) if(wizard.games[i].card) ++count;
    return count;
}
static void view(struct operation *s,unsigned generation) {
    struct kui_vmu_wizard_view *out=s->out;
    counts();out->generation=generation;out->stage=wizard.stage;
    out->publication_started=wizard.publication_started;
    out->existing_card=wizard.existing_card;out->copy_on_commit=wizard.copy_on_commit;
    out->view_only=wizard.view_only;
    out->slot=wizard.slot;out->card=wizard.card;out->cards=wizard.card_count;
    out->sources=wizard.sources;out->page=wizard.page;out->total=wizard.game_count;
    out->selected_games=selected_count();
    for(unsigned i=0;i<wizard.card_count;i++) out->summary[i]=wizard.cards[i].summary;
    unsigned start=wizard.page*KUI_VMU_WIZARD_ROWS;
    for(unsigned i=start;i<wizard.game_count && out->count<KUI_VMU_WIZARD_ROWS;i++) {
        struct kui_vmu_wizard_game *row=&out->games[out->count++];
        memcpy(row->title,wizard.games[i].title,sizeof(row->title));
        memcpy(row->product,wizard.games[i].product,sizeof(row->product));
        row->selected=wizard.games[i].card==wizard.card+1;
        row->supported=wizard.games[i].supported;
    }
    out->status.complete=true;
    out->status.passed=!out->status.errors && !out->status.stopped;
}
static bool capture(struct operation *s,const struct kui_vmu_wizard_request *request) {
    if(wizard.stage!=KUI_VMU_WIZARD_SOURCES)
        return fail(s,"Return to source cards before importing a VMU");
    if(wizard.publication_started)
        return fail(s,"Cards already prepared; finish assigning or start a new import");
    if(request->slot>=8) return fail(s,"Choose a physical VMU slot");
    uint8_t *incoming=malloc(KUI_VMU_IMAGE_BYTES);
    struct kui_vmu_view *physical=calloc(1,sizeof(*physical));
    struct kui_vmu_image_summary summary;
    if(!incoming || !physical) {free(incoming);free(physical);return fail(s,"Not enough memory to read this VMU");}
    notice(s,"Reading physical VMU; keep it connected");
    bool ok=kui_vmu_capture_run(request->slot,incoming,KUI_VMU_IMAGE_BYTES,&summary,
        physical,s->log,s->cancel,s->progress);
    if(!ok) {
        s->out->status=physical->status;
        free(incoming);free(physical);return false;
    }
    free(physical);
    if(stop(s)) {free(incoming);return false;}
    unsigned card=wizard.card_count?request->card:0;
    if(card>=wizard.card_count && wizard.card_count) {free(incoming);return fail(s,"The selected staged card no longer exists");}
    const char *reason=NULL;
    unsigned duplicates=0;
    if(wizard.card_count) {
        uint8_t *merged=malloc(KUI_VMU_IMAGE_BYTES);
        struct kui_vmu_image_merge_workspace *work=malloc(sizeof(*work));
        struct kui_vmu_image_merge_report report;
        if(!merged || !work) {
            free(merged);free(work);free(incoming);
            return fail(s,"Not enough memory to combine the captured cards");
        }
        int result=kui_vmu_image_merge(wizard.cards[card].image,incoming,merged,
            KUI_VMU_IMAGE_BYTES,work,&report);
        free(work);
        if(result==KUI_VMU_MERGE_OK) {
            free(wizard.cards[card].image);wizard.cards[card].image=merged;
            summary=report.summary;duplicates=report.duplicates;
            free(incoming);incoming=NULL;
        } else {
            free(merged);
            if(result==KUI_VMU_MERGE_NAME_CONFLICT) reason="Different saves share a filename";
            else if(result==KUI_VMU_MERGE_FULL) reason="The combined card would be full";
            else if(result==KUI_VMU_MERGE_MINIGAME_CONFLICT) reason="Each mini-game needs its own card";
            else {free(incoming);return fail(s,"Captured card could not be safely combined");}
            card=wizard.card_count;
        }
    }
    if(incoming) {
        if(wizard.card_count==KUI_VMU_WIZARD_CARDS) {
            free(incoming);return fail(s,"Eight cards are staged; finish this import before adding more");
        }
        wizard.cards[card].image=incoming;++wizard.card_count;
    }
    wizard.cards[card].summary.files=summary.files;
    wizard.cards[card].summary.blocks=summary.used_blocks;
    ++wizard.cards[card].summary.sources;++wizard.sources;wizard.card=card;
    if(reason) notice(s,"%s; preserved on card %u",reason,card+1);
    else if(duplicates) notice(s,"VMU added to card %u; %u identical saves already present",card+1,duplicates);
    else notice(s,"VMU added to card %u; add another VMU or choose games",card+1);
    return true;
}

struct discovery {
    struct operation *operation;
    char **paths;
    unsigned count,visited,omitted_folders;
    bool failed;
};
struct directory {struct discovery *scan;const char *root;unsigned depth;};
static bool discover(struct discovery *scan,const char *root,unsigned depth);
static bool discover_entry(void *context,const char *name,bool directory) {
    struct directory *at=context;struct discovery *scan=at->scan;
    if(stop(scan->operation)) return false;
    if(++scan->visited>WIZARD_SCAN_ENTRIES) {
        scan->failed=true;return fail(scan->operation,"Game scan limit reached; reorganize Games and start a new import");
    }
    if(directory) {
        char child[KUI_DEST_ROOT_CAP];
        if(!kui_destination_join(child,at->root,name)) {
            scan->failed=true;return fail(scan->operation,"A game folder path exceeds the scan limit");
        }
        if(at->depth+1u<WIZARD_SCAN_DEPTH) {
            if(!discover(scan,child,at->depth+1u)) return false;
        } else ++scan->omitted_folders;
        return true;
    }
    char image[KUI_GAMES_FILE_CAP];
    int n=snprintf(image,sizeof(image),"%s%s%s",at->root,strcmp(at->root,"/")?"/":"",name);
    if(n<0 || n>=(int)sizeof(image)) {
        scan->failed=true;return fail(scan->operation,"A game image path exceeds the scan limit");
    }
    char *copy=malloc((size_t)n+1);
    if(!copy) {scan->failed=true;return fail(scan->operation,"Not enough memory to discover the Games folder");}
    memcpy(copy,image,(size_t)n+1);scan->paths[scan->count++]=copy;
    return true;
}
static bool discover(struct discovery *scan,const char *root,unsigned depth) {
    struct directory at={scan,root,depth};
    return kui_games_visit_images(root,discover_entry,&at,scan->operation->cancel);
}
static int game_order(const void *left,const void *right) {
    const struct wizard_game *a=left,*b=right;
    int result=strcmp(a->title,b->title);
    return result?result:strcmp(a->product,b->product);
}
static void title_text(char *text) {
    /* VMS/IP metadata is not a trusted source of UI control characters. */
    for(;*text;text++) if((unsigned char)*text<32 || (unsigned char)*text==127) *text=' ';
}
static void catalogue_notice(struct operation *s) {
    if(wizard.view_only) notice(s,"%u games use this virtual VMU (read only)",wizard.game_count);
    else if(wizard.omitted_folders || wizard.unrecognized)
        notice(s,"%u releases; %u unidentified images; %u deeper folders not scanned",
            wizard.game_count,wizard.unrecognized,wizard.omitted_folders);
    else notice(s,"%u releases found; native eligibility is checked again at launch",wizard.game_count);
}
static bool catalogue(struct operation *s,const char *root) {
    char normalized[KUI_DEST_ROOT_CAP];
    if(!root || !memchr(root,0,KUI_DEST_ROOT_CAP) || !kui_destination_normalize(normalized,root))
        return fail(s,"Choose a valid Games folder");
    if(wizard.catalogued && !strcmp(wizard.root,normalized)) {catalogue_notice(s);return true;}
    if(wizard.catalogued && selected_count())
        return fail(s,"Start a new import before changing the Games folder");
    struct discovery scan={.operation=s};
    scan.paths=calloc(WIZARD_SCAN_ENTRIES,sizeof(*scan.paths));
    struct wizard_game *games=calloc(KUI_VMU_WIZARD_GAMES,sizeof(*games));
    struct kui_games_detail *detail=malloc(sizeof(*detail));
    if(!scan.paths || !games || !detail) {
        free(scan.paths);free(games);free(detail);return fail(s,"Not enough memory to list your games");
    }
    notice(s,"Finding games in %s",normalized);
    bool ok=kui_games_storage_open(s->log)==KUI_GAMES_STORAGE_OK;
    if(ok) ok=discover(&scan,normalized,0);
    if(!kui_games_storage_release()) ok=false;
    unsigned count=0,unrecognized=0;
    if(ok && !scan.failed && !stop(s)) {
        /* Discovery closes every directory before inspection opens images.
         * The ordinary read-only inspector may now share its own SD session. */
        kui_games_storage_hold(true);
        for(unsigned i=0;i<scan.count;i++) {
            if(stop(s)) {ok=false;break;}
            s->out->status.done=i;s->out->status.total=scan.count;
            notice(s,"Checking game %u of %u",i+1,scan.count);
            if(!kui_games_inspect(scan.paths[i],detail,s->log,s->cancel) || !detail->valid) {
                if(detail->stopped || stop(s)) {ok=false;break;}
                ++unrecognized;continue;
            }
            char key[12];
            if(!kui_vmu_game_card_key(detail->product,key)) {++unrecognized;continue;}
            unsigned at=0;while(at<count && strcmp(games[at].product,key)) ++at;
            bool supported=!detail->windows_ce && (detail->native_gd || detail->native_cd);
            if(at<count) {games[at].supported|=supported;continue;}
            if(count==KUI_VMU_WIZARD_GAMES) {
                ok=fail(s,"More than 256 game releases found; reorganize Games and start a new import");break;
            }
            memcpy(games[count].product,key,sizeof(key));
            snprintf(games[count].title,sizeof(games[count].title),"%s",detail->title[0]?detail->title:key);
            title_text(games[count].title);games[count++].supported=supported;
        }
        kui_games_storage_hold(false);
        if(!kui_games_storage_release()) ok=false;
    } else ok=false;
    for(unsigned i=0;i<scan.count;i++) free(scan.paths[i]);
    free(scan.paths);free(detail);
    if(!ok) {
        free(games);
        if(!s->out->status.errors && !s->out->status.stopped)
            fail(s,"Could not complete the Games scan; try again");
        return false;
    }
    qsort(games,count,sizeof(*games),game_order);
    free(wizard.games);wizard.games=games;wizard.game_count=count;
    wizard.catalogued=true;memcpy(wizard.root,normalized,sizeof(wizard.root));wizard.page=0;
    wizard.omitted_folders=scan.omitted_folders;wizard.unrecognized=unrecognized;
    s->out->status.done=s->out->status.total;
    catalogue_notice(s);
    return true;
}
static void salt(uint8_t out[16],unsigned card) {
    static uint32_t calls;struct kui_datetime now;int64_t seconds=0;
    if(kui_clock_now(&now)) (void)kui_clock_to_seconds(&now,&seconds);
    uint32_t words[4]={(uint32_t)seconds,(uint32_t)((uint64_t)seconds>>32),++calls,card};
    memcpy(out,words,sizeof(words));
}
/* A failed commit can leave a completed pair for retry. File identity alone
 * cannot distinguish an SD clone whose same pNNNN has since diverged. Pin
 * the physical SCI identity and mounted partition before any publication. */
static bool same_storage(struct operation *s,bool pin) {
    struct kui_vmu_sd_identity identity={0};
    const struct kui_volume *volume=kui_media_volume();
    if(!kui_sd_vmu_profile_identity(&identity) || !identity.sectors || !volume || !volume->count)
        return fail(s,"Cannot verify the SCI SD card; restore it and retry");
    if(wizard.storage_pinned) {
        if(memcmp(identity.cid,wizard.storage.cid,sizeof(identity.cid)) ||
           identity.sectors!=wizard.storage.sectors || identity.high_capacity!=wizard.storage.high_capacity ||
           volume->start!=wizard.volume.start || volume->count!=wizard.volume.count)
            return fail(s,"SD card or partition changed; restore the original card or start a new import");
    } else if(pin) {
        wizard.storage=identity;wizard.volume=*volume;wizard.storage_pinned=true;
    }
    return true;
}
static bool source_file(const char *path,void *out,size_t bytes) {
    FIL file;UINT got=0;
    if(f_open(&file,path,FA_READ)!=FR_OK) return false;
    bool ok=f_size(&file)==bytes && f_read(&file,out,(UINT)bytes,&got)==FR_OK && got==bytes;
    return f_close(&file)==FR_OK && ok;
}
static int staged_read(void *context,uint16_t block,void *out) {
    if(block>=KUI_VMU_IMAGE_BLOCKS) return -1;
    memcpy(out,(const uint8_t *)context+(size_t)block*KUI_VMU_IMAGE_BLOCK_BYTES,KUI_VMU_IMAGE_BLOCK_BYTES);
    return 0;
}
static bool used_by_assignments(struct operation *s,struct kui_vmu_game_assign_table *table) {
    static const char *const paths[2]={KUI_VMU_GAME_CARD_ROOT "/assign-a.bin",KUI_VMU_GAME_CARD_ROOT "/assign-b.bin"};
    uint8_t *wire=malloc(KUI_VMU_GAME_ASSIGN_WIRE_BYTES);
    struct kui_vmu_game_assign_table *candidate=malloc(sizeof(*candidate));
    if(!wire || !candidate) {free(wire);free(candidate);return fail(s,"Not enough memory to read game assignments");}
    bool ok=true,any=false,found=false;memset(table,0,sizeof(*table));
    for(unsigned i=0;i<2u;i++) {
        if(stop(s)) {ok=false;break;}
        FILINFO info;FRESULT r=f_stat(paths[i],&info);
        if(r==FR_NO_FILE || r==FR_NO_PATH) continue;
        if(r!=FR_OK) {ok=fail(s,"Cannot inspect the game assignments");break;}
        any=true;
        if((info.fattrib&AM_DIR) || info.fsize!=KUI_VMU_GAME_ASSIGN_WIRE_BYTES) continue;
        if(!source_file(paths[i],wire,KUI_VMU_GAME_ASSIGN_WIRE_BYTES)) {ok=fail(s,"Cannot read the game assignments");break;}
        if(!kui_vmu_game_assign_decode(wire,KUI_VMU_GAME_ASSIGN_WIRE_BYTES,candidate)) continue;
        if(found && table->sequence==candidate->sequence && memcmp(table,candidate,sizeof(*table))) {
            ok=fail(s,"Game assignment records disagree; inspect their backup");break;
        }
        if(!found || candidate->sequence>table->sequence) {*table=*candidate;found=true;}
    }
    if(ok && any && !found) ok=fail(s,"Game assignments are damaged; inspect their backup");
    free(wire);free(candidate);return ok;
}
static bool used_by(struct operation *s) {
    struct kui_vmu_game_assign_table *table=malloc(sizeof(*table));
    char (*keys)[KUI_VMU_GAME_CARD_KEY_BYTES]=calloc(KUI_VMU_GAME_ASSIGN_MAX+1u,sizeof(*keys));
    if(!table || !keys) {free(table);free(keys);return fail(s,"Not enough memory to list games using this card");}
    bool ok=kui_games_storage_open(s->log)==KUI_GAMES_STORAGE_OK;
    if(!ok) fail(s,"Cannot open the SD card for game assignments");
    if(ok) ok=same_storage(s,false);
    if(ok) ok=used_by_assignments(s,table);
    unsigned count=0,stale=0;
    for(unsigned i=0;ok && i<table->count;i++) {
        const struct kui_vmu_game_assignment *entry=table->entries+i;
        if(entry->pair!=wizard.source_pair) continue;
        if(memcmp(entry->identity,wizard.source_identity,16u)) {++stale;continue;}
        memcpy(keys[count++],entry->key,sizeof(entry->key));
    }
    if(ok && wizard.source_pair) {
        char path[96],key[12];uint8_t wire[KUI_VMU_GAME_CARD_BINDING_BYTES];FILINFO info;
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/game.bin",wizard.source_pair);
        FRESULT r=f_stat(path,&info);
        if(r!=FR_NO_FILE && r!=FR_NO_PATH) {
            if(r!=FR_OK || (info.fattrib&AM_DIR) || !source_file(path,wire,sizeof(wire)) ||
               !kui_vmu_game_card_binding_decode(wire,sizeof(wire),key)) ok=fail(s,"The card's game binding could not be checked");
            else if(!kui_vmu_game_assign_find(table,key)) memcpy(keys[count++],key,sizeof(key));
        }
    }
    if(ok) ok=same_storage(s,false);
    if(!kui_games_storage_release()) ok=fail(s,"Could not release the SD card after reading assignments");
    if(ok && count>KUI_VMU_WIZARD_GAMES) ok=fail(s,"More than 256 games use this card; a complete list cannot be shown");
    if(ok && stop(s)) ok=false;
    if(ok) {
        unsigned kept=0,missing=0;
        /* Keep the full catalogue's human titles where available. Unrecognized
         * or absent library images still get a row, so users are never hidden. */
        for(unsigned i=0;i<wizard.game_count;i++) {
            unsigned at=0;while(at<count && strcmp(keys[at],wizard.games[i].product)) ++at;
            if(at==count) continue;
            wizard.games[kept]=wizard.games[i];wizard.games[kept++].card=0;keys[at][0]=0;
        }
        for(unsigned i=0;i<count;i++) if(keys[i][0]) {
            struct wizard_game *game=wizard.games+kept++;memset(game,0,sizeof(*game));
            memcpy(game->product,keys[i],sizeof(game->product));
            snprintf(game->title,sizeof(game->title),"Unknown title: %s",keys[i]);++missing;
        }
        wizard.game_count=kept;wizard.page=0;qsort(wizard.games,kept,sizeof(*wizard.games),game_order);
        if(stale) notice(s,"%u games use this card; %u stale identity assignments need attention",kept,stale);
        else if(missing) notice(s,"%u games use this card; %u titles are absent from the Games scan",kept,missing);
        else notice(s,kept?"%u games use this virtual VMU (read only)":"No explicit game assignments use this card",kept);
    } else wizard.game_count=0;
    free(table);free(keys);return ok;
}
/* Open an existing virtual VMU without ever writing its image or journal.
 * Imported pNNNN pairs already have the service's shared-card admission marker;
 * other cards become a new shared copy at the normal confirmation boundary. */
static bool use_card(struct operation *s,const struct kui_vmu_wizard_request *request) {
    static const char prefix[]="0:/KUI/vmu-test/p";
    if(!memchr(request->card_path,0,sizeof(request->card_path)) ||
       !kui_vmu_library_path(request->card_path)) return fail(s,"Choose a virtual VMU from the card list");
    uint8_t *image=malloc(KUI_VMU_IMAGE_BYTES);
    struct kui_vmu_image_workspace *work=malloc(sizeof(*work));
    struct kui_vmu_image_summary summary={0};
    if(!image || !work) {free(image);free(work);return fail(s,"Not enough memory to read this virtual VMU");}
    bool opened=kui_games_storage_open(s->log)==KUI_GAMES_STORAGE_OK,ok=opened,reuse=false;
    struct kui_vmu_sd_identity storage={0},check={0};struct kui_volume volume={0};
    unsigned pair=0;uint8_t identity[16]={0};
    if(ok) {
        const struct kui_volume *current=kui_media_volume();
        ok=kui_sd_vmu_profile_identity(&storage) && storage.sectors && current && current->count;
        if(ok) volume=*current;
        else fail(s,"Cannot verify the SD card holding this virtual VMU");
    } else fail(s,"Cannot open the SD card for this virtual VMU");
    if(ok && kui_vmu_library_read(request->card_path,image,s->log,s->cancel)!=KUI_VMU_LIBRARY_OK)
        ok=fail(s,"Virtual VMU changed or could not be checked; refresh the card list");
    struct kui_vmu_image_io io={image,staged_read,NULL,NULL};
    if(ok && kui_vmu_image_validate(&io,KUI_VMU_IMAGE_BYTES,work,&summary)!=KUI_VMU_IMAGE_OK)
        ok=fail(s,"The virtual VMU image is damaged or unsupported");
    if(ok && !strncmp(request->card_path,prefix,sizeof(prefix)-1u)) {
        char name[6]={'p',0,0,0,0,0},path[KUI_VMU_BACKUP_PATH_CAP];
        memcpy(name+1,request->card_path+sizeof(prefix)-1u,4u);
        pair=kui_vmu_game_card_pair_number(name);
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/identity.bin",pair);
        if(!pair || !source_file(path,identity,sizeof(identity))) ok=fail(s,"Virtual VMU identity could not be checked");
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/import.bin",pair);
        FILINFO info;FRESULT present=ok?f_stat(path,&info):FR_NO_FILE;
        if(ok && present==FR_OK) {
            uint8_t marker[64],expected[64]={0};memcpy(expected,"KUIVMUI1",8u);memcpy(expected+16u,identity,16u);
            uint32_t crc=kui_crc32(0,expected,60u);
            for(unsigned i=0;i<4u;i++) expected[60u+i]=(uint8_t)(crc>>(8u*i));
            if((info.fattrib&AM_DIR) || !source_file(path,marker,sizeof(marker)) || memcmp(marker,expected,sizeof(marker)))
                ok=fail(s,"The shared virtual VMU identity marker is damaged");
            else reuse=true;
        } else if(ok && present!=FR_NO_FILE) ok=fail(s,"Cannot inspect the virtual VMU assignment marker");
        /* Dedicated/legacy cards need a new shared pair; never add an import
         * marker to an existing card or change its original game binding. */
    }
    if(ok) {
        const struct kui_volume *current=kui_media_volume();
        if(!kui_sd_vmu_profile_identity(&check) || memcmp(check.cid,storage.cid,16u) ||
           check.sectors!=storage.sectors || check.high_capacity!=storage.high_capacity ||
           !current || current->start!=volume.start || current->count!=volume.count)
            ok=fail(s,"SD card or partition changed while reading the virtual VMU");
    }
    if(!kui_games_storage_release()) ok=fail(s,"Could not release the SD card after reading the virtual VMU");
    free(work);
    if(!ok || stop(s)) {free(image);return false;}
    if(request->view_only) {
        if(wizard.view_only) discard_session(&wizard);
        else {
            discard_session(&suspended);suspended=wizard;suspended_valid=true;
            memset(&wizard,0,sizeof(wizard));
        }
    } else reset();
    wizard.existing_card=true;wizard.copy_on_commit=!reuse;wizard.view_only=request->view_only;
    wizard.source_pair=pair;memcpy(wizard.source_identity,identity,sizeof(identity));
    wizard.storage=storage;wizard.volume=volume;wizard.storage_pinned=true;
    wizard.card_count=1;wizard.cards[0].image=image;
    wizard.cards[0].summary=(struct kui_vmu_wizard_card){.files=summary.files,.blocks=summary.used_blocks,.pair=reuse?pair:0};
    if(reuse) memcpy(wizard.cards[0].identity,identity,sizeof(identity));
    if(!catalogue(s,request->root)) return false;
    wizard.stage=KUI_VMU_WIZARD_GAMES_STAGE;
    if(wizard.view_only) return used_by(s);
    notice(s,reuse?"Choose games; this existing virtual VMU will be reused. Other assignments stay.":
        "Choose games; a new shared copy after review keeps the original and other assignments.");
    return true;
}
static bool commit(struct operation *s) {
    if(wizard.stage!=KUI_VMU_WIZARD_REVIEW || !selected_count())
        return fail(s,"Review at least one game assignment before creating cards");
    if(stop(s)) return false;
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    struct kui_vmu_game_assignment *entries=calloc(KUI_VMU_WIZARD_GAMES,sizeof(*entries));
    if(!work || !entries) {free(work);free(entries);return fail(s,"Not enough memory to prepare imported cards");}
    counts();struct kui_vmu_game_card_choice choice={0};
    bool ok=kui_games_storage_open(s->log)==KUI_GAMES_STORAGE_OK;
    if(!ok) fail(s,"Cannot open the SD card for the import");
    if(ok) ok=same_storage(s,true);
    if(ok && kui_vmu_library_check_allocation(s->cancel)!=KUI_VMU_LIBRARY_OK) {
        if(!stop(s)) fail(s,"VMU file allocation could not be verified; check the SD card before assigning games");
        ok=false;
    }
    if(ok) wizard.publication_started=true;
    for(unsigned i=0;ok && i<wizard.card_count;i++) {
        struct wizard_card *card=&wizard.cards[i];
        if(card->summary.pair) continue;
        if(stop(s)) {ok=false;break;}
        if(!same_storage(s,false)) {ok=false;break;}
        notice(s,"Creating virtual card %u; previous cards are preserved",i+1);
        uint8_t fresh[16];salt(fresh,i);
        int result=kui_vmu_game_card_import_publish(card->image,KUI_VMU_IMAGE_BYTES,fresh,
            work,&choice,card->identity,s->log,s->cancel);
        if(result!=KUI_VMU_GAME_CARD_READY) {
            if(result==KUI_VMU_GAME_CARD_CANCELLED) {
                s->out->status.stopped=true;notice(s,"Import stopped; prepared cards are retained for retry");
            } else fail(s,choice.problem[0]?choice.problem:"Could not publish the imported card");
            ok=false;break;
        }
        if(!same_storage(s,false)) {ok=false;break;}
        card->summary.pair=choice.pair;
    }
    unsigned count=0;
    for(unsigned i=0;ok && i<wizard.game_count;i++) {
        struct wizard_game *game=&wizard.games[i];if(!game->card) continue;
        struct wizard_card *card=&wizard.cards[game->card-1];
        memcpy(entries[count].key,game->product,sizeof(entries[count].key));
        entries[count].pair=card->summary.pair;
        memcpy(entries[count].identity,card->identity,sizeof(entries[count].identity));++count;
    }
    if(ok && stop(s)) ok=false;
    if(ok) ok=same_storage(s,false);
    if(ok) {
        notice(s,"Saving %u game assignments together",count);
        int result=kui_vmu_game_card_assignments_apply(entries,count,work,&choice,s->log,s->cancel);
        if(result!=KUI_VMU_GAME_CARD_READY) {
            if(result==KUI_VMU_GAME_CARD_CANCELLED) {
                s->out->status.stopped=true;notice(s,"Assignment stopped; prepared cards are retained for retry");
            } else fail(s,choice.problem[0]?choice.problem:"Could not publish game assignments");
            ok=false;
        }
        if(ok) ok=same_storage(s,false);
    }
    if(!kui_games_storage_release()) {
        ok=false;fail(s,"Could not release the SD card; assignments may be saved, retry to confirm");
    }
    free(work);free(entries);
    if(ok) {
        wizard.stage=KUI_VMU_WIZARD_DONE;
        for(unsigned i=0;i<wizard.card_count;i++) {free(wizard.cards[i].image);wizard.cards[i].image=NULL;}
        notice(s,"Ready: cards and game assignments saved");
    }
    return ok;
}
void kui_vmu_wizard_run(const struct kui_vmu_wizard_request *request,
    struct kui_vmu_wizard_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));struct operation s={out,log,cancel,progress};
    if(!request) {fail(&s,"Missing import request");view(&s,0);return;}
    if(request->slot<8) wizard.slot=request->slot;
    if(request->card<wizard.card_count) wizard.card=request->card;
    if(stop(&s)) {view(&s,request->generation);return;}
    if(wizard.view_only && (request->op==KUI_VMU_WIZARD_CAPTURE || request->op==KUI_VMU_WIZARD_TOGGLE ||
       request->op==KUI_VMU_WIZARD_SHOW_SOURCES || request->op==KUI_VMU_WIZARD_SHOW_REVIEW ||
       request->op==KUI_VMU_WIZARD_COMMIT)) {
        fail(&s,"This game list is read only; use Assign games to change card choices");view(&s,request->generation);return;
    }
    switch(request->op) {
    case KUI_VMU_WIZARD_OPEN:
        if(wizard.view_only) {
            discard_session(&wizard);
            if(suspended_valid) {wizard=suspended;memset(&suspended,0,sizeof(suspended));suspended_valid=false;}
        }
        if(wizard.stage==KUI_VMU_WIZARD_DONE)
            notice(&s,"Ready: card assignments saved; compatibility is checked at launch");
        else if(wizard.publication_started)
            notice(&s,"Prepared cards are preserved; return to review and retry the import");
        else if(wizard.stage==KUI_VMU_WIZARD_GAMES_STAGE) catalogue_notice(&s);
        else notice(&s,"Import physical VMUs, then choose the games for each virtual card");
        break;
    case KUI_VMU_WIZARD_RESET: {
        bool published=wizard.publication_started;
        reset();wizard.slot=request->slot<8?request->slot:0;
        notice(&s,published?"New import; prepared cards and published assignments are preserved":
            "New import; choose the physical VMU slot");break;
    }
    case KUI_VMU_WIZARD_CAPTURE:
        if(wizard.existing_card) {fail(&s,"Start a new import before adding a physical VMU");break;}
        (void)capture(&s,request);break;
    case KUI_VMU_WIZARD_USE_CARD:
        (void)use_card(&s,request);break;
    case KUI_VMU_WIZARD_LIST_GAMES:
        if(wizard.stage==KUI_VMU_WIZARD_DONE) {fail(&s,"Start a new import before choosing games");break;}
        if(!wizard.card_count) {fail(&s,"Import a physical VMU before choosing games");break;}
        if(catalogue(&s,request->root)) {
            wizard.stage=KUI_VMU_WIZARD_GAMES_STAGE;
            unsigned pages=wizard.game_count?(wizard.game_count-1)/KUI_VMU_WIZARD_ROWS+1:1;
            wizard.page=request->page<pages?request->page:pages-1;
        }
        break;
    case KUI_VMU_WIZARD_TOGGLE: {
        if(wizard.publication_started) {
            fail(&s,"Assignments are locked; retry the import or start a new import");break;
        }
        if(wizard.stage!=KUI_VMU_WIZARD_GAMES_STAGE || !wizard.card_count ||
           request->card>=wizard.card_count || request->page!=wizard.page ||
           request->index<wizard.page*KUI_VMU_WIZARD_ROWS ||
           request->index-wizard.page*KUI_VMU_WIZARD_ROWS>=KUI_VMU_WIZARD_ROWS ||
           request->index>=wizard.game_count) {
            fail(&s,"Refresh the game list before selecting this row");break;
        }
        struct wizard_game *game=&wizard.games[request->index];
        if(!game->supported) {fail(&s,"This loader is not eligible for native virtual VMU assignments");break;}
        game->card=game->card==wizard.card+1?0:wizard.card+1;
        notice(&s,game->card?"%s will use card %u":"%s keeps its previous card",game->title,wizard.card+1);
        break;
    }
    case KUI_VMU_WIZARD_SHOW_SOURCES:
        if(wizard.stage==KUI_VMU_WIZARD_DONE) fail(&s,"Start a new import to add more physical VMUs");
        else {wizard.stage=KUI_VMU_WIZARD_SOURCES;notice(&s,"Add another physical VMU or choose games");}
        break;
    case KUI_VMU_WIZARD_SHOW_REVIEW:
        if(wizard.stage!=KUI_VMU_WIZARD_GAMES_STAGE || !selected_count())
            fail(&s,"Choose at least one eligible game before reviewing");
        else {wizard.stage=KUI_VMU_WIZARD_REVIEW;notice(&s,wizard.existing_card?
            (wizard.copy_on_commit?"Confirm to create a shared copy and assign the selected games":
             "Confirm to assign the selected games to this existing virtual VMU"):
            "Confirm to create cards and switch the selected games");}
        break;
    case KUI_VMU_WIZARD_COMMIT:
        (void)commit(&s);break;
    default:fail(&s,"Unknown import request");break;
    }
    view(&s,request->generation);
}
