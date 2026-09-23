/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/system_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void same(struct kui_system_settings a,struct kui_system_settings b) {
    assert(a.video_mode==b.video_mode && a.show_memory==b.show_memory &&
           a.music_enabled==b.music_enabled && a.music_volume==b.music_volume);
}
static void crc(uint8_t *record) {
    uint32_t sum=kui_crc32(0,record,36);
    for(unsigned i=0;i<4;i++) record[36+i]=(uint8_t)(sum>>(8*i));
}
static void rejects(const void *record,size_t size) {
    struct kui_system_settings before={KUI_VIDEO_PAL50,false,true,100},out=before;uint64_t sequence=123;
    assert(!kui_system_settings_decode(&out,&sequence,record,size));same(out,before);assert(sequence==123);
}
int main(void) {
    struct kui_system_settings defaults,out;kui_system_settings_default(&defaults);
    assert(defaults.video_mode==KUI_VIDEO_AUTO && defaults.show_memory && !defaults.music_enabled && defaults.music_volume==75);
    assert(!strcmp(kui_system_video_name(KUI_VIDEO_AUTO),"Auto"));
    assert(!strcmp(kui_system_video_name(KUI_VIDEO_NTSC60),"NTSC 60 Hz"));
    assert(!strcmp(kui_system_video_name(KUI_VIDEO_PAL50),"PAL 50 Hz"));
    assert(!strcmp(kui_system_video_name(99),"Invalid"));
    /* Independent Python struct.pack/zlib fixture. */
    const uint8_t fixture[KUI_SYSTEM_SETTINGS_RECORD_SIZE]={
        0x4b,0x55,0x49,0x53,0x59,0x53,0x30,0x31,0x01,0,0,0,0x28,0,0,0,
        0x01,0,0,0,0,0,0,0,0,0x01,0x4b,0,0,0,0,0,0,0,0,0,0x6b,0xa1,0x26,0x7a};
    uint8_t record[KUI_SYSTEM_SETTINGS_RECORD_SIZE+1];uint64_t sequence;
    assert(kui_system_settings_encode(record,&defaults,1));assert(!memcmp(record,fixture,sizeof(fixture)));
    assert(kui_system_settings_decode(&out,&sequence,fixture,sizeof(fixture)));same(out,defaults);assert(sequence==1);
    const uint64_t sequences[]={1,0x0807060504030201ull,UINT64_MAX};
    for(unsigned mode=0;mode<KUI_VIDEO_MODE_COUNT;mode++) for(unsigned flags=0;flags<4;flags++)
        for(unsigned volume=0;volume<=100;volume++) for(unsigned i=0;i<3;i++) {
            struct kui_system_settings wanted={mode,(flags&1u)!=0,(flags&2u)!=0,volume};
            assert(kui_system_settings_encode(record,&wanted,sequences[i]));
            assert(kui_system_settings_decode(&out,&sequence,record,KUI_SYSTEM_SETTINGS_RECORD_SIZE));
            same(out,wanted);assert(sequence==sequences[i]);
        }
    for(unsigned bit=0;bit<KUI_SYSTEM_SETTINGS_RECORD_SIZE*8;bit++) {
        memcpy(record,fixture,sizeof(fixture));record[bit/8]^=(uint8_t)(1u<<(bit%8));rejects(record,sizeof(fixture));
    }
    for(size_t size=0;size<KUI_SYSTEM_SETTINGS_RECORD_SIZE;size++) rejects(fixture,size);
    memcpy(record,fixture,sizeof(fixture));record[KUI_SYSTEM_SETTINGS_RECORD_SIZE]=0;
    rejects(record,sizeof(record));rejects(NULL,sizeof(fixture));
    const unsigned bytes[]={0,8,12,16,24,25,26,27,28,29,30,31,32,33,34,35};
    for(unsigned i=0;i<sizeof(bytes)/sizeof(bytes[0]);i++) {
        memcpy(record,fixture,sizeof(fixture));unsigned pos=bytes[i];
        record[pos]=pos==24?KUI_VIDEO_MODE_COUNT:pos==25?4:pos==26?101:pos==16?0:(uint8_t)(record[pos]+1);
        crc(record);rejects(record,sizeof(fixture));
    }
    assert(!kui_system_settings_decode(NULL,&sequence,fixture,sizeof(fixture)));
    assert(!kui_system_settings_decode(&out,NULL,fixture,sizeof(fixture)));
    memcpy(record,fixture,sizeof(fixture));
    assert(!kui_system_settings_encode(record,&defaults,0));assert(!memcmp(record,fixture,sizeof(fixture)));
    struct kui_system_settings invalid=defaults;invalid.video_mode=KUI_VIDEO_MODE_COUNT;
    assert(!kui_system_settings_encode(record,&invalid,1));assert(!memcmp(record,fixture,sizeof(fixture)));
    invalid=defaults;invalid.music_volume=101;assert(!kui_system_settings_valid(&invalid));
    assert(!kui_system_settings_encode(record,&invalid,1));assert(!memcmp(record,fixture,sizeof(fixture)));
    assert(!kui_system_settings_encode(NULL,&defaults,1));assert(!kui_system_settings_encode(record,NULL,1));
    assert(!kui_system_settings_valid(NULL));kui_system_settings_default(NULL);
    puts("PASS system settings: independent fixture, bounded fields, reserved bytes, CRC and sequences");
    return 0;
}
