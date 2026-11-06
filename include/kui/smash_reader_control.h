/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_READER_CONTROL_H
#define KUI_SMASH_READER_CONTROL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Explicit no-VMU/no-reservation comparison03. This tag grants no authority
 * without the separate generic foreground capability and exact owner proof. */
#define KUI_SMASH_READER_CONTROL_MAGIC0 UINT32_C(0x33434953)
#define KUI_SMASH_READER_CONTROL_MAGIC1 UINT32_C(0x4c525443)
#define KUI_SMASH_READER_CONTROL_BYTES 24u
struct kui_smash_reader_control {
    uint32_t magic0,magic1,version,bytes,variant,reserved;
};
#define KUI_SMASH_READER_CONTROL_WORDS \
    KUI_SMASH_READER_CONTROL_MAGIC0,KUI_SMASH_READER_CONTROL_MAGIC1,1u,24u,3u,0u
_Static_assert(sizeof(struct kui_smash_reader_control)==KUI_SMASH_READER_CONTROL_BYTES,"Smash control03 tag ABI");
static inline uint32_t kui_smash_reader_control_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
/* Return1 only for one aligned canonical tag. A damaged signature word,
 * malformed record or duplicate is a refusal, never an unmarked fallback. */
static inline int kui_smash_reader_control_find(const void *data,size_t bytes,size_t *offset) {
    const uint8_t *p=data;bool have=false;size_t found=0u;
    if(!p || bytes<4u)return 0;
    for(size_t i=0u;i<=bytes-4u;i++) {
        uint32_t a=kui_smash_reader_control_u32(p+i);
        if(a==KUI_SMASH_READER_CONTROL_MAGIC1) {
            if(i<4u || kui_smash_reader_control_u32(p+i-4u)!=KUI_SMASH_READER_CONTROL_MAGIC0)return -1;
            continue;
        }
        if(a!=KUI_SMASH_READER_CONTROL_MAGIC0)continue;
        if(have || (i&3u) || bytes-i<KUI_SMASH_READER_CONTROL_BYTES ||
           kui_smash_reader_control_u32(p+i+4u)!=KUI_SMASH_READER_CONTROL_MAGIC1 ||
           kui_smash_reader_control_u32(p+i+8u)!=1u ||
           kui_smash_reader_control_u32(p+i+12u)!=KUI_SMASH_READER_CONTROL_BYTES ||
           kui_smash_reader_control_u32(p+i+16u)!=3u || kui_smash_reader_control_u32(p+i+20u))return -1;
        have=true;found=i;
    }
    if(have && offset)*offset=found;
    return have?1:0;
}
#endif
