/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the ACTUAL console sequence and file callbacks against real FatFs.
 * Only the display, controller and physical media transport are substituted. */
#define _POSIX_C_SOURCE 200809L
#define main kui_vmu_console_main
#include "main.c"
#undef main
#include "kui/media.h"
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static FILE *disk_image;
static uint64_t image_blocks;
static bool connected,profile_raw_phase,inject_raw_failure,inject_identity_change;
static unsigned profile_raw_reads,identity_reads,profile_raw_connections,phase_reads;
#if KUI_VMU_PERSIST_PREP
static bool inject_final_raw_failure,inject_final_identity_change;
#endif
static uint8_t observed_image_hash[32];
static const uint8_t host_cid[16]={0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,
                                 0x13,0x57,0x9b,0xdf,0x24,0x68,0xac,0xe0};
static uint16_t frame[640u*480u];
uint16_t *vram_s=frame;
void vid_waitvbl(void) {}
void vid_flip(int page) {(void)page;}
void vid_set_mode(int mode,int format) {(void)mode;(void)format;}
void thd_sleep(unsigned milliseconds) {(void)milliseconds;}
maple_device_t *maple_enum_type(int n,uint32_t functions) {(void)n;(void)functions;return NULL;}
void *maple_dev_status(maple_device_t *controller) {(void)controller;return NULL;}
void kui_shell_font_draw(uint16_t *pixels,int x,int y,uint16_t color,const char *text,bool large) {
    (void)pixels;(void)x;(void)y;(void)color;(void)text;(void)large;
}
static uint64_t blocks(void *context) {(void)context;return image_blocks;}
static int raw_read(void *context,uint32_t lba,size_t count,uint8_t *bytes) {
    (void)context;assert((uint64_t)lba+count<=image_blocks);
    assert(connected);
    if(profile_raw_phase) {
        assert(count==1u);++profile_raw_reads;++phase_reads;
        if(inject_raw_failure && profile_raw_reads==7u) return -1;
#if KUI_VMU_PERSIST_PREP
        if(inject_final_raw_failure && profile_raw_connections==2u && phase_reads==7u) return -1;
#endif
    }
    return fseeko(disk_image,(off_t)lba*512,SEEK_SET) || fread(bytes,512,count,disk_image)!=count?-1:0;
}
static int raw_write(void *context,uint32_t lba,size_t count,const uint8_t *bytes) {
    (void)context;assert((uint64_t)lba+count<=image_blocks);
    assert(connected && !profile_raw_phase);
    return fseeko(disk_image,(off_t)lba*512,SEEK_SET) || fwrite(bytes,512,count,disk_image)!=count?-1:0;
}
static int raw_sync(void *context) {(void)context;return fflush(disk_image)||fsync(fileno(disk_image))?-1:0;}
void kui_sd_set_params(unsigned selected,bool crc) {assert(selected==KUI_STORAGE_SCI && crc);}
bool kui_sd_connect(void) {
    assert(!connected);connected=true;profile_raw_phase=false;
    const struct kui_media_ops operations={NULL,blocks,raw_read,raw_write,raw_sync};
    kui_media_set(&operations);return true;
}
void kui_sd_disconnect(void) {kui_media_set(NULL);profile_raw_phase=false;connected=false;assert(raw_sync(NULL)==0);}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    if(!out || !connected) return false;
    memset(out,0,sizeof(*out));memcpy(out->cid,host_cid,sizeof(host_cid));
    out->sectors=image_blocks;out->high_capacity=true;++identity_reads;
    if(inject_identity_change && identity_reads>=2u) out->cid[0]^=1u;
#if KUI_VMU_PERSIST_PREP
    if(inject_final_identity_change && identity_reads>=5u) out->cid[0]^=1u;
