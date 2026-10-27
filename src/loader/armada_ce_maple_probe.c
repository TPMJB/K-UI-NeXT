/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact-kernel metadata/mapping observer. The separate HOOK build also
 * reports its pre-NK completion counter; no virtual card or DMA interception. */
#include "kui/armada_ce_maple_probe.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"
#if KUI_ARMADA_CE_MAPLE_STATE
static struct kui_armada_ce_maple_state_workspace state_workspace;
#endif
#if KUI_ARMADA_CE_MAPLE_PROOF
static struct kui_armada_ce_maple_probe_report report={
#if KUI_ARMADA_CE_MAPLE_HEADERS
    .headers=&state_workspace.guard.headers,
#endif
    .labels={
    "DISCOVERY: SAMPLE OK REFUSED READS STATUS",
    "TTB PTEH CCR PROCESS HEAD",
    "MAPLE NODE BASE SPAN SECTIONS ATTACH",
    "MAPLEDEV NODE BASE SPAN SECTIONS ATTACH",
    "MDSTAR MDTSEL MDEN MDST ISTNRM",
    "FAULT WHY AT VALUE EXPECT DETAIL",
    "MAPPING: STATUS READS EXPECT CRC MASK",
    "CODE VA SLOT BLOCK PAGE PHYSICAL",
    "OWNER PROCESS SLOT HANDLE ATTACH ROOT VA",
    "ROOT OWNER P1 PAGE VALUE CANON P1 VALUE",
    "MAP STATUS CODE OWNER CANON WORD CANON VA",
    "FAULT WHY AT VALUE EXPECT DETAIL",
#if KUI_ARMADA_CE_MAPLE_STATE
    "DRIVER: STATUS READS MASK RAW EXT VA",
    "EXT PHYSICAL PAGE SHUTDOWN MMIO SIZE",
    "TX BUS RAW VA LAST END PHYSICAL",
    "RX BUS HIGH RAW VA BIAS PHYSICAL",
    "TX PAGE RX PAGE0 PAGE1 SLOT0 OWNER",
    "FAULT WHY AT VALUE EXPECT DETAIL"
#if KUI_ARMADA_CE_MAPLE_TRAFFIC
    ,"TRAFFIC MAGIC VISITS OK REFUSED READS",
    "SUBUNIT INFO MEDIA READ WRITE OTHER",
    "FIRST OFFSET CONTROL RX REQUEST RESPONSE",
    "LAST OFFSET CONTROL RX REQUEST RESPONSE",
    "RAW EXT TX BUS RX BUS LAST END",
    "TRAFFIC FAULT WHY AT VALUE EXPECT DETAIL"
#if KUI_ARMADA_CE_MAPLE_FENCE
    ,"FENCE MAGIC PRE POST LEASES RESTORES",
    "BUSY REFUSED POLL PEAK ACTIVE FLAGS",
    "BEFORE ENABLE TRIGGER STATE SOURCE EXPECT",
    "HELD ENABLE STATE POST ENABLE STATE SOURCE",
    "RESTORE ENABLE STATE PRE MODE POST MODE EXT",
    "FENCE FAULT WHY AT VALUE EXPECT DETAIL"
#if KUI_ARMADA_CE_MAPLE_A2
    ,"A2 MAGIC VISITS COMPLETE ADVERTISE REPLIES",
    "A2 INFO ALLINFO MEDIA READ FINALIZE",
    "PARENT REQUEST RESPONSE RX CAPACITY FUNC",
    "TARGET REQUEST RESPONSE RX CAPACITY BYTES",
    "REFUSED BACKEND STORES FLAGS ATTEMPTS",
    "A2 FAULT WHY AT VALUE EXPECT DETAIL"
#endif
#endif
#elif KUI_ARMADA_CE_MAPLE_HOOK
    ,"HOOK MAGIC VISITS SAMPLES LIMIT OLD MODE",
    "FIRST EXT SP PR P2 SR OLD MODE",
    "FIRST TTB PTEH CCR PROCESS RESERVED",
    "LAST EXT SP PR P2 SR OLD MODE",
    "LAST TTB PTEH CCR PROCESS RESERVED",
    "HOOK RESERVED ZERO FIELDS"
#endif
#if KUI_ARMADA_CE_MAPLE_HEADERS
    ,"HEADERS: STATUS READS COUNT BYTES MASK",
    "OFFSET CONTROL RX BUS REQUEST RESPONSE",
    "HEADER FAULT WHY AT VALUE EXPECT DETAIL"
#endif
#endif
}};
#define snapshot report.discovery
static struct kui_armada_ce_maple_proof_workspace proof_workspace;
#else
static struct kui_armada_ce_maple_snapshot snapshot;
#endif
static struct kui_armada_ce_maple_workspace workspace;
const uint32_t kui_armada_ce_maple_probe_contract
    __attribute__((used,section(".rodata.kui_armada_ce_maple_probe_contract")))=
    KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT;
