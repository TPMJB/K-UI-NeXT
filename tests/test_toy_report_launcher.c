/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the existing invented Toy/MDK2 maps and typed package mutations. */
#define main native_cdda_reference_main
#include "test_native_cdda_compat.c"
#undef main

static void exact_toy(bool cooked) {
    static const uint32_t starts[15]={0,7257,45000,201900,219712,234091,255599,
        272017,282799,303092,323352,341314,356690,374201,377422};
    static const uint32_t ends[15]={7107,7753,201750,219712,234091,255599,272017,
        282799,303092,323352,341314,356690,374201,377272,549150};
    fixture(KUI_NATIVE_CDDA_TOY,cooked);
    map.track_count=image.count=15u;map.extent_count=15u;map.boot_lba=548634u;
    map.ip_crc32=UINT32_C(0x38ba2868);
    map.gdi_crc32=cooked?UINT32_C(0xb7100f9e):UINT32_C(0xe25531d1);
    uint32_t physical=100u;
    for(unsigned i=0;i<15u;i++) {
        bool data=i==0u || i==2u || i==14u;
        uint32_t stride=data && cooked?2048u:2352u;
        uint32_t blocks=((ends[i]-starts[i])*stride+511u)/512u;
        map.slots[i].track=(struct kui_retail_track){.start_lba=starts[i],.end_lba=ends[i],
            .control=(uint8_t)(data?(4u|(cooked?KUI_RETAIL_TRACK_COOKED:0u)):0u),
            .first_extent=(uint16_t)(15u+i),.extent_count=1u};
        map.slots[15u+i].extent=(struct kui_retail_extent){0u,physical,blocks};
        physical+=blocks+1u;
        image.tracks[i]=(struct kui_game_image_track){.start_lba=starts[i],.end_lba=ends[i],
            .number=i+1u,.control=data?4u:0u,.sector_bytes=stride};
    }
    map.card_sectors=physical+100u;map.partition_end=physical+50u;
    assert(kui_retail_manifest_validate(&map)==KUI_GAME_OK);
}
static void package_contract(bool cooked) {
    struct kui_native_cdda_descriptor d=KUI_NATIVE_CDDA_TOY_DESCRIPTOR(0u);
    if(cooked) d=(struct kui_native_cdda_descriptor)KUI_NATIVE_CDDA_TOY_DESCRIPTOR(1u);
    struct kui_runtime_image p={.data=&d};
    p.info.payload_bytes=p.info.memory_bytes=sizeof(d);
    p.info.crc32=kui_retail_crc32(0u,&d,sizeof(d));memcpy(p.info.build,"123456789abc",13u);
    enum kui_games_cdda_profile profile=cooked?KUI_GAMES_CDDA_TOY_COOKED_COMPAT:KUI_GAMES_CDDA_TOY_RAW_COMPAT;
    assert(kui_games_cdda_launch_package_admit(profile,&p,"123456789abc"));
#if KUI_TOY_REPORT_TEST
    assert(!kui_games_cdda_launch_package_admit(profile,&p,"000000000000"));
#endif
    assert(!kui_games_cdda_launch_package_admit(cooked?KUI_GAMES_CDDA_TOY_RAW_COMPAT:
        KUI_GAMES_CDDA_TOY_COOKED_COMPAT,&p,"123456789abc"));
    d.slots^=1u;assert(!kui_games_cdda_launch_package_admit(profile,&p,"123456789abc"));
    p.info.crc32=kui_retail_crc32(0u,&d,sizeof(d));
    assert(!kui_games_cdda_launch_package_admit(profile,&p,"123456789abc"));
}
int main(void) {
    assert(native_cdda_reference_main(1,NULL)==0);
    for(unsigned cooked=0u;cooked<2u;cooked++) {
        exact_toy(cooked!=0u);
        enum kui_games_cdda_profile exact=cooked?KUI_GAMES_CDDA_TOY_COOKED:KUI_GAMES_CDDA_TOY_RAW;
        assert(kui_games_cdda_compatible_profile(&image,&map)==exact);
        enum kui_games_cdda_profile selected=kui_games_cdda_launch_profile(&image,&map);
#if KUI_TOY_REPORT_TEST
        assert(selected==(cooked?KUI_GAMES_CDDA_TOY_COOKED_COMPAT:KUI_GAMES_CDDA_TOY_RAW_COMPAT));
        assert(strcmp(kui_games_cdda_package(selected),kui_games_cdda_package(exact)));
#else
        assert(selected==exact);
#endif
        map.boot_crc32^=1u;
        assert(kui_games_cdda_launch_profile(&image,&map)==KUI_GAMES_CDDA_UNSUPPORTED);
        package_contract(cooked!=0u);
    }
    fixture(KUI_NATIVE_CDDA_MDK2,false);
#if KUI_TOY_REPORT_TEST
    assert(kui_games_cdda_launch_profile(&image,&map)==KUI_GAMES_CDDA_UNSUPPORTED);
#else
    assert(kui_games_cdda_launch_profile(&image,&map)==KUI_GAMES_CDDA_MDK2_RAW_COMPAT);
#endif
    puts("PASS private Toy launcher: exact/raw/cooked routing, matched build, descriptor/CRC and non-Toy refusal");
    return 0;
}
