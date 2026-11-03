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
static void capture_base(const char *name,const char *expected) {
    char base[KUI_GAMES_GROUP_NAME_CAP]="unchanged";
    assert(kui_games_group_capture_base(name,base) && !strcmp(base,expected));
}
static void capture_refuses(const char *name) {
    char base[KUI_GAMES_GROUP_NAME_CAP]="unchanged";
    assert(!kui_games_group_capture_base(name,base) && !strcmp(base,"unchanged"));
}
static void capture_names(void) {
    capture_base("Shenmue","Shenmue");capture_base("Shenmue (2)","Shenmue");
    capture_base("Shenmue (3).gdi","Shenmue");capture_base("Shenmue (9999).GDI","Shenmue");
    capture_base("  Shenmue  .GDI","Shenmue");capture_base(" Shenmue (2).gdi","Shenmue");
    capture_base("Shenmue (2) (3)","Shenmue (2)");
    capture_base("Shenmue (USA) (Rev 1) (2).cdi","Shenmue (USA) (Rev 1)");
    capture_base("エターナルアルカディア (2).gdi","エターナルアルカディア");
    capture_base("Crazy Taxi 2","Crazy Taxi 2");capture_base("D2","D2");
    capture_base("Skies_of_Arcadia (2)","Skies_of_Arcadia");
    static const char *const suffixes[]={" (0)"," (1)"," (02)"," (0002)"," (10000)",
        " (2a)"," (+2)"," (-2)","(2)"," [2]"," ( 2)"," (2 )"," (2).txt"};
    for(unsigned i=0;i<sizeof(suffixes)/sizeof(suffixes[0]);i++) {
        char name[80];assert(snprintf(name,sizeof(name),"Shenmue%s",suffixes[i])>0);
        capture_base(name,name);
    }
    static const char *const extensions[]={".gdi",".cue",".cdi",".iso",".bin",".img",".cso",".zso",".chd"};
    for(unsigned i=0;i<sizeof(extensions)/sizeof(extensions[0]);i++) {
        char name[80];assert(snprintf(name,sizeof(name),"Shenmue (2)%s",extensions[i])>0);
        capture_base(name,"Shenmue");
    }
    char long_name[160];memset(long_name,'a',127u);long_name[127]=0;capture_base(long_name,long_name);
    long_name[127]='a';long_name[128]=0;capture_refuses(long_name);
    capture_base("(2)","(2)");capture_refuses(NULL);capture_refuses("");capture_refuses(" (2)");
    capture_refuses("CON (2).gdi");capture_refuses("/Games/Shenmue (2)");capture_refuses("bad\xc0\x80 (2)");
    assert(!kui_games_group_capture_base("Shenmue",NULL));
}
static void shenmue_ip(uint8_t ip[2048],unsigned disc) {
    /* Fixed IP release fields from the USA Disc 1/2/3 headers (3028/3029/3034).
     * The rest is synthetic; no executable, boot or game content is included. */
    memset(ip,0,2048u);memset(ip,' ',256u);
    memcpy(ip,"SEGA SEGAKATANA ",16u);memcpy(ip+16u,"SEGA ENTERPRISES",16u);
    memcpy(ip+32u,"0000 GD-ROM1/4  ",16u);ip[43]=(uint8_t)('0'+disc);
    memcpy(ip+48u,"U       ",8u);memcpy(ip+56u,"0799A10 ",8u);
    memcpy(ip+64u,"MK-51059  ",10u);memcpy(ip+74u,"V1.003",6u);
    memcpy(ip+128u,"SHENMUE",7u);
}
static void evidence_refuses(const uint8_t *ip) {
    struct kui_games_group_evidence out,before;memset(&out,0xa5,sizeof(out));before=out;
    assert(!kui_games_group_evidence_read(ip,&out) && !memcmp(&out,&before,sizeof(out)));
}
static void grouping_evidence(void) {
    uint8_t ip[2048],changed[2048];struct kui_games_group_evidence first,second,other;
    shenmue_ip(ip,1u);assert(kui_games_group_evidence_read(ip,&first));
    assert(!strcmp(first.title,"SHENMUE") && !strcmp(first.product,"MK-51059") &&
        !strcmp(first.version,"V1.003") && !strcmp(first.region,"U") && first.disc==1u && first.total==4u);
    for(unsigned disc=2u;disc<=3u;disc++) {
        shenmue_ip(changed,disc);assert(kui_games_group_evidence_read(changed,&second));
        assert(second.disc==disc && kui_games_group_evidence_equal(&first,&second) &&
            kui_games_group_evidence_equal(&second,&first));
    }
    /* Passport has a different release version and must not join the game. */
    shenmue_ip(changed,4u);memcpy(changed+74u,"V1.004",6u);
    assert(kui_games_group_evidence_read(changed,&other) && !kui_games_group_evidence_equal(&first,&other));
    static const struct {unsigned offset;uint8_t value;} differences[]={
        {128u,'s'},{64u,'T'},{79u,'4'},{48u,'J'},{45u,'3'}};
    for(unsigned i=0;i<sizeof(differences)/sizeof(differences[0]);i++) {
        memcpy(changed,ip,sizeof(changed));changed[differences[i].offset]=differences[i].value;
        assert(kui_games_group_evidence_read(changed,&other) && !kui_games_group_evidence_equal(&first,&other));
    }
    /* Outer fixed-field padding is removed; interior title spaces stay exact. */
    memcpy(changed,ip,sizeof(changed));memset(changed+128u,' ',128u);memcpy(changed+130u,"SHEN  MUE",9u);
    assert(kui_games_group_evidence_read(changed,&other) && !strcmp(other.title,"SHEN  MUE"));
    memcpy(changed,ip,sizeof(changed));memset(changed+128u,'A',128u);
    assert(kui_games_group_evidence_read(changed,&other) && strlen(other.title)==128u);
    /* Any nonprintable byte in a fixed evidence field invalidates the whole
     * candidate rather than converting corrupt bytes to the same '?'. */
    static const unsigned starts[]={128u,64u,74u,48u},lengths[]={128u,10u,6u,8u};
    static const uint8_t invalid[]={0u,1u,31u,127u,128u,255u};
    for(unsigned field=0;field<4u;field++) {
        for(unsigned byte=0;byte<lengths[field];byte++) for(unsigned value=0;value<sizeof(invalid);value++) {
            memcpy(changed,ip,sizeof(changed));changed[starts[field]+byte]=invalid[value];evidence_refuses(changed);
        }
        memcpy(changed,ip,sizeof(changed));memset(changed+starts[field],' ',lengths[field]);evidence_refuses(changed);
    }
    static const struct {unsigned offset;uint8_t value;} bad_header[]={
        {0u,'s'},{15u,0u},{36u,'X'},{37u,'C'},{42u,'m'},{43u,'0'},{43u,'5'},
        {44u,':'},{45u,'0'},{45u,'1'},{45u,':'},{46u,0u},{47u,'x'},
        {56u,'G'},{62u,'1'},{63u,0u},{64u,'m'},{66u,' '},{64u,'-'},{71u,'-'}};
    for(unsigned i=0;i<sizeof(bad_header)/sizeof(bad_header[0]);i++) {
        memcpy(changed,ip,sizeof(changed));changed[bad_header[i].offset]=bad_header[i].value;evidence_refuses(changed);
    }
    memcpy(changed,ip,sizeof(changed));memcpy(changed+64u,"1234567890",10u);evidence_refuses(changed);
    memcpy(changed,ip,sizeof(changed));memcpy(changed+64u,"ABCDEFGHIJ",10u);evidence_refuses(changed);
    memcpy(changed,ip,sizeof(changed));memcpy(changed+56u,"0aBcDe0 ",8u);
    assert(kui_games_group_evidence_read(changed,&other));
    evidence_refuses(NULL);assert(!kui_games_group_evidence_read(ip,NULL));
    assert(!kui_games_group_evidence_equal(NULL,&first) && !kui_games_group_evidence_equal(&first,NULL));
    other=first;other.disc=0u;assert(!kui_games_group_evidence_equal(&first,&other));
    other=first;other.total=10u;assert(!kui_games_group_evidence_equal(&first,&other));
    other=first;memset(other.title,'A',sizeof(other.title));assert(!kui_games_group_evidence_equal(&first,&other));
    other=first;other.region[0]=' ';assert(!kui_games_group_evidence_equal(&first,&other));
    other=first;other.product[0]='m';assert(!kui_games_group_evidence_equal(&first,&other));
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
    refuses("Shenmue");refuses("Shenmue (2)");refuses("Shenmue (3).gdi");
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
    capture_names();grouping_evidence();
    puts("PASS explicit disc names, capture candidate families, strict native IP release evidence, qualifiers, bounds and stable keys");
    return 0;
}
