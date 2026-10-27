/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_worker.h"
#include "kui/vmu_readonly.h"
#include "kui/hash.h"
#include <string.h>
static struct kui_vmu_readonly card;
static uint8_t request[12],reply[KUI_VMU_READONLY_MAX_RESPONSE_BYTES];
int kui_vmu_native_worker_protocol_probe(struct kui_vmu_native_worker_result *r) {
    if(!r) return -1;
    if(kui_vmu_readonly_init(&card)) return -6;
    const unsigned commands[3]={1u,2u,10u},lengths[3]={116u,196u,32u};
    for(unsigned i=0;i<3u;i++) {
        memset(request,0,sizeof(request));request[0]=(uint8_t)commands[i];request[1]=1u;
        if(i==2u) {request[3]=2u;request[7]=2u;}
        size_t n=0;
        if(kui_vmu_readonly_handle(&card,request,i==2u?12u:4u,reply,sizeof(reply),&n)!=1 ||
           n!=lengths[i] || reply[0]!=(i==0u?5u:i==1u?6u:8u) ||
           reply[1]!=0u || reply[2]!=1u || reply[3]!=(n-4u)/4u) return -6;
    }
    uint32_t crc=0;
    /* Initialize callbacks only in their final location, then exercise all
     * 256 blocks through those relocated internal callback pointers. */
    for(unsigned block=0;block<256u;block++) {
        memset(request,0,sizeof(request));request[0]=11u;request[1]=1u;
        request[3]=2u;request[7]=2u;request[11]=(uint8_t)block;
        size_t n=0;
        if(kui_vmu_readonly_handle(&card,request,sizeof(request),reply,sizeof(reply),&n)!=1 ||
           n!=524u || reply[0]!=8u || reply[1]!=0u || reply[2]!=1u || reply[3]!=130u ||
           memcmp(request+4u,reply+4u,8u)) return -6;
        crc=kui_crc32(crc,reply+12u,512u);
    }
    request[0]=13u;request[9]=4u;size_t n=999u;
    if(kui_vmu_readonly_handle(&card,request,sizeof(request),reply,sizeof(reply),&n)!=0 || n ||
       card.requests!=259u || card.reads!=256u || card.refused!=1u ||
       card.commit_attempts || card.faulted || crc!=KUI_VMU_NATIVE_WORKER_BLANK_CRC) return -6;
    r->requests=card.requests;r->reads=card.reads;r->refused=card.refused;r->image_crc=crc;return 0;
}
