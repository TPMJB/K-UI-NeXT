/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_group.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void parses(const char *name,const char *expected,unsigned wanted) {
    char base[KUI_GAMES_GROUP_NAME_CAP];unsigned disc=0;
    assert(kui_games_group_parse(name,base,&disc));
    assert(!strcmp(base,expected) && disc==wanted);
}
static void refuses(const char *name) {
    char base[KUI_GAMES_GROUP_NAME_CAP]="unchanged";unsigned disc=777u;
    assert(!kui_games_group_parse(name,base,&disc));
    assert(!strcmp(base,"unchanged") && disc==777u);
}
static void keys_differ(const char *left,const char *right) {
    char a[KUI_GAMES_GROUP_KEY_CAP],b[KUI_GAMES_GROUP_KEY_CAP];
    assert(kui_games_group_key(left,a) && kui_games_group_key(right,b));
    assert(strcmp(a,b));
}
int main(void) {
    parses("SKIES_OF_ARCADIA_D1","SKIES OF ARCADIA",1);
    parses("SKIES_OF_ARCADIA_D2","SKIES OF ARCADIA",2);
    parses("Skies of Arcadia (Disc 1)","Skies of Arcadia",1);
    parses("Skies of Arcadia (Disc 2).gdi","Skies of Arcadia",2);
    parses("Resident Evil - Code Veronica [Disk 1] (USA)","Resident Evil - Code Veronica (USA)",1);
    parses("Resident Evil - Code Veronica (USA) [Disk 2]","Resident Evil - Code Veronica (USA)",2);
    parses("RE_CODE_VERONICA_D1","RE CODE VERONICA",1);
    parses("RE_CODE_VERONICA_D2","RE CODE VERONICA",2);
    parses("D2_D1","D2",1);
    parses("D2_D2","D2",2);
    parses("D2 (USA) (Disc 1)","D2 (USA)",1);
    parses("Disc 1 - Skies of Arcadia","Skies of Arcadia",1);
    parses("Skies of Arcadia - CD2.CHD","Skies of Arcadia",2);
    parses("Skies  Disc_01 (USA) Rev A","Skies (USA) Rev A",1);
    parses("Skies {DISC-2 of 2} (Europe)","Skies (Europe)",2);
    parses("Skies (Disc 1)-2048","Skies 2048",1);
    parses("エターナルアルカディア (Disc 2) (Japan)","エターナルアルカディア (Japan)",2);
    parses("Skies (USA) (Rev 1) D1","Skies (USA) (Rev 1)",1);
    refuses("Crazy Taxi 1");refuses("Crazy Taxi 2");refuses("D2");
    refuses("D2 (USA)");refuses("(D2) (Japan)");
    refuses("Evolution 2.gdi");refuses("MDK2");refuses("DOA2");
    refuses("Time Stalkers");refuses("Resident Evil 2");refuses("Power Stone 2");
    refuses("Sonic Adventure 2");refuses("Shenmue II (USA)");
    refuses("SkiesD1");refuses("Skies_D1x");refuses("Skies_Disc 1st");
    refuses("Disc 1");refuses("(Disc 1)");refuses("Skies Disc 0");
    refuses("Skies Disc 100");refuses("Skies Disc 0001");
    refuses("Skies Disc 3 of 2");refuses("Skies Disc 1 of 0");
    refuses("Skies Disc 1 Disc 2");refuses("Skies D1 CD1");
    refuses("/Games/Skies Disc 1");refuses("Skies Disc 1/2");
    refuses("Skies \xc0\x80 Disc 1");refuses(NULL);
    char long_name[160];memset(long_name,'a',sizeof(long_name));
    strcpy(long_name+130," D1");refuses(long_name);
    memset(long_name,'a',120);strcpy(long_name+120," D1");
    char base[KUI_GAMES_GROUP_NAME_CAP],key[KUI_GAMES_GROUP_KEY_CAP];unsigned disc;
    assert(kui_games_group_parse(long_name,base,&disc) && strlen(base)==120u && disc==1u);
    char unicode_name[160];
    for(unsigned i=0;i<40u;i++)memcpy(unicode_name+i*3u,"海",3);
    strcpy(unicode_name+120," D2");
    assert(kui_games_group_parse(unicode_name,base,&disc) && strlen(base)==120u && disc==2u);
    assert(!memcmp(base,unicode_name,120u));
    memcpy(unicode_name+123,"海海",6);strcpy(unicode_name+129," D1");
    refuses(unicode_name);
    char same[KUI_GAMES_GROUP_KEY_CAP];
    assert(kui_games_group_key("Skies of Arcadia",key));
    assert(kui_games_group_key("SKIES OF ARCADIA",same) && !strcmp(key,same));
    assert(strlen(key)==32u && !strspn(key,"ghijklmnopqrstuvwxyzGHIJKLMNOPQRSTUVWXYZ"));
    assert(strspn(key,"0123456789abcdef")==32u);
    keys_differ("Skies (USA)","Skies (Europe)");
    keys_differ("Skies (USA) (Rev 1)","Skies (USA) (Rev 2)");
    keys_differ("エターナルアルカディア","エターナルアルカディア体験版");
    strcpy(key,"unchanged");assert(!kui_games_group_key(NULL,key) && !strcmp(key,"unchanged"));
    assert(!kui_games_group_key("",key) && !strcmp(key,"unchanged"));
    assert(!kui_games_group_parse("Skies D1",NULL,&disc));
    assert(!kui_games_group_parse("Skies D1",base,NULL));
    assert(!kui_games_group_key("Skies",NULL));
    puts("PASS explicit multi-disc names, sequel refusal, qualifiers, UTF-8, bounds and stable keys");
    return 0;
}
