/* SPDX-License-Identifier: GPL-3.0-only */
#define KUI_VC2_EXFAT_LARGE_MAIN unused_witness_large_fixture
#include "test_vc2_exfat_large_ownership.c"
#include "kui/vmu_fs_witness.h"
#include "kui/retail_image.h"

static struct kui_vmu_fs_witness_capture capture;
static struct kui_vmu_fs_witness_record saved[KUI_VMU_FS_WITNESS_MAX_RECORDS];
static struct kui_vmu_fs_witness_binding binding;
static _Alignas(32) uint8_t scratch[KUI_VMU_FS_WITNESS_SCRATCH_BYTES];
static uint8_t header[512],pin[32];
static struct {unsigned reads,runs,wire_blocks,changed,failed,mutate_manifest;} device;

static uint32_t test_lba(const struct kui_vmu_fs_witness_record *r) {
    return (uint32_t)r->lba[0]|(uint32_t)r->lba[1]<<8|
        (uint32_t)r->lba[2]<<16|(uint32_t)r->lba[3]<<24;
}
static void synthetic_sector(uint32_t lba,uint8_t out[512]) {
    if(lba==777u) {
        memset(out,0,512u);
        if(device.changed) {out[0]=1u;out[508]=161u;out[509]=68u;out[510]=140u;out[511]=199u;}
        return;
    }
    for(unsigned i=0u;i<512u;i++)out[i]=(uint8_t)(lba+17u*i+(lba>>8));
    p32(out,lba);
    if(device.changed && device.changed==lba)out[511]^=1u;
}
static int synthetic_read(void *context,uint32_t lba,uint8_t out[512]) {
    assert(context==&device);++device.reads;
    if(device.failed && device.failed==lba)return -1;
    synthetic_sector(lba,out);++device.wire_blocks;return 0;
}
static int synthetic_run(void *context,uint32_t first,uint32_t count,uint8_t *out) {
    assert(context==&device && count && count<=32u && !((uintptr_t)out&31u));++device.runs;
    assert((uint64_t)first+count<=binding.card_sectors);
    for(unsigned i=0u;i<count;i++)if(synthetic_read(context,first+i,out+i*512u))return -1;
    if(device.mutate_manifest)capture.records[capture.count-1u].sha256[0]^=1u;
    return 0;
}
static void synthetic_begin(void) {
    memset(&device,0,sizeof(device));memset(&binding,0,sizeof(binding));
    binding.card_sectors=UINT64_C(0x100000000);binding.partition_first=16u;
    binding.partition_end=binding.card_sectors;binding.high_capacity=1u;binding.generation=7u;
    binding.cid[0]=1u;binding.image_id[0]=2u;binding.binding_sha256[0]=3u;
    assert(!kui_vmu_fs_witness_capture_begin(&capture,&binding,synthetic_read,&device));
}
static void observe(uint32_t lba) {
    uint8_t out[512];assert(!kui_vmu_fs_witness_capture_read(&capture,lba,out));
}
static size_t manifest_bytes(void) {return (size_t)capture.count*KUI_VMU_FS_WITNESS_RECORD_BYTES;}
static int replay(struct kui_vmu_fs_witness_stats *stats) {
    return kui_vmu_fs_witness_replay(header,capture.records,manifest_bytes(),&binding,pin,
        synthetic_run,&device,scratch,sizeof(scratch),stats);
}
static int witnessed_perf_run(void *context,uint32_t first,uint32_t count,uint8_t *out) {
    return perf_run(context,first,count,out);
}
static void synthetic_seal(void) {
    for(unsigned i=64u;i>0u;)observe(100u+--i);
    observe(0u);observe(1000u);observe(1003u);observe(1002u);observe(1002u);observe(100u);
    assert(capture.count==68u && capture.observed_reads==70u);
    assert(!kui_vmu_fs_witness_capture_finish(&capture,0,header,pin));
    uint32_t count=0u;assert(!kui_vmu_fs_witness_header_count(header,&count) && count==68u);
    assert(!kui_vmu_fs_witness_validate(header,capture.records,manifest_bytes(),&binding,pin));
    for(unsigned i=1u;i<capture.count;i++)assert(test_lba(&capture.records[i-1u])<test_lba(&capture.records[i]));
}
static void sorted_replay_and_refusals(void) {
    synthetic_begin();synthetic_seal();struct kui_vmu_fs_witness_stats stats;
    device.reads=device.runs=device.wire_blocks=0u;
    assert(!replay(&stats) && stats.commands==5u && stats.sectors==68u && stats.bad_lba==UINT32_MAX);
    assert(device.reads==68u && device.wire_blocks==68u && device.runs==5u);
    unsigned before=device.reads;
    assert(kui_vmu_fs_witness_validate(header,capture.records,manifest_bytes(),&binding,header+120u));
    assert(kui_vmu_fs_witness_replay(header,capture.records,manifest_bytes(),&binding,pin,
        synthetic_run,&device,scratch+1u,sizeof(scratch)-1u,&stats));
    assert(kui_vmu_fs_witness_replay(header,capture.records,manifest_bytes(),&binding,pin,
        synthetic_run,&device,scratch,sizeof(scratch)-1u,&stats));
    assert(device.reads==before);
    for(unsigned i=0u;i<512u;i++) {
        header[i]^=1u;assert(replay(&stats));header[i]^=1u;assert(device.reads==before);
    }
    for(unsigned i=0u;i<capture.count;i++) {
        capture.records[i].sha256[0]^=1u;assert(replay(&stats));
        capture.records[i].sha256[0]^=1u;assert(device.reads==before);
    }
    struct kui_vmu_fs_witness_binding wrong=binding;wrong.generation++;
    assert(kui_vmu_fs_witness_validate(header,capture.records,manifest_bytes(),&wrong,pin)==KUI_VMU_FS_WITNESS_BINDING);
    wrong=binding;wrong.cid[0]^=1u;
    assert(kui_vmu_fs_witness_validate(header,capture.records,manifest_bytes(),&wrong,pin));
    assert(kui_vmu_fs_witness_validate(header,capture.records,manifest_bytes()-1u,&binding,pin));
    memcpy(saved,capture.records,manifest_bytes());
    device.changed=163u;assert(replay(&stats)==KUI_VMU_FS_WITNESS_CHANGED && stats.bad_lba==163u);
    device.changed=0u;device.failed=132u;assert(replay(&stats)==KUI_VMU_FS_WITNESS_IO);
    device.failed=0u;device.mutate_manifest=1u;assert(replay(&stats));
    memcpy(capture.records,saved,manifest_bytes());device.mutate_manifest=0u;
    assert(!replay(&stats));
    puts("PASS witness sorted replay, independent live pin, bounds, header/body tampering and I/O faults");
}
static void strong_sector_digest(void) {
    synthetic_begin();observe(777u);uint8_t a[512],b[512];synthetic_sector(777u,a);
    device.changed=1u;synthetic_sector(777u,b);
    assert(memcmp(a,b,512u) && kui_retail_crc32(0u,a,512u)==kui_retail_crc32(0u,b,512u));
    observe(777u);assert(capture.result==KUI_VMU_FS_WITNESS_CHANGED && capture.count==1u);
    observe(778u);assert(device.reads==3u);
    assert(kui_vmu_fs_witness_capture_finish(&capture,0,header,pin)==KUI_VMU_FS_WITNESS_CHANGED);
    for(unsigned i=0u;i<512u;i++)assert(!header[i]);
    for(unsigned i=0u;i<32u;i++)assert(!pin[i]);
    synthetic_begin();observe(777u);assert(!kui_vmu_fs_witness_capture_finish(&capture,0,header,pin));
    device.changed=1u;struct kui_vmu_fs_witness_stats stats;
    assert(replay(&stats)==KUI_VMU_FS_WITNESS_CHANGED && stats.bad_lba==777u);
    puts("PASS SHA256 refuses different sector bytes with the same IEEE CRC32");
}
static void capacity_and_boundaries(void) {
    synthetic_begin();
    for(unsigned i=0u;i<KUI_VMU_FS_WITNESS_MAX_RECORDS;i++)observe(100u+i);
    observe(UINT32_MAX);observe(9000u);assert(capture.count==8192u && device.reads==8194u);
    assert(capture.result==KUI_VMU_FS_WITNESS_LIMIT);
    assert(kui_vmu_fs_witness_capture_finish(&capture,0,header,pin)==KUI_VMU_FS_WITNESS_LIMIT);
    for(unsigned i=0u;i<512u;i++)assert(!header[i]);
    synthetic_begin();observe(UINT32_MAX);observe(UINT32_MAX-1u);
    assert(!kui_vmu_fs_witness_capture_finish(&capture,0,header,pin));
    struct kui_vmu_fs_witness_stats stats;device.runs=0u;
    assert(!replay(&stats) && device.runs==1u && stats.sectors==2u);
    synthetic_begin();uint8_t out[512];assert(kui_vmu_fs_witness_capture_read(&capture,1u,out));
    assert(!device.reads);observe(100u);
    assert(kui_vmu_fs_witness_capture_finish(&capture,-1,header,pin)==KUI_VMU_FS_WITNESS_ADMISSION);
    synthetic_begin();device.failed=100u;assert(kui_vmu_fs_witness_capture_read(&capture,100u,out));
    assert(kui_vmu_fs_witness_capture_finish(&capture,-1,header,pin));
    puts("PASS witness capacity overflow preserves ordinary reads and never publishes truncated authority");
}
static void actual_ownership_parser(void) {
    perf_setup(0u);perf_collect("witness oracle",false);
    struct kui_vmu_fs_stats expected=perf.work.stats;
    binding=(struct kui_vmu_fs_witness_binding){.card_sectors=perf.expected.card_sectors,
        .partition_first=perf.expected.partition_first,.partition_end=perf.expected.partition_end,
        .high_capacity=perf.expected.high_capacity,.generation=19u};
    memcpy(binding.cid,perf.expected.cid,16u);memcpy(binding.image_id,perf.expected.image_id,16u);
    assert(!kui_vmu_persist_profile_ownership_sha256(&perf.expected,binding.binding_sha256));
    assert(!kui_vmu_fs_witness_capture_begin(&capture,&binding,perf_direct,NULL));
    perf_reset_counts();perf.input.read_sector=kui_vmu_fs_witness_capture_read;perf.input.context=&capture;
    int result=kui_vmu_fs_admit(&perf.input,&perf.expected,&perf.work);
    assert(!result && !memcmp(&expected,&perf.work.stats,sizeof(expected)));
    assert(!kui_vmu_fs_witness_capture_finish(&capture,result,header,pin));
    struct kui_vmu_fs_witness_stats stats;
    perf_reset_counts();
    assert(!kui_vmu_fs_witness_replay(header,capture.records,manifest_bytes(),&binding,pin,
        witnessed_perf_run,NULL,scratch,sizeof(scratch),&stats));
    assert(stats.sectors==capture.count && stats.sectors<capture.observed_reads);
    uint32_t source=perf_file_cluster(0u,0u),old=perf.fat[source];perf.fat[source]=20u;
    assert(kui_vmu_fs_witness_replay(header,capture.records,manifest_bytes(),&binding,pin,
        witnessed_perf_run,NULL,scratch,sizeof(scratch),&stats)==KUI_VMU_FS_WITNESS_CHANGED);
    perf.fat[source]=old;
    printf("PASS complete real exFAT ownership: %u logical reads/%u chain checks -> %u unique witnessed sectors; unrelated live-file FAT mutation refused\n",
        capture.observed_reads,expected.chain_steps,capture.count);
}
int main(void) {
    sorted_replay_and_refusals();strong_sector_digest();capacity_and_boundaries();actual_ownership_parser();return 0;
}
