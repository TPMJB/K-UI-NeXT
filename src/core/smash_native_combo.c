/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_combo.h"
#include "kui/smash_native_child.h"
#include <string.h>
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y?y-x<an:x-y<bn);
}
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
bool kui_smash_native_combo_plan(const void *image,size_t bytes,uint32_t load,
    const uint8_t digest[32],const struct kui_vmu_native_patch_window *protected,
    size_t count,struct kui_smash_native_combo_plan *out) {
    if(!image || !digest || !out || (count && !protected) || count>16u ||
       bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES || bytes>UINTPTR_MAX-(uintptr_t)image ||
       sizeof(*out)>UINTPTR_MAX-(uintptr_t)out ||
       32u>UINTPTR_MAX-(uintptr_t)digest ||
       (count && count*sizeof(*protected)>UINTPTR_MAX-(uintptr_t)protected) ||
       load!=UINT32_C(0x8c010000) ||
       overlap(out,sizeof(*out),image,bytes) || overlap(out,sizeof(*out),digest,32u) ||
       (count && overlap(out,sizeof(*out),protected,count*sizeof(*protected))) ||
       !kui_smash_native_child_engine_owner(bytes,digest))return false;
    const uint8_t *p=image;
    /* get-controller-mapping, clear request, test request, conditional skip,
     * taken BRA and its clear-request delay. Fallthrough begins immediately
     * after this span; it is never replaced by the direct-outer policy. */
    static const uint8_t edge[18]={0x28,0xd3,0x27,0xde,0x0b,0x43,0xc2,0x2e,
        0xe2,0x62,0x28,0x22,0x01,0x89,0xf8,0xa1,0xc2,0x2e};
    static const uint8_t call[8]={0xb9,0xb7,0x09,0x00,0xd3,0xad,0x09,0x00};
    if(memcmp(p+UINT32_C(0x480),edge,sizeof(edge)) ||
       memcmp(p+UINT32_C(0x87a),call,sizeof(call)) ||
       word(p+UINT32_C(0x520))!=UINT32_C(0x8c48f514) ||
       word(p+UINT32_C(0x524))!=UINT32_C(0x8c019894))return false;
    const uint32_t patch=KUI_SMASH_NATIVE_COMBO_BRANCH&UINT32_C(0x1fffffff);
    for(size_t i=0u;i<count;i++) {
        uint32_t area=protected[i].begin&UINT32_C(0xe0000000);
        uint32_t begin=protected[i].begin&UINT32_C(0x1fffffff),n=protected[i].bytes;
        if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
           !n || (begin&1u) || begin<UINT32_C(0x0c010000) ||
           begin-UINT32_C(0x0c010000)>bytes || n>bytes-(begin-UINT32_C(0x0c010000)) ||
           (begin<patch+2u && patch<begin+n))return false;
    }
    const struct kui_smash_native_combo_plan plan={KUI_SMASH_NATIVE_COMBO_BRANCH,
        KUI_SMASH_NATIVE_COMBO_CALL,KUI_SMASH_NATIVE_COMBO_RESET,
        KUI_SMASH_NATIVE_COMBO_ORIGINAL,KUI_SMASH_NATIVE_COMBO_DIRECT};
    *out=plan;return true;
}
