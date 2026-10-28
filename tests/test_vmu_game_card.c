/* SPDX-License-Identifier: GPL-3.0-only */
/* Game-card keys, bindings, identities and the module profile slot. argv[1]
 * is the public build template from tools/vmu_persist_profile.py --template. */
#include "kui/vmu_game_card.h"
#include "kui/vmu_persist_profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)

static void key_is(const char *product,const char *expected) {
    char key[KUI_VMU_GAME_CARD_KEY_BYTES];
    memset(key,0x5a,sizeof(key));
    bool ok=kui_vmu_game_card_key(product,key);
    CHECK(ok==(expected!=NULL));
    if(expected) {
        CHECK(strlen(expected)<=10u && !strcmp(key,expected));
        for(size_t i=strlen(expected);i<sizeof(key);i++) CHECK(!key[i]);
    } else for(size_t i=0;i<sizeof(key);i++) CHECK(!key[i]);
}
static void keys(void) {
    key_is("MK-51136","MK-51136");
    key_is("  mk-51136  ","MK-51136");
    key_is("T-8101N","T-8101N");
    key_is("T-9504D-50","T-9504D-50");
    key_is("HDR-0076","HDR-0076");
    key_is("0000001","0000001");
    key_is("ABCDEFGHIJ","ABCDEFGHIJ");
    key_is("ABCDEFGHIJK",NULL);
    key_is("",NULL);
    key_is("   ",NULL);
    key_is("0000000",NULL);
    key_is("---",NULL);
    key_is("MK 51136",NULL);
    key_is("MK_51136",NULL);
    key_is("MK-5113?",NULL);
    key_is("MK/51136",NULL);
    key_is(NULL,NULL);
    CHECK(!kui_vmu_game_card_key("MK-51136",NULL));
}
static void bindings(void) {
    char key[KUI_VMU_GAME_CARD_KEY_BYTES],back[KUI_VMU_GAME_CARD_KEY_BYTES];
    uint8_t wire[KUI_VMU_GAME_CARD_BINDING_BYTES],bad[KUI_VMU_GAME_CARD_BINDING_BYTES+1];
    CHECK(kui_vmu_game_card_key("MK-51136",key));
    CHECK(kui_vmu_game_card_binding_encode(key,wire));
    CHECK(!memcmp(wire,"KUIVMUG1",8u) && wire[8]==1u && wire[12]==64u && !memcmp(wire+16,"MK-51136",8u));
    for(unsigned i=28u;i<60u;i++) CHECK(!wire[i]);
    CHECK(kui_vmu_game_card_binding_decode(wire,sizeof(wire),back) && !memcmp(back,key,sizeof(key)));
    for(unsigned at=0;at<sizeof(wire);at++) for(unsigned bit=0;bit<8u;bit++) {
        memcpy(bad,wire,sizeof(wire));bad[at]^=(uint8_t)(1u<<bit);
        memset(back,0x5a,sizeof(back));
        CHECK(!kui_vmu_game_card_binding_decode(bad,sizeof(wire),back));
        for(unsigned i=0;i<sizeof(back);i++) CHECK(!back[i]);
    }
    memcpy(bad,wire,sizeof(wire));bad[64]=0;
    CHECK(!kui_vmu_game_card_binding_decode(bad,sizeof(wire)+1u,back));
    CHECK(!kui_vmu_game_card_binding_decode(wire,sizeof(wire)-1u,back));
    CHECK(!kui_vmu_game_card_binding_decode(NULL,sizeof(wire),back));
    char lower[KUI_VMU_GAME_CARD_KEY_BYTES]="mk-51136",full[KUI_VMU_GAME_CARD_KEY_BYTES];
    memset(full,'A',sizeof(full));
    CHECK(!kui_vmu_game_card_binding_encode(lower,wire));
    for(unsigned i=0;i<sizeof(wire);i++) CHECK(!wire[i]);
    CHECK(!kui_vmu_game_card_binding_encode(full,wire));
    char padded[KUI_VMU_GAME_CARD_KEY_BYTES]={'M','K','-','1',0,'X'};
    CHECK(!kui_vmu_game_card_binding_encode(padded,wire));
}
static void identities(void) {
    char a[KUI_VMU_GAME_CARD_KEY_BYTES],b[KUI_VMU_GAME_CARD_KEY_BYTES];
    uint8_t salt[16]={9},one[16],two[16];
    CHECK(kui_vmu_game_card_key("MK-51136",a) && kui_vmu_game_card_key("MK-51058",b));
    kui_vmu_game_card_identity(a,12u,salt,one);
    kui_vmu_game_card_identity(a,12u,salt,two);CHECK(!memcmp(one,two,16u));
    kui_vmu_game_card_identity(b,12u,salt,two);CHECK(memcmp(one,two,16u));
    kui_vmu_game_card_identity(a,13u,salt,two);CHECK(memcmp(one,two,16u));
    salt[15]=1;kui_vmu_game_card_identity(a,12u,salt,two);CHECK(memcmp(one,two,16u));
    unsigned any=0;for(unsigned i=0;i<16u;i++) any|=one[i];
    CHECK(any);
}
static void pair_numbers(void) {
    CHECK(kui_vmu_game_card_pair_number("p0001")==1u);
    CHECK(kui_vmu_game_card_pair_number("p0005")==5u);
    CHECK(kui_vmu_game_card_pair_number("p9999")==9999u);
    CHECK(!kui_vmu_game_card_pair_number("p0000"));
    CHECK(!kui_vmu_game_card_pair_number("P0005"));
    CHECK(!kui_vmu_game_card_pair_number("p005"));
    CHECK(!kui_vmu_game_card_pair_number("p00050"));
    CHECK(!kui_vmu_game_card_pair_number("p00a5"));
    CHECK(!kui_vmu_game_card_pair_number("v0005"));
    CHECK(!kui_vmu_game_card_pair_number(""));
    CHECK(!kui_vmu_game_card_pair_number(NULL));
}
static void slots(const char *template_path) {
    static uint8_t wire[4096],payload[65536];
    FILE *file=fopen(template_path,"rb");CHECK(file);
    CHECK(fread(wire,1,sizeof(wire),file)==sizeof(wire) && fgetc(file)==EOF && !fclose(file));
    static struct kui_vmu_persist_profile profile;
    CHECK(!kui_vmu_persist_profile_decode(&profile,wire,sizeof(wire)));
    CHECK(kui_vmu_game_card_template(profile.cid));
    uint8_t other[16]={0};memcpy(other,profile.cid,16u);other[15]^=1u;
    CHECK(!kui_vmu_game_card_template(other) && !kui_vmu_game_card_template(NULL));
    memset(payload,0xa5,sizeof(payload));
    /* Decoys: the codec's own magic string, and a broken copy of the wire. */
    memcpy(payload+100,"KUIVMUP2",8u);
    memcpy(payload+4096,wire,sizeof(wire));payload[4096+700]^=1u;
    CHECK(kui_vmu_game_card_profile_slot(payload,sizeof(payload))==SIZE_MAX);
    memcpy(payload+20480,wire,sizeof(wire));
    CHECK(kui_vmu_game_card_profile_slot(payload,sizeof(payload))==20480u);
    CHECK(kui_vmu_game_card_profile_slot(payload,20480u+4095u)==SIZE_MAX);
    CHECK(kui_vmu_game_card_profile_slot(payload,20480u+4096u)==20480u);
    memcpy(payload+40962,wire,sizeof(wire));
    CHECK(kui_vmu_game_card_profile_slot(payload,sizeof(payload))==20480u);
    memcpy(payload+40960,wire,sizeof(wire));
    CHECK(kui_vmu_game_card_profile_slot(payload,sizeof(payload))==SIZE_MAX);
    CHECK(kui_vmu_game_card_profile_slot(NULL,sizeof(payload))==SIZE_MAX);
    CHECK(kui_vmu_game_card_profile_slot(payload,100u)==SIZE_MAX);
}
int main(int argc,char **argv) {
    if(argc!=2) {fprintf(stderr,"Usage: %s TEMPLATE\n",argv[0]);return 2;}
    keys();bindings();identities();pair_numbers();slots(argv[1]);
    printf("vmu game card: %u checks\n",checks);
    return 0;
}
