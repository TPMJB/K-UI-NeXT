/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_cdda_status.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Public startup fingerprints only; no executable or audio bytes. These
 * exercise evidence selection, not console playback or SHA generation. */
static const uint32_t ip_crc[]={0x684f8040u,0x8fb049c5u,0x86bbb334u,0x38ba2868u};
static const uint8_t boot_sha[4][32]={
    {0xa7,0x62,0x8c,0x78,0x09,0x0f,0xce,0x2c,0x93,0x01,0x2d,0x5e,0x4d,0xb6,0x14,0x20,
     0x96,0xe1,0x38,0xdc,0xeb,0x7e,0xbb,0xf5,0x55,0x14,0x2e,0x90,0x2c,0xa0,0x3c,0x1f},
    {0x2a,0x8d,0x24,0x73,0x8b,0xb5,0xba,0x06,0x87,0xfa,0x56,0xad,0x33,0xe8,0x14,0xfe,
     0x79,0x81,0x58,0xf9,0xcb,0x8c,0xac,0xfd,0x4a,0x8a,0x20,0xde,0xa6,0xbb,0xb3,0x5d},
    {0x39,0xb8,0x0e,0x30,0x59,0x47,0x05,0x65,0x51,0x31,0x8b,0xb6,0x55,0x29,0xcc,0xcc,
     0x88,0x60,0x21,0xbe,0x60,0x40,0x6c,0x06,0xf3,0x11,0x0f,0x7f,0xe0,0x3b,0xf5,0x71},
    {0xee,0x68,0xa3,0x26,0xda,0x96,0x4f,0xec,0x58,0x92,0xb7,0x22,0x63,0x11,0xf2,0x29,
     0x3f,0xd3,0xa0,0xbb,0xf5,0xba,0xc4,0x96,0xec,0x82,0x99,0x19,0x64,0xd4,0x9b,0xbd}};
static const uint32_t armada[]={0u,756u,45000u,149842u,434062u};
static const uint32_t bam4[]={0u,450u,45000u,370673u,371347u,372014u,372915u,373626u,
    379011u,384738u,390481u,395473u,398926u,404448u,425246u,445520u,466032u,
    474231u,485598u,486386u,487098u,487822u,498356u,508297u,527383u,548106u};
static const uint32_t worms[]={0u,450u,45000u,91528u,117095u,232929u,329558u,379575u};
static const uint32_t bam4_ends[]={300u,976u,370523u,371347u,372014u,372915u,373626u,
    379011u,384738u,390481u,395473u,398926u,404448u,425246u,445520u,466032u,
    474231u,485598u,486386u,487098u,487822u,498356u,508297u,527383u,547956u,549150u};
static const uint32_t worms_ends[]={300u,946u,91378u,117095u,232929u,329558u,379425u,549150u};
static const uint32_t toy[]={0u,7257u,45000u,201900u,219712u,234091u,255599u,
    272017u,282799u,303092u,323352u,341314u,356690u,374201u,377422u};
static const uint32_t toy_ends[]={7107u,7753u,201750u,219712u,234091u,255599u,272017u,
    282799u,303092u,323352u,341314u,356690u,374201u,377272u,549150u};
static const uint32_t mdk2[]={0u,14601u,45000u,52290u,74215u,94719u,115029u,
    134578u,154620u,174965u,193871u,213463u,235118u,253694u,278376u,298017u,
    314109u,330973u,349474u,368207u,373047u,377949u,382628u,387412u,396388u,
    400888u,402548u,416575u,420052u,424954u,429498u};
static const uint32_t mdk2_ends[]={14451u,15127u,52140u,74215u,94719u,115029u,
    134578u,154620u,174965u,193871u,213463u,235118u,253694u,278376u,298017u,
    314109u,330973u,349474u,368207u,373047u,377949u,382628u,387412u,396388u,
    400888u,402548u,416575u,420052u,424954u,429348u,549150u};
static struct kui_game_image image;
static struct kui_game_metadata meta;
static unsigned checks;
#define CHECK(expr) do { ++checks; assert(expr); } while(0)

