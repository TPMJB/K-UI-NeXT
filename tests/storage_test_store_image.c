/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/storage_test.h"
#include "kui/media.h"
#include "kui/storage.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *image;
static uint64_t image_blocks;
static bool fail_writes;
static FATFS fs;
static struct kui_storage_test_history history;
static uint64_t blocks(void *ctx) { (void)ctx; return image_blocks; }
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    return fseeko(image,(off_t)block*512,SEEK_SET) || fread(data,512,count,image)!=count?-1:0;
}
static int write_image(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx;
    if(fail_writes) return -1;
    return fseeko(image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,image)!=count?-1:0;
}
static int sync_image(void *ctx) { (void)ctx; return fflush(image) || fsync(fileno(image))?-1:0; }
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};
static void reset_media(void) {
    assert(f_mount(NULL,"0:",0)==FR_OK); kui_media_set(&media);
}
static void mount(void) { reset_media(); assert(f_mount(&fs,"0:",1)==FR_OK); }
static void refresh(void) { reset_media(); assert(kui_storage_test_load_history(&history,NULL)); }
static void create(struct kui_storage_test_result *r) {
    memset(r,0,sizeof(*r)); kui_storage_test_defaults(&r->request);
    strcpy(r->request.card_label,"SD \"A\",\\test"); strcpy(r->metadata.build,"0123456789ab");
    r->metadata.transport=KUI_STORAGE_SCI; r->metadata.ui_hz=30;
    r->metadata.music_playing=true; r->metadata.local_seconds=1790850000;
    assert(kui_storage_test_begin(r,NULL));
    assert(r->id && !r->saved && r->outcome==KUI_STORAGE_TEST_RUNNING);
}
static void complete(struct kui_storage_test_result *r, unsigned outcome) {
    r->outcome=outcome; r->sample_count=1; r->elapsed_us=UINT64_C(100000000);
    r->written_bytes=r->verified_bytes=4*1048576u; r->cycles=1;
    strcpy(r->metadata.filesystem,"exFAT"); r->metadata.cluster_bytes=32768;
    r->metadata.free_bytes=UINT64_C(32000000000); r->metadata.volume_sectors=64000000;
    r->samples[0]=(struct kui_storage_test_sample){32768,1,4*1048576u,9000000,6000000,3000,true};
    r->read_latency=(struct kui_storage_test_latency){128,6000000,10,2,2000,200000,262144};
    strcpy(r->message,"Quoted \"pass\" \\ control\n\t\001");
    assert(kui_storage_test_save(r,NULL) && r->saved);
}
static void with_profile(struct kui_storage_test_result *r) {
    r->sci_profile=(struct kui_storage_test_sci_profile){
        .present=true,.rx_dma_blocks=UINT32_MAX,.tx_dma_blocks=90210,
        .polled_blocks=3,.dma_failures=2,.profiled_rx_blocks=17,.profiled_tx_blocks=18,
        .rx_setup_us=UINT64_C(5000000001),.rx_transfer_us=UINT64_C(5000000002),
        .rx_check_us=UINT64_C(5000000003),.tx_setup_us=UINT64_C(5000000004),
        .tx_transfer_us=UINT64_C(5000000005)
    };
}
static void write_file(const char *path, const void *bytes, UINT size) {
    FIL f; UINT done; assert(f_open(&f,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&f,bytes,size,&done)==FR_OK && done==size); assert(f_sync(&f)==FR_OK);
    assert(f_close(&f)==FR_OK);
}
static void mutate(const char *path, unsigned mode) {
    uint8_t bytes[1536]; FIL f; UINT done;
    mount(); assert(f_open(&f,path,FA_READ)==FR_OK);
    assert(f_read(&f,bytes,sizeof(bytes),&done)==FR_OK && done==sizeof(bytes)); assert(f_close(&f)==FR_OK);
    if(mode==0) bytes[30]^=1; /* Bad CRC. */
    if(mode==1) { /* Valid CRC but an unknown enum must still be rejected. */
        bytes[20]=99; uint32_t crc=kui_crc32(0,bytes,1532);
        for(unsigned i=0;i<4;++i) bytes[1532+i]=(uint8_t)(crc>>(8*i));
    }
    write_file(path,bytes,mode==2?200:sizeof(bytes)); reset_media();
}
static void export_file(const char *path, const char *folder, const char *name) {
    char destination[512]; snprintf(destination,sizeof(destination),"%s/%s",folder,name);
    FILE *out=fopen(destination,"wb"); assert(out); mount();
    FIL f; UINT n; char bytes[512]; assert(f_open(&f,path,FA_READ)==FR_OK);
    do { assert(f_read(&f,bytes,sizeof(bytes),&n)==FR_OK); assert(fwrite(bytes,1,n,out)==n); } while(n);
    assert(f_close(&f)==FR_OK); assert(!fclose(out)); reset_media();
}
static void regular_tests(const char *folder) {
    struct kui_storage_test_result r;
    refresh(); assert(!history.count && !history.baseline_valid);
    create(&r); assert(r.id==1); with_profile(&r); complete(&r,KUI_STORAGE_TEST_PASSED);
    export_file("0:/KUI/tests/t000001/result.json",folder,"result.json");
    export_file("0:/KUI/tests/t000001/result.csv",folder,"result.csv");
    assert(kui_storage_test_set_baseline(1,NULL));
    /* Saving again cannot overwrite an existing run or exports. */
    strcpy(r.message,"overwrite attempted"); assert(!kui_storage_test_save(&r,NULL) && !r.saved);
    refresh(); assert(history.count==1 && history.rows[0].saved && history.baseline_valid);
    assert(history.rows[0].metadata.free_bytes==UINT64_C(32000000000));
    assert(!strcmp(history.rows[0].message,"Quoted \"pass\" \\ control\n\t\001"));
    assert(!strcmp(history.baseline.request.card_label,"SD \"A\",\\test"));
    /* Live exports retain telemetry; the unchanged binary/history does not
     * invent profiling data when loading old or newly saved records. */
    assert(!history.rows[0].sci_profile.present && !history.baseline.sci_profile.present);
    assert(!history.rows[0].sci_profile.profiled_rx_blocks && !history.rows[0].sci_profile.rx_setup_us);
    create(&r); assert(r.id==2); complete(&r,KUI_STORAGE_TEST_STOPPED);
    export_file("0:/KUI/tests/t000002/result.json",folder,"unprofiled.json");
    assert(!kui_storage_test_set_baseline(2,NULL));
    create(&r); assert(r.id==3); r.outcome=KUI_STORAGE_TEST_FAILED;
    r.errors.total=1; r.errors.write_errors=1; r.errors.crc_errors=1;
    r.errors.last_operation=KUI_STORAGE_ERROR_WRITE; r.errors.last_result=KUI_STORAGE_ERROR_CRC;
    r.errors.last_transport=KUI_STORAGE_SCI; r.errors.last_lba=123456;
    r.errors.last_count=64; r.errors.sd_command=25; r.errors.sd_response=11; r.errors.sd_detail_valid=true;
    r.fatfs_error=FR_DISK_ERR; r.failure_offset=UINT64_C(123456789012);
    strcpy(r.failure_phase,"write"); strcpy(r.message,"hardware CRC failure");
    with_profile(&r);
    assert(kui_storage_test_save(&r,NULL)); assert(!kui_storage_test_set_baseline(3,NULL));
    export_file("0:/KUI/tests/t000003/result.json",folder,"failed-result.json");
    refresh(); assert(history.rows[0].errors.last_lba==123456 && history.rows[0].errors.sd_detail_valid);
    assert(history.rows[0].failure_offset==UINT64_C(123456789012));
    assert(!history.rows[0].sci_profile.present && !strcmp(history.rows[0].message,"hardware CRC failure"));
    create(&r); assert(r.id==4); /* Interrupted run has no result.bin. */
    refresh(); assert(history.rows[0].outcome==KUI_STORAGE_TEST_FAILED && !history.rows[0].saved);
    assert(!strcmp(history.rows[0].failure_phase,"interrupted")); assert(!kui_storage_test_set_baseline(4,NULL));
    for(unsigned id=5;id<=12;++id) { create(&r); assert(r.id==id); complete(&r,KUI_STORAGE_TEST_PASSED); }
    refresh(); assert(history.count==8 && history.rows[0].id==12 && history.rows[7].id==5);
    assert(history.baseline_valid && history.baseline.id==1); /* Older than visible history. */
    mutate("0:/KUI/tests/t000012/result.bin",0);
    mutate("0:/KUI/tests/t000011/result.bin",1);
    mutate("0:/KUI/tests/t000010/result.bin",2);
    refresh();
    for(unsigned i=0;i<3;++i) assert(history.rows[i].outcome==KUI_STORAGE_TEST_FAILED && !history.rows[i].saved);
    assert(history.rows[3].outcome==KUI_STORAGE_TEST_PASSED);
    assert(!kui_storage_test_set_baseline(12,NULL));
    assert(kui_storage_test_set_baseline(9,NULL));
    refresh(); assert(history.baseline.id==9);
    /* Corrupt newest baseline slot falls back to the older intact reference. */
    mount(); write_file("0:/KUI/tests/base-b.bin","torn",4); reset_media();
    refresh(); assert(history.baseline_valid && history.baseline.id==1);
    assert(kui_storage_test_set_baseline(8,NULL));
    /* A valid reference with a corrupt target also falls back. */
    mutate("0:/KUI/tests/t000008/result.bin",0);
    refresh(); assert(history.baseline_valid && history.baseline.id==1);
    assert(kui_storage_test_set_baseline(7,NULL));
    refresh(); assert(history.baseline.id==7);
    create(&r); assert(r.id==13); r.outcome=KUI_STORAGE_TEST_FAILED;
    strcpy(r.path,"0:/user-files"); assert(!kui_storage_test_save(&r,NULL));
    mount();
    FILINFO info; assert(f_stat("0:/user-files",&info)==FR_NO_FILE);
    /* A conflicting filename reserves that ID; folders are never reused. */
    write_file("0:/KUI/tests/t000099","user-owned",10);
    assert(f_mkdir("0:/KUI/tests/not-a-test")==FR_OK); reset_media();
    create(&r); assert(r.id==100);
    refresh(); assert(history.rows[0].id==100 && history.rows[0].outcome==KUI_STORAGE_TEST_FAILED);
    assert(history.baseline.id==7);
    /* FAT lookups are case insensitive; uppercase names also reserve IDs. */
    mount(); write_file("0:/KUI/tests/T000200","user-owned",10); reset_media();
    create(&r); assert(r.id==201);
    mount(); assert(f_stat("0:/KUI/tests/T000200",&info)==FR_OK && info.fsize==10); reset_media();
    /* These equal completed records differ only in ID and live telemetry.
     * The Python harness compares every other committed binary byte. */
    create(&r); assert(r.id==202); complete(&r,KUI_STORAGE_TEST_PASSED);
    export_file("0:/KUI/tests/t000202/result.bin",folder,"unprofiled.bin");
    create(&r); assert(r.id==203); with_profile(&r); complete(&r,KUI_STORAGE_TEST_PASSED);
    export_file("0:/KUI/tests/t000203/result.bin",folder,"profiled.bin");
}
static void fault_tests(const char *kind) {
    struct kui_storage_test_result r;
    create(&r); complete(&r,KUI_STORAGE_TEST_PASSED);
    assert(kui_storage_test_set_baseline(1,NULL));
    create(&r); complete(&r,KUI_STORAGE_TEST_PASSED);
    if(!strcmp(kind,"baseline-fail")) {
        fail_writes=true; assert(!kui_storage_test_set_baseline(2,NULL));
        fail_writes=false; refresh(); assert(history.baseline_valid && history.baseline.id==1);
    } else if(!strcmp(kind,"save-fail")) {
        create(&r); r.outcome=KUI_STORAGE_TEST_FAILED;
        fail_writes=true; assert(!kui_storage_test_save(&r,NULL) && !r.saved);
        fail_writes=false; refresh();
        assert(history.rows[0].id==3 && history.rows[0].outcome==KUI_STORAGE_TEST_FAILED && !history.rows[0].saved);
        assert(history.baseline.id==1);
    } else if(!strcmp(kind,"start-fail")) {
        memset(&r,0,sizeof(r)); kui_storage_test_defaults(&r.request);
        fail_writes=true; assert(!kui_storage_test_begin(&r,NULL)); fail_writes=false;
        assert(r.outcome==KUI_STORAGE_TEST_FAILED && r.fatfs_error && !strcmp(r.failure_phase,"save-start"));
        assert(!r.id && !r.path[0]); /* Failed mkdir never grants ownership. */
        refresh(); assert(history.baseline_valid && history.baseline.id==1);
    } else assert(!"unknown scenario");
}
int main(int argc,char **argv) {
    if(argc<3 || argc>4) return 2;
    struct stat st; if(lstat(argv[1],&st) || !S_ISREG(st.st_mode) || st.st_size%512) return 2;
    image=fopen(argv[1],"r+b"); if(!image) return 2;
    image_blocks=(uint64_t)st.st_size/512; kui_media_set(&media);
    if(argc==4) fault_tests(argv[3]); else regular_tests(argv[2]);
    reset_media(); assert(!fclose(image));
    puts("PASS storage test store: validated history, exports, interruption and baseline recovery");
    return 0;
}
