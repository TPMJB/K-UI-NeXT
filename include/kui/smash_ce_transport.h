/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CE_TRANSPORT_H
#define KUI_SMASH_CE_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define KUI_SMASH_CE_TRANSPORT_RAW_BYTES 41152u
#define KUI_SMASH_CE_TRANSPORT_HEADER_BYTES 32u
#define KUI_SMASH_CE_TRANSPORT_CODE_OFFSET 41184u
#define KUI_SMASH_CE_TRANSPORT_FILE_BYTES 49152u
#define KUI_SMASH_CE_TRANSPORT_RUNTIME_BYTES 632u
/* The final region word before native map slots is unused by every live
 * reader overlay. Unlike title/trace words, ordinary GD calls preserve it. */
#define KUI_SMASH_CE_TRANSPORT_SEAL_OFFSET 248u
#define KUI_SMASH_CE_TRANSPORT_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_CE_TRANSPORT_MAGIC1 UINT32_C(0x31525443)
struct kui_smash_ce_transport {
    uint32_t magic0,magic1,version,bytes,raw_bytes,code_bytes,runtime_offset,runtime_bytes;
};
_Static_assert(sizeof(struct kui_smash_ce_transport)==32u,"CE transport header ABI");
static inline bool kui_smash_ce_transport_valid(const struct kui_smash_ce_transport *t) {
    return t && t->magic0==KUI_SMASH_CE_TRANSPORT_MAGIC0 && t->magic1==KUI_SMASH_CE_TRANSPORT_MAGIC1 &&
        t->version==1u && t->bytes==sizeof(*t) && t->raw_bytes==KUI_SMASH_CE_TRANSPORT_RAW_BYTES &&
        t->code_bytes && !(t->code_bytes&3u) &&
        t->code_bytes<=KUI_SMASH_CE_TRANSPORT_FILE_BYTES-KUI_SMASH_CE_TRANSPORT_CODE_OFFSET &&
        !(t->runtime_offset&3u) && t->runtime_bytes==KUI_SMASH_CE_TRANSPORT_RUNTIME_BYTES &&
        t->runtime_offset<=t->code_bytes && t->runtime_bytes<=t->code_bytes-t->runtime_offset;
}
/* The independently admitted complete file CRC owns this descriptor. This
 * function never accepts a shorter raw helper as if it contained a tail. */
static inline bool kui_smash_ce_transport_decode(const void *image,size_t bytes,
    struct kui_smash_ce_transport *out) {
    if(!image || !out || bytes!=KUI_SMASH_CE_TRANSPORT_FILE_BYTES)return false;
    struct kui_smash_ce_transport t;
    memcpy(&t,(const uint8_t *)image+KUI_SMASH_CE_TRANSPORT_RAW_BYTES,sizeof(t));
    if(!kui_smash_ce_transport_valid(&t))return false;
    *out=t;return true;
}
static inline bool kui_smash_ce_transport_static(const void *image,size_t bytes,
    struct kui_smash_ce_transport *out) {
    struct kui_smash_ce_transport t;
    if(!kui_smash_ce_transport_decode(image,bytes,&t))return false;
    const uint8_t *p=image;
    for(uint32_t i=0u;i<t.runtime_bytes;i++)
        if(p[KUI_SMASH_CE_TRANSPORT_CODE_OFFSET+t.runtime_offset+i])return false;
    for(uint32_t i=KUI_SMASH_CE_TRANSPORT_CODE_OFFSET+t.code_bytes;i<bytes;i++)if(p[i])return false;
    if(out)*out=t;
    return true;
}
#endif
