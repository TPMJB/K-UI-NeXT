/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_ram_patches.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct kui_game_ram_patch_seed blank={KUI_GAME_RAM_PATCH_SEED_BLANK_WORDS};
static const struct kui_game_ram_patches choice={{{0x0c010004u,0x12345678u,true},{0xac01003cu,0,true}}};
static void put(uint8_t *p,uint32_t value) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(8*i));}
/* CRC-32/ISO-HDLC, independently expressed as a nibble-table implementation. */
static uint32_t crc32(const uint8_t *bytes,size_t size) {
    static const uint32_t table[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,
        0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,
        0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<size;i++) {
        crc^=bytes[i];crc=(crc>>4)^table[crc&15u];crc=(crc>>4)^table[crc&15u];
    }
    return ~crc;
}
static void reseal(uint8_t *seed) {put(seed+40,0);put(seed+40,crc32(seed,64));}
static void rejects(const void *seed,size_t size,uint32_t exec_bytes) {
    struct kui_game_ram_patches out,before;memset(&out,0xa5,sizeof(out));memcpy(&before,&out,sizeof(out));
    assert(!kui_game_ram_patch_seed_decode(&out,seed,size,exec_bytes));
    assert(!memcmp(&out,&before,sizeof(out)));
}
static void ranges_and_atomicity(void) {
    _Alignas(4) uint8_t buffer[80],before[sizeof(buffer)];
    memset(buffer,0x5a,sizeof(buffer));memcpy(before,buffer,sizeof(buffer));
    struct kui_game_ram_patches off={0};
    off.entries[0].address=0xffffffffu;off.entries[0].value=0xabcdef01u;
    assert(kui_game_ram_patches_valid(&off) && !kui_game_ram_patches_enabled(&off));
    assert(kui_game_ram_patches_apply(NULL,0,&off));
    assert(kui_game_ram_patches_apply(buffer+1,64,&off));assert(!memcmp(buffer,before,sizeof(buffer)));
    const uint32_t aliases[]={0x0c010004u,0x8c010004u,0xac010004u};
    for(unsigned i=0;i<3;i++) {
        struct kui_game_ram_patches p=choice;p.entries[0].address=aliases[i];
        uint32_t offsets[2]={99,98};assert(kui_game_ram_patches_plan(&p,64,offsets));
        assert(offsets[0]==4 && offsets[1]==60);
        memcpy(buffer,before,sizeof(buffer));assert(kui_game_ram_patches_apply(buffer+8,64,&p));
        uint8_t expected[sizeof(buffer)];memcpy(expected,before,sizeof(expected));
        put(expected+12,0x12345678u);put(expected+68,0);
        assert(!memcmp(buffer,expected,sizeof(buffer)));
    }
    const uint32_t bad_addresses[]={0,0x8c000000u,0x8c00fffcu,0x8c010002u,0x8c010040u,
        0x8d000000u,0x0d010000u,0x2c010004u,0x4c010004u,0x6c010004u,0xcc010004u,
        0xec010004u,0xa5000000u,0xff800000u,0xffffffffu};
    for(unsigned i=0;i<sizeof(bad_addresses)/sizeof(bad_addresses[0]);i++) {
        struct kui_game_ram_patches p=choice;p.entries[1].address=bad_addresses[i];
        uint32_t offsets[2]={99,98};memcpy(buffer,before,sizeof(buffer));
        assert(!kui_game_ram_patches_plan(&p,64,offsets));assert(offsets[0]==99 && offsets[1]==98);
        assert(!kui_game_ram_patches_apply(buffer+8,64,&p));assert(!memcmp(buffer,before,sizeof(buffer)));
    }
    for(unsigned i=0;i<3;i++) {
        struct kui_game_ram_patches p=choice;p.entries[1].address=aliases[i];
        assert(!kui_game_ram_patches_valid(&p));
        memcpy(buffer,before,sizeof(buffer));assert(!kui_game_ram_patches_apply(buffer+8,64,&p));
        assert(!memcmp(buffer,before,sizeof(buffer)));
    }
    struct kui_game_ram_patches p={{{0x8c010000u,0xffffffffu,true},{0,0,false}}};
    uint32_t offsets[2];assert(kui_game_ram_patches_plan(&p,4,offsets));
    assert(offsets[0]==0 && offsets[1]==UINT32_MAX);
    assert(!kui_game_ram_patches_plan(&p,3,offsets));
    p.entries[0].address+=4;assert(!kui_game_ram_patches_plan(&p,7,offsets));
    assert(kui_game_ram_patches_plan(&p,8,offsets));
    p.entries[0].address=KUI_GAME_RAM_PATCH_EXEC_ADDRESS+KUI_GAME_RAM_PATCH_EXEC_MAX_BYTES-4;
    assert(kui_game_ram_patches_valid(&p));
    p.entries[0].address+=4;assert(!kui_game_ram_patches_valid(&p));
    assert(!kui_game_ram_patches_plan(&choice,KUI_GAME_RAM_PATCH_EXEC_MAX_BYTES+1,offsets));
    memcpy(buffer,before,sizeof(buffer));assert(!kui_game_ram_patches_apply(buffer+1,64,&choice));
    assert(!memcmp(buffer,before,sizeof(buffer)));
    assert(!kui_game_ram_patches_apply(NULL,64,&choice));
    assert(!kui_game_ram_patches_valid(NULL));
    /* The first store may overlap the caller's second value. Both values
     * must mean what was requested before any mutation occurred. */
    struct kui_game_ram_patches *aliased=(void *)(buffer+16);
    *aliased=(struct kui_game_ram_patches){{{0x8c010020u,0x12345678u,true},{0x8c01003cu,0xabcdef01u,true}}};
    assert((uint8_t *)&aliased->entries[1].value==buffer+32);
    assert(kui_game_ram_patches_apply(buffer,64,aliased));
    uint32_t last;memcpy(&last,buffer+60,sizeof(last));assert(last==0xabcdef01u);
    puts("PASS RAM patches: alias normalization, loaded-byte bounds, zero/all-ones values, duplicate refusal and all-or-nothing preflight");
}
static void seed_contract(void) {
    /* Independent Python struct.pack('<16I', ...) and zlib.crc32 fixture. */
    static const uint8_t fixture[64]={
        0x4b,0x55,0x49,0x52,0x41,0x4d,0x50,0x31,1,0,0,0,0x40,0,0,0,
        3,0,0,0,0x40,0,0,0,4,0,1,0x8c,0x78,0x56,0x34,0x12,
        0x3c,0,1,0x8c,0,0,0,0,0x5d,0xad,0x0a,0x61};
    uint8_t seed[65],before[sizeof(seed)];memcpy(seed,&blank,64);seed[64]=0xee;
    assert(crc32((const uint8_t *)"123456789",9)==0xcbf43926u);
    assert(kui_game_ram_patch_seed_blank(seed,64));
    struct kui_game_ram_patches off={0},out;
    off.entries[0].address=0xffffffffu;off.entries[1].value=0x12345678u;
    memcpy(before,seed,sizeof(seed));assert(kui_game_ram_patch_seed_encode(seed,64,&off,0));
    assert(!memcmp(seed,before,sizeof(seed)));
    assert(kui_game_ram_patch_seed_decode(&out,seed,64,0));assert(!kui_game_ram_patches_enabled(&out));
    assert(kui_game_ram_patch_seed_encode(seed,64,&choice,64));
    assert(!memcmp(seed,fixture,64) && seed[64]==0xee);
    assert(kui_game_ram_patch_seed_decode(&out,fixture,64,64));
    assert(out.entries[0].address==0x8c010004u && out.entries[0].value==0x12345678u && out.entries[0].enabled);
    assert(out.entries[1].address==0x8c01003cu && out.entries[1].value==0 && out.entries[1].enabled);
    memcpy(before,seed,sizeof(seed));assert(!kui_game_ram_patch_seed_encode(seed,64,&off,64));
    assert(!memcmp(seed,before,sizeof(seed)));
    for(unsigned bit=0;bit<512;bit++) {
        memcpy(seed,fixture,64);seed[bit/8]^=(uint8_t)(1u<<(bit%8));rejects(seed,64,64);
    }
    const unsigned at[]={0,8,12,16,16,20,24,24,24,32,44};
    const uint32_t values[]={0,2,60,0,4,60,0x0c010004u,0x8c010002u,0x8c010040u,0x8c010004u,1};
    for(unsigned i=0;i<sizeof(at)/sizeof(at[0]);i++) {
        memcpy(seed,fixture,64);put(seed+at[i],values[i]);reseal(seed);rejects(seed,64,64);
    }
    memcpy(seed,fixture,64);put(seed+16,1);reseal(seed);rejects(seed,64,64); /* disabled pair cannot smuggle bytes */
    for(size_t size=0;size<64;size++) rejects(fixture,size,64);
    memcpy(seed,fixture,64);rejects(seed,65,64);rejects(NULL,64,64);rejects(fixture,64,60);
    memcpy(seed,&blank,64);memcpy(before,seed,sizeof(seed));
    assert(!kui_game_ram_patch_seed_encode(seed,64,&choice,60));
    assert(!memcmp(seed,before,sizeof(seed)));
    assert(!kui_game_ram_patch_seed_blank(seed,63));assert(!kui_game_ram_patch_seed_blank(NULL,64));
    puts("PASS RAM patch seed: independent wire fixture, blank/off identity, exact executable binding, corruption and CRC-valid hostile record refusal");
}
int main(void) {ranges_and_atomicity();seed_contract();return 0;}
