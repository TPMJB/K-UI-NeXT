/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/storage_policy.h"

bool kui_storage_discover(unsigned *selected,const struct kui_storage_probe_ops *ops) {
    if(!selected || !ops || !ops->open || *selected>KUI_STORAGE_AUTO) return false;
    if(*selected<KUI_STORAGE_AUTO) return ops->open(ops->ctx,*selected);
    for(unsigned candidate=KUI_STORAGE_SCIF;candidate<KUI_STORAGE_AUTO;candidate++) {
        if(ops->open(ops->ctx,candidate)) {*selected=candidate;return true;}
    }
    return false;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(8u*i));
}
bool kui_storage_patch_boot(void *payload,size_t bytes,unsigned transport) {
    if(!payload || bytes<20 || transport>=KUI_STORAGE_AUTO) return false;
    uint8_t *p=payload,*found=NULL;
    for(size_t offset=0;offset<=bytes-20;offset+=4) {
        uint8_t *candidate=p+offset;
        if(get32(candidate)!=KUI_STORAGE_BOOT_MAGIC1 ||
           get32(candidate+4)!=KUI_STORAGE_BOOT_MAGIC2) continue;
        /* A duplicate or malformed recognizable marker is ambiguous, even
         * if another valid marker exists elsewhere in the image. */
        if(found || get32(candidate+8)!=1 || get32(candidate+12)!=KUI_STORAGE_AUTO ||
           get32(candidate+16)!=~(uint32_t)KUI_STORAGE_AUTO) return false;
        found=candidate;
    }
    if(!found) return false;
    put32(found+12,transport);put32(found+16,~(uint32_t)transport);
    return true;
}
unsigned kui_storage_boot_transport(const volatile struct kui_storage_boot_marker *marker) {
    if(!marker || marker->magic1!=KUI_STORAGE_BOOT_MAGIC1 || marker->magic2!=KUI_STORAGE_BOOT_MAGIC2 ||
       marker->version!=1 || marker->transport>=KUI_STORAGE_AUTO ||
       marker->inverse!=~marker->transport) return KUI_STORAGE_AUTO;
    return marker->transport;
}
