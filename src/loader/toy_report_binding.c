/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_report_export.h"
#include <string.h>
bool kui_toy_report_binding_offset(const uint8_t *blob,size_t bytes,
    unsigned profile,size_t *out) {
    if(!blob || !out || (profile!=1u && profile!=2u) || bytes<272u || bytes>65536u)
        return false;
    const uint32_t prefix[4]={KUI_TOY_REPORT_BIND_MAGIC0,KUI_TOY_REPORT_BIND_MAGIC1,1u,profile};
    unsigned found=0u;size_t selected=0u;
    for(size_t at=0u;at+sizeof(struct kui_toy_report_binding)<=bytes;at+=4u) {
        if(memcmp(blob+at,prefix,sizeof(prefix)))continue;
        if((at&31u) || at<80u || ++found>1u)
            return false;
        for(size_t i=16u;i<sizeof(struct kui_toy_report_binding);++i)
            if(blob[at+i])return false;
        selected=at;
    }
    if(found!=1u)return false;
    *out=selected;return true;
}
