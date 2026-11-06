/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_prepare.h"
#include "kui/vc2_ce_maple_profile.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *body,*original,*sink,*saved;
static uint8_t bridge[100];
static unsigned pubs,fault,mode;
static const size_t ram=KUI_VC2_CE_RAMEND_WORD_P1-KUI_VC2_CE_KERNEL_BODY_P1;
static const size_t record=KUI_VC2_CE_MAPLE_TEXT_RECORD_P1-KUI_VC2_CE_KERNEL_BODY_P1;
static const size_t text=KUI_VC2_CE_MAPLE_TEXT_P1-KUI_VC2_CE_KERNEL_BODY_P1;
static const size_t worker=KUI_VC2_CE_MAPLE_WORKER_PATCH_RVA-KUI_VC2_CE_MAPLE_TEXT_RVA;
static const size_t cave=KUI_VC2_CE_MAPLE_BRIDGE_RVA-KUI_VC2_CE_MAPLE_TEXT_RVA;
static uint32_t word(const uint8_t *p) {uint32_t v;memcpy(&v,p,4);return v;}
static void put(uint8_t *p,uint32_t v) {memcpy(p,&v,4);}
static bool zero(const uint8_t *p,size_t n) {while(n--)if(*p++)return false;return true;}
static void exact_body(unsigned phase) {
    for(size_t i=0;i<KUI_VC2_CE_KERNEL_BODY_BYTES;i++) {
        if(i>=ram && i<ram+4u)continue;
        if(phase==2u && mode!=4u &&
           ((i>=record && i<record+4u)||(i>=record+12u && i<record+16u)))continue;
        assert(body[i]==original[i]);
    }
    assert(word(body+ram)==KUI_VC2_VMU_RAMEND_P1);
    assert(word(body+record)==(phase==2u && mode!=4u?KUI_VC2_CE_MAPLE_TEXT_EXTENDED:KUI_VC2_CE_MAPLE_TEXT_VSIZE));
    assert(word(body+record+12u)==(phase==2u && mode!=4u?KUI_SWIRL_CE_MAPLE_RELOCATE_P1:KUI_VC2_CE_MAPLE_TEXT_P1));
}
static void exact_sink(void) {
    assert(!memcmp(sink,original+text,worker));
    assert(word(sink+worker)==KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD);
    assert(!memcmp(sink+worker+4u,original+text+worker+4u,KUI_VC2_CE_MAPLE_TEXT_VSIZE-worker-4u));
    assert(zero(sink+KUI_VC2_CE_MAPLE_TEXT_VSIZE,cave-KUI_VC2_CE_MAPLE_TEXT_VSIZE));
    assert(!memcmp(sink+cave,bridge,sizeof(bridge)));
    assert(zero(sink+cave+sizeof(bridge),KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES-cave-sizeof(bridge)));
}
static void publish(void *context) {
    assert(context==body);++pubs;
    if((pubs==2u && fault<=3u && fault) || (pubs==3u && fault>=4u)) {
        assert(!memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES));
        assert(mode==4u || zero(sink,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES));return;
    }
    exact_body(pubs);
    if(mode==4u)assert(!memcmp(sink,saved,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES));
    else exact_sink();
    if(pubs==1u) {
        if(fault==1u)body[ram]^=1u;
        if(fault==2u)sink[worker]^=1u;
        if(fault==3u)sink[KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES-1u]^=1u;
    } else if(pubs==2u) {
        if(fault==4u)body[record+12u]^=1u;
        if(fault==5u)body[record]^=1u;
        if(fault==6u)body[ram]^=1u;
        if(fault==7u)sink[cave+99u]^=1u;
    }
}
static void reset(void) {
    memcpy(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES);
    memset(sink,0x5a,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES);
    memcpy(saved,sink,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES);
    pubs=fault=mode=0u;
}
static uint32_t install(struct kui_vc2_ce_vmu_prepare_report *r) {
    return kui_vc2_ce_vmu_install_body_relocated(body,KUI_VC2_CE_KERNEL_BODY_BYTES,
        bridge,sizeof(bridge),sink,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES,publish,body,r,mode);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    uint8_t *allocation=malloc(KUI_VC2_CE_KERNEL_BODY_BYTES+64u);
    uint8_t *private=malloc(KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES+64u);
    original=malloc(KUI_VC2_CE_KERNEL_BODY_BYTES);saved=malloc(KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES);
    assert(allocation&&private&&original&&saved);
    memset(allocation,0xcd,KUI_VC2_CE_KERNEL_BODY_BYTES+64u);
    memset(private,0xce,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES+64u);
    body=allocation+32u;sink=private+32u;
    assert(!fseek(file,0x800,SEEK_SET));
    assert(fread(original,1,KUI_VC2_CE_KERNEL_BODY_BYTES,file)==KUI_VC2_CE_KERNEL_BODY_BYTES);
    assert(fgetc(file)==EOF);fclose(file);
    assert(kui_vc2_ce_maple_source_valid(original,KUI_VC2_CE_KERNEL_BODY_BYTES));
    assert(!zero(original+text+cave,412u)); /* packed metadata must survive */
    memset(bridge,0xa5,sizeof(bridge));put(bridge+0x54u,0xacfc4800u);
    struct kui_vc2_ce_vmu_prepare_report r;
    const unsigned modes[]={0u,1u,3u,4u};
    for(unsigned i=0;i<4u;i++) {
        reset();mode=modes[i];assert(!install(&r));
        assert(r.phase==3u&&r.ram_end==KUI_VC2_VMU_RAMEND_P1&&r.kernel_crc==KUI_VC2_CE_KERNEL_BODY_CRC32);
        assert(pubs==(mode==4u?1u:2u));exact_body(mode==4u?1u:2u);
        if(mode!=4u)exact_sink();
        assert(install(&r)==KUI_VC2_PREPARE_KERNEL); /* refuses already patched source */
    }
    for(unsigned f=1;f<=7u;f++) {
        reset();fault=f;assert(install(&r)==KUI_VC2_PREPARE_VERIFY);
        assert(pubs==(f<=3u?2u:3u));assert(!memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES));
        assert(zero(sink,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES));
    }
    reset();mode=4u;fault=1u;assert(install(&r)==KUI_VC2_PREPARE_VERIFY);
    assert(pubs==2u&&!memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES)&&!memcmp(sink,saved,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES));
    const size_t mutations[]={0u,0x1000u,ram,record,record+12u,text+worker,text+cave,KUI_VC2_CE_KERNEL_BODY_BYTES-1u};
    for(unsigned i=0;i<sizeof(mutations)/sizeof(mutations[0]);i++) {
        reset();body[mutations[i]]^=1u;assert(install(&r)==KUI_VC2_PREPARE_KERNEL&&!pubs);
        assert(!memcmp(sink,saved,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES));
    }
    reset();bridge[0x57u]=0x8c;assert(install(&r)==KUI_VC2_PREPARE_BRIDGE&&!pubs);
    assert(!memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES)&&!memcmp(sink,saved,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES));bridge[0x57u]=0xac;
    assert(kui_vc2_ce_vmu_install_body_relocated(body,KUI_VC2_CE_KERNEL_BODY_BYTES,bridge,100u,
        body+32u,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES,publish,body,&r,0u)==KUI_VC2_PREPARE_ARGUMENT);
    assert(kui_vc2_ce_vmu_install_body_relocated(body,KUI_VC2_CE_KERNEL_BODY_BYTES,bridge,100u,
        sink+1u,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES,publish,body,&r,0u)==KUI_VC2_PREPARE_ARGUMENT);
    assert(kui_vc2_ce_vmu_install_body_relocated(body,KUI_VC2_CE_KERNEL_BODY_BYTES,bridge,100u,
        sink,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES-1u,publish,body,&r,0u)==KUI_VC2_PREPARE_ARGUMENT);
    for(unsigned i=0;i<32u;i++) {
        assert(allocation[i]==0xcd&&allocation[32u+KUI_VC2_CE_KERNEL_BODY_BYTES+i]==0xcd);
        assert(private[i]==0xce&&private[32u+KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES+i]==0xce);
    }
    free(saved);free(original);free(private);free(allocation);
    puts("PASS: Swirl exact source, all original text/packed metadata preserved, complete relocated payload, pointer-last publication, reserve-only, seven publication faults/rollback, source mutations, refused sinks, both canaries");
}
