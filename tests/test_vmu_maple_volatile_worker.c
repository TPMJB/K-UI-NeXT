/* SPDX-License-Identifier: GPL-3.0-only */
/* Real worker, filtered hardware schedule, RAM protocol and read-only SPI base. */
#define KUI_VMU_SD_WORKER_ENTRY sd_model_legacy_main
#include "test_vmu_maple_sd_worker.c"
#include "kui/vmu_volatile_chain.h"

#if KUI_VMU_READONLY_PROOF != 3
#error This test requires the explicit volatile RAM proof
#endif

static uint8_t saved_media[sizeof(sd.media)];
static const struct kui_vmu_maple_observe_snapshot *const snapshot=&kui_vmu_maple_observer_snapshot;

static void filtered_chain(unsigned physical_packets) {
    assert(private_chain && source==0x0cfde000u && mdstar_writes==snapshot->chain_switched+snapshot->chain_restored);
    unsigned at=0u,count=0u,last=0u;
    while(at<snapshot->chain_bytes) {
        uint32_t c=le32(private_chain+at),h=le32(private_chain+at+8u);
        unsigned destination=(h>>8)&255u,command=h&255u,words=h>>24;
        assert((destination&63u)==32u && destination!=1u);
        assert((command==1u || command==2u)?words==0u:command==9u && words==1u);
        if(command==9u) assert(le32(private_chain+at+12u)==0x01000000u);
        assert(!last);last=!!(c&0x80000000u);at+=12u+words*4u;++count;
    }
    assert(last && count==physical_packets && at==snapshot->chain_bytes);
}

static void prepare_request(unsigned command,unsigned block,unsigned phase,const uint8_t *data) {
    build(snapshot->starts&1u,9u,command,command==12u?34u:2u,
        0x02000000u,(block<<24)|(phase<<8));
    if(command==12u) {
        /* MAIN GETCOND takes four words, then five 0700 padding words;
         * virtual payload starts after its descriptor and function/location. */
        assert(tx[bank][9u+2u]==0x2200010cu);
        for(unsigned i=0;i<32u;i++) tx[bank][14u+i]=le32(data+phase*128u+i*4u);
    }
    pre();assert(!stops && snapshot->completion_pending==5u);
    filtered_chain(4u);
    assert(source!=TX_BASE+bank*0x3000u && snapshot->chain_state==2u);
    assert(*field(0x10b0)==((TX_BASE+bank*0x3000u)|0xa0000000u));
    assert(*field(0x10b4)==bank && *field(0x10b8)==bank && *field(0xfc0)==1u);
    physical_complete(9u);preserve();
}

static void finish_request(unsigned response_bytes) {
    complete();success_bytes(response_bytes);assert(!stops);
    assert(source==TX_BASE+bank*0x3000u && snapshot->chain_state==3u);
    assert(snapshot->chain_prepared==snapshot->chain_switched &&
        snapshot->chain_switched==snapshot->chain_restored && !snapshot->chain_faults);
    assert(snapshot->proof_prepared==snapshot->proof_completed && !snapshot->proof_flags);
    assert(!snapshot->proof_commit_attempts && !memcmp(saved_media,sd.media,sizeof(saved_media)));
}

static void commit_block(unsigned block,const uint8_t data[512]) {
    for(unsigned phase=0;phase<4u;phase++) {
        prepare_request(12u,block,phase,data);finish_request(4u);
        assert(le32(kui_vmu_maple_observer_test_response(target_rx))==0x00010007u);
    }
    prepare_request(13u,block,4u,NULL);finish_request(4u);
    assert(le32(kui_vmu_maple_observer_test_response(target_rx))==0x00010007u);
}

static void filtered_writes_and_ram_load(void) {
    uint8_t data[512];for(unsigned i=0;i<512u;i++) data[i]=(uint8_t)(i*11u+(i>>3)+0x37u);
    reset();memcpy(saved_media,sd.media,sizeof(saved_media));
    build(0u,1u,0u,0u,0u,0u);pre();assert(!stops);filtered_chain(4u);
    physical_complete(1u);preserve();complete();success_bytes(0u);
    assert(!stops && source==TX_BASE && snapshot->chain_restored==1u && !snapshot->proof_replies);
    prepare_request(11u,42u,0u,NULL);finish_request(524u);
    assert(sd.reads==1u && snapshot->sd_cid_checks==1u);
    assert(!memcmp(kui_vmu_maple_observer_test_response(target_rx)+12u,sd.media[142u],512u));
    commit_block(42u,data);
    assert(snapshot->ram_write_phases==4u && snapshot->ram_commits==1u && snapshot->ram_blocks==1u);
    prepare_request(11u,42u,0u,NULL);finish_request(524u);
    assert(!memcmp(kui_vmu_maple_observer_test_response(target_rx)+12u,data,512u));
    assert(snapshot->ram_reads==1u && sd.reads==1u && sd.cids==1u);
    prepare_request(13u,42u,1u,NULL);finish_request(4u);
    /* A new worker lifetime discards RAM changes and sees the immutable base. */
    reset();memcpy(saved_media,sd.media,sizeof(saved_media));
    prepare_request(11u,42u,0u,NULL);finish_request(524u);
    assert(!snapshot->ram_commits && !snapshot->ram_blocks && !snapshot->ram_reads && sd.reads==1u);
    assert(!memcmp(kui_vmu_maple_observer_test_response(target_rx)+12u,sd.media[142u],512u));
}

