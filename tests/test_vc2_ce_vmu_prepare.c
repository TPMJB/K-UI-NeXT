/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_prepare.h"
#include "kui/vc2_ce_maple_profile.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *body,*original;
static uint8_t bridge[KUI_VC2_CE_MAPLE_BRIDGE_BYTES];
static unsigned publications,corrupt_first_ram,corrupt_second,reserve_only;
static uint32_t original_worker;
static const size_t worker=KUI_VC2_CE_MAPLE_WORKER_PATCH_P1-KUI_VC2_CE_KERNEL_BODY_P1;
static const size_t ram=KUI_VC2_CE_RAMEND_WORD_P1-KUI_VC2_CE_KERNEL_BODY_P1;
static const size_t text=KUI_VC2_CE_MAPLE_TEXT_RECORD_P1-KUI_VC2_CE_KERNEL_BODY_P1;
static const size_t padding=KUI_VC2_CE_MAPLE_PADDING_P1-KUI_VC2_CE_KERNEL_BODY_P1;
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void only_ramend_changed(void) {
    assert(!memcmp(body,original,ram));
    assert(word(body+ram)==KUI_VC2_VMU_RAMEND_P1);
    assert(!memcmp(body+ram+4u,original+ram+4u,
                   KUI_VC2_CE_KERNEL_BODY_BYTES-ram-4u));
}
static void publish(void *context) {
    assert(context==body);++publications;
    if(publications==1u) {
        assert(word(body+worker)==original_worker); /* entry is last */
        assert(word(body+ram)==KUI_VC2_VMU_RAMEND_P1);
        if(reserve_only)only_ramend_changed();
        else {
            assert(word(body+text)==KUI_VC2_CE_MAPLE_TEXT_EXTENDED);
            assert(!memcmp(body+padding,bridge,sizeof(bridge)));
        }
        if(corrupt_first_ram)body[ram]^=1u;
    } else if(reserve_only) {
        assert(publications==2u && corrupt_first_ram);
        assert(!memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES));
    } else if(publications==2u) {
        assert(word(body+worker)==KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD);
        if(corrupt_second)body[worker]^=1u;
    } else {
        assert(publications==3u && corrupt_second);
        assert(!memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES));
    }
}
static void reset(void) {
    memcpy(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES);
    publications=corrupt_first_ram=corrupt_second=reserve_only=0u;
}
static uint32_t install(struct kui_vc2_ce_vmu_prepare_report *r) {
    return kui_vc2_ce_vmu_install_body(body,KUI_VC2_CE_KERNEL_BODY_BYTES,
                                    bridge,sizeof(bridge),publish,body,r);
}
static uint32_t install_mode(struct kui_vc2_ce_vmu_prepare_report *r,uint32_t mode) {
    reserve_only=mode==4u;
    return kui_vc2_ce_vmu_install_body_mode(body,KUI_VC2_CE_KERNEL_BODY_BYTES,
                                          bridge,sizeof(bridge),publish,body,r,mode);
}
int main(int argc,char **argv) {
    if(argc!=2) {fputs("usage: test-vc2-prepare OWNER/0WINCEOS.BIN\n",stderr);return 2;}
    FILE *file=fopen(argv[1],"rb");assert(file);
    uint8_t *allocation=malloc(KUI_VC2_CE_KERNEL_BODY_BYTES+64u);
    original=malloc(KUI_VC2_CE_KERNEL_BODY_BYTES);assert(allocation && original);
    memset(allocation,0xcd,KUI_VC2_CE_KERNEL_BODY_BYTES+64u);body=allocation+32u;
    assert(!fseek(file,KUI_VC2_CE_KERNEL_PREFIX_BYTES,SEEK_SET));
    assert(fread(original,1,KUI_VC2_CE_KERNEL_BODY_BYTES,file)==KUI_VC2_CE_KERNEL_BODY_BYTES);
    assert(fgetc(file)==EOF);fclose(file);
    assert(kui_vc2_ce_maple_source_valid(original,KUI_VC2_CE_KERNEL_BODY_BYTES));
    original_worker=word(original+worker);
    /* Authored bridge-shaped payload: byte publication is tested here;
     * the separate linked-instruction test executes the actual SH wrapper. */
    memset(bridge,0xa5,sizeof(bridge));
    const uint32_t callback=UINT32_C(0xacfc4800);
    memcpy(bridge+0x54u,&callback,sizeof(callback));
    struct kui_vc2_ce_vmu_prepare_report report;
    reset();assert(install(&report)==0u && publications==2u && report.phase==3u);
    uint8_t *changed=malloc(KUI_VC2_CE_KERNEL_BODY_BYTES);assert(changed);
    memcpy(changed,body,KUI_VC2_CE_KERNEL_BODY_BYTES);
    publications=0u;assert(install(&report)==KUI_VC2_PREPARE_KERNEL);
    assert(!publications && !memcmp(body,changed,KUI_VC2_CE_KERNEL_BODY_BYTES));
    const size_t mutations[]={0u,0x1000u,ram,text,worker,padding,padding+99u,
                             KUI_VC2_CE_KERNEL_BODY_BYTES-1u};
    for(unsigned i=0u;i<sizeof(mutations)/sizeof(mutations[0]);i++) {
        reset();body[mutations[i]]^=1u;
        memcpy(changed,body,KUI_VC2_CE_KERNEL_BODY_BYTES);
        assert(install(&report)==KUI_VC2_PREPARE_KERNEL && !publications);
        assert(!memcmp(body,changed,KUI_VC2_CE_KERNEL_BODY_BYTES));
    }
    reset();bridge[0x57u]=0x8cu;
    assert(install(&report)==KUI_VC2_PREPARE_BRIDGE && !publications);
    assert(!memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES));bridge[0x57u]=0xacu;
    reset();corrupt_second=1u;
    assert(install(&report)==KUI_VC2_PREPARE_VERIFY && publications==3u);
    assert(!memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES));
    /* All helpers carry the same code. Modes 0/1/3 retain the ordinary
     * installer, while mode 4 changes exactly the original RAMEND word. */
    const uint32_t modes[]={0u,1u,3u,4u};
    for(unsigned m=0u;m<sizeof(modes)/sizeof(modes[0]);m++) {
        reset();assert(!install_mode(&report,modes[m]));
        assert(report.phase==3u && !report.reason &&
               report.kernel_crc==KUI_VC2_CE_KERNEL_BODY_CRC32 &&
               report.ram_end==KUI_VC2_VMU_RAMEND_P1);
        assert(publications==(reserve_only?1u:2u));
        assert(report.worker_word==(reserve_only?original_worker:
                                   KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD));
        if(reserve_only)only_ramend_changed();
        memcpy(changed,body,KUI_VC2_CE_KERNEL_BODY_BYTES);
        publications=0u;
        assert(install_mode(&report,modes[m])==KUI_VC2_PREPARE_KERNEL);
        assert(!publications && !memcmp(body,changed,KUI_VC2_CE_KERNEL_BODY_BYTES));
        for(unsigned i=0u;i<sizeof(mutations)/sizeof(mutations[0]);i++) {
            reset();body[mutations[i]]^=1u;
            memcpy(changed,body,KUI_VC2_CE_KERNEL_BODY_BYTES);
            assert(install_mode(&report,modes[m])==KUI_VC2_PREPARE_KERNEL);
            assert(!publications && !memcmp(body,changed,KUI_VC2_CE_KERNEL_BODY_BYTES));
        }
    }
    reset();bridge[0x57u]=0x8cu;
    assert(install_mode(&report,4u)==KUI_VC2_PREPARE_BRIDGE && !publications);
    assert(!memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES));bridge[0x57u]=0xacu;
    reset();corrupt_first_ram=1u;
    assert(install_mode(&report,4u)==KUI_VC2_PREPARE_VERIFY && publications==2u);
    assert(report.phase==2u && !memcmp(body,original,KUI_VC2_CE_KERNEL_BODY_BYTES));
    assert(kui_vc2_ce_vmu_install_body(NULL,0u,bridge,sizeof(bridge),publish,body,&report)==
           KUI_VC2_PREPARE_ARGUMENT);
    for(unsigned i=0u;i<32u;i++)assert(allocation[i]==0xcdu &&
        allocation[32u+KUI_VC2_CE_KERNEL_BODY_BYTES+i]==0xcdu);
    free(changed);free(original);free(allocation);
    printf("PASS: original source, normal modes, reserve-only exact body, publication order, "
           "source mutations, rollback, canaries; original worker %08x\n",original_worker);
    return 0;
}
