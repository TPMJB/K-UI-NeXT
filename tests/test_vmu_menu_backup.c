/* SPDX-License-Identifier: GPL-3.0-only */
/* Menu-level backup oracle: actual FatFs and public VMU APIs, with physical
 * replies supplied by the existing independent transport fixture. */
#define main inherited_vmu_app_main
#define kui_vmu_read_bounded inherited_vmu_read_bounded
#include "test_vmu_app.c"
#undef kui_vmu_read_bounded
#undef main

static bool unused_noise;
static unsigned unused_samples;
int kui_vmu_read_bounded(maple_device_t *device,uint16_t block,uint8_t out[512]) {
    int result=inherited_vmu_read_bounded(device,block,out);
    if(!result && unused_noise && block<200u &&
       card[254][block*2u]==0xfcu && card[254][block*2u+1u]==0xffu) {
        out[17]^=(uint8_t)(++unused_samples&1u);
    }
    return result;
}

static void four_saves(void) {
    collection_setup(4u);
    put16(card[254]+4u*2u,0xfffcu);put16(card[254]+5u*2u,0xfffcu);
    for(unsigned b=128u;b<144u;b++) {
        for(unsigned n=0u;n<512u;n++) card[b][n]=(uint8_t)(b*7u+n);
        put16(card[254]+b*2u,b==143u?0xfffau:b+1u);
    }
    card[128][123]=8u;
    uint8_t *entry=card[253]+2u*32u;memcpy(entry+4u,"TIME_STK.001",12u);
    put16(entry+2u,128u);put16(entry+24u,16u);
    /* Opaque original metadata must survive canonicalizing the allocation. */
    card[253][1]=0xffu;card[253][16]=0x20u;card[253][29]=0x53u;
    collection_read_guard=true;
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void inspect_image(const uint8_t *captured,bool recovered) {
    bool used[200]={false};unsigned total=0u;
    for(unsigned file=0u;file<4u;file++) {
        const uint8_t *before=original_card[253]+file*32u;
        const uint8_t *after=captured+253u*512u+file*32u;
        assert(!memcmp(before,after,2u) && !memcmp(before+4u,after+4u,28u));
        unsigned from=get16(before+2u),to=get16(after+2u),count=get16(before+24u);
        for(unsigned n=0u;n<count;n++) {
            assert(from<200u && to<200u && !used[to]);used[to]=true;++total;
            for(unsigned byte=0u;byte<512u;byte++) {
                uint8_t wanted=original_card[from][byte];
                if(recovered && file==2u && !n && byte==123u) wanted=9u;
                assert(captured[to*512u+byte]==wanted);
            }
            from=get16(original_card[254]+from*2u);
            to=get16(captured+254u*512u+to*2u);
        }
        assert(from==0xfffau && to==0xfffau);
    }
    assert(total==22u);
    for(unsigned b=0u;b<200u;b++) if(!used[b])
        for(unsigned n=0u;n<512u;n++) assert(!captured[b*512u+n]);
}
static void inspect_backup(const struct kui_vmu_view *result,bool recovered) {
    const char *path="0:/KUI/backups/vmu/A1-0002/saves.vmd";
    assert(!strcmp(result->backup_path,path));
    FATFS fs;mount_image(&fs);FIL file;UINT got;uint8_t *captured=malloc(KUI_VMU_IMAGE_BYTES);assert(captured);
    assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==KUI_VMU_IMAGE_BYTES);
    assert(f_read(&file,captured,KUI_VMU_IMAGE_BYTES,&got)==FR_OK && got==KUI_VMU_IMAGE_BYTES);
    assert(f_close(&file)==FR_OK);inspect_image(captured,recovered);
    uint8_t proof[32];
    assert(f_open(&file,"0:/KUI/backups/vmu/A1-0002/saves.vmd.crc",FA_READ)==FR_OK && f_size(&file)==32u);
    assert(f_read(&file,proof,sizeof(proof),&got)==FR_OK && got==sizeof(proof));assert(f_close(&file)==FR_OK);
    assert(!memcmp(proof,"KUIVMD1\0",8u) && get32(proof+8u)==KUI_VMU_IMAGE_BYTES);
    assert(get32(proof+12u)==kui_crc32(0u,captured,KUI_VMU_IMAGE_BYTES));
    assert(get32(proof+28u)==kui_crc32(0u,proof,28u));
    FILINFO info;assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)==FR_OK);
    assert(f_stat("0:/KUI/backups/vmu/A1-0002/card.vmd",&info)==FR_NO_FILE);
    verify_preserved();assert(f_mount(NULL,"0:",0)==FR_OK);free(captured);
    /* Public browser validates sidecar and image without touching Maple. */
    unsigned before_reads=reads,before_enum=enumerations;struct kui_vmu_view browsed;
    kui_vmu_image_run(path,0u,0u,0u,&browsed,log_line,cancelled,progress);
    assert(browsed.status.passed && browsed.total==4u && browsed.count==4u && browsed.free_blocks==178u);
    assert(reads==before_reads && enumerations==before_enum);
}
static void inspect_files(bool all) {
    static const char *names[]={"SAVE00","SAVE01","TIME_STK_001","SAVE03"};
    FATFS fs;mount_image(&fs);char path[160];uint8_t payload[8192];
    for(unsigned file=all?0u:2u;file<(all?4u:3u);file++) {
        const uint8_t *entry=card[253]+file*32u;
        unsigned blocks=get16(entry+24u),at=get16(entry+2u);
        for(unsigned n=0u;n<blocks;n++) {
            assert(at<200u);memcpy(payload+n*512u,card[at],512u);at=get16(card[254]+at*2u);
        }
        assert(at==0xfffau);
        snprintf(path,sizeof(path),"0:/KUI/backups/vmu/A1-0002/%03u_%s.vms",file+1u,names[file]);
        equal_file(path,payload,blocks*512u);
        snprintf(path,sizeof(path),"0:/KUI/backups/vmu/A1-0002/%03u_%s.dir",file+1u,names[file]);
        equal_file(path,entry,32u);
    }
    FILINFO info;assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)==FR_OK);
    verify_preserved();assert(f_mount(NULL,"0:",0)==FR_OK);
}
int main(int argc,char **argv) {
    assert(argc==3);
    if(!strcmp(argv[2],"seed")) return inherited_vmu_app_main(argc,argv);
    image=fopen(argv[1],"r+b");assert(image);struct stat st;
    assert(!fstat(fileno(image),&st));image_blocks=(uint64_t)st.st_size/512u;
    const char *mode=argv[2];four_saves();memcpy(original_card,card,sizeof(card));
    struct kui_vmu_view out;
    kui_vmu_app_run(0u,0u,0u,0u,&out,log_line,cancelled,progress);assert(out.status.passed);
    reads=free_user_reads=collection_unowned_reads=0u;
    bool selected_files=!strcmp(mode,"selected-recovery"),all_files=!strcmp(mode,"all-files-recovery");
    if(selected_files || all_files) fault="read-recovery-between-passes";
    else if(!strncmp(mode,"whole-recovery-",15u)) {whole_mode=true;fault=mode;}
    else if(!strcmp(mode,"unused-noise") || !strcmp(mode,"raw-unused-noise")) unused_noise=true;
    else if(!strcmp(mode,"metadata-change")) fault="contents-change";
    else if(!strcmp(mode,"stale-list")) card[255][48]^=1u;
    else if(!strcmp(mode,"write-fail") || !strcmp(mode,"sync-fail") ||
            !strcmp(mode,"readback-fail") || !strcmp(mode,"readback-corrupt") ||
            !strcmp(mode,"cancel-write")) fault=mode;
    else assert(!strcmp(mode,"healthy"));
    bool raw=!strcmp(mode,"raw-unused-noise");
    bool recovered=!strcmp(mode,"whole-recovery-stable") || !strcmp(mode,"whole-recovery-source-wave");
    bool expected=!strcmp(mode,"healthy") || !strcmp(mode,"unused-noise") || recovered || selected_files || all_files;
    kui_vmu_app_run(selected_files?1u:all_files?2u:raw?3u:4u,0u,0u,selected_files?2u:0u,&out,log_line,cancelled,progress);
    assert(out.status.passed==expected && out.status.complete==expected);
    assert(!vmu_writes && !source_vmu_writes && !locks && connects==disconnects);
    if(!raw) assert(!free_user_reads && !collection_unowned_reads && !unused_samples);
    else assert(free_user_reads && unused_samples==8u);
    if(expected) {
        assert(!out.status.errors && !out.status.stopped && out.status.done==out.status.total);
        if(recovered) assert(whole_successes==1u && whole_streak==3u && whole_started<=6u);
        whole_mode=false;fault=NULL;
        if(selected_files || all_files) {
            assert(recovery_reads==10u && card[128][11]==(uint8_t)(original_card[128][11]^1u));
            inspect_files(all_files);
        } else {
            assert(out.status.done==KUI_VMU_IMAGE_BYTES && out.bulk_total==4u && out.bulk_completed==4u);
            inspect_backup(&out,recovered);
        }
    } else {
        assert(out.status.errors || out.status.stopped);
        whole_mode=false;fault=NULL;FATFS fs;mount_image(&fs);FILINFO info;
        assert(f_stat("0:/KUI/backups/vmu/A1-0002/complete.txt",&info)!=FR_OK);
        if(strncmp(mode,"readback-",9u))
            assert(f_stat("0:/KUI/backups/vmu/A1-0002/saves.vmd",&info)!=FR_OK);
        verify_preserved();assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    if(!selected_files && !all_files && strcmp(mode,"metadata-change") && strcmp(mode,"stale-list") &&
       strcmp(mode,"whole-recovery-metadata-pre") && strcmp(mode,"whole-recovery-metadata-post"))
        assert(!memcmp(card,original_card,sizeof(card)));
    assert(!locks && !vmu_writes && !source_vmu_writes);assert(!fclose(image));
    printf("PASS menu VMU backup: %s\n",mode);return 0;
}
