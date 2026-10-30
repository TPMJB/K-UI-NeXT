/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_vmu_standard.h"
#include "kui/vmu_game_card.h"
#include "kui/hash.h"
#include <string.h>

#define STANDARD_PAYLOAD_BYTES 213288u
#define STANDARD_PROFILE_OFFSET 97600u
/* Authored from the separately audited d6d517fa133a and1734830d08df modules.
 * The complete entry, stage, every resident and worker remain hashed. Only
 * the installer's canonical profile slot and exact twelve-hex labels vary. */
static const size_t build_offsets[6]={148930u,159190u,176082u,188306u,200530u,212754u};
static const uint8_t payload_hashes[2][32]={
    {0xda,0x55,0x96,0x8c,0x2f,0x7f,0x8e,0x46,0x2e,0x18,0xb3,0x2a,0xb4,0x46,0x63,0x63,
     0x1e,0x36,0x22,0xc9,0x30,0x7e,0x0a,0x5c,0x86,0x62,0x84,0xa0,0x1f,0x35,0xc1,0x40},
    {0x41,0x3b,0x50,0x76,0x11,0x5f,0x33,0x4a,0x7f,0x94,0xfb,0xeb,0x81,0x61,0x74,0xe8,
     0x94,0x6a,0x3f,0x7b,0x5a,0x72,0xd2,0x7f,0xef,0x6f,0x29,0x03,0x83,0xc7,0x46,0x4d}
};
bool kui_native_vmu_standard_recognize(const struct kui_runtime_image *package,size_t *profile_slot) {
    if(!package || !package->data || package->info.payload_bytes!=STANDARD_PAYLOAD_BYTES ||
       package->info.memory_bytes!=STANDARD_PAYLOAD_BYTES || package->info.build[12]) return false;
    for(unsigned i=0;i<12u;i++) if(!((package->info.build[i]>='0' && package->info.build[i]<='9') ||
        (package->info.build[i]>='a' && package->info.build[i]<='f'))) return false;
    const uint8_t *data=package->data;
    if(kui_vmu_game_card_profile_slot(data,STANDARD_PAYLOAD_BYTES)!=STANDARD_PROFILE_OFFSET) return false;
    for(unsigned i=0;i<6u;i++) if(memcmp(data+build_offsets[i]-6u,"BUILD ",6u) ||
        memcmp(data+build_offsets[i],package->info.build,12u) || data[build_offsets[i]+12u]) return false;
    unsigned labels=0;
    for(size_t i=0;i<=STANDARD_PAYLOAD_BYTES-12u;i++) {
        if(i<STANDARD_PROFILE_OFFSET+4096u && i+12u>STANDARD_PROFILE_OFFSET) continue;
        if(!memcmp(data+i,package->info.build,12u)) ++labels;
    }
    if(labels!=6u) return false;
    struct kui_sha256 hash;uint8_t digest[32];static const uint8_t zero[64]={0};
    static const uint8_t label[12]={'0','0','0','0','0','0','0','0','0','0','0','0'};
    kui_sha256_init(&hash);kui_sha256_update(&hash,data,STANDARD_PROFILE_OFFSET);
    for(unsigned i=0;i<4096u/sizeof(zero);i++) kui_sha256_update(&hash,zero,sizeof(zero));
    size_t cursor=STANDARD_PROFILE_OFFSET+4096u;
    for(unsigned i=0;i<6u;i++) {
        kui_sha256_update(&hash,data+cursor,build_offsets[i]-cursor);
        kui_sha256_update(&hash,label,sizeof(label));cursor=build_offsets[i]+sizeof(label);
    }
    kui_sha256_update(&hash,data+cursor,STANDARD_PAYLOAD_BYTES-cursor);kui_sha256_digest(&hash,digest);
    if(memcmp(digest,payload_hashes[0],32u) && memcmp(digest,payload_hashes[1],32u)) return false;
    if(profile_slot)*profile_slot=STANDARD_PROFILE_OFFSET;
    return true;
}
