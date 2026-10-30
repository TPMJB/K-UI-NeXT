/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_wizard.h"
#include "kui/vmu_import_plan.h"
#include "kui/vmu_game_candidates.h"
#include "kui/vmu_collection.h"
#include "kui/hash.h"
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
    struct kui_vmu_import_plan *plan;
    struct kui_vmu_wizard_card summary;
    unsigned source_slot,archive_floor;
    bool archive_attempted;
    uint8_t source_hash[32];
    struct kui_vmu_collection_card archive;
    uint8_t identity[16];
};
#define WIZARD_PICK_BYTES ((KUI_VMU_IMAGE_DIRECTORY_ENTRIES+7u)/8u)
struct wizard_output { unsigned pair;uint8_t identity[16];bool labelled; };
struct wizard_game {
    char title[129],product[12];
    unsigned card; /* Zero is unselected, otherwise staged card index + 1. */
    bool supported,accepted,blank;
    uint8_t picks[KUI_VMU_WIZARD_CARDS][WIZARD_PICK_BYTES];
    struct wizard_output outputs[KUI_VMU_WIZARD_CARDS];
};
struct wizard_session {
    struct wizard_card cards[KUI_VMU_WIZARD_CARDS];
    struct wizard_game *games;
    unsigned card_count,game_count,sources,card,page,slot,omitted_folders,unrecognized;
    enum kui_vmu_wizard_stage stage;
    bool catalogued,publication_started,existing_card,copy_on_commit,view_only;
    unsigned save_game,save_page;
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
    for(unsigned i=0;i<session->card_count;i++) {free(session->cards[i].image);free(session->cards[i].plan);}
    free(session->games);memset(session,0,sizeof(*session));
}
static void reset(void) {
    discard_session(&wizard);discard_session(&suspended);suspended_valid=false;
}
static bool picked(const struct wizard_game *game,unsigned source,unsigned index) {
    return (game->picks[source][index/8u]&(1u<<(index%8u)))!=0;
}
static unsigned picked_count(const struct wizard_game *game,unsigned source,unsigned *blocks) {
    unsigned count=0,total=0;
    const struct kui_vmu_import_plan *plan=wizard.cards[source].plan;
    if(plan) for(unsigned i=0;i<plan->entry_count;i++) if(picked(game,source,i)) {
        ++count;total+=plan->entries[i].blocks;
    }
    if(blocks) *blocks=total;
    return count;
}
static unsigned game_variants(const struct wizard_game *game,unsigned *files,unsigned *blocks) {
    unsigned count=0,n=0,total=0;
    if(wizard.existing_card) {
        if(game->card) count=1;
    } else if(game->accepted) {
        if(game->blank) count=1;
        else for(unsigned i=0;i<wizard.card_count;i++) {
            unsigned used=0,found=picked_count(game,i,&used);
            if(found) {++count;n+=found;total+=used;}
        }
    }
    if(files) *files=n;
    if(blocks) *blocks=total;
    return count;
}
static void counts(void) {
    for(unsigned i=0;i<wizard.card_count;i++) wizard.cards[i].summary.games=0;
    for(unsigned i=0;i<wizard.game_count;i++) {
        const struct wizard_game *game=&wizard.games[i];
        if(wizard.existing_card) {
            if(game->card && game->card<=wizard.card_count) ++wizard.cards[game->card-1].summary.games;
        } else if(game->accepted && !game->blank) {
            for(unsigned j=0;j<wizard.card_count;j++) if(picked_count(game,j,NULL)) ++wizard.cards[j].summary.games;
        }
    }
}
static unsigned selected_count(void) {
    unsigned count=0;
    for(unsigned i=0;i<wizard.game_count;i++) if(game_variants(&wizard.games[i],NULL,NULL)) ++count;
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
    for(unsigned i=0;i<wizard.game_count;i++) out->variants+=game_variants(&wizard.games[i],NULL,NULL);
    unsigned start=wizard.page*KUI_VMU_WIZARD_ROWS;
    for(unsigned i=start;i<wizard.game_count && out->count<KUI_VMU_WIZARD_ROWS;i++) {
        struct kui_vmu_wizard_game *row=&out->games[out->count++];
        const struct wizard_game *game=&wizard.games[i];
        memcpy(row->title,game->title,sizeof(row->title));
        memcpy(row->product,game->product,sizeof(row->product));
        row->variants=game_variants(game,&row->saves,&row->blocks);
        row->selected=wizard.existing_card?game->card==wizard.card+1:row->variants!=0;
        row->supported=game->supported;row->blank=game->accepted && game->blank;
    }
    if(wizard.stage==KUI_VMU_WIZARD_SAVES && wizard.save_game<wizard.game_count && wizard.card<wizard.card_count) {
        const struct wizard_game *game=&wizard.games[wizard.save_game];
        const struct kui_vmu_import_plan *plan=wizard.cards[wizard.card].plan;
        out->game_index=wizard.save_game;out->saves_page=wizard.save_page;
        memcpy(out->game_title,game->title,sizeof(out->game_title));
        memcpy(out->game_product,game->product,sizeof(out->game_product));
        out->saves_selected_files=picked_count(game,wizard.card,&out->saves_selected_blocks);
        for(unsigned i=0;i<wizard.card_count;i++) if(picked_count(game,i,NULL)) ++out->saves_variants;
        out->saves_total=plan?plan->entry_count:0;
        for(unsigned i=wizard.save_page*KUI_VMU_WIZARD_ROWS;plan && i<plan->entry_count && out->saves_count<KUI_VMU_WIZARD_ROWS;i++) {
            const struct kui_vmu_import_entry *entry=plan->entries+i;
            struct kui_vmu_wizard_save *row=&out->saves[out->saves_count++];
            row->index=i;row->slot=entry->slot;row->blocks=entry->blocks;row->type=entry->type;
            memcpy(row->name,entry->name,sizeof(row->name));
            memcpy(row->short_description,entry->short_description,sizeof(row->short_description));
            memcpy(row->long_description,entry->long_description,sizeof(row->long_description));
            row->picked=picked(game,wizard.card,i);row->suggested=entry->hints!=0;
        }
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
    if(wizard.card_count==KUI_VMU_WIZARD_CARDS) return fail(s,"Eight sources are captured; finish this import before adding more");
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
    struct kui_vmu_import_plan *plan=malloc(sizeof(*plan));
    struct kui_vmu_image_workspace *work=malloc(sizeof(*work));
    if(!plan || !work) {free(plan);free(work);free(incoming);return fail(s,"Not enough memory to inspect captured saves");}
    int inspected=kui_vmu_import_plan_inspect(incoming,KUI_VMU_IMAGE_BYTES,NULL,NULL,work,plan);
    free(work);
    if(inspected!=KUI_VMU_IMAGE_OK) {free(plan);free(incoming);return fail(s,"Captured saves could not be safely inspected");}
    unsigned card=wizard.card_count++;
    wizard.cards[card].image=incoming;wizard.cards[card].plan=plan;
    wizard.cards[card].source_slot=request->slot;
    struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,incoming,KUI_VMU_IMAGE_BYTES);
    kui_sha256_digest(&sha,wizard.cards[card].source_hash);
    wizard.cards[card].summary.files=summary.files;
    wizard.cards[card].summary.blocks=summary.used_blocks;
    wizard.cards[card].summary.sources=1;++wizard.sources;wizard.card=card;
    notice(s,"Source VMU %u captured separately; choose each game's saves",card+1);
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
    bool planned=selected_count()!=0;
    if(wizard.catalogued && !planned) for(unsigned i=0;i<wizard.game_count;i++)
        for(unsigned j=0;j<wizard.card_count;j++) if(picked_count(wizard.games+i,j,NULL)) planned=true;
    if(wizard.catalogued && planned)
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
static void candidates_storage(struct kui_vmu_game_candidates_storage *out);
struct wizard_card_users {
    char (*keys)[KUI_VMU_GAME_CARD_KEY_BYTES];
    char covered[KUI_VMU_GAME_CANDIDATES_PRODUCTS][KUI_VMU_GAME_CARD_KEY_BYTES];
    unsigned count,covered_count,stale;
    bool overflow;
};
static bool wizard_user_covered(const struct wizard_card_users *users,const char *key) {
    for(unsigned i=0;i<users->covered_count;i++) if(!strcmp(users->covered[i],key)) return true;
    return false;
}
static bool wizard_candidate_user(void *context,const char key[KUI_VMU_GAME_CARD_KEY_BYTES],
    const struct kui_vmu_game_candidate_target *target,bool is_default) {
    (void)is_default;struct wizard_card_users *users=context;
    if(!wizard_user_covered(users,key)) {
        if(users->covered_count==KUI_VMU_GAME_CANDIDATES_PRODUCTS) {users->overflow=true;return false;}
        memcpy(users->covered[users->covered_count++],key,KUI_VMU_GAME_CARD_KEY_BYTES);
    }
    if(target->pair!=wizard.source_pair) return true;
    if(memcmp(target->identity,wizard.source_identity,16u)) {++users->stale;return true;}
    if(users->count==KUI_VMU_WIZARD_GAMES) {users->overflow=true;return false;}
    memcpy(users->keys[users->count++],key,KUI_VMU_GAME_CARD_KEY_BYTES);return true;
}
static bool used_by(struct operation *s) {
    struct kui_vmu_game_assign_table *table=malloc(sizeof(*table));
    char (*keys)[KUI_VMU_GAME_CARD_KEY_BYTES]=calloc(KUI_VMU_GAME_ASSIGN_MAX+1u,sizeof(*keys));
    if(!table || !keys) {free(table);free(keys);return fail(s,"Not enough memory to list games using this card");}
    bool ok=kui_games_storage_open(s->log)==KUI_GAMES_STORAGE_OK;
    if(!ok) fail(s,"Cannot open the SD card for game assignments");
    if(ok) ok=same_storage(s,false);
    if(ok) ok=used_by_assignments(s,table);
    struct wizard_card_users *users=calloc(1,sizeof(*users));
    if(!users) {free(table);free(keys);if(!kui_games_storage_release()) fail(s,"Could not release the SD card");return fail(s,"Not enough memory to list game card choices");}
    users->keys=keys;
    if(ok) {
        struct kui_vmu_game_candidates_storage storage;candidates_storage(&storage);
        int r=kui_vmu_game_candidates_visit(&storage,wizard_candidate_user,users,s->log,s->cancel);
        if(users->overflow) ok=fail(s,"More than 256 games use this card; a complete list cannot be shown");
        else if(r==KUI_VMU_GAME_CANDIDATES_CANCELLED) {s->out->status.stopped=true;notice(s,"Game card choice listing stopped");ok=false;}
        else if(r!=KUI_VMU_GAME_CANDIDATES_OK && r!=KUI_VMU_GAME_CANDIDATES_ABSENT)
            ok=fail(s,"Game card choices could not be checked");
    }
    unsigned count=users->count,stale=users->stale;
    for(unsigned i=0;ok && i<table->count;i++) {
        const struct kui_vmu_game_assignment *entry=table->entries+i;
        if(wizard_user_covered(users,entry->key) || entry->pair!=wizard.source_pair) continue;
        if(memcmp(entry->identity,wizard.source_identity,16u)) {++stale;continue;}
        if(count==KUI_VMU_WIZARD_GAMES) {ok=fail(s,"More than 256 games use this card; a complete list cannot be shown");break;}
        memcpy(keys[count++],entry->key,sizeof(entry->key));
    }
    if(ok && wizard.source_pair) {
        char path[96],key[12];uint8_t wire[KUI_VMU_GAME_CARD_BINDING_BYTES];FILINFO info;
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/game.bin",wizard.source_pair);
        FRESULT r=f_stat(path,&info);
        if(r!=FR_NO_FILE && r!=FR_NO_PATH) {
            if(r!=FR_OK || (info.fattrib&AM_DIR) || !source_file(path,wire,sizeof(wire)) ||
               !kui_vmu_game_card_binding_decode(wire,sizeof(wire),key)) ok=fail(s,"The card's game binding could not be checked");
            else if(!wizard_user_covered(users,key) && !kui_vmu_game_assign_find(table,key)) {
                if(count==KUI_VMU_WIZARD_GAMES) ok=fail(s,"More than 256 games use this card; a complete list cannot be shown");
                else memcpy(keys[count++],key,sizeof(key));
            }
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
    free(users);free(table);free(keys);return ok;
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
static bool editable_game(struct operation *s,const struct kui_vmu_wizard_request *request) {
    if(wizard.publication_started) return fail(s,"Assignments are locked; retry the import or start a new import");
    if(wizard.stage!=KUI_VMU_WIZARD_GAMES_STAGE || !wizard.card_count ||
       request->card>=wizard.card_count || request->page!=wizard.page ||
       request->index<wizard.page*KUI_VMU_WIZARD_ROWS ||
       request->index-wizard.page*KUI_VMU_WIZARD_ROWS>=KUI_VMU_WIZARD_ROWS ||
       request->index>=wizard.game_count)
        return fail(s,"Refresh the game list before selecting this row");
    if(!wizard.games[request->index].supported)
        return fail(s,"This loader is not eligible for native virtual VMU assignments");
    return true;
}
static bool open_saves(struct operation *s,const struct kui_vmu_wizard_request *request) {
    if(wizard.existing_card) return fail(s,"Existing cards use manual shared game assignments");
    bool switching=wizard.stage==KUI_VMU_WIZARD_SAVES;
    if(!switching) {
        if(!editable_game(s,request)) return false;
        wizard.save_game=request->index;wizard.save_page=0;
    } else if(wizard.publication_started || request->card>=wizard.card_count)
        return fail(s,"Refresh the save list before choosing a source");
    struct wizard_card *card=&wizard.cards[request->card];
    struct wizard_game *game=&wizard.games[wizard.save_game];
    struct kui_vmu_image_workspace *work=malloc(sizeof(*work));
    if(!work) return fail(s,"Not enough memory to inspect these saves");
    int result=kui_vmu_import_plan_inspect(card->image,KUI_VMU_IMAGE_BYTES,game->title,game->product,work,card->plan);
    free(work);
    if(result!=KUI_VMU_IMAGE_OK) return fail(s,"Captured saves could not be safely inspected");
    wizard.card=request->card;wizard.stage=KUI_VMU_WIZARD_SAVES;
    unsigned pages=card->plan->entry_count?(card->plan->entry_count-1u)/KUI_VMU_WIZARD_ROWS+1u:1u;
    wizard.save_page=switching?(request->page<pages?request->page:pages-1u):0;
    notice(s,"Choose all related save files for %s; text hints need your review",game->title);
    return true;
}
static bool toggle_save(struct operation *s,const struct kui_vmu_wizard_request *request) {
    if(wizard.publication_started || wizard.stage!=KUI_VMU_WIZARD_SAVES ||
       request->card!=wizard.card || request->page!=wizard.save_page ||
       request->index<wizard.save_page*KUI_VMU_WIZARD_ROWS ||
       request->index-wizard.save_page*KUI_VMU_WIZARD_ROWS>=KUI_VMU_WIZARD_ROWS ||
       request->index>=wizard.cards[wizard.card].plan->entry_count)
        return fail(s,"Refresh the save list before selecting this row");
    struct wizard_game *game=&wizard.games[wizard.save_game];
    game->picks[wizard.card][request->index/8u]^=(uint8_t)(1u<<(request->index%8u));
    unsigned blocks=0;picked_count(game,wizard.card,&blocks);
    if(blocks>KUI_VMU_IMAGE_USER_BLOCKS) {
        game->picks[wizard.card][request->index/8u]^=(uint8_t)(1u<<(request->index%8u));
        return fail(s,"These saves exceed one 200-block card");
    }
    game->accepted=false;game->blank=false;
    notice(s,"Save choices are a draft; accept after reviewing every source");
    return true;
}
static bool accept_saves(struct operation *s) {
    if(wizard.publication_started || wizard.stage!=KUI_VMU_WIZARD_SAVES)
        return fail(s,"Review this game's save choices before accepting");
    struct wizard_game *game=&wizard.games[wizard.save_game];
    unsigned variants=0;
    for(unsigned i=0;i<wizard.card_count;i++) {
        unsigned blocks=0,count=picked_count(game,i,&blocks);
        if(blocks>KUI_VMU_IMAGE_USER_BLOCKS) return fail(s,"These saves exceed one 200-block card");
        if(count) ++variants;
    }
    game->accepted=true;game->blank=variants==0;
    wizard.stage=KUI_VMU_WIZARD_GAMES_STAGE;
    wizard.page=wizard.save_game/KUI_VMU_WIZARD_ROWS;
    notice(s,variants?"%s: %u separate card choices accepted":"%s: a new blank 200-block card accepted",game->title,variants);
    return true;
}
static void candidates_storage(struct kui_vmu_game_candidates_storage *out) {
    memset(out,0,sizeof(*out));memcpy(out->cid,wizard.storage.cid,16);
    out->card_sectors=wizard.storage.sectors;out->high_capacity=wizard.storage.high_capacity;
    out->partition_first=wizard.volume.start;out->partition_end=wizard.volume.start+wizard.volume.count;
}
static bool commit_shared(struct operation *s) {
    if(wizard.stage!=KUI_VMU_WIZARD_REVIEW || !selected_count())
        return fail(s,"Review at least one game assignment before creating cards");
    if(stop(s)) return false;
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    struct kui_vmu_game_assignment *entries=calloc(KUI_VMU_WIZARD_GAMES,sizeof(*entries));
    struct kui_vmu_game_candidates_plan *plans=calloc(KUI_VMU_WIZARD_GAMES,sizeof(*plans));
    if(!work || !entries || !plans) {free(work);free(entries);free(plans);return fail(s,"Not enough memory to prepare imported cards");}
    counts();struct kui_vmu_game_card_choice choice={0};
    bool ok=kui_games_storage_open(s->log)==KUI_GAMES_STORAGE_OK;
    if(!ok) fail(s,"Cannot open the SD card for the import");
    if(ok) ok=same_storage(s,true);
    struct kui_vmu_game_candidates_storage storage;candidates_storage(&storage);
    bool candidate_batch=false;unsigned planned=0;
    for(unsigned i=0;ok && i<wizard.game_count;i++) if(wizard.games[i].card) {
        const struct wizard_game *game=wizard.games+i;
        const struct wizard_card *card=wizard.cards+game->card-1u;
        struct kui_vmu_game_candidates_view current;
        int r=kui_vmu_game_candidates_list(game->product,&storage,&current,s->log,s->cancel);
        if(r==KUI_VMU_GAME_CANDIDATES_OK) candidate_batch=true;
        else if(r==KUI_VMU_GAME_CANDIDATES_CANCELLED) {s->out->status.stopped=true;notice(s,"Assignment stopped; no cards were published");ok=false;}
        else if(r!=KUI_VMU_GAME_CANDIDATES_ABSENT) ok=fail(s,"Existing game card choices could not be checked");
        if(!ok) break;
        bool found=false;
        if(card->summary.pair) for(unsigned j=0;j<current.count;j++)
            if(current.targets[j].pair==card->summary.pair && !memcmp(current.targets[j].identity,card->identity,16)) found=true;
        memcpy(plans[planned].key,game->product,sizeof(plans[planned].key));
        plans[planned++].additional=found?0u:1u;
    }
    if(ok && candidate_batch) {
        int r=kui_vmu_game_candidates_preflight(plans,planned,&storage,s->log,s->cancel);
        if(r==KUI_VMU_GAME_CANDIDATES_CANCELLED) {s->out->status.stopped=true;notice(s,"Assignment stopped; no cards were published");ok=false;}
        else if(r!=KUI_VMU_GAME_CANDIDATES_OK) ok=fail(s,"Game card choices are full or could not be checked; no new cards were published");
    }
    if(ok) ok=same_storage(s,false);
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
        card->summary.pair=choice.pair;
        if(!same_storage(s,false)) {ok=false;break;}
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
        if(candidate_batch) {
            int r=kui_vmu_game_candidates_apply_selected(entries,count,&storage,s->log,s->cancel);
            if(r==KUI_VMU_GAME_CANDIDATES_CANCELLED) {s->out->status.stopped=true;notice(s,"Assignment stopped; prepared cards are retained for retry");ok=false;}
            else if(r!=KUI_VMU_GAME_CANDIDATES_OK) ok=fail(s,"Could not publish the selected game cards; prepared cards are retained for retry");
        } else {
            int result=kui_vmu_game_card_assignments_apply(entries,count,work,&choice,s->log,s->cancel);
            if(result!=KUI_VMU_GAME_CARD_READY) {
                if(result==KUI_VMU_GAME_CARD_CANCELLED) {
                    s->out->status.stopped=true;notice(s,"Assignment stopped; prepared cards are retained for retry");
                } else fail(s,choice.problem[0]?choice.problem:"Could not publish game assignments");
                ok=false;
            }
        }
        if(ok) ok=same_storage(s,false);
    }
    if(!kui_games_storage_release()) {
        ok=false;fail(s,"Could not release the SD card; assignments may be saved, retry to confirm");
    }
    free(work);free(entries);free(plans);
    if(ok) {
        wizard.stage=KUI_VMU_WIZARD_DONE;
        for(unsigned i=0;i<wizard.card_count;i++) {free(wizard.cards[i].image);wizard.cards[i].image=NULL;}
        notice(s,"Ready: cards and game assignments saved");
    }
    return ok;
}
static bool candidates_error(struct operation *s,int result,const char *message) {
    if(result==KUI_VMU_GAME_CANDIDATES_CANCELLED) {
        s->out->status.stopped=true;notice(s,"Assignment stopped; prepared cards are retained for retry");return false;
    }
    return fail(s,message);
}
/* A publication error can occur after ready.bin becomes durable. Before a
 * retry allocates another snapshot, re-admit only newer completed snapshots
 * and match both the full source hash and its source label. */
static bool archive_source(struct operation *s,struct wizard_card *card) {
    if(card->archive.id) {
        struct kui_vmu_collection_card check;
        int r=kui_vmu_collection_verify(card->archive.path,&check,s->log,s->cancel);
        if(r!=KUI_VMU_COLLECTION_OK || check.id!=card->archive.id || memcmp(check.sha256,card->source_hash,32))
            return fail(s,"A prepared source archive changed; restore it before retrying");
        return true;
    }
    char label[16];snprintf(label,sizeof(label),"Source VMU %c%u",'A'+card->source_slot/2u,card->source_slot%2u+1u);
    struct kui_vmu_collection_page *page=malloc(sizeof(*page));
    if(!page) return fail(s,"Not enough memory to check source archives");
    unsigned highest=0;bool ok=true;
    for(unsigned at=0;;at++) {
        int r=kui_vmu_collection_list(at,page,s->log,s->cancel);
        if(r!=KUI_VMU_COLLECTION_OK) {
            if(r==KUI_VMU_COLLECTION_CANCELLED) {s->out->status.stopped=true;notice(s,"Import stopped; prepared source archives are retained");}
            else fail(s,"Source archives could not be checked before publication");
            ok=false;break;
        }
        for(unsigned i=0;i<page->count;i++) {
            const struct kui_vmu_collection_card *found=&page->entries[i];
            if(found->id>highest) highest=found->id;
            if(card->archive_attempted && found->id>card->archive_floor &&
               !strcmp(found->label,label) && !memcmp(found->sha256,card->source_hash,32)) card->archive=*found;
        }
        if((size_t)(at+1u)*KUI_VMU_COLLECTION_ROWS>=page->total) break;
    }
    free(page);
    if(!ok) return false;
    if(card->archive.id) {card->summary.pair=card->archive.id;return true;}
    if(!card->archive_attempted) {card->archive_floor=highest;card->archive_attempted=true;}
    notice(s,"Archiving source VMU %c%u; original saves are preserved",'A'+card->source_slot/2u,card->source_slot%2u+1u);
    int r=kui_vmu_collection_publish(card->image,KUI_VMU_IMAGE_BYTES,label,&card->archive,s->log,s->cancel);
    if(r!=KUI_VMU_COLLECTION_OK) {
        if(r==KUI_VMU_COLLECTION_CANCELLED) {s->out->status.stopped=true;notice(s,"Import stopped; prepared source archives are retained for retry");}
        else fail(s,"Source archive publication failed; retry will check completed copies");
        return false;
    }
    card->summary.pair=card->archive.id;
    return true;
}
static bool commit_import(struct operation *s) {
    if(wizard.stage!=KUI_VMU_WIZARD_REVIEW || !selected_count())
        return fail(s,"Review at least one game before creating separate cards");
    if(stop(s)) return false;
    unsigned variants=0;
    for(unsigned i=0;i<wizard.game_count;i++) variants+=game_variants(wizard.games+i,NULL,NULL);
    if(variants>KUI_VMU_GAME_CANDIDATES_MAX) return fail(s,"Too many card choices; select fewer games");
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    struct kui_vmu_game_assignment *entries=calloc(variants,sizeof(*entries));
    struct kui_vmu_game_candidates_plan *plans=calloc(KUI_VMU_WIZARD_GAMES,sizeof(*plans));
    uint8_t *image=malloc(KUI_VMU_IMAGE_BYTES);
    if(!work || !entries || !plans || !image) {
        free(work);free(entries);free(plans);free(image);return fail(s,"Not enough memory to prepare separate game cards");
    }
    bool ok=kui_games_storage_open(s->log)==KUI_GAMES_STORAGE_OK;
    if(!ok) fail(s,"Cannot open the SD card for the import");
    if(ok) ok=same_storage(s,true);
    struct kui_vmu_game_candidates_storage storage;candidates_storage(&storage);
    unsigned planned=0;
    for(unsigned i=0;ok && i<wizard.game_count;i++) {
        const struct wizard_game *game=wizard.games+i;unsigned count=game_variants(game,NULL,NULL);
        if(!count) continue;
        struct kui_vmu_game_candidates_view current;
        int r=kui_vmu_game_candidates_list(game->product,&storage,&current,s->log,s->cancel);
        if(r!=KUI_VMU_GAME_CANDIDATES_OK && r!=KUI_VMU_GAME_CANDIDATES_ABSENT) {
            ok=candidates_error(s,r,"Existing game card choices could not be checked");break;
        }
        memcpy(plans[planned].key,game->product,sizeof(plans[planned].key));
        for(unsigned j=0;j<wizard.card_count;j++) {
            if(game->blank?j!=0:!picked_count(game,j,NULL)) continue;
            const struct wizard_output *out=game->outputs+j;bool found=false;
            if(out->pair) for(unsigned k=0;k<current.count;k++)
                if(current.targets[k].pair==out->pair && !memcmp(current.targets[k].identity,out->identity,16)) found=true;
            if(!found) ++plans[planned].additional;
        }
        ++planned;
    }
    if(ok) {
        int r=kui_vmu_game_candidates_preflight(plans,planned,&storage,s->log,s->cancel);
        if(r!=KUI_VMU_GAME_CANDIDATES_OK) ok=candidates_error(s,r,"Game card choices are full or could not be checked; no new cards were published");
    }
    if(ok && kui_vmu_library_check_allocation(s->cancel)!=KUI_VMU_LIBRARY_OK) {
        if(!stop(s)) fail(s,"VMU file allocation could not be verified; check the SD card before importing");
        ok=false;
    }
    if(ok) ok=same_storage(s,false);
    if(ok) wizard.publication_started=true;
    for(unsigned i=0;ok && i<wizard.card_count;i++) {
        if(stop(s) || !same_storage(s,false)) {ok=false;break;}
        ok=archive_source(s,wizard.cards+i);
        if(ok) ok=same_storage(s,false);
    }
    struct kui_vmu_game_card_choice choice={0};unsigned count=0;
    for(unsigned i=0;ok && i<wizard.game_count;i++) {
        struct wizard_game *game=wizard.games+i;if(!game_variants(game,NULL,NULL)) continue;
        for(unsigned j=0;ok && j<wizard.card_count;j++) {
            if(game->blank?j!=0:!picked_count(game,j,NULL)) continue;
            struct wizard_output *out=game->outputs+j;
            if(!out->pair) {
                if(stop(s) || !same_storage(s,false)) {ok=false;break;}
                uint16_t slots[KUI_VMU_IMAGE_DIRECTORY_ENTRIES];unsigned n=0;
                if(!game->blank) for(unsigned k=0;k<wizard.cards[j].plan->entry_count;k++)
                    if(picked(game,j,k)) slots[n++]=wizard.cards[j].plan->entries[k].slot;
                struct kui_vmu_image_summary summary;
                if(kui_vmu_import_plan_build(wizard.cards[j].image,KUI_VMU_IMAGE_BYTES,image,KUI_VMU_IMAGE_BYTES,
                   n?slots:NULL,n,&work->image,&summary)!=KUI_VMU_IMAGE_OK) {
                    ok=fail(s,"This game's reviewed saves could not form a separate card");break;
                }
                notice(s,"Creating separate card for %s, source %u",game->title,j+1u);
                uint8_t fresh[16];salt(fresh,i*KUI_VMU_WIZARD_CARDS+j);
                int r=kui_vmu_game_card_import_publish(image,KUI_VMU_IMAGE_BYTES,fresh,work,&choice,out->identity,s->log,s->cancel);
                if(r!=KUI_VMU_GAME_CARD_READY) {
                    if(r==KUI_VMU_GAME_CARD_CANCELLED) {s->out->status.stopped=true;notice(s,"Import stopped; prepared game cards are retained for retry");}
                    else fail(s,choice.problem[0]?choice.problem:"Could not publish this game's separate card");
                    ok=false;break;
                }
                out->pair=choice.pair;
                if(!same_storage(s,false)) {ok=false;break;}
            }
            memcpy(entries[count].key,game->product,sizeof(entries[count].key));entries[count].pair=out->pair;
            memcpy(entries[count++].identity,out->identity,16);
        }
    }
    /* All synthesis is complete. The label service can now use its own one
     * admission image without retaining a second output mirror. */
    free(image);image=NULL;
    for(unsigned i=0;ok && i<wizard.game_count;i++) {
        struct wizard_game *game=wizard.games+i;if(!game_variants(game,NULL,NULL)) continue;
        for(unsigned j=0;ok && j<wizard.card_count;j++) {
            if(game->blank?j!=0:!picked_count(game,j,NULL)) continue;
            struct wizard_output *out=game->outputs+j;if(out->labelled) continue;
            char path[96],label[49];
            snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.vmd",out->pair);
            if(game->blank) snprintf(label,sizeof(label),"%.39s / Blank",game->title);
            else snprintf(label,sizeof(label),"%.25s / Source %c%u #%c",game->title,
                'A'+wizard.cards[j].source_slot/2u,wizard.cards[j].source_slot%2u+1u,(int)('1'+j));
            for(char *at=label;*at;at++) if((unsigned char)*at<32u || (unsigned char)*at>126u) *at='?';
            if(stop(s) || !same_storage(s,false)) {ok=false;break;}
            int r=kui_vmu_library_rename(path,label,out->identity,s->log,s->cancel);
            if(r!=KUI_VMU_LIBRARY_OK) {
                if(r==KUI_VMU_LIBRARY_CANCELLED) {s->out->status.stopped=true;notice(s,"Import stopped; prepared game cards are retained for retry");}
                else fail(s,"Could not label this game's source card; prepared cards are retained for retry");
                ok=false;break;
            }
            out->labelled=true;
            if(!same_storage(s,false)) {ok=false;break;}
        }
    }
    if(ok && stop(s)) ok=false;
    if(ok) ok=same_storage(s,false);
    if(ok) {
        notice(s,"Saving %u game assignments together",count);
        int r=kui_vmu_game_candidates_apply(entries,count,&storage,s->log,s->cancel);
        if(r!=KUI_VMU_GAME_CANDIDATES_OK) ok=candidates_error(s,r,"Could not publish game card choices; prepared cards are retained for retry");
        if(ok) ok=same_storage(s,false);
    }
    if(!kui_games_storage_release()) {ok=false;fail(s,"Could not release the SD card; choices may be saved, retry to confirm");}
    free(image);free(plans);free(entries);free(work);
    if(ok) {
        wizard.stage=KUI_VMU_WIZARD_DONE;
        for(unsigned i=0;i<wizard.card_count;i++) {free(wizard.cards[i].image);wizard.cards[i].image=NULL;}
        notice(s,"Ready: full sources archived; separate game cards and choices saved");
    }
    return ok;
}
static bool commit(struct operation *s) {
    return wizard.existing_card?commit_shared(s):commit_import(s);
}
void kui_vmu_wizard_run(const struct kui_vmu_wizard_request *request,
    struct kui_vmu_wizard_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));struct operation s={out,log,cancel,progress};
    if(!request) {fail(&s,"Missing import request");view(&s,0);return;}
    if(request->slot<8) wizard.slot=request->slot;
    if(wizard.stage!=KUI_VMU_WIZARD_SAVES && request->card<wizard.card_count) wizard.card=request->card;
    if(stop(&s)) {view(&s,request->generation);return;}
    if(wizard.view_only && (request->op==KUI_VMU_WIZARD_CAPTURE || request->op==KUI_VMU_WIZARD_TOGGLE ||
       request->op==KUI_VMU_WIZARD_SHOW_SOURCES || request->op==KUI_VMU_WIZARD_SHOW_REVIEW ||
       request->op==KUI_VMU_WIZARD_COMMIT || request->op==KUI_VMU_WIZARD_OPEN_SAVES ||
       request->op==KUI_VMU_WIZARD_TOGGLE_SAVE || request->op==KUI_VMU_WIZARD_ACCEPT_SAVES ||
       request->op==KUI_VMU_WIZARD_TOGGLE_BLANK)) {
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
        else notice(&s,"Capture source VMUs, then review saves for each game's separate card");
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
        if(!wizard.existing_card) {fail(&s,"Open this game's save choices or choose a blank card");break;}
        if(!editable_game(&s,request)) break;
        struct wizard_game *game=&wizard.games[request->index];
        game->card=game->card==wizard.card+1?0:wizard.card+1;
        notice(&s,game->card?"%s will use card %u":"%s keeps its previous card",game->title,wizard.card+1);
        break;
    }
    case KUI_VMU_WIZARD_OPEN_SAVES:
        (void)open_saves(&s,request);break;
    case KUI_VMU_WIZARD_TOGGLE_SAVE:
        (void)toggle_save(&s,request);break;
    case KUI_VMU_WIZARD_ACCEPT_SAVES:
        (void)accept_saves(&s);break;
    case KUI_VMU_WIZARD_TOGGLE_BLANK: {
        if(wizard.existing_card) {fail(&s,"Existing cards use manual shared game assignments");break;}
        if(!editable_game(&s,request)) break;
        struct wizard_game *game=&wizard.games[request->index];
        bool selected=game->accepted && game->blank;
        memset(game->picks,0,sizeof(game->picks));game->accepted=!selected;game->blank=!selected;
        notice(&s,selected?"%s keeps its previous card":"%s: a new blank 200-block card selected",game->title);
        break;
    }
    case KUI_VMU_WIZARD_SHOW_GAMES:
        if(!wizard.catalogued || wizard.stage==KUI_VMU_WIZARD_DONE) fail(&s,"Choose games before opening this list");
        else {
            wizard.stage=KUI_VMU_WIZARD_GAMES_STAGE;
            unsigned pages=wizard.game_count?(wizard.game_count-1u)/KUI_VMU_WIZARD_ROWS+1u:1u;
            wizard.page=request->page<pages?request->page:pages-1u;
            catalogue_notice(&s);
        }
        break;
    case KUI_VMU_WIZARD_REVIEW_PAGE:
        if(wizard.stage!=KUI_VMU_WIZARD_REVIEW && wizard.stage!=KUI_VMU_WIZARD_DONE)
            fail(&s,"Review game choices before changing the review page");
        else {
            unsigned pages=wizard.game_count?(wizard.game_count-1u)/KUI_VMU_WIZARD_ROWS+1u:1u;
            wizard.page=request->page<pages?request->page:pages-1u;
        }
        break;
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
            "Confirm to archive sources and create separate cards for the selected games");}
        break;
    case KUI_VMU_WIZARD_COMMIT:
        (void)commit(&s);break;
    default:fail(&s,"Unknown import request");break;
    }
    view(&s,request->generation);
}
