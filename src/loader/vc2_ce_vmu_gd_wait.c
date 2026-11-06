/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_gd_wait.h"

uint32_t kui_vc2_ce_vmu_gd_wait_admit(uint32_t pr,uint32_t sr,
    uint32_t command,uint32_t source) {
    /* These return sites and immediate commands belong to the exact original
     * wsegacd.dll whose full kernel is admitted before this helper exists. */
    static const uint16_t sites[18][2]={
        {0x1f52,24},{0x246e,36},{0x25d8,20},{0x2630,30},{0x267c,31},{0x270c,21},
        {0x2758,33},{0x27a0,26},{0x27e8,22},{0x2830,23},{0x28a0,27},{0x293e,19},
        {0x2d96,17},{0x2dc2,38},{0x30a6,16},{0x30fe,39},{0x3192,16},{0x324e,34}};
    if((sr&UINT32_C(0x500000f0))!=UINT32_C(0x40000000) || source!=3u ||
       (pr&UINT32_C(0x81ff0000))!=UINT32_C(0x01cf0000))return 0u;
    for(unsigned i=0u;i<18u;i++)if((pr&UINT32_C(0xffff))==sites[i][0])
        return command==sites[i][1]?(pr&UINT32_C(0x7e000000))|UINT32_C(0x01cf581c):0u;
    return 0u;
}

#ifndef KUI_VC2_GD_WAIT_HOST_TEST
#include "kui/armada_ce_vmu_a2_publish.h"
#include "kui/armada_ce_maple_auto.h"
#include "kui/vc2_ce_vmu_diagnostic.h"
extern struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
void kui_vc2_ce_vmu_gd_wait_fault(uint32_t why,uint32_t value,uint32_t pr,uint32_t sr) {
    uint32_t *fault=kui_armada_ce_vmu_a2_log.fault;
    if(!fault[0]) {fault[1]=pr;fault[2]=value;fault[3]=sr;fault[4]=why;fault[0]=114u;}
    kui_vc2_ce_vmu_diagnostic_stop(KUI_ARMADA_CE_MAPLE_AUTO_FAULT,0u);
}
#endif
