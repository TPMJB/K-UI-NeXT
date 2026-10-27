/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact worker, immutable physical-only DMA, and real SD/store protocol. */
#define _POSIX_C_SOURCE 200809L
#define main readonly_worker_legacy_main
#include "test_vmu_maple_readonly_worker.c"
#undef main
#include "kui/vmu_durable.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include "vmu_sd_model.h"
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#if KUI_VMU_READONLY_PROOF != 4
#error This test requires the explicit persistent SD proof
#endif
#define PROFILE_TOKEN UINT32_C(0x8ce01000)
#define LEASE_TOKEN UINT32_C(0x8c006000)
static struct kui_vmu_durable_handoff handoff;
static bool handoff_missing,readback_corrupt;
static unsigned lease_calls,callback_calls;
static uint8_t saved_media[sizeof(sd.media)];
static const struct kui_vmu_maple_observe_snapshot *const snapshot=&kui_vmu_maple_observer_snapshot;

static uint8_t worker_model_transfer(void *context,uint8_t value,bool slow) {
    if(readback_corrupt && sd.frame_n==5u && (sd.frame[0]&63u)==17u && sd.writes>=4u) {
        uint32_t lba=(uint32_t)sd.frame[1]<<24|(uint32_t)sd.frame[2]<<16|
            (uint32_t)sd.frame[3]<<8|sd.frame[4];
        if(lba==image_lba(42u)) {sd.media[lba][33]^=1u;readback_corrupt=false;}
    }
    return transfer(context,value,slow);
}
void kui_vmu_test_sd_reset(void) {
    model_reset();handoff_missing=readback_corrupt=false;lease_calls=callback_calls=0u;
    for(unsigned block=0;block<256u;block++)
        assert(kui_vmu_image_blank_block((uint16_t)block,sd.media[image_lba(block)])==0);
    for(unsigned i=0;i<512u;i++) sd.media[image_lba(42u)][i]=(uint8_t)(i*7u+(i>>3)+0x31u);
    memset(&handoff,0,sizeof(handoff));
    handoff.magic=KUI_VMU_DURABLE_MAGIC;handoff.version=KUI_VMU_DURABLE_VERSION;
    handoff.bytes=sizeof(handoff);handoff.clean_checkpoint=KUI_VMU_DURABLE_CLEAN;
    handoff.card_sectors=1024u;handoff.high_capacity=1u;handoff.base_count=2u;handoff.journal_count=2u;
    memcpy(handoff.cid,sd.cid,16u);memcpy(handoff.image_id,store_config.image_id,16u);
    for(unsigned i=0;i<2u;i++) {
        handoff.base[i]=(struct kui_vmu_durable_extent){(uint32_t)image[i].first_lba,image[i].sectors};
        handoff.journal[i]=(struct kui_vmu_durable_extent){(uint32_t)journal[i].first_lba,journal[i].sectors};
    }
    struct kui_sha256 hash;kui_sha256_init(&hash);
    for(unsigned block=0;block<256u;block++) {
        uint8_t *bytes=sd.media[image_lba(block)];
        handoff.block_crc[block]=kui_retail_crc32(0,bytes,512u);kui_sha256_update(&hash,bytes,512u);
    }
    kui_sha256_digest(&hash,handoff.base_sha256);
    assert(kui_vmu_durable_handoff_seal(&handoff)==0);
    card.bus.transfer=worker_model_transfer;
    memcpy(saved_media,sd.media,sizeof(saved_media));
}
void kui_vmu_test_sd_configure(struct kui_vmu_maple_observe_config *config) {
    config->sd_lease=LEASE_TOKEN;config->sd_profile=PROFILE_TOKEN;config->sd_profile_bytes=sizeof(handoff);
}
const struct kui_vmu_durable_handoff *kui_vmu_maple_observer_test_durable_handoff(uint32_t token) {
    return token==PROFILE_TOKEN && !handoff_missing?&handoff:NULL;
}
int kui_vmu_maple_observer_test_sd_lease(kui_vmu_sd_transaction transaction,void *context) {
    assert(transaction==kui_vmu_maple_observer_sd_transaction && context);
    assert(!sd.owned && !sd.locked && !sd.selected && !sd.streaming);
    /* No storage operation can precede complete physical validation and
     * restoration of the owner's original MDSTAR. */
    assert(source==TX_BASE+bank*0x3000u && snapshot->chain_state==3u);
    assert(*field(0xfc0)==0u && *field(0x10b4)==(bank^1u) && *field(0x10b8)==(bank^1u));
    assert(!(kui_vmu_maple_observer_test_response(parent_rx)[2]&1u));
    ++lease_calls;
    if(!enter(&sd)) return KUI_VMU_SD_LEASE_BUSY;
    if(acquire(&sd)!=KUI_LOADER_SD_OK) {leave(&sd);return KUI_VMU_SD_LEASE_BUSY;}
    struct kui_loader_sd loan=card;++callback_calls;
    int result=transaction(context,&loan);
    assert(!sd.selected && !sd.streaming && !sd.write_phase);
    release(&sd);
    bool ok=!result && loan.ready && healthy(&sd);
    leave(&sd);
    if(!ok) {card.ready=false;return KUI_VMU_SD_LEASE_FAULT;}
    return KUI_VMU_SD_LEASE_OK;
}

