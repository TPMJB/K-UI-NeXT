/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_loader.h"
#include "kui/smash_menu_loader_capability.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_smash_menu_loader_seed prepared(void) {
    return (struct kui_smash_menu_loader_seed){.payload_bytes=233120u,
        .padded_bytes=233472u,.payload_crc32=0x12345678u,.menu_seed_offset=8192u,
        .first_lba=10000u,.first_blocks=300u,.second_lba=20000u,.block_count=456u};
}
int main(void) {
    const struct kui_smash_menu_loader_seed blank={KUI_SMASH_MENU_LOADER_SEED_BLANK_WORDS};
    assert(kui_smash_menu_loader_seed_blank(&blank));
    assert(!kui_smash_menu_loader_seed_valid(&blank));
    unsigned char data[192]={0};
    memcpy(data+4,&blank,sizeof(blank));
    assert(kui_smash_menu_loader_seed_find(data,sizeof(data))==4u);
    memcpy(data+100,&blank,sizeof(blank));
    assert(kui_smash_menu_loader_seed_find(data,sizeof(data))==SIZE_MAX);
    memset(data,0,sizeof(data));memcpy(data+3,&blank,sizeof(blank));
    assert(kui_smash_menu_loader_seed_find(data,sizeof(data))==SIZE_MAX);
    assert(kui_smash_menu_loader_seed_find(NULL,sizeof(data))==SIZE_MAX);
    assert(kui_smash_menu_loader_seed_find(data,63u)==SIZE_MAX);
    struct kui_smash_menu_loader_seed seed=prepared();
    assert(kui_smash_menu_loader_seed_seal(&seed));
    assert(kui_smash_menu_loader_seed_valid(&seed));
    assert(!kui_smash_menu_loader_seed_blank(&seed));
    for(size_t bit=0u;bit<sizeof(seed)*8u;bit++) {
        struct kui_smash_menu_loader_seed changed=seed;
        ((unsigned char *)&changed)[bit/8u]^=(unsigned char)(1u<<(bit%8u));
        assert(!kui_smash_menu_loader_seed_valid(&changed));
    }
    seed.handoff_crc32=0x13579bdfu;seed.raw_crc32=0x2468ace0u;
    assert(!kui_smash_menu_loader_seed_valid(&seed));
    assert(kui_smash_menu_loader_seed_seal(&seed));
    assert(kui_smash_menu_loader_seed_valid(&seed));
    const struct kui_smash_menu_loader_seed malformed[]={
        {.payload_bytes=303u,.padded_bytes=512u,.first_lba=1u,.first_blocks=1u,.block_count=1u},
        {.payload_bytes=0x200000u,.padded_bytes=0x200000u,.first_lba=1u,.first_blocks=4096u,.block_count=4096u},
        {.payload_bytes=1024u,.padded_bytes=1536u,.first_lba=1u,.first_blocks=3u,.block_count=3u},
        {.payload_bytes=1024u,.padded_bytes=1024u,.first_lba=1u,.first_blocks=2u,.block_count=1u},
        {.payload_bytes=1024u,.padded_bytes=1024u,.first_lba=1u,.first_blocks=0u,.block_count=2u},
        {.payload_bytes=1024u,.padded_bytes=1024u,.first_lba=0u,.first_blocks=2u,.block_count=2u},
        {.payload_bytes=1024u,.padded_bytes=1024u,.first_lba=UINT32_MAX,.first_blocks=2u,.block_count=2u},
        {.payload_bytes=1024u,.padded_bytes=1024u,.first_lba=4u,.first_blocks=1u,.block_count=2u},
        {.payload_bytes=1024u,.padded_bytes=1024u,.first_lba=4u,.first_blocks=2u,.second_lba=9u,.block_count=2u},
        {.payload_bytes=1536u,.padded_bytes=1536u,.first_lba=4u,.first_blocks=2u,.second_lba=5u,.block_count=3u},
        {.payload_bytes=1536u,.padded_bytes=1536u,.first_lba=5u,.first_blocks=1u,.second_lba=4u,.block_count=3u},
        {.payload_bytes=1024u,.padded_bytes=1024u,.menu_seed_offset=721u,.first_lba=1u,.first_blocks=2u,.block_count=2u},
        {.payload_bytes=1024u,.padded_bytes=1024u,.menu_seed_offset=724u,.first_lba=1u,.first_blocks=2u,.block_count=2u},
        {.payload_bytes=1024u,.padded_bytes=1024u,.first_lba=1u,.first_blocks=2u,.block_count=2u,.flags=1u},
    };
    for(size_t i=0;i<sizeof(malformed)/sizeof(*malformed);i++) {
        struct kui_smash_menu_loader_seed bad=malformed[i];
        assert(!kui_smash_menu_loader_seed_seal(&bad));
    }
    seed=(struct kui_smash_menu_loader_seed){.payload_bytes=1024u,.padded_bytes=1024u,
        .menu_seed_offset=720u,.first_lba=UINT32_MAX-1u,.first_blocks=2u,.block_count=2u};
    assert(kui_smash_menu_loader_seed_seal(&seed));
    seed=(struct kui_smash_menu_loader_seed){.payload_bytes=1536u,.padded_bytes=1536u,
        .first_lba=4u,.first_blocks=2u,.second_lba=6u,.block_count=3u};
    assert(kui_smash_menu_loader_seed_seal(&seed));
    seed.second_lba=3u;assert(kui_smash_menu_loader_seed_seal(&seed));
    const uint32_t capability[]={KUI_SMASH_MENU_LOADER_CAP_WORDS};
    assert(kui_smash_menu_loader_capability_valid(capability,sizeof(capability)));
    for(size_t bit=0u;bit<sizeof(capability)*8u;bit++) {
        uint32_t changed[8];memcpy(changed,capability,sizeof(changed));
        ((unsigned char *)changed)[bit/8u]^=(unsigned char)(1u<<(bit%8u));
        assert(!kui_smash_menu_loader_capability_valid(changed,sizeof(changed)));
    }
    memset(data,0,sizeof(data));memcpy(data+4,capability,sizeof(capability));
    size_t offset=SIZE_MAX;
    assert(kui_smash_menu_loader_capability_find(data,sizeof(data),&offset)==1 && offset==4u);
    memcpy(data+100,capability,sizeof(capability));
    assert(kui_smash_menu_loader_capability_find(data,sizeof(data),NULL)==-1);
    memset(data,0,sizeof(data));memcpy(data+3,capability,sizeof(capability));
    assert(kui_smash_menu_loader_capability_find(data,sizeof(data),NULL)==-1);
    puts("Smash menu loader seed/capability: OK (512 seed-bit and 256 capability-bit refusals)");
}