#endif
    return true;
}
bool kui_sd_raw_read_ops(struct kui_media_ops *out) {
    if(!out || !connected) return false;
    assert(!volume.fs_type);profile_raw_phase=true;
    ++profile_raw_connections;phase_reads=0;
    *out=(struct kui_media_ops){.blocks=blocks,.read=raw_read};return true;
}
void kui_storage_boot_begin(void) {}
unsigned kui_storage_selected(void) {return KUI_STORAGE_SCI;}
static uint32_t image_crc(unsigned number) {
    assert(connect_volume());char name[96];assert(path(name,number,"card.vmd"));
    FIL file;assert(f_open(&file,name,FA_READ)==FR_OK);assert(f_size(&file)==131072);
    uint32_t crc=0;uint8_t bytes[512];struct kui_sha256 hash;kui_sha256_init(&hash);
    for(unsigned i=0;i<256;i++) {
        UINT done=0;assert(f_read(&file,bytes,512,&done)==FR_OK && done==512);
        if(i==PROBE_FIRST_BLOCK || i==PROBE_SECOND_BLOCK) {
            pattern(number,expected);assert(!memcmp(bytes,expected+(i==PROBE_FIRST_BLOCK?0:512),512));
        }
        crc=kui_crc32(crc,bytes,512);
        kui_sha256_update(&hash,bytes,512);
    }
    kui_sha256_digest(&hash,observed_image_hash);
    assert(f_close(&file)==FR_OK);assert(disconnect_volume());return crc;
}
#if KUI_VMU_PERSIST_PREP
static int diagnostic_read_forbidden(void *context,uint32_t lba,uint8_t bytes[512]) {
    (void)context;(void)lba;(void)bytes;
    assert(!"Diagnostic display must not reread or replace the original refusal");return -1;
}
static void check_persist_boot_diagnostics(void) {
    struct kui_vmu_fs_input input={.read_sector=diagnostic_read_forbidden,
        .partition_first=UINT64_C(0x300000004),.partition_end=UINT64_C(0x80000000a)};
    memset(&persist_workspace.stats,0,sizeof(persist_workspace.stats));
    persist_workspace.stats.result=KUI_VMU_FS_UNSUPPORTED;
    persist_workspace.stats.detail=15u;persist_workspace.stats.last_lba=0x12345678u;
    persist_workspace.stats.reads=2u;persist_workspace.stats.boot.valid=1u;
    persist_workspace.stats.boot.reject_mask=0xe2u;
    persist_workspace.stats.boot.partition_offset=UINT64_C(0x100000002);
    persist_workspace.stats.boot.volume_sectors=UINT64_C(0x700000008);
    persist_workspace.stats.boot.revision=0x101u;persist_workspace.stats.boot.flags=2u;
    persist_workspace.stats.boot.sector_shift=9u;persist_workspace.stats.boot.cluster_shift=7u;
    persist_workspace.stats.boot.fat_count=1u;
    const char *const expected[]={"STATUS -4 DETAIL 15","FS LBA 12345678 READS 2",
        "FLAGS 0002 REV 0101 ZERO53 00","BYTE 9 CLUSTER 7 FATS 1",
        "PART ACT 00000001:00000002","PART EXP 00000003:00000004",
        "LEN ACT 00000007:00000008","LEN EXP 00000005:00000006","MASK e2"};
    for(unsigned phase=0;phase<2u;phase++) {
        line_count=0;struct kui_vmu_fs_stats before=persist_workspace.stats;
        assert(!persist_fs_result(KUI_VMU_FS_UNSUPPORTED,phase?"FINAL":"SOURCE",&input));
        assert(line_count==11u && !memcmp(&before,&persist_workspace.stats,sizeof(before)));
        for(unsigned row=0;row<line_count;row++) assert(strlen(lines[row])<=65u && !strstr(lines[row],"READY"));
        for(unsigned item=0;item<sizeof(expected)/sizeof(expected[0]);item++) {
            bool found=false;
            for(unsigned row=0;row<line_count;row++) if(strstr(lines[row],expected[item])) found=true;
            assert(found);
        }
    }
    persist_workspace.stats.boot.valid=0u;line_count=0;
    assert(!persist_fs_result(KUI_VMU_FS_IO,"SOURCE",&input) && line_count==3u);
    assert(strstr(lines[2],"UNAVAILABLE") && !connected && !exported_profile_path[0]);
}
static void pair_content_hashes(unsigned number,uint8_t hashes[3][32]) {
    static const char *const leaves[]={"card.vmd","card.journal","identity.bin"};
    assert(connect_volume());
    for(unsigned role=0;role<3u;role++) {
        char name[96];FIL file;uint8_t bytes[512];struct kui_sha256 hash;
        assert(path(name,number,leaves[role]));FRESULT opened=f_open(&file,name,FA_READ);
        if(opened==FR_NO_FILE || opened==FR_NO_PATH) {memset(hashes[role],0,32u);continue;}
        assert(opened==FR_OK);
        kui_sha256_init(&hash);
        for(;;) {
            UINT done=0;assert(f_read(&file,bytes,sizeof(bytes),&done)==FR_OK);
            if(!done) break;
            kui_sha256_update(&hash,bytes,done);
        }
        kui_sha256_digest(&hash,hashes[role]);assert(f_close(&file)==FR_OK);
    }
    assert(disconnect_volume());
}
static void persisted_prep_profile(void) {
    assert(connect_volume());FIL file;UINT done=0;
    assert(f_open(&file,exported_profile_path,FA_READ)==FR_OK);
    assert(f_size(&file)==sizeof(persist_check));
    assert(f_read(&file,persist_check,sizeof(persist_check),&done)==FR_OK && done==sizeof(persist_check));
    assert(f_close(&file)==FR_OK && disconnect_volume());
    assert(kui_vmu_persist_profile_decode(&persist_verify,persist_check,sizeof(persist_check))==0);
    assert(kui_vmu_persist_profile_same_ownership(&persist_profile,&persist_verify));
    printf("PERSIST-PATH %s\nPERSIST-WIRE ",exported_profile_path);
    for(size_t i=0;i<sizeof(persist_check);i++) printf("%02x",persist_check[i]);
    putchar('\n');
}
#endif
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    struct stat info;assert(stat(argv[1],&info)==0 && info.st_size%512==0);
    image_blocks=(uint64_t)info.st_size/512;disk_image=fopen(argv[1],"r+b");assert(disk_image);
    bool ok,expect_failure=false;
    if(!strcmp(argv[2],"create")) {
        ok=run_test(true);
    } else if(!strcmp(argv[2],"verify")) {
        ok=run_test(false);
    }
