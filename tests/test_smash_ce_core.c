/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_child.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_smash_ce_child_seed blank_ce(unsigned profile) {
    return (struct kui_smash_ce_child_seed){.magic0=KUI_SMASH_CE_CHILD_MAGIC0,
        .magic1=KUI_SMASH_CE_CHILD_MAGIC1,.version=1u,.bytes=64u,.profile=profile};
}
static void parser_input(unsigned profile,uint8_t argv[256],uint8_t ip[16]) {
    const char *folder=kui_smash_ce_child_folder(profile);assert(folder);
    memset(argv,0,256u);snprintf((char *)argv,256u,"\\%s\\SAMPLE.BIN",folder);
    memset(ip,' ',12u);memcpy(ip,folder,strlen(folder));ip[12]=1u;ip[13]=4u;
    ip[14]=0x53u;ip[15]=0xa7u; /* Original parser's opaque trailing output. */
}
static void ce_cases(void) {
    static const uint8_t sha[32]={0x82,0xbb,0x11,0x60,0x81,0x45,0x48,0xf4,
        0x65,0x6b,0x27,0x33,0x53,0xfd,0x61,0x68,0xa6,0x91,0xfb,0x5f,0x21,0x5b,0x77,0x37,
        0x78,0x60,0xff,0x9f,0xd2,0x15,0x6c,0x8a};
    assert(kui_smash_ce_child_owner(204576u,sha));
    assert(!kui_smash_ce_child_owner(204575u,sha) && !kui_smash_ce_child_owner(204577u,sha));
    assert(!kui_smash_ce_child_owner(204576u,NULL));
    for(unsigned i=0u;i<32u;++i) {
        uint8_t changed[32];memcpy(changed,sha,sizeof(changed));changed[i]^=1u;
        assert(!kui_smash_ce_child_owner(204576u,changed));
    }
    for(unsigned profile=1u;profile<=2u;++profile) {
        uint8_t argv[256],ip[16];parser_input(profile,argv,ip);
        assert(kui_smash_ce_child_identify(argv,ip)==profile);
        assert(!strcmp(kui_smash_ce_child_bootfile(profile),profile==1u?"VIRTCOP2/SAMPLE.BIN":"SWIRL/SAMPLE.BIN"));
        struct kui_smash_ce_child_seed seed=blank_ce(profile),before=seed;
        assert(kui_smash_ce_child_blank(&seed) && !kui_smash_ce_child_valid(&seed));
        assert(kui_smash_ce_child_capture(&seed,argv,ip) && kui_smash_ce_child_valid(&seed));
        assert(!kui_smash_ce_child_blank(&seed) && !memcmp(seed.ip_tail,ip,16u));
        struct kui_smash_ce_child_seed captured=seed;
        assert(!kui_smash_ce_child_capture(&seed,argv,ip) && !memcmp(&seed,&captured,sizeof(seed)));
        for(unsigned i=0u;i<sizeof(seed);++i) {
            struct kui_smash_ce_child_seed bad=captured;((uint8_t *)&bad)[i]^=1u;
            assert(!kui_smash_ce_child_valid(&bad));
        }
        for(unsigned i=0u;i<256u;++i) {
            argv[i]^=1u;seed=before;
            assert(kui_smash_ce_child_identify(argv,ip)==KUI_SMASH_MENU_CHILD_COUNT);
            assert(!kui_smash_ce_child_capture(&seed,argv,ip) && !memcmp(&seed,&before,sizeof(seed)));
            argv[i]^=1u;
        }
        for(unsigned i=0u;i<14u;++i) {
            ip[i]^=1u;seed=before;
            assert(kui_smash_ce_child_identify(argv,ip)==KUI_SMASH_MENU_CHILD_COUNT);
            assert(!kui_smash_ce_child_capture(&seed,argv,ip) && !memcmp(&seed,&before,sizeof(seed)));
            ip[i]^=1u;
        }
        for(unsigned i=14u;i<16u;++i) {
            ip[i]^=1u;seed=before;
            assert(kui_smash_ce_child_capture(&seed,argv,ip) && kui_smash_ce_child_valid(&seed));
            assert(!memcmp(seed.ip_tail,ip,16u));ip[i]^=1u;
        }
        seed=blank_ce(3u-profile);before=seed;
        assert(!kui_smash_ce_child_capture(&seed,argv,ip) && !memcmp(&seed,&before,sizeof(seed)));
        uint8_t payload[256]={0};seed=blank_ce(profile);memcpy(payload+16u,&seed,sizeof(seed));
        assert(kui_smash_ce_child_find(payload,sizeof(payload),profile)==16u);
        /* Real linked validators contain these two comparison literals
         * before and after the actual typed seed. They are not records. */
        const uint32_t literals[4]={KUI_SMASH_CE_CHILD_MAGIC0,KUI_SMASH_CE_CHILD_MAGIC1,
            UINT32_C(0x8ce0cd50),UINT32_C(0x2f964786)};
        memcpy(payload,literals,sizeof(literals));memcpy(payload+224u,literals,sizeof(literals));
        assert(kui_smash_ce_child_find(payload,sizeof(payload),profile)==16u);
        for(unsigned word=2u;word<4u;word++) {
            payload[16u+word*4u]^=1u;
            assert(kui_smash_ce_child_find(payload,sizeof(payload),profile)==SIZE_MAX);
            payload[16u+word*4u]^=1u;
        }
        assert(kui_smash_ce_child_find(payload,sizeof(payload),3u-profile)==SIZE_MAX);
        memcpy(payload+128u,&seed,sizeof(seed));
        assert(kui_smash_ce_child_find(payload,sizeof(payload),profile)==SIZE_MAX);
        memset(payload,0,sizeof(payload));memcpy(payload+17u,&seed,sizeof(seed));
        assert(kui_smash_ce_child_find(payload,sizeof(payload),profile)==SIZE_MAX);
        memcpy(payload,&seed,sizeof(seed));
        for(size_t bytes=8u;bytes<sizeof(seed);++bytes)
            assert(kui_smash_ce_child_find(payload,bytes,profile)==SIZE_MAX);
        memset(payload,0,sizeof(payload));memcpy(payload+16u,&captured,sizeof(captured));
        assert(kui_smash_ce_child_find(payload,sizeof(payload),profile)==SIZE_MAX);
    }
    assert(!kui_smash_ce_child_folder(0u) && !kui_smash_ce_child_bootfile(3u));
    puts("PASS CE core: exact original argv/IP, opaque tail, capture and immutable refusals, unique blank seeds, full SAMPLE SHA owner");
}
static struct kui_smash_menu_collection blank_collection(void) {
    return (struct kui_smash_menu_collection){.magic0=KUI_SMASH_MENU_COLLECTION_MAGIC0,
        .magic1=KUI_SMASH_MENU_COLLECTION_MAGIC1,.version=1u,.bytes=224u,.count=3u,.flags=7u};
}
static struct kui_smash_menu_collection collection(void) {
    struct kui_smash_menu_collection s=blank_collection();
    for(unsigned i=0u;i<3u;++i) {
        s.plans[i]=(struct kui_smash_menu_loader_seed){.payload_bytes=204576u,.padded_bytes=204800u,
            .payload_crc32=0x12340000u+i,.menu_seed_offset=512u,.first_lba=10000u+i*1000u,
            .first_blocks=200u,.second_lba=30000u+i*1000u,.block_count=400u};
        assert(kui_smash_menu_loader_seed_seal(&s.plans[i]));
    }
    assert(kui_smash_menu_collection_seal(&s) && kui_smash_menu_collection_valid(&s));return s;
}
static void refused_collection(struct kui_smash_menu_collection *bad) {
    struct kui_smash_menu_collection before=*bad;
    assert(!kui_smash_menu_collection_seal(bad) && !memcmp(bad,&before,sizeof(before)));
    assert(!kui_smash_menu_collection_valid(bad));
}
static void collection_cases(void) {
    struct kui_smash_menu_collection blank=blank_collection(),s=collection();
    assert(kui_smash_menu_collection_blank(&blank) && !kui_smash_menu_collection_valid(&blank));
    uint8_t payload[640]={0};memcpy(payload+16u,&blank,sizeof(blank));
    assert(kui_smash_menu_collection_find(payload,sizeof(payload))==16u);
    const uint32_t literals[4]={KUI_SMASH_MENU_COLLECTION_MAGIC0,KUI_SMASH_MENU_COLLECTION_MAGIC1,
        UINT32_C(0x2f964786),UINT32_C(0x2fa68343)};
    memcpy(payload,literals,sizeof(literals));memcpy(payload+280u,literals,sizeof(literals));
    assert(kui_smash_menu_collection_find(payload,sizeof(payload))==16u);
    for(unsigned word=2u;word<4u;word++) {
        payload[16u+word*4u]^=1u;
        assert(kui_smash_menu_collection_find(payload,sizeof(payload))==SIZE_MAX);
        payload[16u+word*4u]^=1u;
    }
    memcpy(payload+320u,&blank,sizeof(blank));
    assert(kui_smash_menu_collection_find(payload,sizeof(payload))==SIZE_MAX);
    memset(payload,0,sizeof(payload));memcpy(payload+17u,&blank,sizeof(blank));
    assert(kui_smash_menu_collection_find(payload,sizeof(payload))==SIZE_MAX);
    memcpy(payload,&blank,sizeof(blank));
    for(size_t bytes=8u;bytes<sizeof(blank);++bytes)
        assert(kui_smash_menu_collection_find(payload,bytes)==SIZE_MAX);
    memset(payload,0,sizeof(payload));memcpy(payload+16u,&s,sizeof(s));
    assert(kui_smash_menu_collection_find(payload,sizeof(payload))==SIZE_MAX);
    for(unsigned i=0u;i<sizeof(s);++i) {
        struct kui_smash_menu_collection bad=s;((uint8_t *)&bad)[i]^=1u;
        assert(!kui_smash_menu_collection_valid(&bad));
    }
    for(unsigned a=0u;a<3u;++a)for(unsigned b=a+1u;b<3u;++b)
        for(unsigned arun=0u;arun<2u;++arun)for(unsigned brun=0u;brun<2u;++brun) {
            struct kui_smash_menu_collection bad=s;
            uint32_t lba=arun?bad.plans[a].second_lba:bad.plans[a].first_lba;
            if(brun)bad.plans[b].second_lba=lba;else bad.plans[b].first_lba=lba;
            assert(kui_smash_menu_loader_seed_seal(&bad.plans[b]));refused_collection(&bad);
        }
    for(unsigned i=0u;i<3u;++i) {
        struct kui_smash_menu_collection bad=s;
        bad.plans[i].handoff_crc32=0x12345678u;
        assert(kui_smash_menu_loader_seed_seal(&bad.plans[i]));refused_collection(&bad);
        bad=s;bad.plans[i].raw_crc32=0xabcdef01u;
        assert(kui_smash_menu_loader_seed_seal(&bad.plans[i]));refused_collection(&bad);
        bad=s;bad.plans[i].first_lba=UINT32_MAX-100u;refused_collection(&bad);
        bad=s;bad.plans[i].second_lba=UINT32_MAX-100u;refused_collection(&bad);
        bad=s;bad.plans[i].flags=1u;refused_collection(&bad);
    }
    /* Last sector UINT32_MAX is valid; crossing beyond it refuses. */
    struct kui_smash_menu_collection edge=s;
    edge.plans[2].first_lba=UINT32_MAX-399u;edge.plans[2].first_blocks=400u;edge.plans[2].second_lba=0u;
    assert(kui_smash_menu_loader_seed_seal(&edge.plans[2]));
    assert(kui_smash_menu_collection_seal(&edge) && kui_smash_menu_collection_valid(&edge));
    ++edge.plans[2].first_lba;refused_collection(&edge);
    /* Code ownership is launch-normalized across all three backends. */
    for(unsigned i=0u;i<3u;++i) {
        s.plans[i].handoff_crc32=0x13579bdfu;s.plans[i].raw_crc32=0x2468ace0u;
        assert(kui_smash_menu_loader_seed_seal(&s.plans[i]));
    }
    assert(kui_smash_menu_collection_seal(&s) && kui_smash_menu_collection_valid(&s));
    const unsigned header_words[]={0u,1u,2u,3u,4u,5u,54u};
    for(unsigned i=0u;i<sizeof(header_words)/sizeof(header_words[0]);++i) {
        struct kui_smash_menu_collection bad=s;((uint32_t *)&bad)[header_words[i]]^=1u;refused_collection(&bad);
    }
    puts("PASS collection core: three normalized plans, all12 cross-run overlaps,32-bit boundaries, code CRC coherence, seals and blank-only discovery");
}
int main(void) {ce_cases();collection_cases();return 0;}
