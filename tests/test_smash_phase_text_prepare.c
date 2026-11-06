/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the real FatFs/media fixture and production capsule allocation guard.
 * No file/allocation operation is replaced in this test. */
#define main phase_text_original_capsule_io_suite
#include "test_smash_capsule_launch_io.c"
#undef main
#include "kui/game_launch_settings.h"
#include "kui/vc2_ce_vmu_persist.h"
#include "../src/apps/smash_menu_collection_files.inc"
struct files {char root[512];bool reader_open;};
static unsigned source_closes;
static bool close_failure,cancel_now;
static bool close_reader(struct files *files) {++source_closes;files->reader_open=false;return !close_failure;}
#include "../src/apps/smash_phase_text_prepare.inc"

static struct files source;
static struct kui_retail_manifest current_map;
static struct kui_vmu_persist_profile current_profile;
static struct smash_collection_files collection;
static uint8_t phase_page[4096],native_payload[128],trace[4096],saved_trace[4096];
static struct kui_runtime_image native;
static const uint8_t text_marker[16]={
    'K','U','I','S','T','X','T','1',1u,0u,0u,0u,16u,0u,0u,0u};
static bool phase_cancel(void) {return cancel_now;}
static bool prepare_trace(void) {
    return smash_phase_text_prepare(&source,&collection,&native,&problem,log_line,phase_cancel);
}
static void phase_fixture(void) {
    memset(native_payload,0x77,sizeof(native_payload));memcpy(native_payload+32u,text_marker,sizeof(text_marker));
    native=(struct kui_runtime_image){.data=native_payload,
        .info={.payload_bytes=sizeof(native_payload),.memory_bytes=sizeof(native_payload),.build="123456abcdef"}};
    /* The function uses the exact12-byte field, without reading a terminator. */
    native.info.build[12]='X';
    memset(&source,0,sizeof(source));strcpy(source.root,"/games/smash");source.reader_open=true;
    source_closes=0u;close_failure=cancel_now=false;
    current_map=(struct kui_retail_manifest){.partition_start=volume.start,
        .partition_end=(uint64_t)volume.start+volume.count};
    memset(&current_profile,0,sizeof(current_profile));
    collection=(struct smash_collection_files){.fs=&fs,.volume=&volume,.map=&current_map,
        .profile=&current_profile,.page=phase_page};
    CHECK(smash_collection_files_inspect(&collection,&problem,phase_cancel));
    for(unsigned i=0u;i<smash_collection_file_count(&collection);i++)CHECK(collection.allocated[i].blocks);
}
static void trace_read(struct kui_smash_menu_capsule_runs *allocated) {
    FIL file;UINT got=0u;struct kui_smash_menu_capsule_runs useful;
    CHECK(f_open(&file,SMASH_PHASE_TEXT_PATH,FA_READ)==FR_OK && f_size(&file)==4096u);
    CHECK(smash_menu_capsule_file_map(&file,&fs,&volume,4096u,&useful));
    CHECK(useful.blocks==8u && !useful.second_lba && useful.first_blocks==8u);
    if(allocated)CHECK(smash_menu_capsule_allocation_safe(&fs,&volume,&current_map,&current_profile,
        NULL,0u,allocated));
    CHECK(f_lseek(&file,0u)==FR_OK && f_read(&file,trace,sizeof(trace),&got)==FR_OK && got==sizeof(trace));
    CHECK(f_close(&file)==FR_OK);
}
static void trace_damage(void) {
    FIL file;UINT put=0u;uint8_t changed='!';
    CHECK(f_open(&file,SMASH_PHASE_TEXT_PATH,FA_WRITE)==FR_OK && f_lseek(&file,4094u)==FR_OK);
    CHECK(f_write(&file,&changed,1u,&put)==FR_OK && put==1u && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void no_write_refusal(void) {
    memcpy(saved_trace,trace,sizeof(trace));reset_counts();observing=true;
    CHECK(!prepare_trace());observing=false;CHECK(!writes && !write_sectors);
    trace_read(NULL);CHECK(!memcmp(trace,saved_trace,sizeof(trace)));
    verify_payloads();
}
static void trial_phase(BYTE kind,DWORD cluster) {
    wide=true;memset(payload_runs,0,sizeof(payload_runs));format_volume(kind,cluster);prepare_files();
    phase_fixture();reset_counts();observing=true;CHECK(prepare_trace());observing=false;
    CHECK(source_closes==1u && !source.reader_open && writes && write_sectors);
    struct kui_smash_menu_capsule_runs trace_allocated;
    trace_read(&trace_allocated);
    static const char prefix[]="KUI SMASH PHASE TRACE v1\nBUILD 123456abcdef\nPending: native measurement has not completed.\n";
    CHECK(!memcmp(trace,prefix,sizeof(prefix)-1u) && trace[4095u]=='\n');
    for(unsigned i=0u;i<4096u;i++)CHECK(trace[i]);
    for(unsigned i=0u;i<smash_collection_file_count(&collection);i++)CHECK(kui_smash_menu_capsule_runs_disjoint(&trace_allocated,&collection.allocated[i]));
    verify_payloads();
    /* Complete immutable comparison permits exact reuse, but stale native
     * measurements anywhere in the4096-byte file are reset and read back. */
    phase_fixture();reset_counts();observing=true;CHECK(prepare_trace());observing=false;CHECK(!writes);
    trace_damage();phase_fixture();reset_counts();observing=true;CHECK(prepare_trace());observing=false;CHECK(writes);
    trace_read(&trace_allocated);CHECK(trace[4094u]==' ');verify_payloads();
    for(unsigned i=0u;i<smash_collection_file_count(&collection);i++) {
        phase_fixture();collection.allocated[i]=trace_allocated;no_write_refusal();
    }
    phase_fixture();current_map.track_count=1u;current_map.extent_count=1u;
    current_map.slots[1].extent=(struct kui_retail_extent){.card_lba=trace_allocated.first_lba,.blocks=1u};
    no_write_refusal();
    phase_fixture();current_profile.files[0].allocated_count=1u;
    current_profile.files[0].allocated[0]=(struct kui_vmu_persist_extent){.lba=trace_allocated.first_lba,.blocks=1u};
    no_write_refusal();
    phase_fixture();memcpy(native_payload+64u,text_marker,sizeof(text_marker));no_write_refusal();CHECK(!source_closes);
    phase_fixture();memset(native_payload+32u,0x77,sizeof(text_marker));memcpy(native_payload+33u,text_marker,sizeof(text_marker));
    no_write_refusal();CHECK(!source_closes);
    phase_fixture();native.info.build[6]='\n';no_write_refusal();CHECK(!source_closes);
    phase_fixture();strcpy(source.root,"/KUI/apps/GAMES");no_write_refusal();CHECK(!source_closes);
    phase_fixture();strcpy(source.root,"../games");no_write_refusal();CHECK(!source_closes);
    phase_fixture();memset(&collection.allocated[5],0,sizeof(collection.allocated[5]));no_write_refusal();CHECK(!source_closes);
    phase_fixture();close_failure=true;no_write_refusal();CHECK(source_closes==1u);
    phase_fixture();cancel_now=true;no_write_refusal();CHECK(!source_closes);
    phase_fixture();memset(native_payload,0x77,sizeof(native_payload));memcpy(native_payload,"KUISTXT1",8u);
    reset_counts();observing=true;CHECK(prepare_trace());observing=false;CHECK(!source_closes && !reads && !writes);
    phase_fixture();native_payload[44u]=17u;reset_counts();observing=true;CHECK(prepare_trace());observing=false;
    CHECK(!source_closes && !reads && !writes);
    /* Force the real allocator to choose a known free extent, then declare
     * that prospective extent forbidden in each of the six positions. The
     * allocation can create metadata, but not a single payload sector may be
     * written before the production post-allocation guard rejects it. */
    DWORD first_cluster=(DWORD)((trace_allocated.first_lba-volume.start-fs.database)/fs.csize+2u);
    for(unsigned i=0u;i<smash_collection_file_count(&collection);i++) {
        CHECK(f_unlink(SMASH_PHASE_TEXT_PATH)==FR_OK);phase_fixture();
        collection.allocated[i]=trace_allocated;fs.last_clst=first_cluster;
        memset(payload_runs,0,sizeof(payload_runs));payload_runs[0]=trace_allocated;
        reset_counts();observing=true;CHECK(!prepare_trace());observing=false;
        CHECK(source_closes==1u && !payload_write_sectors);
        FIL file;struct kui_smash_menu_capsule_runs selected;
        CHECK(f_open(&file,SMASH_PHASE_TEXT_PATH,FA_READ)==FR_OK);
        CHECK(smash_menu_capsule_file_map(&file,&fs,&volume,4096u,&selected));
        CHECK(selected.first_lba==trace_allocated.first_lba && f_close(&file)==FR_OK);
        verify_payloads();
    }
    unmount_volume();free(disk);disk=NULL;
    printf("PASS real %s/%u phase TXT: fixed4096 reset/reuse/readback, six old/new capsule guards, VMU/GDI, typed record and source-path refusals\n",
        kind==FM_FAT32?"FAT32":"exFAT",(unsigned)cluster);
}
int main(void) {
    for(unsigned i=0u;i<FILES;i++) {
        payload[i]=malloc(sizes[i]);CHECK(payload[i]);
        for(uint32_t at=0u;at<sizes[i];at++)payload[i][at]=(uint8_t)((at*37u)^(at>>7)^(i*0x5bu));
    }
    trial_phase(FM_FAT32,512u);trial_phase(FM_EXFAT,4096u);trial_phase(FM_EXFAT,131072u);
    for(unsigned i=0u;i<FILES;i++)free(payload[i]);
    return 0;
}
