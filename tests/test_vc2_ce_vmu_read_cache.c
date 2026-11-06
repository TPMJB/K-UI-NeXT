/* SPDX-License-Identifier: GPL-3.0-only */
/* Production runtime/cache and deferred mailbox. The separately tested SD
 * provider is replaced only at its verified-success/BUSY/fault boundary. */
#include "../src/loader/armada_ce_vmu_runtime.c"
#include <assert.h>
#include <stdio.h>

#define OWNER UINT32_C(0x00030a60)
static uint32_t sr;
static uintptr_t sp;
static bool owner_valid;
static unsigned owner_checks,read_calls,commit_calls,busy_calls;
static bool provider_fault,commit_released;
static uint8_t disk[256][512];
struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;

uint32_t kui_armada_ce_vmu_runtime_test_sr(void) {return sr;}
void kui_armada_ce_vmu_runtime_test_set_sr(uint32_t value) {sr=value;}
uintptr_t kui_armada_ce_vmu_runtime_test_sp(void) {return sp;}
bool kui_armada_ce_maple_runtime_owner(uint32_t owner) {
    assert(sr==UINT32_C(0x400000f0) && owner==OWNER);++owner_checks;return owner_valid;
}
static void provider_context(void *context,uint16_t block,const void *buffer) {
    assert(context==&runtime.sd && block<256u && buffer==runtime.queue.bytes);
    assert(sr==UINT32_C(0x40000000) && runtime.worker && !runtime.handling);
    assert(runtime.queue.state==KUI_VMU_DEFERRED_RUNNING && runtime.queue.block==block);
    assert(runtime_worker_ready(&runtime));
}
int kui_armada_ce_vmu_sd_read(void *context,uint16_t block,uint8_t out[512]) {
    provider_context(context,block,out);++read_calls;
    if(busy_calls) {--busy_calls;return KUI_VMU_EMU_IO_BUSY;}
    if(provider_fault) {runtime.sd.faulted=1u;return KUI_VMU_EMU_IO_ERROR;}
    assert(kui_retail_crc32(0u,disk[block],512u)==runtime.sd.handoff.block_crc[block]);
    memcpy(out,disk[block],512u);return KUI_VMU_EMU_IO_OK;
}
int kui_armada_ce_vmu_sd_commit(void *context,uint16_t block,const uint8_t in[512]) {
    provider_context(context,block,in);++commit_calls;
    if(busy_calls) {--busy_calls;return KUI_VMU_EMU_IO_BUSY;}
    if(provider_fault) {runtime.sd.faulted=1u;return KUI_VMU_EMU_IO_ERROR;}
    /* The cache cannot change until the actual provider has returned its
     * durable, verified and released result. */
    assert(!commit_released);
    memcpy(disk[block],in,512u);
    runtime.sd.handoff.block_crc[block]=kui_retail_crc32(0u,in,512u);
    commit_released=true;return KUI_VMU_EMU_IO_OK;
}
static void reset(void) {
    memset(&runtime,0,sizeof(runtime));memset(&kui_armada_ce_vmu_a2_log,0,sizeof(kui_armada_ce_vmu_a2_log));
    sr=UINT32_C(0x400000f0);sp=KUI_ARMADA_CE_VMU_WORKER_STACK_TOP-128u;
    owner_valid=true;owner_checks=read_calls=commit_calls=busy_calls=0u;
    provider_fault=commit_released=false;runtime.started=runtime.ready=1u;runtime.owner=OWNER;
    runtime.sd.started=1u;runtime.sd.self=&runtime.sd;
    for(unsigned block=0u;block<256u;block++) {
        for(unsigned i=0u;i<512u;i++)disk[block][i]=(uint8_t)(block*17u+i*31u);
        runtime.sd.handoff.block_crc[block]=kui_retail_crc32(0u,disk[block],512u);
    }
    const struct kui_vmu_deferred_guard guard={&runtime,runtime_guard_enter,runtime_guard_leave,runtime_worker_ready};
    const struct kui_vmu_emulation_io provider={&runtime,runtime_cached_read,runtime_cached_commit};
    assert(!kui_vmu_deferred_io_init(&runtime.queue,&provider,&guard));
}
static int work(void) {
    sr=UINT32_C(0x40000000);int result=kui_armada_ce_vmu_runtime_work(OWNER);
    assert(sr==UINT32_C(0x40000000) && !runtime.worker);sr|=UINT32_C(0xf0);return result;
}
static void read_block(unsigned block) {
    uint8_t out[512];memset(out,0xa5,sizeof(out));
    unsigned before=read_calls;
    assert(kui_vmu_deferred_io_read(&runtime.queue,(uint16_t)block,out)==KUI_VMU_EMU_IO_BUSY);
    assert(read_calls==before && out[0]==0xa5u); /* A hit is never a masked shortcut. */
    assert(work()==1);
    assert(kui_vmu_deferred_io_read(&runtime.queue,(uint16_t)block,out)==KUI_VMU_EMU_IO_OK);
    assert(!memcmp(out,disk[block],512u));
}
static void collisions_and_hits(void) {
    reset();for(unsigned block=0u;block<8u;block++)read_block(block);
    assert(read_calls==8u);
    unsigned checks=owner_checks;
    for(unsigned block=0u;block<8u;block++)read_block(block);
    assert(read_calls==8u && owner_checks>checks);
    read_block(8u);assert(read_calls==9u);read_block(1u);assert(read_calls==9u);
    read_block(0u);assert(read_calls==10u);
    uint8_t out[512];assert(runtime_cached_read(&runtime,256u,out)==KUI_VMU_EMU_IO_ERROR);
}
static void busy_and_commit(void) {
    reset();uint8_t out[512],data[512];memset(out,0xa5,sizeof(out));memset(data,0x57,sizeof(data));
    busy_calls=1u;
    assert(kui_vmu_deferred_io_read(&runtime.queue,1u,out)==KUI_VMU_EMU_IO_BUSY);
    assert(work()==0 && !runtime.read_tag[1] && runtime.queue.state==KUI_VMU_DEFERRED_PENDING);
    assert(out[0]==0xa5u && work()==1);
    assert(kui_vmu_deferred_io_read(&runtime.queue,1u,out)==KUI_VMU_EMU_IO_OK);
    assert(runtime.read_tag[1]==2u && read_calls==2u);
    uint8_t old[512];memcpy(old,disk[1],512u);busy_calls=1u;
    assert(kui_vmu_deferred_io_commit(&runtime.queue,1u,data)==KUI_VMU_EMU_IO_BUSY);
    assert(!commit_calls && runtime.read_tag[1]==2u);
    assert(work()==0 && runtime.read_tag[1]==2u && !memcmp(old,disk[1],512u));
    assert(!commit_released && work()==1 && commit_released && !runtime.read_tag[1]);
    assert(kui_vmu_deferred_io_commit(&runtime.queue,1u,data)==KUI_VMU_EMU_IO_OK);
    read_block(1u);assert(read_calls==3u && !memcmp(disk[1],data,512u));
    read_block(1u);assert(read_calls==3u);
}
static void hit_admission(void) {
    reset();read_block(1u);uint8_t other[512];memset(other,0xa5,sizeof(other));
    runtime.worker=1u;sr=UINT32_C(0x40000000);
    assert(runtime_cached_read(NULL,1u,runtime.queue.bytes)==KUI_VMU_EMU_IO_ERROR);
    assert(runtime_cached_read(&runtime,1u,other)==KUI_VMU_EMU_IO_ERROR && other[0]==0xa5u);
    sr|=UINT32_C(0xf0);
    assert(runtime_cached_read(&runtime,1u,runtime.queue.bytes)==KUI_VMU_EMU_IO_ERROR);
    sr=UINT32_C(0x50000000);
    assert(runtime_cached_read(&runtime,1u,runtime.queue.bytes)==KUI_VMU_EMU_IO_ERROR);
    sr=UINT32_C(0x40000000);sp=0u;
    assert(runtime_cached_read(&runtime,1u,runtime.queue.bytes)==KUI_VMU_EMU_IO_ERROR);
    sp=KUI_ARMADA_CE_VMU_WORKER_STACK_TOP-128u;owner_valid=false;
    assert(runtime_cached_read(&runtime,1u,runtime.queue.bytes)==KUI_VMU_EMU_IO_ERROR);
    assert(read_calls==1u);owner_valid=true;runtime.worker=0u;sr|=UINT32_C(0xf0);
    assert(kui_vmu_deferred_io_read(&runtime.queue,1u,other)==KUI_VMU_EMU_IO_BUSY);
    owner_valid=false;assert(work()==-2 && runtime.queue.state==KUI_VMU_DEFERRED_PENDING && read_calls==1u);
    owner_valid=true;assert(work()==1 && read_calls==1u);
    assert(kui_vmu_deferred_io_read(&runtime.queue,1u,other)==KUI_VMU_EMU_IO_OK);
}
static void cache_fault(void) {
    reset();read_block(1u);runtime.read_cache[1][17]^=1u;uint8_t out[512];memset(out,0xa5,sizeof(out));
    assert(kui_vmu_deferred_io_read(&runtime.queue,1u,out)==KUI_VMU_EMU_IO_BUSY);
    assert(work()==-1 && runtime.faulted && runtime.queue.state==KUI_VMU_DEFERRED_FAULT);
    assert(read_calls==1u && kui_armada_ce_vmu_a2_log.fault[0]==113u);
    assert(kui_vmu_deferred_io_read(&runtime.queue,1u,out)==KUI_VMU_EMU_IO_ERROR && out[0]==0xa5u);
}
static void provider_faults(void) {
    reset();read_block(1u);provider_fault=true;uint8_t out[512];memset(out,0xa5,sizeof(out));
    assert(kui_vmu_deferred_io_read(&runtime.queue,2u,out)==KUI_VMU_EMU_IO_BUSY);
    assert(work()==-1 && runtime.sd.faulted && !runtime.read_tag[2]);
    runtime.worker=1u;sr=UINT32_C(0x40000000);
    assert(runtime_cached_read(&runtime,1u,runtime.queue.bytes)==KUI_VMU_EMU_IO_ERROR);
    assert(read_calls==2u && out[0]==0xa5u);
    reset();read_block(1u);uint8_t data[512];memset(data,0x79,sizeof(data));provider_fault=true;
    assert(kui_vmu_deferred_io_commit(&runtime.queue,1u,data)==KUI_VMU_EMU_IO_BUSY);
    assert(work()==-1 && runtime.sd.faulted && runtime.read_tag[1]==2u);
    assert(kui_vmu_deferred_io_commit(&runtime.queue,1u,data)==KUI_VMU_EMU_IO_ERROR && !commit_released);
    runtime.worker=1u;sr=UINT32_C(0x40000000);
    assert(runtime_cached_read(&runtime,1u,runtime.queue.bytes)==KUI_VMU_EMU_IO_ERROR);
}
int main(void) {
    collisions_and_hits();busy_and_commit();hit_admission();cache_fault();provider_faults();
    puts("PASS production VC2 eight-block cache: deferred-only hits, owner/context/CRC guards, collision refill, BUSY preservation, durable commit invalidation and provider quarantine");
    return 0;
}
