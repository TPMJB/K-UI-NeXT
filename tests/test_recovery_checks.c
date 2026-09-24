/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/recovery_checks.h"
#include "kui/core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "recovery-vectors.inc"

/* Direct byte-by-byte IEEE CRC oracle. Unlike production replacement, this
 * computes the entire changed message and uses no matrix or shift shortcut. */
static uint32_t reference_crc(const uint8_t *data,size_t size) {
    uint32_t value=UINT32_MAX;
    for(size_t i=0;i<size;++i) {
        value^=data[i];
        for(unsigned bit=0;bit<8;++bit)
            value=(value>>1)^((value&1u)?UINT32_C(0xedb88320):0u);
    }
    return ~value;
}
static uint32_t random_state=UINT32_C(0xa4c83d21);
static uint32_t random_word(void) {
    random_state^=random_state<<13;random_state^=random_state>>17;
    random_state^=random_state<<5;return random_state;
}
static uint32_t patch(uint8_t *data,size_t size,size_t at,size_t bytes,uint32_t whole) {
    assert(at<=size && bytes<=size-at);
    uint32_t old_crc=reference_crc(data+at,bytes);
    for(size_t i=0;i<bytes;++i) data[at+i]=(uint8_t)random_word();
    uint32_t new_crc=reference_crc(data+at,bytes);
    uint32_t got=kui_recovery_crc_replace(whole,old_crc,new_crc,size-at-bytes);
    assert(got==reference_crc(data,size));
    assert(got==kui_crc32(0,data,size));
    return got;
}
static void test_replacement(void) {
    uint8_t data[5u*KUI_RECOVERY_RAW_BYTES+7u],original[sizeof(data)];
    assert(reference_crc((const uint8_t *)"123456789",9)==UINT32_C(0xcbf43926));
    assert(reference_crc(NULL,0)==0);
    for(size_t i=0;i<sizeof(data);++i) data[i]=(uint8_t)random_word();
    memcpy(original,data,sizeof(data));
    uint32_t original_crc=reference_crc(data,sizeof(data));
    assert(kui_crc32(0,data,sizeof(data))==original_crc);
    const size_t offsets[]={0,2u*KUI_RECOVERY_RAW_BYTES,sizeof(data)-KUI_RECOVERY_RAW_BYTES};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        memcpy(data,original,sizeof(data));
        (void)patch(data,sizeof(data),offsets[i],KUI_RECOVERY_RAW_BYTES,original_crc);
    }
    memcpy(data,original,sizeof(data));uint32_t whole=original_crc;
    /* Multiple and overlapping replacements, including a previously repaired
     * sector and a torn partial patch, all match an independent whole-file CRC. */
    whole=patch(data,sizeof(data),0,KUI_RECOVERY_RAW_BYTES,whole);
    whole=patch(data,sizeof(data),2u*KUI_RECOVERY_RAW_BYTES,KUI_RECOVERY_RAW_BYTES,whole);
    whole=patch(data,sizeof(data),2u*KUI_RECOVERY_RAW_BYTES+73u,137u,whole);
    whole=patch(data,sizeof(data),2u*KUI_RECOVERY_RAW_BYTES,KUI_RECOVERY_RAW_BYTES,whole);
    whole=patch(data,sizeof(data),sizeof(data)-1u,1u,whole);
    whole=patch(data,sizeof(data),sizeof(data),0,whole);
    for(unsigned i=0;i<64;++i) {
        size_t at=random_word()%sizeof(data);
        size_t count=random_word()%(sizeof(data)-at+1u);
        whole=patch(data,sizeof(data),at,count,whole);
    }
    whole=patch(data,sizeof(data),0,sizeof(data),whole);
    assert(kui_recovery_crc_replace(whole,whole,whole,0)==whole);
    assert(kui_recovery_crc_replace(whole,0,0,UINT64_MAX)==whole);
    /* The generator uses normal-polynomial arithmetic and Python zlib;
     * distances above 4 GiB and bit 63 detect narrowing/overflow errors. */
    for(unsigned i=0;i<sizeof(crc_vectors)/sizeof(crc_vectors[0]);++i) {
        assert(kui_recovery_crc_replace(UINT32_C(0x12345678),
            UINT32_C(0xcbf43926),UINT32_C(0xe3069283),crc_vectors[i].suffix)==crc_vectors[i].expected);
    }
}
static void make_sector(uint8_t out[KUI_RECOVERY_RAW_BYTES],unsigned index) {
    memset(out,0,KUI_RECOVERY_RAW_BYTES);memset(out+1,255,10);
    memcpy(out+12,sector_vectors[index].header,4);
    for(unsigned i=0;i<2048;++i) out[16+i]=(uint8_t)(i*37u+sector_vectors[index].seed);
    memcpy(out+2064,sector_vectors[index].trailer,288);
    assert(kui_crc32(0,out,KUI_RECOVERY_RAW_BYTES)==sector_vectors[index].crc);
}
static void test_sector(void) {
    uint8_t sector[KUI_RECOVERY_RAW_BYTES],saved[sizeof(sector)];
    for(unsigned i=0;i<sizeof(sector_vectors)/sizeof(sector_vectors[0]);++i) {
        make_sector(sector,i);memcpy(saved,sector,sizeof(saved));
        uint32_t fad=sector_vectors[i].fad;
        assert(kui_recovery_sector_check(sector,sizeof(sector),fad)==sector_vectors[i].expected);
        uint32_t wrong=fad==KUI_RECOVERY_FAD_MAX?fad-1u:fad+1u;
        assert(kui_recovery_sector_check(sector,sizeof(sector),wrong)==
               (sector_vectors[i].expected|KUI_RECOVERY_SECTOR_ADDRESS));
        assert(!memcmp(sector,saved,sizeof(sector)));
    }
    /* Reserved-field fixture has recomputed valid P/Q and unchanged valid
     * EDC: only the explicit reserved-field check should reject it. */
    assert(sector_vectors[4].expected==KUI_RECOVERY_SECTOR_RESERVED);
    make_sector(sector,4);
    assert(kui_recovery_sector_check(sector,sizeof(sector),sector_vectors[4].fad)==KUI_RECOVERY_SECTOR_RESERVED);
    /* GD high-density addresses legitimately exceed 99 minutes. */
    assert(sector_vectors[2].header[0]==0xc2);
    assert(sector_vectors[3].header[0]==0xf9);
    make_sector(sector,1);
    const uint32_t fad=sector_vectors[1].fad;
    const size_t bad_sizes[]={0,1,2064,2351,2353,SIZE_MAX};
    for(unsigned i=0;i<sizeof(bad_sizes)/sizeof(bad_sizes[0]);++i)
        assert(kui_recovery_sector_check(sector,bad_sizes[i],fad)==KUI_RECOVERY_SECTOR_INPUT);
    assert(kui_recovery_sector_check(NULL,sizeof(sector),fad)==KUI_RECOVERY_SECTOR_INPUT);
    const uint32_t bad_fads[]={0,149,KUI_RECOVERY_FAD_MAX+1u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad_fads)/sizeof(bad_fads[0]);++i)
        assert(kui_recovery_sector_check(sector,sizeof(sector),bad_fads[i])==KUI_RECOVERY_SECTOR_INPUT);
    for(unsigned i=0;i<12;++i) {
        make_sector(sector,1);sector[i]^=1;
        assert(kui_recovery_sector_check(sector,sizeof(sector),fad)==KUI_RECOVERY_SECTOR_SYNC);
    }
    const unsigned modes[]={0,2,3,255};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i) {
        make_sector(sector,1);sector[15]=(uint8_t)modes[i];
        assert(kui_recovery_sector_check(sector,sizeof(sector),fad)==KUI_RECOVERY_SECTOR_UNSUPPORTED);
    }
    const unsigned data_offsets[]={16,17,255,1024,2063,2064,2067};
    for(unsigned i=0;i<sizeof(data_offsets)/sizeof(data_offsets[0]);++i) {
        make_sector(sector,1);sector[data_offsets[i]]^=1;
        unsigned flags=kui_recovery_sector_check(sector,sizeof(sector),fad);
        assert((flags&(KUI_RECOVERY_SECTOR_EDC|KUI_RECOVERY_SECTOR_ECC))==
                      (KUI_RECOVERY_SECTOR_EDC|KUI_RECOVERY_SECTOR_ECC));
    }
    /* The entire P/Q region is outside EDC coverage. Checking only EDC must
     * not accidentally stand in for Advanced CRC's additional parity checks. */
    for(unsigned offset=2068;offset<KUI_RECOVERY_RAW_BYTES;++offset) {
        make_sector(sector,1);sector[offset]^=1;
        unsigned want=KUI_RECOVERY_SECTOR_ECC;
        if(offset<2076) want|=KUI_RECOVERY_SECTOR_RESERVED;
        assert(kui_recovery_sector_check(sector,sizeof(sector),fad)==want);
    }
    make_sector(sector,1);sector[12]^=1;
    unsigned flags=kui_recovery_sector_check(sector,sizeof(sector),fad);
    assert((flags&(KUI_RECOVERY_SECTOR_ADDRESS|KUI_RECOVERY_SECTOR_EDC|KUI_RECOVERY_SECTOR_ECC))==
                  (KUI_RECOVERY_SECTOR_ADDRESS|KUI_RECOVERY_SECTOR_EDC|KUI_RECOVERY_SECTOR_ECC));
}
int main(void) {
    test_replacement();test_sector();
    puts("PASS recovery helpers: full-data CRC replacements, 64-bit suffix oracles, independent Mode 1 vectors and corruption checks");
    return 0;
}
