/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_return.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct kui_smash_menu_loader_seed outer_plan(void) {
    struct kui_smash_menu_loader_seed s={.payload_bytes=58000u,.padded_bytes=58368u,
        .payload_crc32=0x13579bdfu,.menu_seed_offset=9000u,.first_lba=7000u,
        .first_blocks=114u,.block_count=114u};
    assert(kui_smash_menu_loader_seed_seal(&s));return s;
}
static void unchanged_capture(struct kui_smash_menu_return_seed *s,
    const uint8_t argv[256],const uint8_t ip[16]) {
    const struct kui_smash_menu_return_seed before=*s;
    assert(!kui_smash_menu_return_seed_capture(s,argv,ip));
    assert(!memcmp(s,&before,sizeof(*s)));
}
int main(int argc,char **argv) {
    struct kui_smash_menu_return_seed blank={.magic0=KUI_SMASH_MENU_RETURN_SEED_MAGIC0,
        .magic1=KUI_SMASH_MENU_RETURN_SEED_MAGIC1,.version=1u,.bytes=368u};
    assert(kui_smash_menu_return_seed_blank(&blank));
    assert(!kui_smash_menu_return_seed_valid(&blank));
    uint8_t module[800]={0};memcpy(module+4u,&blank,sizeof(blank));
    assert(kui_smash_menu_return_seed_find(module,sizeof(module))==4u);
    memcpy(module+400u,&blank,sizeof(blank));
    assert(kui_smash_menu_return_seed_find(module,sizeof(module))==SIZE_MAX);
    memset(module,0,sizeof(module));memcpy(module+3u,&blank,sizeof(blank));
    assert(kui_smash_menu_return_seed_find(module,sizeof(module))==SIZE_MAX);
    struct kui_smash_menu_loader_seed plan=outer_plan();
    struct kui_smash_menu_return_seed s=blank;
    assert(kui_smash_menu_return_seed_configure(&s,&plan));
    assert(kui_smash_menu_return_seed_valid(&s));
    for(size_t bit=0u;bit<sizeof(s)*8u;bit++) {
        struct kui_smash_menu_return_seed bad=s;
        ((uint8_t *)&bad)[bit/8u]^=(uint8_t)(1u<<(bit%8u));
        assert(!kui_smash_menu_return_seed_valid(&bad));
    }
    uint8_t returned[256]={0},ip[16];
    memcpy(returned,"\\manifest.bin",sizeof("\\manifest.bin"));
    memset(ip,' ',12u);ip[12]=1u;ip[13]=4u;ip[14]=0xa7u;ip[15]=0xb8u;
    assert(kui_smash_menu_return_seed_capture(&s,returned,ip));
    assert(s.flags==KUI_SMASH_MENU_RETURN_CONTEXT && kui_smash_menu_return_seed_valid(&s));
    assert(!memcmp(s.argv,returned,256u) && !memcmp(s.ip_tail,ip,16u));
    const struct kui_smash_menu_return_seed captured=s;
    uint8_t bad_argv[256];memcpy(bad_argv,returned,256u);bad_argv[255]=1u;
    unchanged_capture(&s,bad_argv,ip);
    memset(bad_argv,0,256u);
    static const char old_selection[]="\\MDE_US.BIN\0SHFORCE.SGA\0MODE0\0SKIP0\0SOUND1";
    memcpy(bad_argv,old_selection,sizeof(old_selection));
    unchanged_capture(&s,bad_argv,ip);
    ip[13]=0u;unchanged_capture(&s,returned,ip);ip[13]=4u;
    assert(kui_smash_menu_return_seed_configure(&s,&s.outer));
    assert(s.flags==KUI_SMASH_MENU_RETURN_CONFIGURED && kui_smash_menu_return_seed_valid(&s));
    for(size_t i=0;i<sizeof(s.argv)+sizeof(s.ip_tail);i++)assert(((uint8_t *)s.argv)[i]==0u);
    if(argc>1) {
        FILE *f=fopen(argv[1],"rb");assert(f);
        uint8_t *image=malloc(212324u),*context=malloc(8192u),*before=malloc(8192u);assert(image&&context&&before);
        assert(fread(image,1,212324u,f)==212324u && fgetc(f)==EOF);assert(!fclose(f));
        assert(kui_retail_crc32(0u,image,212324u)==0xc28e5f94u);
        memcpy(context,image+0x26a44u,8192u);memcpy(before,context,8192u);
        uint8_t output_ip[16]={0};
        assert(kui_smash_menu_return_seed_apply(context,8192u,output_ip,&captured));
        assert(!memcmp(context,before,256u) && !memcmp(context+512u,before+512u,8192u-512u));
        assert(!memcmp(context+256u,returned,256u) && !memcmp(output_ip,ip,16u));
        memcpy(context,before,8192u);context[0x20]^=1u;
        uint8_t changed[8192];memcpy(changed,context,8192u);
        memset(output_ip,0xa5,sizeof(output_ip));
        assert(!kui_smash_menu_return_seed_apply(context,8192u,output_ip,&captured));
        assert(!memcmp(context,changed,8192u));
        for(size_t i=0;i<16u;i++)assert(output_ip[i]==0xa5u);
        memcpy(context,before,8192u);
        assert(!kui_smash_menu_return_seed_apply(context,8192u,context+600u,&captured));
        assert(!memcmp(context,before,8192u));
        free(before);free(context);free(image);
    }
    puts("Smash return seed: CRC, canonical context, prior-argument clearing and late publication OK");
}
