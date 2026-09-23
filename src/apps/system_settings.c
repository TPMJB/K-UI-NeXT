/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/system_settings.h"
#include <string.h>

static const uint8_t magic[8]={'K','U','I','S','Y','S','0','1'};
static void put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i));
}
static uint32_t get32(const uint8_t *p) {
    uint32_t n=0;for(unsigned i=0;i<4;i++) n|=(uint32_t)p[i]<<(8*i);return n;
}
static void put64(uint8_t *p,uint64_t n) {
    for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(n>>(8*i));
}
static uint64_t get64(const uint8_t *p) {
    uint64_t n=0;for(unsigned i=0;i<8;i++) n|=(uint64_t)p[i]<<(8*i);return n;
}
void kui_system_settings_default(struct kui_system_settings *out) {
    if(out) *out=(struct kui_system_settings){
        .video_mode=KUI_VIDEO_AUTO,.show_memory=true,.music_enabled=false,
        .music_volume=75,.startup_chime=true,.startup_app=KUI_STARTUP_HOME
    };
}
bool kui_system_settings_valid(const struct kui_system_settings *settings) {
    return settings && settings->video_mode<KUI_VIDEO_MODE_COUNT &&
        settings->music_volume<=100 && settings->startup_app<KUI_STARTUP_APP_COUNT;
}
const char *kui_system_video_name(unsigned mode) {
    static const char *const names[]={"Auto","NTSC 60 Hz","PAL 50 Hz"};
    return mode<KUI_VIDEO_MODE_COUNT?names[mode]:"Invalid";
}
const char *kui_system_startup_name(unsigned app) {
    static const char *const names[]={"Home","Disc Ripper","VMU Manager","Music","Diagnostics"};
    return app<KUI_STARTUP_APP_COUNT?names[app]:"Invalid";
}
bool kui_system_settings_encode(uint8_t out[KUI_SYSTEM_SETTINGS_RECORD_SIZE],
                                const struct kui_system_settings *settings,uint64_t sequence) {
    if(!out || !sequence || !kui_system_settings_valid(settings)) return false;
    memset(out,0,KUI_SYSTEM_SETTINGS_RECORD_SIZE);
    memcpy(out,magic,sizeof(magic));put32(out+8,2);put32(out+12,KUI_SYSTEM_SETTINGS_RECORD_SIZE);
    put64(out+16,sequence);out[24]=(uint8_t)settings->video_mode;
    out[25]=(uint8_t)((settings->show_memory?1u:0u)|(settings->music_enabled?2u:0u)|
                      (settings->startup_chime?4u:0u));
    out[26]=(uint8_t)settings->music_volume;
    out[27]=(uint8_t)settings->startup_app;
    put32(out+36,kui_crc32(0,out,36));return true;
}
bool kui_system_settings_decode(struct kui_system_settings *settings,uint64_t *sequence,
                                const void *record,size_t size) {
    if(!settings || !sequence || !record || size!=KUI_SYSTEM_SETTINGS_RECORD_SIZE) return false;
    const uint8_t *p=record;
    uint32_t version=get32(p+8);
    if(memcmp(p,magic,sizeof(magic)) || (version!=1 && version!=2) ||
       get32(p+12)!=KUI_SYSTEM_SETTINGS_RECORD_SIZE || !get64(p+16) ||
       (p[25]&~(version==1?3u:7u)) || get32(p+36)!=kui_crc32(0,p,36)) return false;
    for(unsigned i=version==1?27:28;i<36;i++) if(p[i]) return false;
    struct kui_system_settings decoded;
    kui_system_settings_default(&decoded);
    decoded.video_mode=p[24];decoded.show_memory=(p[25]&1u)!=0;
    decoded.music_enabled=(p[25]&2u)!=0;decoded.music_volume=p[26];
    if(version==2) {decoded.startup_chime=(p[25]&4u)!=0;decoded.startup_app=p[27];}
    if(!kui_system_settings_valid(&decoded)) return false;
    *settings=decoded;*sequence=get64(p+16);return true;
}
