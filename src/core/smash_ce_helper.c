/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_helper.h"
#include "kui/smash_ce_transport.h"
#include <string.h>
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int kui_smash_ce_helper_find(const void *module,size_t bytes,unsigned kind,
    struct kui_smash_ce_helper *out) {
    uintptr_t base=(uintptr_t)module,output=(uintptr_t)out;
    if(out && (output>UINTPTR_MAX-sizeof(*out) ||
       (module && base<=UINTPTR_MAX-bytes && output<base+bytes && base<output+sizeof(*out))))return -1;
    if(out)memset(out,0,sizeof(*out));
    if(!module || base>UINTPTR_MAX-bytes || (kind!=1u && kind!=2u))return -1;
    if(bytes<16u)return 0;
    const uint8_t *p=module;struct kui_smash_ce_helper found={0};
    for(size_t at=0u;at<=bytes-16u;++at) {
        if(word(p+at)!=KUI_SMASH_CE_HELPER_MAGIC0 || word(p+at+4u)!=KUI_SMASH_CE_HELPER_MAGIC1 ||
           (word(p+at+8u)!=1u && word(p+at+8u)!=2u) || word(p+at+12u)!=KUI_SMASH_CE_HELPER_HEADER_BYTES)continue;
        if(found.raw || (at&3u) || KUI_SMASH_CE_HELPER_HEADER_BYTES>bytes-at)return -1;
        uint32_t raw=word(p+at+20u),padded=word(p+at+24u);
        uint32_t stored=word(p+at+8u)==2u?padded:raw;
        if(word(p+at+16u)!=kind || raw<16u || (raw&3u) ||
           padded!=KUI_SMASH_CE_HELPER_PADDED_BYTES || raw>padded ||
           word(p+at+28u)!=(word(p+at+8u)==2u?padded:0u) ||
           stored>bytes-at-KUI_SMASH_CE_HELPER_HEADER_BYTES)return -1;
        if(word(p+at+8u)==2u && (raw!=KUI_SMASH_CE_TRANSPORT_RAW_BYTES ||
           !kui_smash_ce_transport_static(p+at+KUI_SMASH_CE_HELPER_HEADER_BYTES,stored,NULL)))return -1;
        found=(struct kui_smash_ce_helper){p+at+KUI_SMASH_CE_HELPER_HEADER_BYTES,raw,padded,stored};
    }
    if(found.raw && out)*out=found;
    return found.raw?1:0;
}
