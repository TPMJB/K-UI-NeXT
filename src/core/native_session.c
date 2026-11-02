/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_session.h"
#include <string.h>

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool zero(const uint8_t *p,size_t bytes) {
    while(bytes--)if(*p++)return false;
    return true;
}
uint32_t kui_native_session_crc(const void *wire) {
    const uint8_t *p=wire;uint32_t crc=UINT32_MAX;
    if(!p)return 0;
    for(size_t at=0;at<KUI_NATIVE_SESSION_WIRE_BYTES;++at) {
        crc^=at>=KUI_NATIVE_SESSION_CRC_OFFSET && at<KUI_NATIVE_SESSION_CRC_OFFSET+4u?0u:p[at];
        for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    }
    return ~crc;
}
bool kui_native_session_wire_valid(const void *wire,size_t bytes,bool configured) {
    const uint8_t *p=wire;
    if(!p || bytes!=KUI_NATIVE_SESSION_WIRE_BYTES || memcmp(p,KUI_NATIVE_SESSION_MAGIC,8u) ||
       word(p+8u)!=KUI_NATIVE_SESSION_VERSION || word(p+12u)!=KUI_NATIVE_SESSION_WIRE_BYTES ||
       !zero(p+32u,KUI_NATIVE_SESSION_RETURN_OFFSET-32u) ||
       !zero(p+KUI_NATIVE_SESSION_RETURN_OFFSET+KUI_NATIVE_SESSION_RETURN_BYTES,
             KUI_NATIVE_SESSION_MAP_OFFSET-KUI_NATIVE_SESSION_RETURN_OFFSET-KUI_NATIVE_SESSION_RETURN_BYTES))return false;
    uint32_t flags=word(p+KUI_NATIVE_SESSION_FLAGS_OFFSET),count=word(p+KUI_NATIVE_SESSION_COUNT_OFFSET);
    uint32_t selected=word(p+KUI_NATIVE_SESSION_SELECTED_OFFSET);
    if(flags&~KUI_NATIVE_SESSION_FLAGS_ALL)return false;
    if(configured) {
        if(!flags || word(p+KUI_NATIVE_SESSION_CRC_OFFSET)!=kui_native_session_crc(p))return false;
        if(flags&KUI_NATIVE_SESSION_FLAG_DISCS) {if(count<2u || count>KUI_NATIVE_SESSION_DISCS_MAX || selected>=count)return false;}
        else if(count || selected)return false;
    } else if(flags || count || selected || word(p+KUI_NATIVE_SESSION_CRC_OFFSET))return false;
    if(!(flags&KUI_NATIVE_SESSION_FLAG_RETURN) && !zero(p+KUI_NATIVE_SESSION_RETURN_OFFSET,KUI_NATIVE_SESSION_RETURN_BYTES))return false;
    return zero(p+KUI_NATIVE_SESSION_MAP_OFFSET+count*KUI_NATIVE_SESSION_MAP_BYTES,
        (KUI_NATIVE_SESSION_DISCS_MAX-count)*KUI_NATIVE_SESSION_MAP_BYTES);
}
int kui_native_session_descriptor_find(const void *module,size_t bytes,size_t *offset) {
    const uint8_t *p=module;size_t found=0;bool have=false;
    if(!p || bytes<8u)return 0;
    for(size_t at=0;at<=bytes-8u;++at) {
        if(memcmp(p+at,KUI_NATIVE_SESSION_MAGIC,8u))continue;
        /* The comparison literal itself may occur in linked code. Only the
         * complete version/size header identifies a descriptor candidate. */
        if(bytes-at<16u || word(p+at+8u)!=KUI_NATIVE_SESSION_VERSION ||
           word(p+at+12u)!=KUI_NATIVE_SESSION_WIRE_BYTES)continue;
        if(bytes-at<KUI_NATIVE_SESSION_WIRE_BYTES ||
           !kui_native_session_wire_valid(p+at,KUI_NATIVE_SESSION_WIRE_BYTES,false))return -1;
        if(have)return -1;
        have=true;found=at;
    }
    if(have && offset)*offset=found;
    return have?1:0;
}