static void fixture(unsigned id) {
    static const uint32_t *const starts[]={armada,bam4,worms,toy,mdk2};
    static const unsigned counts[]={5u,26u,8u,15u,31u};
    static const uint32_t bytes[]={1253376u,1830912u,1110016u,748444u,750804u};
    static const uint32_t boot_lba[]={548388u,548106u,548458u,548634u,548633u};
    static const char *const products[]={"T40301N 00","T-8117N","T40601N","T-40202N","T-12501N"};
    assert(id>=1u && id<=5u);unsigned index=id-1u;
    memset(&image,0,sizeof(image));memset(&meta,0,sizeof(meta));
    image.format=KUI_GAME_IMAGE_GDI;image.count=counts[index];image.data_lba=45000u;
    meta.ip_valid=meta.boot_valid=true;meta.windows_ce=id<=3u;meta.native_gd=id>=4u;
    meta.session_lba=45000u;meta.boot_bytes=bytes[index];meta.boot_lba=boot_lba[index];
    strcpy(meta.product,products[index]);strcpy(meta.bootfile,id<=3u?"0WINCEOS.BIN":"1ST_READ.BIN");
    strcpy(meta.title,"Synthetic startup identity");
    for(unsigned i=0;i<image.count;i++) {
        bool data=id==1u?(i==0u || i==2u || i==4u):
            id==2u?(i==0u || i==2u || i==25u):
            id==3u?(i==0u || i==2u || i==7u):
            id==4u?(i==0u || i==2u || i==14u):(i==0u || i==2u || i==30u);
        struct kui_game_image_track *t=&image.tracks[i];
        t->number=i+1u;t->start_lba=starts[index][i];
        t->end_lba=id==5u?mdk2_ends[i]:id==4u?toy_ends[i]:id==3u?worms_ends[i]:id==2u?bam4_ends[i]:
            i+1u<image.count?starts[index][i+1u]:549150u;
        t->control=data?4u:0u;t->sector_bytes=2352u;
        t->sector_mode=data?1u:0u;t->data_offset=data?16u:0u;
        t->file_bytes=(uint64_t)(t->end_lba-t->start_lba)*2352u;
        snprintf(t->name,sizeof(t->name),"track%02u.bin",i+1u);
    }
}
static enum kui_game_cdda_compatibility status(unsigned id) {
    return kui_games_cdda_test_status(kui_games_cdda_test_candidate(&image,&meta),
        ip_crc[id-1u],boot_sha[id-1u]);
}
static void cooked_fixture(unsigned id) {
    fixture(id);
    for(unsigned i=0;i<image.count;i++) if(image.tracks[i].control==4u) {
        struct kui_game_image_track *t=&image.tracks[i];
        t->sector_bytes=2048u;t->data_offset=0u;
        t->file_bytes=(uint64_t)(t->end_lba-t->start_lba)*2048u;
    }
}
static void known_identities(void) {
    for(unsigned id=1u;id<=4u;id++) {
        fixture(id);
        enum kui_game_cdda_compatibility expected=id==4u?KUI_GAME_CDDA_TESTED_LIMITED:KUI_GAME_CDDA_TESTED;
        CHECK(kui_games_cdda_test_candidate(&image,&meta)==id && status(id)==expected);
        strcpy(meta.title,"Unrelated display title");CHECK(status(id)==expected);
        for(unsigned byte=0;byte<32u;byte++) {
            uint8_t changed[32];memcpy(changed,boot_sha[id-1u],sizeof(changed));changed[byte]^=1u;
            CHECK(kui_games_cdda_test_status(id,ip_crc[id-1u],changed)==KUI_GAME_CDDA_UNTESTED);
        }
        for(unsigned bit=0;bit<32u;bit++)
            CHECK(kui_games_cdda_test_status(id,ip_crc[id-1u]^(UINT32_C(1)<<bit),boot_sha[id-1u])==KUI_GAME_CDDA_UNTESTED);
        for(unsigned other=1u;other<=4u;other++) if(other!=id)
            CHECK(kui_games_cdda_test_status(id,ip_crc[id-1u],boot_sha[other-1u])==KUI_GAME_CDDA_UNTESTED);
        CHECK(kui_games_cdda_test_status(id,ip_crc[id-1u],NULL)==KUI_GAME_CDDA_UNTESTED);
        fixture(id);unsigned tracks=image.count;
        for(unsigned track=0;track<tracks;track++) if(image.tracks[track].control==4u) {
            fixture(id);image.tracks[track].sector_bytes=2048u;image.tracks[track].data_offset=0u;
            CHECK(!kui_games_cdda_test_candidate(&image,&meta) && status(id)==KUI_GAME_CDDA_UNTESTED);
        }
    }
    /* Having an MDK2 adapter is not evidence of a successful console test. */
    fixture(5u);strcpy(meta.title,"MDK2");
    CHECK(!kui_games_cdda_test_candidate(&image,&meta));
    CHECK(kui_games_cdda_test_status(0u,0x34122049u,boot_sha[0])==KUI_GAME_CDDA_UNTESTED);
}
static void cooked_worms_evidence(void) {
    cooked_fixture(3u);
    CHECK(kui_games_cdda_test_candidate(&image,&meta)==3u && status(3u)==KUI_GAME_CDDA_TESTED);
    /* The successful 2048 copy is one additional exact tested storage
     * variant. Other titles and partial conversions receive no new claim. */
    for(unsigned id=1u;id<=5u;id++) if(id!=3u) {
        cooked_fixture(id);CHECK(!kui_games_cdda_test_candidate(&image,&meta));
    }
    for(unsigned byte=0;byte<32u;byte++) {
        cooked_fixture(3u);uint8_t changed[32];memcpy(changed,boot_sha[2],sizeof(changed));changed[byte]^=1u;
        unsigned candidate=kui_games_cdda_test_candidate(&image,&meta);
        CHECK(kui_games_cdda_test_status(candidate,ip_crc[2],changed)==KUI_GAME_CDDA_UNTESTED);
    }
    cooked_fixture(3u);
    CHECK(kui_games_cdda_test_status(kui_games_cdda_test_candidate(&image,&meta),ip_crc[2]^1u,
        boot_sha[2])==KUI_GAME_CDDA_UNTESTED);
    for(unsigned track=0;track<8u;track++) for(unsigned fault=0;fault<6u;fault++) {
        cooked_fixture(3u);struct kui_game_image_track *t=&image.tracks[track];
        if(fault==0u) t->start_lba++;
        if(fault==1u) {t->end_lba--;t->file_bytes-=t->sector_bytes;}
        if(fault==2u) t->file_offset=t->sector_bytes;
        if(fault==3u) t->file_bytes--;
        if(fault==4u) t->data_offset++;
        if(fault==5u) t->sector_mode=t->control?0u:1u;
        CHECK(!kui_games_cdda_test_candidate(&image,&meta) && status(3u)==KUI_GAME_CDDA_UNTESTED);
    }
    for(unsigned track=0;track<8u;track++) {
        cooked_fixture(3u);struct kui_game_image_track *t=&image.tracks[track];
        if(t->control==4u) {
            t->sector_bytes=2352u;t->data_offset=16u; /* A partial conversion. */
        } else t->sector_bytes=2048u; /* Audio payload must remain raw. */
        t->file_bytes=(uint64_t)(t->end_lba-t->start_lba)*t->sector_bytes;
        CHECK(!kui_games_cdda_test_candidate(&image,&meta));
    }
}
static void world_party_fixture(bool cooked) {
    /* Independently recorded startup metadata, not the adapter's table.
     * A valid new runtime family still carries no console-test evidence. */
    static const uint32_t starts[]={0u,300u,45000u,89954u,99685u,118988u,
        141520u,160930u,180346u,199619u,219848u,244069u,263559u,283684u,
        303393u,322564u,347640u,372686u};
    static const uint32_t ends[]={300u,976u,89954u,99685u,118988u,141520u,
        160930u,180346u,199619u,219848u,244069u,263559u,283684u,303393u,
        322564u,347640u,372686u,549150u};
    memset(&image,0,sizeof(image));memset(&meta,0,sizeof(meta));
    image.format=KUI_GAME_IMAGE_GDI;image.count=18u;image.data_lba=45000u;
    meta.ip_valid=meta.boot_valid=meta.windows_ce=true;
    meta.session_lba=45000u;meta.boot_bytes=1828864u;meta.boot_lba=548107u;
    strcpy(meta.product,"T22904N");strcpy(meta.bootfile,"0WINCEOS.BIN");
    strcpy(meta.title,"WORMS WORLD PARTY");
    for(unsigned i=0;i<image.count;i++) {
        const bool data=i==0u || i==2u || i==17u;
        const uint32_t stride=cooked && data?2048u:2352u;
        struct kui_game_image_track *t=&image.tracks[i];
        t->number=i+1u;t->start_lba=starts[i];t->end_lba=ends[i];
        t->control=data?4u:0u;t->sector_bytes=stride;
        t->sector_mode=data?1u:0u;t->data_offset=data && !cooked?16u:0u;
        t->file_bytes=(uint64_t)(ends[i]-starts[i])*stride;
        snprintf(t->name,sizeof(t->name),"track%02u.bin",i+1u);
    }
}
static void world_party_has_no_tested_evidence(void) {
    static const uint8_t sha[32]={
        0x18,0x97,0x19,0xd8,0xd3,0x76,0x32,0x84,0xf7,0x4c,0x5e,0x66,0x70,0x47,0x10,0xb9,
        0xf9,0x96,0x81,0x18,0xcb,0x58,0xae,0xb0,0xa0,0x84,0x4b,0xd1,0x21,0xa6,0x29,0xab};
    const uint32_t world_party_ip=UINT32_C(0xafe32089);
    const unsigned toy_evidence_candidate=4u;
    /* Runtime profiles and evidence candidates are separate identities.
     * World Party profile 4 must never inherit Toy's evidence slot 4. */
    CHECK((unsigned)KUI_WINCE_CDDA_WORMS_WORLD_PARTY==toy_evidence_candidate);
    for(unsigned cooked=0;cooked<2u;cooked++) {
        world_party_fixture(cooked!=0u);
        CHECK(kui_wince_cdda_kernel_profile(meta.boot_bytes,sha)==KUI_WINCE_CDDA_WORMS_WORLD_PARTY);
        unsigned candidate=kui_games_cdda_test_candidate(&image,&meta);
        CHECK(candidate==0u);
        CHECK(kui_games_cdda_test_status(candidate,world_party_ip,sha)==KUI_GAME_CDDA_UNTESTED);
        CHECK(kui_games_cdda_test_status(candidate,ip_crc[3],boot_sha[3])==KUI_GAME_CDDA_UNTESTED);
        for(unsigned forced=1u;forced<=4u;forced++)
            CHECK(kui_games_cdda_test_status(forced,world_party_ip,sha)==KUI_GAME_CDDA_UNTESTED);
        CHECK(kui_games_cdda_test_status(toy_evidence_candidate,ip_crc[3],sha)==KUI_GAME_CDDA_UNTESTED);
        CHECK(kui_games_cdda_test_status(toy_evidence_candidate,world_party_ip,boot_sha[3])==KUI_GAME_CDDA_UNTESTED);
    }
    fixture(4u);
    CHECK(kui_games_cdda_test_candidate(&image,&meta)==toy_evidence_candidate);
    CHECK(status(4u)==KUI_GAME_CDDA_TESTED_LIMITED);
}
static void malformed_identity(void) {
    CHECK(!kui_games_cdda_test_candidate(NULL,&meta));CHECK(!kui_games_cdda_test_candidate(&image,NULL));
    const unsigned invalid[]={0u,5u,UINT_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)
        CHECK(kui_games_cdda_test_status(invalid[i],ip_crc[0],boot_sha[0])==KUI_GAME_CDDA_UNTESTED);
    for(unsigned id=1u;id<=4u;id++) {
        for(unsigned fault=0;fault<17u;fault++) {
            fixture(id);
            if(fault==0u) meta.ip_valid=false;
            if(fault==1u) meta.boot_valid=false;
            if(fault==2u) image.format=KUI_GAME_IMAGE_CUE;
            if(fault==3u) image.cd_image=true;
            if(fault==4u) image.scrambled=true;
            if(fault==5u) image.count--;
            if(fault==6u) meta.boot_bytes--;
            if(fault==7u) meta.boot_lba++;
            if(fault==8u) meta.windows_ce=meta.native_gd=false;
            if(fault==9u) image.count=0u;
            if(fault==10u) image.count=KUI_GAME_TRACK_MAX+1u;
            if(fault==11u) meta.native_cd=true;
            if(fault==12u) meta.windows_ce=meta.native_gd=true;
            if(fault==13u) image.data_lba++;
            if(fault==14u) meta.session_lba++;
            if(fault==15u) memset(meta.product,'A',sizeof(meta.product));
            if(fault==16u) memset(meta.bootfile,'A',sizeof(meta.bootfile));
            CHECK(!kui_games_cdda_test_candidate(&image,&meta));
        }
        fixture(id);unsigned tracks=image.count;
        for(unsigned track=0;track<tracks;track++) for(unsigned fault=0;fault<10u;fault++) {
            fixture(id);struct kui_game_image_track *t=&image.tracks[track];
            if(fault==0u) t->number++;
            if(fault==1u) t->start_lba++;
            if(fault==2u) t->control=t->control?0u:4u;
            if(fault==3u) t->file_offset=2352u;
            if(fault==4u) t->end_lba=t->start_lba;
            if(fault==5u) t->sector_bytes=2448u;
            if(fault==6u) t->sector_mode=t->control?0u:1u;
            if(fault==7u) t->data_offset++;
            if(fault==8u) t->file_bytes--;
            if(fault==9u) t->end_lba=KUI_GAME_LBA_LIMIT+1u;
            CHECK(!kui_games_cdda_test_candidate(&image,&meta));
        }
        /* Exact ends were supplied for BAM4/Worms/Toy; changing a length
         * consistently must not inherit those known versions' evidence. */
        if(id>=2u) for(unsigned track=0;track<tracks;track++) {
            fixture(id);image.tracks[track].end_lba--;
            image.tracks[track].file_bytes-=2352u;
            CHECK(!kui_games_cdda_test_candidate(&image,&meta));
        }
        if(id<=3u) {
            fixture(id);strcpy(meta.product,"UNKNOWN");CHECK(!kui_games_cdda_test_candidate(&image,&meta));
            fixture(id);strcpy(meta.bootfile,"1ST_READ.BIN");CHECK(!kui_games_cdda_test_candidate(&image,&meta));
        }
    }
    /* Familiar title, product and boot strings alone grant no evidence. */
    memset(&image,0,sizeof(image));memset(&meta,0,sizeof(meta));
    image.format=KUI_GAME_IMAGE_GDI;image.count=3u;
    meta.ip_valid=meta.boot_valid=meta.windows_ce=true;
    strcpy(meta.title,"ARMADA");strcpy(meta.product,"T40301N 00");strcpy(meta.bootfile,"0WINCEOS.BIN");
    CHECK(!kui_games_cdda_test_candidate(&image,&meta));
    CHECK(kui_games_cdda_test_status(0u,ip_crc[0],boot_sha[0])==KUI_GAME_CDDA_UNTESTED);
}
int main(void) {
    known_identities();cooked_worms_evidence();world_party_has_no_tested_evidence();malformed_identity();
    printf("PASS CD audio evidence: %u checks; raw CE and exact cooked Worms tested, Toy limited, World Party/other cooked/MDK2/unknown untested, changed identities rejected\n",checks);
    return 0;
}
