/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/system_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void same(struct kui_system_settings a,struct kui_system_settings b) {
    assert(a.video_mode==b.video_mode && a.show_memory==b.show_memory &&
           a.music_enabled==b.music_enabled && a.music_volume==b.music_volume &&
           a.startup_chime==b.startup_chime && a.startup_app==b.startup_app && a.screen_inset==b.screen_inset && a.menu_sounds==b.menu_sounds);
}
static void crc(uint8_t *record) {
    uint32_t sum=kui_crc32(0,record,36);
    for(unsigned i=0;i<4;i++) record[36+i]=(uint8_t)(sum>>(8*i));
}
static void rejects(const void *record,size_t size) {
    struct kui_system_settings before={KUI_VIDEO_PAL50,false,true,100,false,KUI_STARTUP_MUSIC,false,0},out=before;
    uint64_t sequence=123;
    assert(!kui_system_settings_decode(&out,&sequence,record,size));same(out,before);assert(sequence==123);
}
int main(void) {
    struct kui_system_settings defaults,out;kui_system_settings_default(&defaults);
    assert(defaults.video_mode==KUI_VIDEO_AUTO && defaults.show_memory && !defaults.music_enabled &&
           defaults.music_volume==75 && defaults.startup_chime && defaults.startup_app==KUI_STARTUP_HOME);
    assert(!strcmp(kui_system_video_name(KUI_VIDEO_AUTO),"Auto"));
    assert(!strcmp(kui_system_video_name(KUI_VIDEO_NTSC60),"NTSC 60 Hz"));
    assert(!strcmp(kui_system_video_name(KUI_VIDEO_PAL50),"PAL 50 Hz"));
    assert(!strcmp(kui_system_video_name(99),"Invalid"));
    assert(!strcmp(kui_system_startup_name(KUI_STARTUP_HOME),"Home"));
    assert(!strcmp(kui_system_startup_name(KUI_STARTUP_RIPPER),"Disc Ripper"));
    assert(!strcmp(kui_system_startup_name(KUI_STARTUP_VMU),"VMU Manager"));
    assert(!strcmp(kui_system_startup_name(KUI_STARTUP_MUSIC),"Music"));
    assert(!strcmp(kui_system_startup_name(KUI_STARTUP_DIAGNOSTICS),"Diagnostics"));
    assert(!strcmp(kui_system_startup_name(KUI_STARTUP_APP_COUNT),"Invalid"));
    /* Independent Python struct.pack/zlib fixtures: same defaults in all three
     * formats. New fields are defaults when reading the older record. */
    const uint8_t fixtures[3][KUI_SYSTEM_SETTINGS_RECORD_SIZE]={
        {0x4b,0x55,0x49,0x53,0x59,0x53,0x30,0x31,0x01,0,0,0,0x28,0,0,0,
         0x01,0,0,0,0,0,0,0,0,0x01,0x4b,0,0,0,0,0,0,0,0,0,0x6b,0xa1,0x26,0x7a},
        {0x4b,0x55,0x49,0x53,0x59,0x53,0x30,0x31,0x02,0,0,0,0x28,0,0,0,
         0x01,0,0,0,0,0,0,0,0,0x05,0x4b,0,0,0,0,0,0,0,0,0,0xce,0xd5,0x21,0xc8},
        {0x4b,0x55,0x49,0x53,0x59,0x53,0x30,0x31,0x03,0,0,0,0x28,0,0,0,
         0x01,0,0,0,0,0,0,0,0,0x05,0x4b,0,0,0,0,0,0,0,0,0,0xd3,0x28,0x94,0xc9}
    };
    uint8_t record[KUI_SYSTEM_SETTINGS_RECORD_SIZE+1];uint64_t sequence;
    assert(kui_system_settings_encode(record,&defaults,1));
    assert(!memcmp(record,fixtures[2],KUI_SYSTEM_SETTINGS_RECORD_SIZE));
    for(unsigned version=0;version<3;version++) {
        const uint8_t *fixture=fixtures[version];
        assert(kui_system_settings_decode(&out,&sequence,fixture,KUI_SYSTEM_SETTINGS_RECORD_SIZE));
        same(out,defaults);assert(sequence==1);
        for(unsigned bit=0;bit<KUI_SYSTEM_SETTINGS_RECORD_SIZE*8;bit++) {
            memcpy(record,fixture,KUI_SYSTEM_SETTINGS_RECORD_SIZE);
            record[bit/8]^=(uint8_t)(1u<<(bit%8));rejects(record,KUI_SYSTEM_SETTINGS_RECORD_SIZE);
        }
        for(size_t size=0;size<KUI_SYSTEM_SETTINGS_RECORD_SIZE;size++) rejects(fixture,size);
        memcpy(record,fixture,KUI_SYSTEM_SETTINGS_RECORD_SIZE);record[KUI_SYSTEM_SETTINGS_RECORD_SIZE]=0;
        rejects(record,sizeof(record));rejects(NULL,KUI_SYSTEM_SETTINGS_RECORD_SIZE);
        const unsigned bytes[]={0,8,12,16,24,25,26,27,28,29,30,31,32,33,34,35};
        for(unsigned i=0;i<sizeof(bytes)/sizeof(bytes[0]);i++) {
            memcpy(record,fixture,KUI_SYSTEM_SETTINGS_RECORD_SIZE);unsigned pos=bytes[i];
            record[pos]=pos==24?KUI_VIDEO_MODE_COUNT:pos==25?(version==2?16:8):pos==26?101:pos==16?0:
                        pos==8?4:pos==27?KUI_STARTUP_APP_COUNT:pos==28&&version==2?3:(uint8_t)(record[pos]+1);
            crc(record);rejects(record,KUI_SYSTEM_SETTINGS_RECORD_SIZE);
        }
    }
    const uint64_t sequences[]={1,0x0807060504030201ull,UINT64_MAX};
    for(unsigned mode=0;mode<KUI_VIDEO_MODE_COUNT;mode++) for(unsigned flags=0;flags<8;flags++)
        for(unsigned app=0;app<KUI_STARTUP_APP_COUNT;app++) for(unsigned volume=0;volume<=100;volume++)
            for(unsigned i=0;i<3;i++) {
                struct kui_system_settings wanted={mode,(flags&1u)!=0,(flags&2u)!=0,volume,(flags&4u)!=0,app,(volume%2)!=0,(volume%3)};
                assert(kui_system_settings_encode(record,&wanted,sequences[i]));
                assert(kui_system_settings_decode(&out,&sequence,record,KUI_SYSTEM_SETTINGS_RECORD_SIZE));
                same(out,wanted);assert(sequence==sequences[i]);
            }
    /* Version 1 migration keeps every preexisting preference; an old nonzero
     * reserved byte/bit must not become a newly accepted setting. */
    for(unsigned mode=0;mode<KUI_VIDEO_MODE_COUNT;mode++) for(unsigned flags=0;flags<4;flags++) {
        memcpy(record,fixtures[0],KUI_SYSTEM_SETTINGS_RECORD_SIZE);
        record[24]=(uint8_t)mode;record[25]=(uint8_t)flags;record[26]=10;crc(record);
        assert(kui_system_settings_decode(&out,&sequence,record,KUI_SYSTEM_SETTINGS_RECORD_SIZE));
        struct kui_system_settings migrated={mode,(flags&1u)!=0,(flags&2u)!=0,10,true,KUI_STARTUP_HOME,false,0};
        same(out,migrated);
    }
    memcpy(record,fixtures[0],KUI_SYSTEM_SETTINGS_RECORD_SIZE);record[25]|=4;crc(record);rejects(record,KUI_SYSTEM_SETTINGS_RECORD_SIZE);
    memcpy(record,fixtures[0],KUI_SYSTEM_SETTINGS_RECORD_SIZE);record[27]=KUI_STARTUP_RIPPER;crc(record);rejects(record,KUI_SYSTEM_SETTINGS_RECORD_SIZE);
    assert(!kui_system_settings_decode(NULL,&sequence,fixtures[1],KUI_SYSTEM_SETTINGS_RECORD_SIZE));
    assert(!kui_system_settings_decode(&out,NULL,fixtures[1],KUI_SYSTEM_SETTINGS_RECORD_SIZE));
    memcpy(record,fixtures[1],KUI_SYSTEM_SETTINGS_RECORD_SIZE);
    assert(!kui_system_settings_encode(record,&defaults,0));assert(!memcmp(record,fixtures[1],KUI_SYSTEM_SETTINGS_RECORD_SIZE));
    struct kui_system_settings invalid=defaults;invalid.video_mode=KUI_VIDEO_MODE_COUNT;
    assert(!kui_system_settings_encode(record,&invalid,1));assert(!memcmp(record,fixtures[1],KUI_SYSTEM_SETTINGS_RECORD_SIZE));
    invalid=defaults;invalid.music_volume=101;assert(!kui_system_settings_valid(&invalid));
    assert(!kui_system_settings_encode(record,&invalid,1));assert(!memcmp(record,fixtures[1],KUI_SYSTEM_SETTINGS_RECORD_SIZE));
    invalid=defaults;invalid.screen_inset=3;assert(!kui_system_settings_valid(&invalid));
    assert(!kui_system_settings_encode(record,&invalid,1));assert(!memcmp(record,fixtures[1],KUI_SYSTEM_SETTINGS_RECORD_SIZE));
    invalid=defaults;invalid.startup_app=KUI_STARTUP_APP_COUNT;assert(!kui_system_settings_valid(&invalid));
    assert(!kui_system_settings_encode(record,&invalid,1));assert(!memcmp(record,fixtures[1],KUI_SYSTEM_SETTINGS_RECORD_SIZE));
    assert(!kui_system_settings_encode(NULL,&defaults,1));assert(!kui_system_settings_encode(record,NULL,1));
    assert(!kui_system_settings_valid(NULL));kui_system_settings_default(NULL);
    puts("PASS system settings: v1/v2 migration, v3 safe-area fixtures, bounded fields, reserved bytes, CRC and sequences");
    return 0;
}
