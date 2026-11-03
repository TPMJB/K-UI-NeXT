/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_query.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(condition) do {++checks;assert(condition);} while(0)
static bool matches(const struct kui_games_query *query,const char *title,const char *name,const char *region,unsigned disc) {
    struct kui_games_query before=*query;
    bool result=kui_games_query_matches(query,title,name,region,disc);
    CHECK(!memcmp(query,&before,sizeof(before)));return result;
}
int main(void) {
    struct kui_games_query q;
    memset(&q,255,sizeof(q));kui_games_query_default(&q);
    CHECK(kui_games_query_valid(&q));CHECK(!q.text[0] && !q.activity && !q.region && !q.disc);
    CHECK(matches(&q,"Skies of Arcadia","SKIES_OF_ARCADIA_D1","JUE",1));
    CHECK(matches(&q,"","Untitled", "",0));
    strcpy(q.text,"aRcAdIa");CHECK(matches(&q,"Skies of Arcadia","other","U",1));
    CHECK(!matches(&q,"Grandia II","other","U",1));
    strcpy(q.text,"original folder");CHECK(matches(&q,"My renamed game","ORIGINAL FOLDER (Disc 2)","E",2));
    strcpy(q.text,"neo");CHECK(matches(&q,"Neo","Short","",0));
    strcpy(q.text,"neon");CHECK(!matches(&q,"Neo","Short","",0));
    q.text[0]=0;
    for(unsigned region=KUI_GAMES_REGION_JAPAN;region<=KUI_GAMES_REGION_EUROPE;region++) {
        q.region=region;CHECK(matches(&q,"a","b","JUE",1));
        CHECK(matches(&q,"a","b","j u e",1));
        CHECK(!matches(&q,"Japan USA Europe","b","",1));
        CHECK(!matches(&q,"a","b","???",1));
        const char *fields[]={"J","U","E"};
        for(unsigned i=0;i<3;i++)CHECK(matches(&q,"a","b",fields[i],1)==(region==i+1));
    }
    q.region=KUI_GAMES_REGION_UNKNOWN;
    CHECK(matches(&q,"a","b","",0));CHECK(matches(&q,"a","b","   ",0));
    CHECK(matches(&q,"a","b","???",0));CHECK(matches(&q,"a","b","J?",0));
    CHECK(!matches(&q,"a","b","JUE",0));
    q.region=KUI_GAMES_REGION_ANY;
    for(unsigned disc=1;disc<=99;disc++) {
        q.disc=disc;CHECK(matches(&q,"Skies","d","JUE",disc));
        CHECK(!matches(&q,"Skies","d","JUE",0));
        CHECK(!matches(&q,"Skies","d","JUE",disc==99?1:disc+1));
    }
    q.disc=0;CHECK(matches(&q,"Skies","d","JUE",0));
    strcpy(q.text,"Phantasy");q.region=KUI_GAMES_REGION_JAPAN;q.disc=2;
    CHECK(matches(&q,"Phantasy Star Online","d","JU",2));
    CHECK(!matches(&q,"Phantasy Star Online","d","U",2));
    CHECK(!matches(&q,"Phantasy Star Online","d","JU",1));
    CHECK(!matches(&q,"Other","d","JU",2));
    for(unsigned i=0;i<3;i++) {q.activity=i;CHECK(kui_games_query_valid(&q));}
    q.activity=3;CHECK(!kui_games_query_valid(&q));q.activity=0;
    q.region=KUI_GAMES_REGION_COUNT;CHECK(!kui_games_query_valid(&q));q.region=0;
    q.disc=100;CHECK(!kui_games_query_valid(&q));q.disc=0;
    memset(q.text,'x',sizeof(q.text));CHECK(!kui_games_query_valid(&q));
    q.text[64]=0;CHECK(kui_games_query_valid(&q));
    char title[129],name[128],region[9];memset(title,'x',sizeof(title));title[128]=0;
    memset(name,'x',sizeof(name));name[127]=0;memset(region,' ',sizeof(region));region[8]=0;
    CHECK(matches(&q,title,name,region,0));
    title[128]='x';CHECK(!matches(&q,title,name,region,0));title[128]=0;
    name[127]='x';CHECK(!matches(&q,title,name,region,0));name[127]=0;
    region[8]=' ';CHECK(!matches(&q,title,name,region,0));region[8]=0;
    for(unsigned control=1;control<32;control++) {
        q.text[0]=(char)control;q.text[1]=0;CHECK(!kui_games_query_valid(&q));
    }
    q.text[0]=127;CHECK(!kui_games_query_valid(&q));
    static const char *const malformed[]={"\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\x80","\xe2\x82","\xc2\x85"};
    for(unsigned i=0;i<sizeof(malformed)/sizeof(malformed[0]);i++) {
        strcpy(q.text,malformed[i]);CHECK(!kui_games_query_valid(&q));
    }
    strcpy(q.text,"\xc3\xa9");CHECK(kui_games_query_valid(&q));
    CHECK(matches(&q,"Caf\xc3\xa9","other","U",0));
    CHECK(!matches(&q,"Caf\xc3\x89","other","U",0));
    CHECK(kui_games_query_letter("  skies", "z")== 'S');
    CHECK(kui_games_query_letter("", "Armada")== 'A');
    CHECK(kui_games_query_letter("123", "Armada")== '#');
    CHECK(kui_games_query_letter("\xc3\xa9", "Armada")== '#');
    CHECK(kui_games_query_letter("", "")== '#');
    CHECK(!kui_games_query_letter(NULL,"a"));
    for(char letter='A';letter<='Z';letter++) {
        char field[2]={letter,0};CHECK(kui_games_query_jump_matches(field,"",letter));
        CHECK(kui_games_query_jump_matches(field,"",(char)(letter+32)));
    }
    CHECK(kui_games_query_jump_matches("1999","","#"[0]));
    CHECK(!kui_games_query_jump_matches("Armada","",'B'));
    CHECK(!kui_games_query_jump_matches("Armada","",'*'));
    CHECK(!kui_games_query_jump_matches("\x01","",'#'));
    CHECK(!kui_games_query_valid(NULL));
    CHECK(!kui_games_query_matches(NULL,"a","b","U",0));
    kui_games_query_default(NULL);
    for(unsigned i=0;i<KUI_GAMES_REGION_COUNT;i++)CHECK(kui_games_query_region_label(i)!=NULL);
    CHECK(kui_games_query_region_label(KUI_GAMES_REGION_COUNT)==NULL);
    printf("PASS Games queries: %u bounded search/region/disc/jump checks\n",checks);
    return 0;
}
