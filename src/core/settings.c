/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/settings.h"
#include <string.h>

static const uint8_t magic[8]={'K','U','I','S','E','T','0','1'};
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(8*i));
}
static uint32_t get32(const uint8_t *p) {
    uint32_t value=0;for(unsigned i=0;i<4;i++) value|=(uint32_t)p[i]<<(8*i);return value;
}
static void put64(uint8_t *p,uint64_t value) {
    for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(value>>(8*i));
}
static uint64_t get64(const uint8_t *p) {
    uint64_t value=0;for(unsigned i=0;i<8;i++) value|=(uint64_t)p[i]<<(8*i);return value;
}
void kui_settings_default(struct kui_settings *out) {
    if(out) *out=(struct kui_settings){true,false,true,KUI_CAPTURE_FORMAT_GDI};
}
bool kui_settings_encode(uint8_t out[KUI_SETTINGS_RECORD_SIZE],
                         const struct kui_settings *settings,uint64_t sequence) {
    if(!out || !settings || !sequence || (unsigned)settings->capture_format>=KUI_CAPTURE_FORMAT_COUNT) return false;
    memcpy(out,magic,sizeof(magic));
    unsigned version=settings->capture_format==KUI_CAPTURE_FORMAT_GDI?1u:2u;
    out[7]=(uint8_t)('0'+version);put32(out+8,version);put32(out+12,KUI_SETTINGS_RECORD_SIZE);
    put64(out+16,sequence);
    put32(out+24,(settings->crc_only?1u:0u)|(settings->end_readback?2u:0u)|(settings->show_memory?4u:0u)|
        ((uint32_t)settings->capture_format<<3));
    put32(out+28,kui_crc32(0,out,28));return true;
}
bool kui_settings_decode(struct kui_settings *settings,uint64_t *sequence,
                         const void *record,size_t size) {
    if(!settings || !sequence || !record || size!=KUI_SETTINGS_RECORD_SIZE) return false;
    const uint8_t *p=record;
    unsigned version=get32(p+8);
    uint32_t flags=get32(p+24),format=flags>>3;
    if(memcmp(p,magic,sizeof(magic)-1u) || (version!=1u && version!=2u) ||
       p[7]!=(uint8_t)('0'+version) || get32(p+12)!=KUI_SETTINGS_RECORD_SIZE ||
       !get64(p+16) || (version==1u && format) || format>=KUI_CAPTURE_FORMAT_COUNT ||
       get32(p+28)!=kui_crc32(0,p,28)) return false;
    *settings=(struct kui_settings){(flags&1u)!=0,(flags&2u)!=0,(flags&4u)!=0,(enum kui_capture_format)format};
    *sequence=get64(p+16);return true;
}
