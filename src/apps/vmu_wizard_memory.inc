/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after assign_match: remembered intent never replaces contradictory
 * metadata or the catalogue's native eligibility checks. */
#include "kui/vmu_match_memory.h"
#define WIZARD_MATCH_REMEMBERED (1u<<16)
static unsigned remembered_game(const struct kui_vmu_import_match_group *group,
    const struct kui_vmu_match_memory_target *target) {
    if(group->reasons&KUI_VMU_IMPORT_MATCH_CONFLICT) return KUI_VMU_IMPORT_MATCH_NO_GAME;
    unsigned found=KUI_VMU_IMPORT_MATCH_NO_GAME,count=0u;
    for(unsigned i=0;i<wizard.game_count;i++)
        if(!strcmp(wizard.games[i].product,target->product) && !strcmp(wizard.games[i].title,target->title)) {
            found=i;++count;
        }
    if(count!=1u || !wizard.games[found].supported || !picker_allows(group,found))
        return KUI_VMU_IMPORT_MATCH_NO_GAME;
    return found;
}
static bool remembered_load(struct operation *s,struct kui_vmu_import_matches **fresh) {
    bool opened=kui_games_storage_open(s->log)==KUI_GAMES_STORAGE_OK;
    bool stopped=false;
    if(opened) for(unsigned i=0;i<wizard.card_count && !stopped;i++) if(fresh[i])
        for(unsigned j=0;j<fresh[i]->group_count;j++) {
            struct kui_vmu_import_match_group *group=fresh[i]->groups+j;
            if(stop(s)) {stopped=true;break;}
            if(group->state==KUI_VMU_IMPORT_MATCH_AUTO) continue;
            struct kui_vmu_match_memory_target target;
            int r=kui_vmu_match_memory_load(wizard.cards[i].plan,group,&target);
            if(r==KUI_VMU_MATCH_MEMORY_OK) {
                unsigned game=remembered_game(group,&target);
                if(game!=KUI_VMU_IMPORT_MATCH_NO_GAME) {
                    group->game_index=game;group->state=KUI_VMU_IMPORT_MATCH_AUTO;
                    group->reasons|=WIZARD_MATCH_REMEMBERED;
                }
            } else if(r!=KUI_VMU_MATCH_MEMORY_MISSING)
                if(s->log) s->log("VMU remembered match unavailable for source %u group %u: %d; review required",i+1u,j+1u,r);
        }
    if(!opened || !kui_games_storage_release())
        if(s->log) s->log("VMU remembered matches could not be read; using current metadata and manual review");
    return !stopped;
}
static unsigned remembered_count(void) {
    unsigned count=0u;
    for(unsigned i=0;i<wizard.card_count;i++) if(wizard.cards[i].matches)
        for(unsigned j=0;j<wizard.cards[i].matches->group_count;j++)
            if(wizard.cards[i].decisions[j]==GROUP_ASSIGNED &&
               (wizard.cards[i].matches->groups[j].reasons&WIZARD_MATCH_REMEMBERED)) ++count;
    return count;
}
/* Reconcile the final picks, including explicit advanced edits. A split or
 * removed group is not learned as a complete-file assignment. */