#if KUI_VMU_PERSIST_PREP
    else if(!strcmp(argv[2],"prep-boot-diagnostics")) {
        check_persist_boot_diagnostics();ok=true;
    } else if(!strcmp(argv[2],"export-persist") || !strcmp(argv[2],"refuse-persist") ||
            !strcmp(argv[2],"refuse-persist-scif") || !strcmp(argv[2],"refuse-persist-raw") ||
            !strcmp(argv[2],"refuse-persist-identity") || !strcmp(argv[2],"refuse-persist-final-raw") ||
            !strcmp(argv[2],"refuse-persist-final-identity")) {
        expect_failure=strcmp(argv[2],"export-persist")!=0;
        assert(connect_volume());unsigned number;assert(latest_probe(&number));assert(disconnect_volume());
        uint8_t before[3][32],after[3][32];pair_content_hashes(number,before);
        if(!strcmp(argv[2],"refuse-persist-scif")) transport=KUI_STORAGE_SCIF;
        inject_raw_failure=!strcmp(argv[2],"refuse-persist-raw");
        inject_identity_change=!strcmp(argv[2],"refuse-persist-identity");
        inject_final_raw_failure=!strcmp(argv[2],"refuse-persist-final-raw");
        inject_final_identity_change=!strcmp(argv[2],"refuse-persist-final-identity");
        ok=export_persistent_profile();assert(!connected);
        if(ok) {
            assert(profile_raw_connections==2u && profile_raw_reads>=518u && identity_reads==6u);
            persisted_prep_profile();
        } else assert(!exported_profile_path[0]);
        transport=KUI_STORAGE_SCI;
        pair_content_hashes(number,after);assert(!memcmp(before,after,sizeof(before)));
    } else if(!strcmp(argv[2],"prep-refuse-create") || !strcmp(argv[2],"prep-refuse-verify")) {
        expect_failure=true;ok=run_test(!strcmp(argv[2],"prep-refuse-create"));
        assert(!connected && !profile_raw_reads && !identity_reads);
    }
