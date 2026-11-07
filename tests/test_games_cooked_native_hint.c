/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
/* Actual launcher/FatFs callbacks; synthetic NVR is deliberately not SDK proof. */
#define main retail_fixture_main
#include "games_retail.c"
#undef main
#define main resolution_fixture_main
#include "test_vmu_native_resolution_cache.c"
#undef main
static void install_hint(void) {
    const uint32_t bytes=3001u;uint8_t source[3001],wire[1024];
    /* Source contents are irrelevant to config inspection. A deliberately
     * synthetic SDK mapping cannot pass detached strict validation. */
    memset(source,0x59,sizeof(source));struct kui_vmu_native_resolution record;
    mock(source,bytes,&record);record.heap.initializer_slot=0x8c010100u;
    CHECK(kui_vmu_native_resolution_cache_encode(&record,wire,sizeof(wire)));
    char directory[128];
    const char *paths[]={"0:/KUI/VMU","0:/KUI/VMU/native-cache","0:/KUI/VMU/native-cache/v8","0:/KUI/VMU/native-cache/v8/hints"};
    for(unsigned i=0;i<4;i++){FRESULT r=f_mkdir(paths[i]);assert(r==FR_OK || r==FR_EXIST);}
    FIL f;UINT got;uint8_t gdi[4096];assert(f_open(&f,"0:/Games/Reader Test/disc.gdi",FA_READ)==FR_OK);
    assert(f_read(&f,gdi,sizeof(gdi),&got)==FR_OK);assert(f_close(&f)==FR_OK);
    uint32_t words[]={8u,45021u,bytes,0x8c010000u,1u,0u,0u,0u,kui_retail_crc32(0,gdi,got)};
    struct kui_sha256 hash;uint8_t digest_bytes[32];kui_sha256_init(&hash);
    static const char domain[]="K-UI native mapping advisory alias v1";
    static const char selected_path[]="/games/reader test/disc.gdi";
    kui_sha256_update(&hash,domain,sizeof(domain));kui_sha256_update(&hash,selected_path,sizeof(selected_path));
    for(unsigned i=0;i<9;i++){uint8_t b[4];put32(b,words[i]);kui_sha256_update(&hash,b,sizeof(b));}
    kui_sha256_digest(&hash,digest_bytes);char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",digest_bytes[i]);
    snprintf(directory,sizeof(directory),"0:/KUI/VMU/native-cache/v8/hints/%s.nvr",hex);
    write_file(directory,wire,sizeof(wire));
    struct kui_vmu_native_resolution out;uint8_t pin[32];
    CHECK(kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),pin));
    source[0]^=1u;CHECK(!kui_vmu_native_resolution_cache_replay(source,sizeof(source),load,wire,sizeof(wire),pin,&out));
}
int main(int argc,char **argv) {
    assert(argc==5);struct stat st;assert(!lstat(argv[1],&st));
    test.image=fopen(argv[1],"r+b");assert(test.image);test.blocks=(uint64_t)st.st_size/512u;
    test.fault=argv[4];
    if(!strcmp(argv[3],"seed")) {
        kui_media_set(&media);assert(kui_mount(&fs,log_line));seed(argv[2]);
        assert(kui_vmu_emulation_settings_save(true,log_line));install_hint();
        assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);
    } else {
        assert(!strcmp(argv[3],"check"));test.active=true;
        test.hash_begin[0]=0;test.hash_end[0]=16u*track_stride(2);
        test.hash_begin[1]=21u*track_stride(2);test.hash_end[1]=23u*track_stride(2);test.hash_bounded=true;
        struct kui_runtime_image staged={0};struct kui_game_ram_patches patches={0};
        bool ok=kui_games_retail_prepare_launch(selected,KUI_RETAIL_READER_STANDARD,&patches,&staged,log_line,cancel);
        assert(ok && test.native_source_passes==EXPECT_SOURCE && test.direct_crc_passes==EXPECT_CRC);
        assert(test.hash_reads[1]==EXPECT_READS && !test.files && !test.connected && test.connects==test.disconnects);
        /* The cooked expected CRC is fresh from this exact source, never NVR. */
        struct kui_retail_manifest manifest;assert(kui_retail_manifest_decode((const uint8_t *)staged.data+KUI_RETAIL_MAP_OFFSET,&manifest)==KUI_GAME_OK);
        if(cooked_track(2))assert(manifest.flags&KUI_RETAIL_IMAGE_BOOT_CRC);
        if(manifest.boot_crc32==0x4a3efbf8u)check_mapping(argv[2],&staged);
        printf("POLICY source=%u crc=%u reads=%u bootcrc=%08x flags=%08x\n",test.native_source_passes,test.direct_crc_passes,test.hash_reads[1],manifest.boot_crc32,manifest.flags);
        kui_runtime_free(&staged);
    }
    assert(!fclose(test.image));return 0;
}
