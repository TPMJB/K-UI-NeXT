/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently authored payloads and public game metadata spellings. No
 * captured save payload, owner identity, card identifier or profile is used. */
#include "kui/vmu_import_match.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t image[KUI_VMU_IMAGE_BYTES],original[KUI_VMU_IMAGE_BYTES];
static struct kui_vmu_image_workspace work;
static struct kui_vmu_import_plan plan;
static struct kui_vmu_import_matches matches;
static struct kui_vmu_import_match_game games[32];
static unsigned files;
static void put16(uint8_t *p,unsigned v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void blank(void) {
    for(unsigned i=0;i<256u;i++) assert(!kui_vmu_image_blank_block((uint16_t)i,image+i*512u));
    files=0;
}
static void game(unsigned index,const char *title,const char *product,int eligible) {
    memset(&games[index],0,sizeof(games[index]));
    assert(strlen(title)<sizeof(games[index].title) && strlen(product)<sizeof(games[index].product));
    strcpy(games[index].title,title);strcpy(games[index].product,product);
    games[index].native_eligible=eligible!=0;
}
static void add(const char *name,const char *short_text,const char *long_text,const char *app) {
    assert(files<200u && strlen(name)<=12u && strlen(short_text)<=16u &&
        strlen(long_text)<=32u && strlen(app)<=16u);
    unsigned slot=files+8u,block=199u-files;
    uint8_t *e=image+(253u-slot/16u)*512u+(slot%16u)*32u;
    e[0]=0x33;put16(e+2u,block);memcpy(e+4u,name,strlen(name));put16(e+24u,1u);
    put16(image+254u*512u+block*2u,0xfffa);
    uint8_t *h=image+block*512u;memset(h,' ',64u);
    memcpy(h,short_text,strlen(short_text));memcpy(h+16u,long_text,strlen(long_text));
    memcpy(h+48u,app,strlen(app));
    for(unsigned i=64u;i<512u;i++) h[i]=(uint8_t)(i*13u+files*41u);
    ++files;
}
static void run(unsigned count) {
    memcpy(original,image,sizeof(image));
    assert(!kui_vmu_import_plan_inspect(image,sizeof(image),NULL,NULL,&work,&plan));
    assert(!kui_vmu_import_match(&plan,games,count,&matches));
    assert(!memcmp(image,original,sizeof(image)));
    unsigned seen[208]={0},found=0,blocks=0;
    for(unsigned g=0;g<matches.group_count;g++) {
        const struct kui_vmu_import_match_group *group=&matches.groups[g];
        unsigned n=0,b=0,candidates=0;
        assert(group->files && group->label[sizeof(group->label)-1u]==0);
        for(unsigned i=0;i<208u;i++) if(group->members[i/8u]&(1u<<(i%8u))) {
            assert(i<plan.entry_count && !seen[i]++);++n;b+=plan.entries[i].blocks;
        }
        for(unsigned i=0;i<256u;i++) if(group->candidates[i/8u]&(1u<<(i%8u))) {
            assert(i<count);++candidates;
        }
        assert(n==group->files && b==group->blocks && candidates==group->candidate_count);
        found+=n;blocks+=b;
        if(group->state==KUI_VMU_IMPORT_MATCH_AUTO) {
            assert(group->game_index<count && games[group->game_index].native_eligible);
            assert(candidates==1u && (group->candidates[group->game_index/8u]&(1u<<(group->game_index%8u))));
        } else assert(group->game_index==KUI_VMU_IMPORT_MATCH_NO_GAME);
    }
    assert(found==files && blocks==plan.summary.used_blocks);
}
static const struct kui_vmu_import_match_group *for_entry(unsigned ordinal) {
    for(unsigned g=0;g<matches.group_count;g++)
        if(matches.groups[g].members[ordinal/8u]&(1u<<(ordinal%8u))) return &matches.groups[g];
    assert(0);return NULL;
}
static void representative_metadata_inventory(void) {
    static const char *const titles[]={"Toy Commander","Armada","Silver","Time Stalkers",
        "Omikron: The Nomad Soul","Evolution: The World of Sacred Device","Phantasy Star Online",
        "Zombie Revenge","Evolution 2: Far Off Promise","Power Stone 2","Sonic Adventure 2",
        "Grandia II","Rayman 2: The Great Escape","Record of Lodoss War","Crazy Taxi 2",
        "Resident Evil Code: Veronica","Skies of Arcadia","Prince of Persia 3D","Sonic Adventure"};
    for(unsigned i=0;i<sizeof(titles)/sizeof(titles[0]);i++) {
        char product[12];(void)snprintf(product,sizeof(product),"T-%04uN",1200u+i);
        game(i,titles[i],product,i!=17u);
    }
    blank();
    add("KUIPROOF","K-UI VMU proof01","Owned emulation persistence tes","KUI-VMU-PROOF");
    add("TOY_COMM.___","TOY SYSTEM FILE","TOY COMMANDER : SYSTEM FILE","");
    add("TOY_COMM.AAA","TOY SAVED GAME","TOY COMMANDER : SAVED GAME","");
    add("ARMADA00.DAT","Armada Comment","","Armada");
    add("_SILVER_.SYS","SILVER  OPTIONS","SILVER","SILVER");
    add("_SILVER_.001","SILVER  SAVE","SILVER","SILVER");
    add("_SILVER_.002","SILVER  SAVE","SILVER","SILVER");
    add("TIME_STK.001","TIME STALKERS","TIME STALKERS","");
    add("ICONDATA_VMS","Dream Key","","");
    add("THENOMADSOUL","THE NOMAD SOUL","THE NOMAD SOUL","THENOMADSOUL");
    add("EVO_DATA.001","EVOLUTION","Evolution Mag's House Storeroom","EVOLUTION");
    add("EVO_DATA.000","EVOLUTION","Evolution Sheol Ruins","EVOLUTION");
    add("PSO______SYS","PSO/MAIN_DATA","PSO/MAIN_DATA","");
    add("PSO______GCD","PSO/GUILD_CARD","PSO/GUILD_CARD","");
    add("PSO______CHR","PSO/CHARACTER","PSO/CHARACTER","");
    add("PW_BOOKMARKS","Web Favorites","SEGA Dreamcast Browser","");
    add("ZOMBIE_U_SYS","ZOMBIE REVENGE","ZOMBIE REVENGE SYSTEM FILE","");
    add("PRINCELV.OPT","POP 3D Options","Prince of Persia 3D Options","PRNCE OF PRSA3D");
    add("P_STONE2_DAT","POWERSTONE2 DAT","POWERSTONE2 DAT","");
    add("SONIC2___S01","MAIN FILE","SONIC ADVENTURE 2 / Main File","");
    add("EVO2_DAT.001","EVOLUTION2","EVOLUTION2 Hotel Lobby","");
    add("GRANDIA2_000","GRANDIA2","GRANDIA2 WITT FOREST PATH","");
    add("RAYMAN_2.AAA","00.5%","Rayman 2 The great escape","Rayman 2");
    add("LODOSS__.001","LODOSS WAR","RECORD OF LODOSS WAR","");
    add("C_TAXI02.SYS","CT2 System Data","CRAZY TAXI 2  SYSTEM DATA","");
    add("RE_CV000.001","RECV01_GAMEDATA","RE_CV/CLAIRE/N/Prison: front","");
    add("ARCADIA__001","ETERNAL ARCADIA","ETERNAL ARCADIA SAVE","");
    add("TOY_COMM.666","TOY SAVED GAME","TOY COMMANDER : SAVED GAME","");
    add("UNKNOWN.001","\x82\xa0\x82\xa2","\x82\xa0\x82\xa2","");
    assert(files==29u);run(19u);
    unsigned automatic_files=0,automatic_groups=0;
    for(unsigned g=0;g<matches.group_count;g++) if(matches.groups[g].state==KUI_VMU_IMPORT_MATCH_AUTO) {
        automatic_files+=matches.groups[g].files;++automatic_groups;
    }
    assert(automatic_files==24u && automatic_groups==17u);
    assert(for_entry(1)==for_entry(2) && for_entry(1)==for_entry(27) && for_entry(1)->files==3u);
    assert(for_entry(4)==for_entry(5) && for_entry(5)==for_entry(6));
    assert(for_entry(10)==for_entry(11) && for_entry(10)!=for_entry(20));
    assert(for_entry(12)==for_entry(13) && for_entry(13)==for_entry(14));
    assert(for_entry(12)->game_index==6u && for_entry(26)->game_index==16u);
    assert(for_entry(17)->state==KUI_VMU_IMPORT_MATCH_CHOOSE_GAME &&
           (for_entry(17)->reasons&KUI_VMU_IMPORT_MATCH_UNSUPPORTED));
    assert(for_entry(28)->candidate_count==0u);
    printf("Representative 29-file metadata inventory: %u files in %u automatic game groups; 5 unresolved/ineligible files retained\n",
        automatic_files,automatic_groups);
}
static void separate_pso_sources_and_release_choice(void) {
    game(0,"Phantasy Star Online","T-1200N",1);
    for(unsigned source=0;source<2u;source++) {
        blank();
        add("PSO______SYS","PSO/MAIN_DATA","PSO/MAIN_DATA","");
        add("PSO______GCD","PSO/GUILD_CARD","PSO/GUILD_CARD","");
        add("PSO______CHR","PSO/CHARACTER","PSO/CHARACTER","");
        image[199u*512u+100u]=(uint8_t)(source+1u);
        run(1u);assert(matches.group_count==1u && matches.groups[0].files==3u);
        assert(matches.groups[0].state==KUI_VMU_IMPORT_MATCH_AUTO && matches.groups[0].game_index==0u);
    }
    game(1,"Phantasy Star Online Ver.2","T-1201N",1);run(2u);
    assert(matches.group_count==1u && matches.groups[0].state==KUI_VMU_IMPORT_MATCH_CHOOSE_GAME);
    assert(matches.groups[0].candidate_count==2u && (matches.groups[0].candidates[0]&3u)==3u);
    /* A complete exact product pin can distinguish releases; a filename's
     * numeric save suffix never provides this product/version evidence. */
    memcpy(image+199u*512u+48u,"T-1201N         ",16u);run(2u);
    assert(matches.group_count==1u && matches.groups[0].game_index==1u);
}
static void truncated_long_and_case_punctuation(void) {
    const char *title="The Chronicles of the Lost Kingdom";
    game(0,title,"T-2200N",1);blank();
    add("CHRONICLE001","Save","","");
    memcpy(image+199u*512u+16u,title,32u);run(1u);
    assert(matches.groups[0].state==KUI_VMU_IMPORT_MATCH_AUTO &&
           (matches.groups[0].reasons&KUI_VMU_IMPORT_MATCH_TRUNCATED));
    game(1,"The Chronicles of the Lost Kingdom II","T-2201N",0);run(2u);
    assert(matches.groups[0].state==KUI_VMU_IMPORT_MATCH_CHOOSE_GAME && matches.groups[0].candidate_count==2u);
    blank();game(0,"Harbor Racer","T-3300N",1);
    add("HRACER__.001","Harbor-Racer","Harbor Racer / Main File","HARBOR RACER");
    add("HRACER__.CFG","Settings","","Harbor Racer");run(1u);
    assert(matches.group_count==1u && matches.groups[0].files==2u && matches.groups[0].game_index==0u);
}
int main(void) {
    representative_metadata_inventory();
    separate_pso_sources_and_release_choice();
    truncated_long_and_case_punctuation();
    puts("VMU import matching: admitted snapshots, whole families, source variants and unique-title safety passed");
    return 0;
}
