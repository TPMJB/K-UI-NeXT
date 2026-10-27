/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently authored display parser. Layout: KallistiOS dc/vmu_pkg.h,
 * vmu_hdr_t (16-byte short, 32-byte long and 16-byte app ID), documented at
 * https://kos-docs.dreamcast.wiki/vmu__pkg_8h_source.html . */
#include "kui/vmu_description.h"
#include <string.h>

static unsigned get16(const uint8_t *p) {return p[0]|((unsigned)p[1]<<8);}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void text(char *out,const uint8_t *in,size_t width) {
    size_t length=0;
    while(length<width && in[length]) {
        unsigned c=in[length];
        out[length]=(c>=32 && c<=126)?(char)c:(c<32 || c==127?' ':'?');
        ++length;
    }
    while(length && out[length-1]==' ') --length;
    size_t start=0;while(start<length && out[start]==' ') ++start;
    if(start) {length-=start;memmove(out,out+start,length);}
    out[length]=0;
}
bool kui_vmu_description_parse(const uint8_t *header,size_t header_bytes,
    size_t package_bytes,struct kui_vmu_description *out) {
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!header || header_bytes<128 || package_bytes<128) return false;
    unsigned icons=get16(header+64),eye=get16(header+68);
    static const unsigned eye_bytes[]={0,8064,4544,2048};
    if(icons>3 || eye>3) return false;
    for(unsigned i=76;i<96;i++) if(header[i]) return false;
    size_t prefix=128+(size_t)icons*512+eye_bytes[eye];
    if(prefix>package_bytes || get32(header+72)>package_bytes-prefix) return false;
    text(out->description,header+16,32);
    if(!out->description[0]) text(out->description,header,16);
    text(out->app_id,header+48,16);
    return true;
}