#endif
    else if(!strcmp(argv[2],"export")) {
        assert(connect_volume());unsigned number;assert(latest_probe(&number));assert(disconnect_volume());
        uint32_t before=image_crc(number);uint8_t before_hash[32];
        memcpy(before_hash,observed_image_hash,sizeof(before_hash));ok=export_readonly_profile();
        assert(ok && profile_raw_reads==256u && identity_reads==3u && !connected);
        assert(image_crc(number)==before);
        assert(!memcmp(before_hash,observed_image_hash,sizeof(before_hash)));
        assert(connect_volume());FIL file;UINT done=0;
        assert(f_open(&file,exported_profile_path,FA_READ)==FR_OK);
        assert(f_size(&file)==sizeof(profile_check));
        assert(f_read(&file,profile_check,sizeof(profile_check),&done)==FR_OK && done==sizeof(profile_check));
        assert(f_close(&file)==FR_OK && disconnect_volume());
        struct kui_vmu_sd_profile decoded;
        assert(kui_vmu_sd_profile_decode(&decoded,profile_check,sizeof(profile_check))==0);
        assert(decoded.card_sectors==image_blocks && !memcmp(decoded.cid,host_cid,16));
        printf("PROFILE-PATH %s\nPROFILE-WIRE ",exported_profile_path);
        for(size_t i=0;i<sizeof(profile_check);i++) printf("%02x",profile_check[i]);
        putchar('\n');
    } else if(!strcmp(argv[2],"refuse-export") || !strcmp(argv[2],"refuse-export-scif") ||
              !strcmp(argv[2],"refuse-export-raw") || !strcmp(argv[2],"refuse-export-identity")) {
        expect_failure=true;
        if(!strcmp(argv[2],"refuse-export-scif")) transport=KUI_STORAGE_SCIF;
        inject_raw_failure=!strcmp(argv[2],"refuse-export-raw");
        inject_identity_change=!strcmp(argv[2],"refuse-export-identity");
        ok=export_readonly_profile();assert(!exported_profile_path[0]);
    } else if(!strcmp(argv[2],"refuse-create")) {
        expect_failure=true;ok=run_test(true);
    } else if(!strcmp(argv[2],"refuse-verify")) {
        expect_failure=true;ok=run_test(false);
    } else if(!strcmp(argv[2],"bad-identity")) {
        assert(connect_volume());char name[96];assert(path(name,2,"identity.bin"));
        FIL file;assert(f_open(&file,name,FA_READ|FA_WRITE)==FR_OK);
        UINT done=0;uint8_t byte=0xff;assert(f_write(&file,&byte,1,&done)==FR_OK && done==1);
        assert(f_close(&file)==FR_OK);assert(disconnect_volume());ok=true;
    } else if(!strcmp(argv[2],"short-journal")) {
        assert(connect_volume());char name[96];assert(path(name,2,"card.journal"));
        FIL file;assert(f_open(&file,name,FA_READ|FA_WRITE)==FR_OK);
        assert(f_lseek(&file,512)==FR_OK && f_truncate(&file)==FR_OK);
        assert(f_close(&file)==FR_OK);assert(disconnect_volume());ok=true;
    } else if(!strcmp(argv[2],"bad-vmu-geometry") || !strcmp(argv[2],"orphan-vmu-chain")) {
        assert(connect_volume());char name[96];assert(path(name,2,"card.vmd"));
        FIL file;assert(f_open(&file,name,FA_READ|FA_WRITE)==FR_OK);
        bool geometry=!strcmp(argv[2],"bad-vmu-geometry");
        assert(f_lseek(&file,geometry?255u*512u+70u:254u*512u+43u*2u)==FR_OK);
        uint8_t changed[2]={0xff,0xff};if(!geometry) changed[0]=0xfa;
        UINT done=0;assert(f_write(&file,changed,2,&done)==FR_OK && done==2);
        assert(f_close(&file)==FR_OK);assert(disconnect_volume());ok=true;
    } else if(!strcmp(argv[2],"bad-vms-crc")) {
        assert(connect_volume());char name[96];assert(path(name,2,"card.vmd"));
        FIL file;assert(f_open(&file,name,FA_READ|FA_WRITE)==FR_OK);
        assert(f_lseek(&file,PROBE_SECOND_BLOCK*512u+128u)==FR_OK);
        uint8_t changed=0xff;UINT done=0;
        assert(f_write(&file,&changed,1,&done)==FR_OK && done==1);
        assert(f_close(&file)==FR_OK);assert(disconnect_volume());ok=true;
    } else if(!strcmp(argv[2],"repeat")) {
        uint32_t before=image_crc(1);ok=run_test(true);
        assert(image_crc(1)==before);assert(image_crc(2)!=before);
    } else return 2;
    if(ok==expect_failure) {for(unsigned i=0;i<line_count;i++) fprintf(stderr,"%s\n",lines[i]);}
    close_pair();(void)f_mount(NULL,"0:",0);kui_sd_disconnect();assert(fclose(disk_image)==0);
    assert(ok!=expect_failure);printf("PASS native harness host sequence: %s\n",argv[2]);return 0;
}
