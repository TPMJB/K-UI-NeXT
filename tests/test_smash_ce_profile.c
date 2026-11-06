/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_profile.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *path,unsigned kind) {
    struct kui_smash_ce_profile p;
    FILE *f=fopen(path,"rb");assert(f);
    assert(fread(&p,1u,sizeof(p),f)==sizeof(p) && fgetc(f)==EOF && !fclose(f));
    assert(kui_smash_ce_profile_valid(&p) && p.kind==kind);
    uint8_t payload[256]={0};memcpy(payload+16u,&p,sizeof(p));
    struct kui_smash_ce_profile found;
    assert(kui_smash_ce_profile_find(payload,sizeof(payload),kind,&found)==1 && !memcmp(&found,&p,sizeof(p)));
    assert(kui_smash_ce_profile_find(payload,sizeof(payload),3u-kind,&found)==-1);
    for(size_t i=0u;i<sizeof(p);++i) {
        payload[16u+i]^=1u;
        assert(kui_smash_ce_profile_find(payload,sizeof(payload),kind,&found)!=1);
        payload[16u+i]^=1u;
    }
    /* Re-sealing cannot turn a different virtual slot or kernel into this
     * admitted backend. Every immutable owner word has a semantic check. */
    for(unsigned i=2u;i<19u;++i) {
        if(i==15u)continue; /* Helper bytes are independently bound by seed. */
        struct kui_smash_ce_profile bad=p;uint32_t *words=(void *)&bad;
        words[i]^=1u;bad.crc32=0u;bad.crc32=kui_retail_crc32(0u,&bad,sizeof(bad));
        assert(!kui_smash_ce_profile_valid(&bad));
    }
    memcpy(payload+128u,&p,sizeof(p));
    assert(kui_smash_ce_profile_find(payload,sizeof(payload),kind,&found)==-1);
    memset(payload,0,sizeof(payload));memcpy(payload+17u,&p,sizeof(p));
    assert(kui_smash_ce_profile_find(payload,sizeof(payload),kind,&found)==-1);
    memset(payload,0,sizeof(payload));
    assert(kui_smash_ce_profile_find(payload,sizeof(payload),kind,&found)==0);
    uint32_t magic=KUI_SMASH_CE_PROFILE_MAGIC1;memcpy(payload,&magic,4u);
    assert(kui_smash_ce_profile_find(payload,sizeof(payload),kind,&found)==-1);
    memcpy(payload,&p,sizeof(p));
    for(size_t bytes=8u;bytes<sizeof(p);++bytes)
        assert(kui_smash_ce_profile_find(payload,bytes,kind,&found)==-1);
    const char *module,*helper,*session;
    assert(kui_smash_ce_profile_paths(kind,&module,&helper,&session));
    assert(strstr(module,kind==1u?"retail-smash-vc2.kui":"retail-smash-swirl.kui"));
    assert(strstr(helper,kind==1u?"smash-vc2-vmu-helper.bin":"smash-swirl-vmu-helper.bin"));
    assert(strstr(session,kind==1u?"smash-vc2-vmu-session.bin":"smash-swirl-vmu-session.bin"));
}
int main(int argc,char **argv) {
    assert(argc==3 || argc==5);check(argv[1],1u);check(argv[2],2u);
    if(argc==5) {check(argv[3],1u);check(argv[4],2u);}
    const char *a="bad",*b="bad",*c="bad";
    assert(!kui_smash_ce_profile_paths(0u,&a,&b,&c) && !a && !b && !c);
    puts("PASS sealed CE owners: distinct paths, slots, kernels, helper identity, mutations and ambiguity refusals");
    return 0;
}