static void failed_completion_preserves_rx_and_ram(void) {
    uint8_t data[512];memset(data,0x73,sizeof(data));
    for(unsigned fault=0;fault<12u;fault++) {
        reset();memcpy(saved_media,sd.media,sizeof(saved_media));
        prepare_request(12u,42u,0u,data);
        switch(fault) {
        case 0:private_chain[12u]^=1u;break;
        case 1:tx[bank][14u]^=1u;break; /* Full source payload fingerprint. */
        case 2:source=TX_BASE+bank*0x3000u;break;
        case 3:busy=1u;break;
        case 4:*field(0x10b4)^=1u;break;
        case 5:put32(kui_vmu_maple_observer_test_response(parent_rx),0x03210008u);break;
        case 6:put32(kui_vmu_maple_observer_test_response(parent_rx),0x03200005u);break;
        case 7:put32(rx+bank*0x6000u+6u*1024u,0x03006008u);break; /* B wrong DEVINFO. */
        case 8:*field(0xfe8u+24u)^=32u;break;
        case 9:enable=0u;break;
        case 10:lock_byte=0u;break;
        default:deny_mdstar_write=2u;break;
        }
        preserve();complete();unchanged();
        assert(stops==1u && !snapshot->ram_write_phases && !snapshot->ram_commits && !snapshot->ram_blocks);
        assert(!snapshot->proof_replies && !snapshot->proof_completed && !sd.lease_calls);
        assert(!memcmp(saved_media,sd.media,sizeof(saved_media)));
        unsigned writes=mdstar_writes;complete();unchanged();assert(mdstar_writes==writes);
    }
    /* No final D commit is permitted until every physical result passes. */
    reset();memcpy(saved_media,sd.media,sizeof(saved_media));
    for(unsigned phase=0;phase<4u;phase++) {
        prepare_request(12u,42u,phase,data);finish_request(4u);
    }
    prepare_request(13u,42u,4u,NULL);
    put32(kui_vmu_maple_observer_test_response(parent_rx),0x03210008u);preserve();complete();unchanged();
    assert(stops==1u && snapshot->ram_write_phases==4u && !snapshot->ram_commits && !snapshot->ram_blocks);
}

static void strict_physical_whitelist_and_stale_virtual_slot(void) {
    reset();memcpy(saved_media,sd.media,sizeof(saved_media));
    build(0u,9u,1u,0u,0u,0u);
    /* A physical B GETCOND must survive with its function word intact. */
    source=TX_BASE;tx_words=0u;*field(0x10b0)=source|0xa0000000u;
    packet(0u,32u,9u,1u,0x01000000u,0u);
    packet(0u,1u,1u,0u,0u,0u);
    packet(1u,32u,9u,1u,0x01000000u,0u);
    tx[bank][last_control]|=0x80000000u;
    memset(kui_vmu_maple_observer_test_response(target_rx),0x6b,1024u);
    pre();assert(!stops);filtered_chain(2u);physical_complete(9u);
    put32(rx+6u*1024u,0x03604008u);put32(rx+6u*1024u+4u,0x01000000u);
    preserve();complete();success_bytes(116u);assert(!stops && snapshot->chain_restored==1u);
    /* A physical command outside MAIN controller discovery/polling stops
     * before MDSTAR is changed, even with an otherwise allowed A1 request. */
    reset();build(0u,9u,11u,2u,0x02000000u,42u<<24);
    tx[bank][16u]=(0x40u<<16)|(0x60u<<8)|12u;
    pre();assert(stops==1u && !mdstar_writes && !snapshot->ram_write_phases);
    /* A controller parent is mandatory, so an all-virtual batch never starts. */
    reset();build(0u,9u,0u,0u,0u,0u);tx_words=0u;
    packet(0u,1u,1u,0u,0u,0u);tx[bank][last_control]|=0x80000000u;
    pre();assert(stops==1u && !mdstar_writes && !snapshot->chain_prepared);
    /* Physical subunits other than virtual A1 remain outside this proof. */
    reset();build(0u,9u,0u,0u,0u,0u);tx_words=0u;
    packet(0u,32u,9u,1u,0x01000000u,0u);packet(1u,1u,1u,0u,0u,0u);
    tx[bank][last_control]|=0x80000000u;
    pre();assert(stops==1u && !mdstar_writes && !snapshot->chain_prepared);
    reset();build(0u,9u,12u,34u,0x02000000u,42u<<24);deny_mdstar_write=1u;
    pre();assert(stops==1u && mdstar_writes==1u && snapshot->chain_faults==1u && !snapshot->ram_write_phases);
}

static void full_cache_refuses_before_dma(void) {
    uint8_t data[512];memset(data,0x39,sizeof(data));
    reset();memcpy(saved_media,sd.media,sizeof(saved_media));
    /* The normal 100-schedule harness limit leaves room for 16 five-packet
     * commits and the bounded 17th-block refusal. */
    for(unsigned block=0;block<16u;block++) commit_block(block,data);
    assert(snapshot->ram_blocks==16u && snapshot->ram_commits==16u);
    unsigned switches=snapshot->chain_switched,writes=mdstar_writes;
    build(0u,9u,12u,34u,0x02000000u,16u<<24);pre();
    assert(stops==1u && snapshot->ram_full==1u && snapshot->ram_blocks==16u);
    assert(snapshot->chain_switched==switches && mdstar_writes==writes);
    assert(!memcmp(saved_media,sd.media,sizeof(saved_media)));
}

int main(void) {
    filtered_writes_and_ram_load();failed_completion_preserves_rx_and_ram();
    strict_physical_whitelist_and_stale_virtual_slot();
    full_cache_refuses_before_dma();
    puts("VMU volatile worker: private physical-only DMA, original source restore, RAM save/load, immutable SD and fail-closed completion guards passed");
    return 0;
}