static unsigned remembered_final_game(const struct wizard_card *source,unsigned source_index,
    unsigned ordinal,bool *explicit_choice) {
    const struct kui_vmu_import_match_group *group=source->matches->groups+ordinal;
    *explicit_choice=source->confirmed[ordinal]!=0u ||
        (group->game_index<wizard.game_count && wizard.games[group->game_index].manual &&
         wizard.games[group->game_index].accepted);
    unsigned winner=KUI_VMU_IMPORT_MATCH_NO_GAME;bool complete=true;
    for(unsigned n=0;n<source->plan->entry_count;n++) if(group->members[n/8u]&(1u<<(n%8u))) {
        unsigned owner=KUI_VMU_IMPORT_MATCH_NO_GAME,count=0u;
        for(unsigned g=0;g<wizard.game_count;g++) if(picked(wizard.games+g,source_index,n)) {
            if(wizard.games[g].manual && wizard.games[g].accepted && !wizard.games[g].blank) *explicit_choice=true;
            if(wizard.games[g].accepted && !wizard.games[g].blank) {owner=g;++count;}
        }
        if(count!=1u || (winner!=KUI_VMU_IMPORT_MATCH_NO_GAME && winner!=owner)) complete=false;
        if(count==1u) winner=owner;
    }
    return complete?winner:KUI_VMU_IMPORT_MATCH_NO_GAME;
}
struct remembered_choice {
    uint8_t key[KUI_VMU_MATCH_MEMORY_KEY_BYTES];
    unsigned source,ordinal,game;
};
static bool remembered_same_storage(void) {
    struct kui_vmu_sd_identity identity={0};const struct kui_volume *volume=kui_media_volume();
    return wizard.storage_pinned && volume && kui_sd_vmu_profile_identity(&identity) &&
        !memcmp(identity.cid,wizard.storage.cid,sizeof(identity.cid)) &&
        identity.sectors==wizard.storage.sectors && identity.high_capacity==wizard.storage.high_capacity &&
        volume->start==wizard.volume.start && volume->count==wizard.volume.count;
}
/* Optional recognition metadata is published only after the cards and their
 * assignments succeeded. Failure here cannot undo or misreport that import. */
static bool remembered_save(struct operation *s) {
    size_t capacity=(size_t)wizard.card_count*KUI_VMU_IMAGE_DIRECTORY_ENTRIES;
    struct remembered_choice *choices=calloc(capacity?capacity:1u,sizeof(*choices));
    if(!choices) {if(s->log) s->log("VMU choices saved; no RAM to remember manual matches");return false;}
    unsigned count=0u;bool ok=true;
    for(unsigned i=0;i<wizard.card_count;i++) if(wizard.cards[i].matches)
        for(unsigned j=0;j<wizard.cards[i].matches->group_count;j++) {
            bool explicit_choice=false;
            unsigned game=remembered_final_game(wizard.cards+i,i,j,&explicit_choice);
            if(!explicit_choice) continue;
            struct remembered_choice *choice=choices+count;
            int r=kui_vmu_match_memory_key(wizard.cards[i].plan,wizard.cards[i].matches->groups+j,choice->key);
            if(r) {ok=false;continue;}
            choice->source=i;choice->ordinal=j;choice->game=game;++count;
        }
    for(unsigned i=0;i<count;i++) {
        bool prior=false,ambiguous=choices[i].game==KUI_VMU_IMPORT_MATCH_NO_GAME;
        for(unsigned j=0;j<count;j++) if(!memcmp(choices[i].key,choices[j].key,sizeof(choices[i].key))) {
            if(j<i) prior=true;
            if(choices[i].game!=choices[j].game) ambiguous=true;
        }
        if(prior) continue;
        if((s->cancel && s->cancel()) || !remembered_same_storage()) {ok=false;break;}
        const struct wizard_card *source=wizard.cards+choices[i].source;
        const struct kui_vmu_import_match_group *group=source->matches->groups+choices[i].ordinal;
        int r;
        if(ambiguous) r=kui_vmu_match_memory_forget(source->plan,group);
        else {
            struct kui_vmu_match_memory_target target;
            memcpy(target.product,wizard.games[choices[i].game].product,sizeof(target.product));
            memcpy(target.title,wizard.games[choices[i].game].title,sizeof(target.title));
            r=kui_vmu_match_memory_remember(source->plan,group,&target);
        }
        if(r!=KUI_VMU_MATCH_MEMORY_OK) {ok=false;if(s->log) s->log("VMU cards saved; remembering source %u group %u failed: %d",
            choices[i].source+1u,choices[i].ordinal+1u,r);}
        if(!remembered_same_storage()) {ok=false;break;}
    }
    free(choices);return ok;
}