static bool read_word(void *context,uint32_t address,uint32_t *value) {
    if(!value || !kui_armada_ce_maple_read_address(address)) return false;
#if defined(KUI_ARMADA_CE_MAPLE_PROBE_HOST_TEST)
    return kui_armada_ce_maple_probe_test_read(context,address,value);
#else
    (void)context;
    *value=*(volatile const uint32_t *)(uintptr_t)address;return true;
#endif
}
#if KUI_ARMADA_CE_MAPLE_PROOF
#if !defined(KUI_ARMADA_CE_MAPLE_PROBE_HOST_TEST)
extern uint32_t kui_armada_ce_excerpt_array_read(uint32_t address);
#endif
/* Same complete-byte audited P2 array reader as the existing CE coherent
 * reader. Header mode additionally reads admitted DMA header words through
 * P2. This callback never dereferences a CE virtual address. */
static bool read_cache(void *unused,uint32_t address,uint32_t *value) {
#if defined(KUI_ARMADA_CE_MAPLE_PROBE_HOST_TEST)
    return kui_armada_ce_maple_probe_test_cache(unused,address,value);
#else
    uint32_t checked;
    (void)unused;
    if((address&3u) || !value) return false;
    if((address&0xffffc01fu)==0xf4000000u ||
       (address&0xffffc003u)==0xf5000000u) {
        uint32_t (*array)(uint32_t)=(uint32_t (*)(uint32_t))
            ((uintptr_t)kui_armada_ce_excerpt_array_read|0x20000000u);
        *value=array(address);return true;
    }
    if((address&0xe0000000u)!=0xa0000000u ||
       !kui_armada_ce_ram_p1(address,&checked)) return false;
    *value=*(volatile const uint32_t *)(uintptr_t)address;
    return true;
#endif
}
#endif
const struct kui_armada_ce_maple_snapshot *kui_armada_ce_bind_probe(
    uint32_t function,uint32_t command,int32_t result,uint32_t sample)
    __attribute__((section(".entry")));
const struct kui_armada_ce_maple_snapshot *kui_armada_ce_bind_probe(
    uint32_t function,uint32_t command,int32_t result,uint32_t sample) {
    if(sample==1u && function==KUI_GD_REQUEST && result>0 &&
       (command==KUI_RETAIL_GD_PLAY || command==KUI_RETAIL_GD_PLAY2)) {
#if KUI_ARMADA_CE_MAPLE_PROOF
        uint32_t before=snapshot.counts[0];
#endif
        (void)kui_armada_ce_maple_observe(&workspace,&snapshot,read_word,NULL);
#if KUI_ARMADA_CE_MAPLE_PROOF
        if(snapshot.counts[0]!=before) {
            kui_armada_ce_maple_zero(&report.proof,sizeof(report.proof));
#if KUI_ARMADA_CE_MAPLE_STATE
            kui_armada_ce_maple_zero(&report.state,sizeof(report.state));
#if KUI_ARMADA_CE_MAPLE_HEADERS
            kui_armada_ce_maple_zero(&state_workspace.guard.headers,sizeof(state_workspace.guard.headers));
#endif
#endif
            if(snapshot.counts[4]==KUI_ARMADA_CE_MAPLE_OK ||
               snapshot.counts[4]==KUI_ARMADA_CE_MAPLE_MISSING)
                (void)kui_armada_ce_maple_proof(&snapshot,&workspace,read_word,read_cache,
                    NULL,&proof_workspace,&report.proof);
#if KUI_ARMADA_CE_MAPLE_STATE
            if(report.proof.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_OK)
                (void)kui_armada_ce_maple_state(&snapshot,&workspace,&report.proof,
                    &proof_workspace,read_word,read_cache,NULL,&state_workspace,&report.state);
#endif
        }
#endif
    }
    return &snapshot;
}
#if defined(KUI_ARMADA_CE_MAPLE_PROBE_HOST_TEST)
void kui_armada_ce_maple_probe_test_reset(void) {
    kui_armada_ce_maple_zero(&snapshot,sizeof(snapshot));
    kui_armada_ce_maple_zero(&workspace,sizeof(workspace));
#if KUI_ARMADA_CE_MAPLE_PROOF
    kui_armada_ce_maple_zero(&report.proof,sizeof(report.proof));
    kui_armada_ce_maple_zero(&proof_workspace,sizeof(proof_workspace));
#if KUI_ARMADA_CE_MAPLE_STATE
    kui_armada_ce_maple_zero(&report.state,sizeof(report.state));
    kui_armada_ce_maple_zero(&state_workspace,sizeof(state_workspace));
#endif
#endif
}
#endif
