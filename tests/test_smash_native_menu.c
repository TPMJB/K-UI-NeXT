/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_menu.h"
#include "kui/smash_native_child.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *folders[]={"ALTBEAST","COLUMNS","GOLDAXE","PHSTAR2","REVSHIN",
    "SHFORCE","SONIC1","STRAGE2","VECTORM","WRESTWAR"};
static const char *modes[]={"MODE2","MODE0","MODE0","MODE0","MODE0","MODE0","MODE0","MODE0","MODE4","MODE0"};
static const char *skips[]={"SKIP0","SKIP0","SKIP0","SKIP0","SKIP1","SKIP0","SKIP1","SKIP1","SKIP2","SKIP0"};
static struct kui_smash_native_menu_seed blank(void) {
    const uint32_t words[8]={KUI_SMASH_NATIVE_MENU_SEED_BLANK_WORDS};
    struct kui_smash_native_menu_seed s;memset(&s,0,sizeof(s));memcpy(&s,words,sizeof(words));return s;
}
static void selected(unsigned which,uint8_t argv[256],uint8_t ip[16]) {
    char rom[32];assert(snprintf(rom,sizeof(rom),"%s.SGA",folders[which])>0);
    const char *tokens[]={"\\MDE_US.BIN",rom,modes[which],skips[which],which==5u?"SOUND1":"SOUND0"};
    memset(argv,0,256u);size_t at=0;
    for(unsigned i=0;i<5u;i++) {size_t n=strlen(tokens[i])+1u;memcpy(argv+at,tokens[i],n);at+=n;}
    memset(ip,' ',12u);memcpy(ip,folders[which],strlen(folders[which]));
    ip[12]=1u;ip[13]=4u;ip[14]=0x71u;ip[15]=0xa9u;
}
static void slots(void) {
    struct kui_smash_native_menu_seed s=blank();uint8_t payload[1024];
    assert(kui_smash_native_menu_seed_blank(&s) && !kui_smash_native_menu_seed_valid(&s));
    assert(!kui_smash_native_menu_seed_blank(NULL) && !kui_smash_native_menu_seed_valid(NULL));
    assert(kui_smash_native_menu_seed_find(NULL,sizeof(payload))==SIZE_MAX);
    memset(payload,0xa7,sizeof(payload));memcpy(payload+64u,&s,sizeof(s));
    assert(kui_smash_native_menu_seed_find(payload,sizeof(payload))==64u);
    assert(kui_smash_native_menu_seed_find(payload,64u+sizeof(s)-1u)==SIZE_MAX);
    memcpy(payload+512u,&s,sizeof(s));assert(kui_smash_native_menu_seed_find(payload,sizeof(payload))==SIZE_MAX);
    memset(payload,0xa7,sizeof(payload));memcpy(payload+65u,&s,sizeof(s));
    assert(kui_smash_native_menu_seed_find(payload,sizeof(payload))==SIZE_MAX);
    for(size_t i=16u;i<sizeof(s);i++) {
        struct kui_smash_native_menu_seed changed=s;((uint8_t *)&changed)[i]=1u;
        assert(!kui_smash_native_menu_seed_blank(&changed));
        memset(payload,0xa7,sizeof(payload));memcpy(payload+64u,&changed,sizeof(changed));
        assert(kui_smash_native_menu_seed_find(payload,sizeof(payload))==SIZE_MAX);
    }
}
static void captures(void) {
    uint8_t argv[256],ip[16];
    for(unsigned which=0;which<10u;which++) {
        struct kui_smash_native_menu_seed s=blank();selected(which,argv,ip);
        assert(kui_smash_native_menu_seed_capture(&s,argv,ip));
        assert(kui_smash_native_menu_seed_valid(&s) && s.selection==which+1u);
        assert(!memcmp(s.argv,argv,256u) && !memcmp(s.ip_tail,ip,16u));
        assert(kui_smash_native_menu_name(s.selection));
        struct kui_smash_native_menu_seed before=s;
        assert(!kui_smash_native_menu_seed_capture(&s,argv,ip) && !memcmp(&s,&before,sizeof(s)));
        for(size_t i=0;i<sizeof(s);i++)for(unsigned bit=0;bit<8u;bit++) {
            struct kui_smash_native_menu_seed changed=s;((uint8_t *)&changed)[i]^=(uint8_t)(1u<<bit);
            assert(!kui_smash_native_menu_seed_valid(&changed));
        }
        for(size_t i=0;i<256u+14u;i++) {
            struct kui_smash_native_menu_seed fresh=blank(),original=fresh;
            uint8_t badargv[256],badip[16];memcpy(badargv,argv,256u);memcpy(badip,ip,16u);
            if(i<256u)badargv[i]^=0x80u;else badip[i-256u]^=0x80u;
            assert(!kui_smash_native_menu_seed_capture(&fresh,badargv,badip));
            assert(!memcmp(&fresh,&original,sizeof(fresh)));
        }
    }
    assert(!kui_smash_native_menu_name(0u) && !kui_smash_native_menu_name(11u));
}
static void module(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long bytes=ftell(f);assert(bytes>64 && bytes<4*1024*1024);assert(!fseek(f,0,SEEK_SET));
    uint8_t *file=malloc((size_t)bytes);assert(file);
    assert(fread(file,1u,(size_t)bytes,f)==(size_t)bytes);assert(!fclose(f));
    assert(!memcmp(file,"KUIRUN1\0",8u));
    size_t at=kui_smash_native_menu_seed_find(file+64u,(size_t)bytes-64u);assert(at!=SIZE_MAX);
    struct kui_smash_native_menu_seed *s=(void *)(file+64u+at);
    uint8_t argv[256],ip[16];selected(5u,argv,ip);
    assert(kui_smash_native_menu_seed_capture(s,argv,ip) && kui_smash_native_menu_seed_valid(s));
    assert(kui_smash_native_menu_seed_find(file+64u,(size_t)bytes-64u)==SIZE_MAX);
    free(file);
}
static void arguments(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,KUI_SMASH_NATIVE_CONTEXT_OFFSET,SEEK_SET));
    uint8_t original[KUI_SMASH_NATIVE_CONTEXT_BYTES],context[sizeof(original)],before[sizeof(original)];
    assert(fread(original,1u,sizeof(original),f)==sizeof(original));assert(!fclose(f));
    for(unsigned which=0;which<10u;which++) {
        uint8_t argv[256],ip[16],out[16];selected(which,argv,ip);
        struct kui_smash_native_menu_seed s=blank();assert(kui_smash_native_menu_seed_capture(&s,argv,ip));
        memcpy(context,original,sizeof(context));memset(out,0x5c,sizeof(out));
        assert(kui_smash_native_menu_arguments(context,sizeof(context),out,&s));
        assert(!memcmp(context,original,256u) && !memcmp(context+256u,argv,256u));
        assert(!memcmp(context+512u,original+512u,sizeof(context)-512u) && !memcmp(out,ip,16u));
        assert(!kui_smash_native_menu_arguments(context,sizeof(context),out,&s));
        for(size_t i=0;which==0u && i<sizeof(context);i++) {
            memcpy(context,original,sizeof(context));context[i]^=1u;memcpy(before,context,sizeof(before));
            memset(out,0x5c,sizeof(out));assert(!kui_smash_native_menu_arguments(context,sizeof(context),out,&s));
            assert(!memcmp(context,before,sizeof(context)));
            for(size_t j=0;j<sizeof(out);j++)assert(out[j]==0x5c);
        }
        memcpy(context,original,sizeof(context));
        assert(!kui_smash_native_menu_arguments(NULL,sizeof(context),out,&s));
        assert(!kui_smash_native_menu_arguments(context,sizeof(context),NULL,&s));
        assert(!kui_smash_native_menu_arguments(context,sizeof(context),out,NULL));
        assert(!kui_smash_native_menu_arguments(context,sizeof(context)-1u,out,&s));
        assert(!kui_smash_native_menu_arguments(context,sizeof(context),context+100u,&s));
        assert(!memcmp(context,original,sizeof(context)));
    }
}
int main(int argc,char **argv) {
    assert(argc>=1 && argc<=3);slots();captures();if(argc>=2)arguments(argv[1]);if(argc==3)module(argv[2]);
    puts("PASS Smash menu native seed: ten exact source selections, CRC, immutable context, carried argv/IP");return 0;
}
