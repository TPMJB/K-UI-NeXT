/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void same(struct kui_settings a,struct kui_settings b) {
    assert(a.crc_only==b.crc_only && a.end_readback==b.end_readback && a.show_memory==b.show_memory);
}
static void crc(uint8_t *record) {
    uint32_t sum=kui_crc32(0,record,28);
    for(unsigned i=0;i<4;i++) record[28+i]=(uint8_t)(sum>>(8*i));
}
static void rejects(const void *record,size_t size) {
    struct kui_settings before={false,true,false},out=before;uint64_t sequence=123;
    assert(!kui_settings_decode(&out,&sequence,record,size));same(out,before);assert(sequence==123);
}
int main(void) {
    struct kui_settings defaults,out;kui_settings_default(&defaults);
    assert(defaults.crc_only && !defaults.end_readback && defaults.show_memory);
    /* Byte fixture generated independently with Python struct/zlib. */
    const uint8_t fixture[KUI_SETTINGS_RECORD_SIZE]={
        0x4b,0x55,0x49,0x53,0x45,0x54,0x30,0x31,0x01,0,0,0,0x20,0,0,0,
        0x01,0,0,0,0,0,0,0,0x05,0,0,0,0x82,0xe6,0xfb,0xc7};
    uint8_t record[KUI_SETTINGS_RECORD_SIZE+1];uint64_t sequence;
    assert(kui_settings_encode(record,&defaults,1));assert(!memcmp(record,fixture,sizeof(fixture)));
    assert(kui_settings_decode(&out,&sequence,fixture,sizeof(fixture)));same(out,defaults);assert(sequence==1);
    const uint64_t sequences[]={1,0x0807060504030201ull,UINT64_MAX};
    for(unsigned flags=0;flags<8;flags++) for(unsigned i=0;i<3;i++) {
        struct kui_settings wanted={(flags&1)!=0,(flags&2)!=0,(flags&4)!=0};
        assert(kui_settings_encode(record,&wanted,sequences[i]));
        assert(kui_settings_decode(&out,&sequence,record,KUI_SETTINGS_RECORD_SIZE));
        same(out,wanted);assert(sequence==sequences[i]);
    }
    for(unsigned bit=0;bit<KUI_SETTINGS_RECORD_SIZE*8;bit++) {
        memcpy(record,fixture,sizeof(fixture));record[bit/8]^=(uint8_t)(1u<<(bit%8));rejects(record,sizeof(fixture));
    }
    for(size_t size=0;size<KUI_SETTINGS_RECORD_SIZE;size++) rejects(fixture,size);
    memcpy(record,fixture,sizeof(fixture));record[KUI_SETTINGS_RECORD_SIZE]=0;
    rejects(record,sizeof(record));rejects(NULL,sizeof(fixture));
    /* A self-consistent CRC cannot authorize an unknown version/flag or a
     * zero sequence. Those structural checks are independent of corruption. */
    memcpy(record,fixture,sizeof(fixture));record[8]=2;crc(record);rejects(record,sizeof(fixture));
    memcpy(record,fixture,sizeof(fixture));record[12]=31;crc(record);rejects(record,sizeof(fixture));
    memcpy(record,fixture,sizeof(fixture));record[24]|=8;crc(record);rejects(record,sizeof(fixture));
    memcpy(record,fixture,sizeof(fixture));memset(record+16,0,8);crc(record);rejects(record,sizeof(fixture));
    memcpy(record,fixture,sizeof(fixture));record[0]^=1;crc(record);rejects(record,sizeof(fixture));
    assert(!kui_settings_decode(NULL,&sequence,fixture,sizeof(fixture)));
    assert(!kui_settings_decode(&out,NULL,fixture,sizeof(fixture)));
    memcpy(record,fixture,sizeof(fixture));
    assert(!kui_settings_encode(record,&defaults,0));assert(!memcmp(record,fixture,sizeof(fixture)));
    assert(!kui_settings_encode(NULL,&defaults,1));assert(!kui_settings_encode(record,NULL,1));
    puts("PASS settings: defaults, all flags, fixed endian fixture, CRC corruption, bounded decode, versions and 64-bit sequences");
    return 0;
}
