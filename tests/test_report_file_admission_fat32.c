/* SPDX-License-Identifier: GPL-3.0-only */
#define main unused_vmu_fat32_main
#include "test_vmu_fs_admission_adversarial.c"
#undef main
#include "kui/report_file.h"
static struct kui_report_file_descriptor report;
static struct kui_report_file_input request;
static void report_fresh(void) {
    fresh();
    short_entry(disk.dirs[1]+64u,"REPORTS    ",0x10u,4u,0u);
    memset(disk.dirs[2]+64u,0,512u-64u);
    short_entry(disk.dirs[2]+64u,"TC      TXT",0x20u,BASE_FIRST,KUI_REPORT_FILE_BYTES);
    disk.fat[BASE_FIRST+7u]=EOC;
    request=(struct kui_report_file_input){.read_sector=read_sector,.context=&disk,
        .card_sectors=PARTITION+VOLUME_SECTORS,.partition_first=PARTITION,
        .partition_end=PARTITION+VOLUME_SECTORS,.high_capacity=1u,.file_name="TC.TXT",.profile=3u};
    memcpy(request.cid,disk.input.cid,16u);
}
static int report_collect(void) {return kui_report_file_admit(&request,&report,&disk.work);}
static void report_refusal(void) {
    memset(&report,0xa5,sizeof(report));assert(report_collect());
    const uint8_t *p=(const void *)&report;for(size_t i=0;i<sizeof(report);i++)assert(!p[i]);
    assert(disk.reads<=KUI_VMU_FS_MAX_READS && !disk.outside);
}
int main(void) {
    report_fresh();assert(!report_collect() && kui_report_file_descriptor_valid(&report));
    assert(report.extent_count==1u && report.extents[0].blocks==64u && report.extents[0].lba==cluster_lba(BASE_FIRST));
    report_fresh();short_entry(disk.dirs[0]+32u,"VMU     VMD",0x20u,BASE_FIRST,17u);report_refusal();
    report_fresh();short_entry(disk.dirs[0]+32u,"ALIAS   BIN",0x20u,BASE_FIRST+3u,17u);report_refusal();
    report_fresh();disk.fat[100u]=BASE_FIRST;report_refusal();
    report_fresh();disk.fat[100u]=BASE_FIRST+3u;report_refusal();
    report_fresh();disk.fat[BASE_FIRST+7u]=40u;disk.fat[40u]=EOC;report_refusal();
    report_fresh();disk.fat[BASE_FIRST]=BASE_FIRST;report_refusal();
    report_fresh();struct kui_vmu_fs_range protected={cluster_lba(BASE_FIRST+7u)+7u,1u};
    request.forbidden=&protected;request.forbidden_count=1u;report_refusal();
    report_fresh();short_entry(disk.dirs[2]+64u,"TC      TXT",0x20u,2u,KUI_REPORT_FILE_BYTES);report_refusal();
    for(unsigned bad=1u;bad<=16u;bad<<=1u) {
        report_fresh();disk.dirs[2][64u+11u]|=(uint8_t)bad;report_refusal();
    }
    report_fresh();put32(disk.dirs[2]+64u+28u,KUI_REPORT_FILE_BYTES-1u);report_refusal();
    report_fresh();disk.dirs[2][64u+2u]='X';report_refusal();
    report_fresh();disk.boot[13u]=3u;report_refusal();
    report_fresh();put32(disk.boot+36u,UINT32_MAX);report_refusal();
    report_fresh();put32(disk.boot+44u,CLUSTERS+2u);report_refusal();
    report_fresh();put16(disk.boot+11u,4096u);report_refusal();
    puts("report FAT32 ownership contracts OK");return 0;
}
