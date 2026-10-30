/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent matcher contract tests. All metadata and cards are synthetic. */
#include "kui/vmu_import_match.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static struct kui_vmu_import_plan plan, plan_before;
static struct kui_vmu_import_match_game games[256], games_before[256];
static struct kui_vmu_import_matches matches, before, reordered;

static void reset(void) {
    memset(&plan,0,sizeof(plan));memset(games,0,sizeof(games));
    memset(&matches,0xa5,sizeof(matches));
}
static void game(unsigned i,const char *title,const char *product,bool eligible) {
    CHECK(i<256 && strlen(title)<sizeof(games[i].title) && strlen(product)<sizeof(games[i].product));
    strcpy(games[i].title,title);strcpy(games[i].product,product);games[i].native_eligible=eligible;
}
static void entry(unsigned i,const char *name,const char *short_text,const char *long_text,const char *app) {
    CHECK(i<200 && strlen(name)<=12 && strlen(short_text)<=16 && strlen(long_text)<=32 && strlen(app)<=16);
    struct kui_vmu_import_entry *e=&plan.entries[i];memset(e,0,sizeof(*e));
    e->slot=(uint16_t)(i+8u);e->type=0x33;e->blocks=1;
    strcpy(e->name,name);strcpy(e->short_description,short_text);
    strcpy(e->long_description,long_text);strcpy(e->application,app);
    if(plan.entry_count<=i) plan.entry_count=i+1u;
    plan.summary.files=plan.entry_count;plan.summary.used_blocks=plan.entry_count;
    plan.summary.free_blocks=200u-plan.entry_count;
}
static bool member(const struct kui_vmu_import_match_group *g,unsigned i) {
    return (g->members[i/8u]&(1u<<(i%8u)))!=0;
}
static const struct kui_vmu_import_match_group *containing(unsigned i) {
    for(unsigned g=0;g<matches.group_count;g++) if(member(&matches.groups[g],i)) return &matches.groups[g];
    CHECK(false);return NULL;
}
static void run(unsigned count) {
    plan_before=plan;memcpy(games_before,games,sizeof(games));
    CHECK(kui_vmu_import_match(&plan,games,count,&matches)==0);
    CHECK(!memcmp(&plan,&plan_before,sizeof(plan)) && !memcmp(games,games_before,sizeof(games)));
    CHECK(matches.group_count<=plan.entry_count);
    unsigned seen[208]={0},all_files=0,all_blocks=0;
    for(unsigned g=0;g<matches.group_count;g++) {
        const struct kui_vmu_import_match_group *v=&matches.groups[g];
        unsigned files=0,blocks=0,candidates=0;
        CHECK(memchr(v->label,0,sizeof(v->label)) && v->files && v->candidate_count<=count);
        for(unsigned i=0;i<208;i++) if(member(v,i)) {
            CHECK(i<plan.entry_count);++seen[i];++files;blocks+=plan.entries[i].blocks;
        }
        for(unsigned i=0;i<256;i++) if(v->candidates[i/8u]&(1u<<(i%8u))) {
            CHECK(i<count);++candidates;
        }
        CHECK(candidates==v->candidate_count);
        CHECK(files==v->files && blocks==v->blocks);all_files+=files;all_blocks+=blocks;
        if(v->state==KUI_VMU_IMPORT_MATCH_AUTO) {
            CHECK(v->candidate_count==1 && v->game_index<count && games[v->game_index].native_eligible);
            CHECK(v->candidates[v->game_index/8u]&(1u<<(v->game_index%8u)));
        } else CHECK(v->state==KUI_VMU_IMPORT_MATCH_CHOOSE_GAME && v->game_index==KUI_VMU_IMPORT_MATCH_NO_GAME);
    }
    for(unsigned i=0;i<plan.entry_count;i++) CHECK(seen[i]==1);
    CHECK(all_files==plan.entry_count && all_blocks==plan.summary.used_blocks);
}
static void unresolved(unsigned i) {
    const struct kui_vmu_import_match_group *g=containing(i);
    CHECK(g->state==KUI_VMU_IMPORT_MATCH_CHOOSE_GAME && g->game_index==KUI_VMU_IMPORT_MATCH_NO_GAME);
}
static void automatic(unsigned i,unsigned index) {
    const struct kui_vmu_import_match_group *g=containing(i);
    CHECK(g->state==KUI_VMU_IMPORT_MATCH_AUTO && g->game_index==index);
}
static void publisher_ids_are_not_families(void) {
    static const char *const publishers[]={"SEGA","SONICTEAM","SEGA ENTERPRISES","CAPCOM","VISUAL CONCEPTS","DREAMCAST"};
    for(unsigned p=0;p<sizeof(publishers)/sizeof(*publishers);p++) {
        reset();game(0,"Harbor Racer","RACE-1",true);game(1,"Mountain Quest","QUEST-1",true);
        entry(0,"RACE_SAVE","Harbor Racer","Main data",publishers[p]);
        entry(1,"QUEST_SAVE","Mountain Quest","Main data",publishers[p]);
        entry(2,"CONFIG","Settings","System data",publishers[p]);
        run(2);automatic(0,0);automatic(1,1);unresolved(2);
        CHECK(containing(0)!=containing(1) && containing(0)!=containing(2) && containing(1)!=containing(2));
    }
}
static void regions_and_unsupported_competitors(void) {
    reset();game(0,"Harbor Racer","RACE-1U",true);game(1,"Harbor Racer","RACE-1J",true);
    entry(0,"SAVE","Harbor Racer","Main save","");run(2);unresolved(0);
    CHECK(containing(0)->candidate_count==2);
    strcpy(plan.entries[0].application,"RACE-1J");run(2);automatic(0,1);
    /* Eligibility must not erase a competing regional identity. */
    games[1].native_eligible=false;plan.entries[0].application[0]=0;run(2);unresolved(0);
    CHECK(containing(0)->candidate_count==2);
    strcpy(plan.entries[0].application,"RACE-1J");run(2);unresolved(0);
    CHECK(containing(0)->reasons&KUI_VMU_IMPORT_MATCH_UNSUPPORTED);
}
static void contradictory_evidence_is_not_a_tiebreak(void) {
    reset();game(0,"Harbor Racer","RACE-1",true);game(1,"Mountain Quest","QUEST-1",true);
    entry(0,"SAVE","Harbor Racer","Mountain Quest","RACE-1");run(2);unresolved(0);
    CHECK(containing(0)->reasons&KUI_VMU_IMPORT_MATCH_CONFLICT);
    /* A generic application cannot attach the contradiction to the next file. */
    entry(1,"CONFIG","Mountain Quest","Settings","SEGA");run(2);unresolved(0);
    CHECK(containing(1)->state==KUI_VMU_IMPORT_MATCH_CHOOSE_GAME || containing(1)->game_index==1);
    /* A contradictory file must not detach from companions sharing a proven
     * specific application. The user resolves the complete set once. */
    strcpy(plan.entries[0].application,"Harbor Racer");
    strcpy(plan.entries[1].application,"Harbor Racer");run(2);
    unresolved(0);unresolved(1);CHECK(containing(0)==containing(1) && containing(0)->files==2);
}
static void exact_product_preserves_whole_source_group(void) {
    reset();game(0,"Harbor Racer","RACE-1U",true);game(1,"Harbor Racer","RACE-1J",true);
    entry(0,"MAIN","Harbor Racer","Main save","RACE-1J");
    entry(1,"CONFIG","Settings","Related settings","RACE-1J");
    entry(2,"EXTRA","Mini game","Related mini game","RACE-1J");
    run(2);automatic(0,1);automatic(1,1);automatic(2,1);
    CHECK(containing(0)==containing(1) && containing(1)==containing(2) && containing(0)->files==3);
    before=matches;run(2);CHECK(!memcmp(&before,&matches,sizeof(matches)));
    /* A later independent source has no implicit membership or remembered
     * assignments from the previous result, even with the same game list. */
    memset(&plan,0,sizeof(plan));entry(0,"MAIN","Harbor Racer","Main save","RACE-1U");
    run(2);automatic(0,0);CHECK(containing(0)->files==1);
    /* Regional suffixes stay significant even when punctuation is compacted. */
    strcpy(plan.entries[0].application,"RACE-1");run(2);unresolved(0);
}
static void uncertain_region_still_keeps_companions_together(void) {
    reset();game(0,"Harbor Racer","RACE-1U",true);game(1,"Harbor Racer","RACE-1J",true);
    entry(0,"MAIN","Harbor Racer","Main save","Harbor Racer");
    entry(1,"CONFIG","Settings","Related settings","Harbor Racer");
    run(2);unresolved(0);unresolved(1);
    CHECK(containing(0)==containing(1) && containing(0)->files==2 && containing(0)->candidate_count==2);
}
static void uninstalled_explicit_product_does_not_guess_region(void) {
    static const char *const installed[]={"T-1234N","MK-12345","HDR-0123"};
    static const char *const absent[]={"T-1234M","MK-12346","HDR-0124"};
    for(unsigned i=0;i<3;i++) {
        reset();game(0,"Harbor Racer",installed[i],true);
        entry(0,"MAIN","Harbor Racer","Main save",absent[i]);
        entry(1,"CONFIG","Harbor Racer","Settings","");run(1);
        unresolved(0);unresolved(1);CHECK(containing(0)==containing(1));
        CHECK((containing(0)->reasons&(KUI_VMU_IMPORT_MATCH_CONFLICT|KUI_VMU_IMPORT_MATCH_UNKNOWN))==
              (KUI_VMU_IMPORT_MATCH_CONFLICT|KUI_VMU_IMPORT_MATCH_UNKNOWN));
        strcpy(plan.entries[0].application,installed[i]);run(1);automatic(0,0);automatic(1,0);
    }
}
static void transitive_companions_are_one_complete_uncertain_group(void) {
    reset();game(0,"Harbor Racer","RACE-1",true);game(1,"Mountain Quest","QUEST-1",true);
    entry(0,"BRIDGEAA.000","Harbor Racer","Main save","");
    entry(1,"BRIDGEBB.000","Mountain Quest","Main save","");
    entry(2,"BRIDGEAA.001","Mountain Quest","Related data","");run(2);
    CHECK(matches.group_count==1 && containing(0)->files==3);
    unresolved(0);unresolved(1);unresolved(2);
    CHECK(containing(0)->candidate_count==2);
    reset();game(0,"Phantasy Star Online","PSO-1",true);game(1,"Toy Commander","TOY-1",true);
    entry(0,"PSO______SYS","PSO","System data","");
    entry(1,"TOY_COMM.000","Toy Commander","Main save","");
    entry(2,"PSO______GCD","Toy Commander","Guild card","");run(2);
    CHECK(containing(0)==containing(2) && containing(0)->files==2);
    unresolved(0);unresolved(2);automatic(1,1);
}
static void sequel_and_version_prefixes_are_not_ownership(void) {
    reset();game(0,"Harbor Racer","RACE-1",true);game(1,"Harbor Racer 2","RACE-2",true);
    entry(0,"SAVE0","Harbor Racer 2","Main save","");
    entry(1,"SAVE1","Harbor Race","Partial title","");
    entry(2,"SAVE2","Harbor Racer 20","Not the sequel","");
    run(2);automatic(0,1);unresolved(1);unresolved(2);
    reset();game(0,"Phantasy Star Online","PSO-V1",true);game(1,"Phantasy Star Online Ver.2","PSO-V2",true);
    entry(0,"PSO______SYS","PSO","System data","SONICTEAM");
    entry(1,"PSO______GCD","PSO","Guild card","SONICTEAM");run(2);unresolved(0);unresolved(1);
    CHECK(containing(0)->candidate_count==2 && containing(0)==containing(1));
    reset();game(0,"Evolution","EVO-1",true);game(1,"Evolution 2","EVO-2",true);
    entry(0,"EVO_DATA.000","Save data","","SEGA");
    entry(1,"EVO_DATA.001","Settings","","SEGA");
    entry(2,"EVO2_DAT.000","Save data","","SEGA");
    run(2);automatic(0,0);automatic(1,0);automatic(2,1);
    CHECK(containing(0)==containing(1) && containing(0)!=containing(2));
}
static void truncation_and_short_words_require_review(void) {
    reset();game(0,"A Very Long Adventure Beyond The Sea","LONG-1",true);
    game(1,"A Very Long Adventure Beyond The Sky","LONG-2",true);
    entry(0,"SAVE","","A Very Long Adventure Beyond The","");run(2);unresolved(0);
    reset();game(0,"The","THE-1",true);game(1,"Save","SAVE-1",true);
    entry(0,"DATA0","The","System data","");entry(1,"DATA1","Save","Settings","");
    run(2);unresolved(0);unresolved(1);
}
static void full_ordinal_coverage_and_catalogue_permutation(void) {
    reset();game(0,"Harbor Racer","RACE-1",true);game(1,"Mountain Quest","QUEST-1",true);
    for(unsigned i=0;i<200;i++) {
        char name[13];snprintf(name,sizeof(name),"X%03u",i);
        entry(i,name,i%3u==0?"Harbor Racer":i%3u==1?"Mountain Quest":"Unknown data", "", "SEGA");
    }
    run(2);CHECK(plan.entries[199].slot==207);
    before=matches;struct kui_vmu_import_match_game temporary=games[0];games[0]=games[1];games[1]=temporary;
    run(2);reordered=matches;CHECK(before.group_count==reordered.group_count);
    for(unsigned i=0;i<before.group_count;i++) {
        const struct kui_vmu_import_match_group *a=&before.groups[i],*b=&reordered.groups[i];
        CHECK(!memcmp(a->members,b->members,sizeof(a->members)) && a->files==b->files && a->blocks==b->blocks);
        CHECK(a->state==b->state && a->candidate_count==b->candidate_count && a->reasons==b->reasons);
        if(a->state==KUI_VMU_IMPORT_MATCH_AUTO) CHECK(b->game_index==1u-a->game_index);
    }
}
static void maximum_catalogue_and_unknown_files(void) {
    reset();
    for(unsigned i=0;i<256;i++) {
        char title[129],product[12];
        snprintf(title,sizeof(title),"Installed Release %03u",i);
        snprintf(product,sizeof(product),"T-%04uN",i);game(i,title,product,i%2u==0);
    }
    for(unsigned i=0;i<200;i++) {
        char name[13];snprintf(name,sizeof(name),"FILE%03u",i);
        entry(i,name,"Not recognized","Unknown data","SEGA");
    }
    run(256);CHECK(matches.group_count==200);
    for(unsigned i=0;i<200;i++) {unresolved(i);CHECK(containing(i)->candidate_count==0 && containing(i)->files==1);}
    reset();plan.summary.free_blocks=200;run(0);CHECK(matches.group_count==0);
    CHECK(kui_vmu_import_match(&plan,NULL,0,&matches)==0 && !matches.group_count);
}
static void bounds_overlap_and_input_immutability(void) {
    reset();game(0,"Harbor Racer","RACE-1",true);entry(0,"SAVE","Harbor Racer","","");
    before=matches;
    CHECK(kui_vmu_import_match(NULL,games,1,&matches)==-1 && !memcmp(&before,&matches,sizeof(before)));
    CHECK(kui_vmu_import_match(&plan,games,257,&matches)==-1 && !memcmp(&before,&matches,sizeof(before)));
    plan_before=plan;
    CHECK(kui_vmu_import_match(&plan,games,1,(struct kui_vmu_import_matches *)(void *)&plan)==-1);
    CHECK(!memcmp(&plan,&plan_before,sizeof(plan)));
    memcpy(games_before,games,sizeof(games));
    CHECK(kui_vmu_import_match(&plan,games,1,(struct kui_vmu_import_matches *)(void *)games)==-1);
    CHECK(!memcmp(games,games_before,sizeof(games)));
    plan.entry_count=209;
    CHECK(kui_vmu_import_match(&plan,games,1,&matches)==-1 && !memcmp(&before,&matches,sizeof(before)));
    plan=plan_before;memset(plan.entries[0].application,'X',sizeof(plan.entries[0].application));
    CHECK(kui_vmu_import_match(&plan,games,1,&matches)==-1 && !memcmp(&before,&matches,sizeof(before)));
}
int main(void) {
    publisher_ids_are_not_families();regions_and_unsupported_competitors();
    contradictory_evidence_is_not_a_tiebreak();exact_product_preserves_whole_source_group();
    uncertain_region_still_keeps_companions_together();
    uninstalled_explicit_product_does_not_guess_region();
    transitive_companions_are_one_complete_uncertain_group();
    sequel_and_version_prefixes_are_not_ownership();
    truncation_and_short_words_require_review();full_ordinal_coverage_and_catalogue_permutation();
    maximum_catalogue_and_unknown_files();
    bounds_overlap_and_input_immutability();
    printf("VMU import matcher independent adversarial: %u checks passed\n",checks);return 0;
}