static void filtered_chain(void) {
    assert(private_chain && source==0x0cfde000u);
    unsigned at=0,count=0,last=0;
    while(at<snapshot->chain_bytes) {
        uint32_t control=le32(private_chain+at),header=le32(private_chain+at+8u);
        unsigned address=(header>>8)&255u,command=header&255u,words=header>>24;
        assert((address&63u)==32u && address!=1u && !last);
        assert((command==1u || command==2u)?!words:command==9u && words==1u);
        if(command==9u) assert(le32(private_chain+at+12u)==0x01000000u);
        last=!!(control&0x80000000u);at+=12u+words*4u;++count;
    }
    assert(last && count==4u && at==snapshot->chain_bytes);
}
static void prepare_request(unsigned command,unsigned block,unsigned phase,const uint8_t *data) {
    build(snapshot->starts&1u,9u,command,command==12u?34u:2u,0x02000000u,(block<<24)|(phase<<8));
    if(command==12u) for(unsigned i=0;i<32u;i++) tx[bank][14u+i]=le32(data+phase*128u+i*4u);
    pre();assert(!stops && snapshot->completion_pending==5u);filtered_chain();
    physical_complete(9u);preserve();
}
static void finish_request(unsigned bytes) {
    complete();success_bytes(bytes);assert(!stops && snapshot->chain_state==3u);
    assert(source==TX_BASE+bank*0x3000u && !snapshot->chain_faults);
    assert(snapshot->chain_prepared==snapshot->chain_switched && snapshot->chain_switched==snapshot->chain_restored);
    assert(snapshot->proof_prepared==snapshot->proof_completed && !snapshot->proof_flags);
}
static void data_block(uint8_t data[512]) {
    for(unsigned i=0;i<512u;i++) data[i]=(uint8_t)(i*11u+(i>>3)+0x37u);
}
static void stage_four(const uint8_t data[512]) {
    unsigned leases=lease_calls;
    for(unsigned phase=0;phase<4u;phase++) {
        prepare_request(12u,42u,phase,data);finish_request(4u);
        assert(le32(kui_vmu_maple_observer_test_response(target_rx))==0x00010007u);
        assert(lease_calls==leases && !sd.writes);
    }
    assert(snapshot->ram_write_phases==4u && !snapshot->ram_commits);
}
static void only_mapped_writes(void) {
    for(unsigned lba=0;lba<1024u;lba++)
        if(lba!=image_lba(42u) && lba!=900u && lba!=950u)
            assert(!memcmp(saved_media+lba*512u,sd.media[lba],512u));
}
static void happy(unsigned unused) {
    (void)unused;uint8_t data[512];data_block(data);reset();
    /* The departed stage object is copied and may no longer exist. */
    memset(&handoff,0,sizeof(handoff));handoff_missing=true;
    prepare_request(11u,42u,0u,NULL);finish_request(524u);
    assert(!memcmp(kui_vmu_maple_observer_test_response(target_rx)+12u,sd.media[image_lba(42u)],512u));
    assert(lease_calls==1u && snapshot->ram_reads==1u);
    stage_four(data);
    prepare_request(13u,42u,4u,NULL);finish_request(4u);
    assert(lease_calls==2u && snapshot->ram_commits==1u && snapshot->proof_commit_attempts==1u);
    assert(!snapshot->ram_blocks && !snapshot->ram_full && !snapshot->ram_faults);
    assert(!memcmp(sd.media[image_lba(42u)],data,512u) && !memcmp(sd.media[900u],data,512u));
    assert(!memcmp(sd.media[950u],"KUIVMUJ1",8u) && sd.writes==4u);
    prepare_request(11u,42u,0u,NULL);finish_request(524u);
    assert(!memcmp(kui_vmu_maple_observer_test_response(target_rx)+12u,data,512u));
    assert(snapshot->ram_reads==2u && lease_calls==3u && callback_calls==3u);
    assert(sd.acquires==sd.releases && sd.enters==sd.leaves && card.ready);
    only_mapped_writes();
}
static void completion_guard(unsigned fault) {
    uint8_t data[512];data_block(data);reset();stage_four(data);
    prepare_request(13u,42u,4u,NULL);
    switch(fault) {
    case 0:private_chain[12u]^=1u;break;
    case 1:tx[bank][13u]^=1u;break;
    case 2:source=TX_BASE+bank*0x3000u;break;
    case 3:busy=1u;break;
    case 4:*field(0x10b4)^=1u;break;
    case 5:put32(kui_vmu_maple_observer_test_response(parent_rx),0x03210008u);break;
    case 6:put32(rx+bank*0x6000u+6u*1024u,0x03006008u);break;
    case 7:*field(0xfe8u+24u)^=32u;break;
    case 8:trigger=1u;break;
    case 9:lock_byte=0u;break;
    default:deny_mdstar_write=mdstar_writes+1u;break;
    }
    preserve();complete();unchanged();
    assert(stops==1u && !lease_calls && !sd.writes && !snapshot->ram_commits);
    assert(snapshot->ram_write_phases==4u && !snapshot->proof_commit_attempts);
    assert(!memcmp(saved_media,sd.media,sizeof(saved_media)));
}
static void write_capture_guard(unsigned unused) {
    (void)unused;uint8_t data[512];data_block(data);reset();prepare_request(12u,42u,0u,data);
    tx[bank][14u]^=1u;preserve();complete();unchanged();
    assert(stops==1u && !snapshot->ram_write_phases && !lease_calls && !sd.writes);
}
static void commit_fault(unsigned fault) {
    uint8_t data[512];data_block(data);reset();stage_four(data);
    prepare_request(13u,42u,4u,NULL);
    switch(fault) {
    case 0:sd.fail_release=true;break;
    case 1:readback_corrupt=true;break;
    case 2:sd.fail_write=3u;break;
    case 3:sd.fail_status=1u;break;
    case 4:sd.cid[0]^=1u;break;
    case 5:sd.deny_enter=true;break;
    case 6:sd.fail_read=1u;break;
    default:sd.media[950u][73]=1u;break;
    }
    complete();unchanged();
    assert(stops==1u && !snapshot->ram_commits);
    assert(snapshot->proof_commit_attempts==1u && snapshot->ram_write_phases==4u);
    assert(snapshot->proof_replies==4u && snapshot->proof_completed==4u);
    assert(lease_calls==1u && !sd.owned && !sd.locked);
    if(fault==0u) {
        assert(sd.writes==4u && !memcmp(sd.media[image_lba(42u)],data,512u));
        assert(!memcmp(sd.media[950u],"KUIVMUJ1",8u) && snapshot->ram_faults);
    } else if(fault==1u) {assert(!readback_corrupt && sd.writes==4u && snapshot->ram_faults);}
    else if(fault>=4u) assert(!sd.writes);
    unsigned wire_bytes=sd.bytes,calls=lease_calls;complete();unchanged();
    assert(sd.bytes==wire_bytes && lease_calls==calls);only_mapped_writes();
}
static void read_fault(unsigned fault) {
    reset();prepare_request(11u,42u,0u,NULL);
    switch(fault) {
    case 0:sd.media[image_lba(42u)][99]^=1u;break;
    case 1:sd.fail_release=true;break;
    case 2:sd.deny_enter=true;break;
    default:sd.fail_read=1u;break;
    }
    complete();unchanged();
    assert(stops==1u && !snapshot->proof_replies && !snapshot->proof_completed);
    assert(!snapshot->ram_reads && !snapshot->ram_commits && !sd.writes);
}
static void configuration_fault(unsigned fault) {
    struct kui_vmu_maple_observe_config config={
        KUI_VMU_MAPLE_OBSERVE_MAGIC,KUI_VMU_MAPLE_OBSERVE_VERSION,
        sizeof(config),100u,100u,{0},0,0,0};
    kui_vmu_test_sd_reset();kui_vmu_test_sd_configure(&config);
    switch(fault) {
    case 0:config.sd_profile_bytes--;break;
    case 1:handoff_missing=true;break;
    case 2:handoff.seal^=1u;break;
    case 3:handoff.clean_checkpoint=0u;break;
    case 4:handoff.base[1].lba=handoff.base[0].lba;break;
    case 5:config.sd_lease|=1u;break;
    case 6:config.sd_profile=0xace01000u;break;
    case 7:config.sd_profile=0x8cfcffe0u;break;
    case 8:config.sd_profile_bytes=1320u;break;
    default:config.sd_profile|=1u;break;
    }
    assert(kui_vmu_maple_observer_init(&config)!=0);
    assert(!snapshot->initialized && !lease_calls && !sd.bytes && !sd.writes);
}
static void isolated(void (*test)(unsigned),unsigned argument) {
    /* Each child models a distinct native worker lifetime, whose private BSS
     * starts zero. A started durable card is never reset or reinitialized. */
    pid_t child=fork();assert(child>=0);
    if(!child) {test(argument);exit(0);}
    int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
int main(void) {
    isolated(happy,0u);isolated(write_capture_guard,0u);
    for(unsigned fault=0;fault<11u;fault++) isolated(completion_guard,fault);
    for(unsigned fault=0;fault<8u;fault++) isolated(commit_fault,fault);
    for(unsigned fault=0;fault<4u;fault++) isolated(read_fault,fault);
    for(unsigned fault=0;fault<10u;fault++) isolated(configuration_fault,fault);
    puts("VMU durable worker: filtered DMA, immutable write capture, real-map durable ACK, restoration/readback/CRC faults and no-publication guards passed");
    return 0;
}
