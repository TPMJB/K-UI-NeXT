/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact Silver banks/manager plus real durable worker/protocol/SPI storage.
 * All requests, physical replies, and media bytes are independently authored. */
#define _POSIX_C_SOURCE 200809L
#include "kui/vmu_maple_observe.h"
#include "kui/vmu_readonly_overlay.h"
#include "kui/vmu_image.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if KUI_VMU_READONLY_PROOF == 4
void kui_vmu_test_sd_reset(void);
void kui_vmu_test_sd_configure(struct kui_vmu_maple_observe_config *);
#endif

#define MANAGER UINT32_C(0x8c290340)
#define RX UINT32_C(0x0c1bd920)
#define TX UINT32_C(0x0c1c9940)
#define P2 UINT32_C(0xa0000000)
#define RETURN_PR UINT32_C(0x8c12b7a6)

static uint8_t rx[48u*1024u],rx_before[sizeof(rx)];
static uint32_t tx[2][0x3000u/4u],tx_before[2][0x3000u/4u],sdk[0x10f8u/4u];
static uint32_t source,manager,lock_pointer,lock_byte,gun,trigger,enable,busy;
static uint32_t timer1_callback,timer2_callback;
static uint8_t *private_chain;
static unsigned mdstar_writes,deny_mdstar_write;
static unsigned active_mask=6u;
uint32_t kui_vmu_maple_observer_test_private_chain(uint8_t *bytes,size_t capacity) {
    assert(bytes && !((uintptr_t)bytes&31u) && capacity==512u);
    private_chain=bytes;return UINT32_C(0x0cfde000);
}
void kui_vmu_maple_observer_test_write(uint32_t address,uint32_t value) {
    assert(address==UINT32_C(0xa05f6c04));++mdstar_writes;
    if(mdstar_writes!=deny_mdstar_write) source=value;
}
static uint32_t parent_rx,target_rx,frame_rx[32];
static unsigned bank,stops,rx_reads,table_reads,manager_reads,tx_words,last_control;
static unsigned frame_count,target_payload_begin,target_payload_words,forbid_target_payload;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static uint32_t *field(unsigned offset) {
    assert(!(offset&3u) && offset<sizeof(sdk));return &sdk[offset/4u];
}

uint8_t *kui_vmu_maple_observer_test_response(uint32_t physical) {
    if(physical<RX || physical-RX>=sizeof(rx) || (physical-RX)%1024u) return NULL;
    return rx+physical-RX;
}

uint32_t kui_vmu_maple_observer_test_read(uint32_t address) {
    switch(address) {
    case 0xa05f6c04u:return source;
    case 0xa05f6c10u:return trigger;
    case 0xa05f6c14u:return enable;
    case 0xa05f6c18u:return busy;
    case 0x8c29143cu:return manager;
    case 0x8c11cd24u:return lock_pointer;
    case 0xac11cd28u:return lock_byte;
    case 0x8c25972cu:return gun;
    case 0x8c238aacu:return timer1_callback;
    case 0x8c238ab4u:return timer2_callback;
    default:break;
    }
    if(address>=MANAGER && address<MANAGER+sizeof(sdk)) {
        assert(!(address&3u));++manager_reads;
        if(address>=MANAGER+0xfe8u && address<MANAGER+0x10a8u) ++table_reads;
        return sdk[(address-MANAGER)/4u];
    }
    if(address>=(RX|P2) && address<(RX|P2)+sizeof(rx)) {
        assert(!(address&3u)); /* Durable physical validation also reads function. */
        ++rx_reads;return le32(rx+address-(RX|P2));
    }
    if(address>=(TX|P2) && address<(TX|P2)+sizeof(tx)) {
        unsigned b=(address-(TX|P2))/0x3000u;
        unsigned word=((address-(TX|P2))%0x3000u)/4u;
        assert(!(address&3u) && b==bank && word<tx_words);
        if(forbid_target_payload)
            assert(word<target_payload_begin || word>=target_payload_begin+target_payload_words);
        return tx[b][word];
    }
    fprintf(stderr,"Unexpected Silver readonly fixture read: %08x\n",address);
    assert(!"unowned SDK alias, pointer dereference or MMIO read");return 0;
}
void kui_vmu_maple_observer_test_stop(void) {++stops;}

static void reset_window(unsigned stop_after,unsigned post_after) {
    struct kui_vmu_maple_observe_config config={
        KUI_VMU_MAPLE_OBSERVE_MAGIC,KUI_VMU_MAPLE_OBSERVE_VERSION,
        sizeof(config),stop_after,post_after,{0},0,0,0
    };
    memset(sdk,0,sizeof(sdk));memset(tx,0,sizeof(tx));memset(rx,0xa5,sizeof(rx));
    manager=MANAGER;lock_pointer=0xac11cd28u;lock_byte=0x80u;
    gun=trigger=busy=0u;enable=1u;source=TX;bank=0u;
    timer1_callback=timer2_callback=0u;
    stops=rx_reads=table_reads=manager_reads=0u;
    private_chain=NULL;mdstar_writes=deny_mdstar_write=0u;
    tx_words=frame_count=target_payload_begin=target_payload_words=forbid_target_payload=0u;
    parent_rx=target_rx=0u;
    *field(0xfc4u)=TX|P2;*field(0xfc8u)=RX|P2;
    *field(0x10a8u)=TX|P2;*field(0x10acu)=(TX+0x3000u)|P2;
    *field(0x10f4u)=0xa05f6c00u;
    for(unsigned i=0;i<48u;i++) *field(0xfe8u+4u*i)=(RX+i*1024u)|P2;
#if KUI_VMU_READONLY_PROOF == 4
    kui_vmu_test_sd_reset();kui_vmu_test_sd_configure(&config);
#endif
    assert(kui_vmu_maple_observer_init(&config)==0);
    assert(kui_vmu_maple_observer_snapshot.proof_mode==KUI_VMU_READONLY_PROOF);
}
static void reset(unsigned stop_after) {reset_window(stop_after,stop_after);}

static void packet(unsigned port,unsigned ordinal,unsigned command,unsigned words,
    uint32_t function,uint32_t location) {
    static const unsigned unit_bits[]={0x20u,1u,2u,4u,8u,16u};
    assert(port<4u && ordinal<6u && words<64u && frame_count<32u);
    const uint32_t physical=RX+bank*0x6000u+(port*6u+ordinal)*1024u;
    last_control=tx_words;tx[bank][tx_words++]=(port<<16)|words;
    tx[bank][tx_words++]=physical;
    tx[bank][tx_words++]=(words<<24)|(port<<22)|(((port<<6)|unit_bits[ordinal])<<8)|command;
    if(port==0u && ordinal==1u) {
        target_rx=physical;target_payload_begin=tx_words;target_payload_words=words;
    }
    for(unsigned i=0;i<words;i++)
        tx[bank][tx_words++]=i==0u?function:i==1u?location:0x5a5a0000u+i;
    put32(kui_vmu_maple_observer_test_response(physical),UINT32_MAX);
    frame_rx[frame_count++]=physical;
    if(port==0u && ordinal==0u) parent_rx=physical;
}

static void build(unsigned selected_bank,unsigned parent_command,unsigned target_command,
    unsigned words,uint32_t function,uint32_t location) {
    bank=selected_bank;source=TX+bank*0x3000u;tx_words=frame_count=0u;
    target_rx=0u;target_payload_begin=target_payload_words=0u;
    memset(tx[bank],0,sizeof(tx[bank]));
    *field(0xfc0u)=1u;*field(0xfccu)=source|P2;*field(0x10b0u)=source|P2;
    *field(0x10b4u)=*field(0x10b8u)=bank;
    packet(0u,0u,parent_command,parent_command==9u?1u:0u,0x01000000u,0u);
    if(target_command) {
        for(unsigned i=0;i<5u;i++) tx[bank][tx_words++]=0x00000700u;
        packet(0u,1u,target_command,words,function,location);
    }
    /* B-D parents and every unrequested subunit slot must remain unchanged. */
    packet(1u,0u,1u,0u,0u,0u);
    packet(2u,0u,1u,0u,0u,0u);packet(3u,0u,1u,0u,0u,0u);
    tx[bank][last_control]|=0x80000000u;
}

static uint32_t sr(unsigned mask) {return 0x60000101u|(mask<<4);}
static void preserve(void) {memcpy(rx_before,rx,sizeof(rx));memcpy(tx_before,tx,sizeof(tx));}
static void unchanged(void) {
    assert(!memcmp(rx_before,rx,sizeof(rx)) && !memcmp(tx_before,tx,sizeof(tx)));
}
static void pre(unsigned mask) {
    preserve();kui_vmu_maple_observer_visit(sr(mask));unchanged();
}
static void physical_complete(unsigned parent_command) {
    unsigned bytes=parent_command==9u?16u:parent_command==2u?196u:116u;
    unsigned words=parent_command==9u?3u:parent_command==2u?48u:28u;
    unsigned code=parent_command==9u?8u:parent_command==2u?6u:5u;
    uint8_t *p=kui_vmu_maple_observer_test_response(parent_rx);
    for(unsigned i=0;i<bytes;i++) p[i]=(uint8_t)(0x80u+i);
    put32(p,(words<<24)|(0x24u<<16)|code); /* Real A2 presence must survive adding A1. */
    put32(p+4u,0x01000000u);
    for(unsigned i=0;i<frame_count;i++) if(frame_rx[i]!=parent_rx && frame_rx[i]!=target_rx) {
        p=kui_vmu_maple_observer_test_response(frame_rx[i]);
        for(unsigned j=0;j<16u;j++) p[j]=(uint8_t)(0x30u+i+j);
        const unsigned port=(frame_rx[i]-RX-bank*0x6000u)/(6u*1024u);
        put32(p,0x1c000005u|((port<<6)<<8)|((port<<6|32u)<<16));
        put32(p+4u,0x01000000u); /* Valid unrelated physical controller. */
    }
    *field(0xfc0u)=0u;*field(0x10b4u)=*field(0x10b8u)=bank^1u;
}
static void complete(unsigned mask) {kui_vmu_maple_observer_complete(sr(mask),RETURN_PR);}
static void success_bytes(unsigned response_bytes) {
    assert(!memcmp(tx_before,tx,sizeof(tx)));
    for(unsigned i=0;i<sizeof(rx);i++) {
        if(i==parent_rx-RX+2u) continue;
        if(target_rx && i>=target_rx-RX && i<target_rx-RX+response_bytes) continue;
        assert(rx[i]==rx_before[i]);
    }
    assert(rx[parent_rx-RX+2u]==(uint8_t)(rx_before[parent_rx-RX+2u]|1u));
}

#include "kui/vmu_durable.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include "vmu_sd_model.h"
#include "../src/dreamcast/sci_sd_storage.h"
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define TX_BASE TX
#define RX_BASE RX

#if KUI_VMU_READONLY_PROOF != 4
#error This test requires the explicit persistent SD proof
#endif
#define PROFILE_TOKEN UINT32_C(0x8ce01000)
#define LEASE_TOKEN UINT32_C(0x8c006000)
static struct kui_vmu_durable_handoff handoff;
static bool handoff_missing,readback_corrupt;
static unsigned lease_calls,callback_calls;
static uint8_t saved_media[sizeof(sd.media)],reboot_media[sizeof(sd.media)];
static unsigned reboot_mode;
static const struct kui_vmu_maple_observe_snapshot *const snapshot=&kui_vmu_maple_observer_snapshot;

static uint8_t worker_model_transfer(void *context,uint8_t value,bool slow) {
    if(readback_corrupt && sd.frame_n==5u && (sd.frame[0]&63u)==17u && sd.writes>=4u) {
        uint32_t lba=(uint32_t)sd.frame[1]<<24|(uint32_t)sd.frame[2]<<16|
            (uint32_t)sd.frame[3]<<8|sd.frame[4];
        if(lba==image_lba(42u)) {sd.media[lba][33]^=1u;readback_corrupt=false;}
    }
    return transfer(context,value,slow);
}
/* High-stage-compatible store recovery/checkpoint over the real SD PIO
 * implementation. A worker is never allowed to clear/replay the old marker. */
static int reboot_read_sector(void *context,uint64_t lba,void *out) {
    assert(context==&sd && sd.owned && sd.locked && allowed((uint32_t)lba));
    return kui_loader_sd_read(&card,(uint32_t)lba,1u,out)==KUI_LOADER_SD_OK?0:-1;
}
static int reboot_write_sector(void *context,uint64_t lba,const void *data) {
    assert(context==&sd && sd.owned && sd.locked && allowed((uint32_t)lba));
    return kui_sci_sd_write(&card,(uint32_t)lba,1u,data)==KUI_LOADER_SD_OK?0:-1;
}
static int reboot_sync(void *context) {
    assert(context==&sd && sd.owned && sd.locked);
    return kui_sci_sd_sync(&card)==KUI_LOADER_SD_OK?0:-1;
}
static void reboot_checkpoint(void) {
    assert(enter(&sd) && acquire(&sd)==KUI_LOADER_SD_OK);
    struct kui_vmu_store store={0};
    const struct kui_vmu_store_io io={&sd,reboot_read_sector,reboot_write_sector,reboot_sync};
    assert(kui_vmu_store_open(&store,&io,&store_config)==0);
    assert(kui_vmu_store_checkpoint(&store)==0);
    release(&sd);leave(&sd);assert(card.ready && sd.health);
    for(unsigned i=0;i<512u;i++) assert(!sd.media[950u][i]);
    sd.bytes=sd.reads=sd.writes=sd.statuses=sd.cids=0u;
    sd.acquires=sd.releases=sd.enters=sd.leaves=0u;
}
void kui_vmu_test_sd_reset(void) {
    model_reset();handoff_missing=readback_corrupt=false;lease_calls=callback_calls=0u;
    for(unsigned block=0;block<256u;block++)
        assert(kui_vmu_image_blank_block((uint16_t)block,sd.media[image_lba(block)])==0);
    for(unsigned i=0;i<512u;i++) sd.media[image_lba(42u)][i]=(uint8_t)(i*7u+(i>>3)+0x31u);
    if(reboot_mode) {
        memcpy(sd.media,reboot_media,sizeof(sd.media));
        if(reboot_mode==2u) reboot_checkpoint();
    }
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
    /* Physical replies stay private until transaction AND release succeed. */
    unchanged();
    assert(!sd.selected && !sd.streaming && !sd.write_phase);
    release(&sd);unchanged();
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
    if(command==12u) for(unsigned i=0;i<32u;i++) tx[bank][target_payload_begin+2u+i]=le32(data+phase*128u+i*4u);
    pre(active_mask);assert(!stops && snapshot->completion_pending==5u);filtered_chain();
    physical_complete(9u);preserve();
}
static void finish_request(unsigned bytes) {
    complete(active_mask);success_bytes(bytes);assert(!stops && snapshot->chain_state==3u);
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
    (void)unused;uint8_t data[512];data_block(data);reset(100u);
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
    uint8_t data[512];data_block(data);reset(100u);stage_four(data);
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
    preserve();complete(active_mask);unchanged();
    assert(stops==1u && !lease_calls && !sd.writes && !snapshot->ram_commits);
    assert(snapshot->ram_write_phases==4u && !snapshot->proof_commit_attempts);
    assert(!memcmp(saved_media,sd.media,sizeof(saved_media)));
}
static void write_capture_guard(unsigned unused) {
    (void)unused;uint8_t data[512];data_block(data);reset(100u);prepare_request(12u,42u,0u,data);
    tx[bank][14u]^=1u;preserve();complete(active_mask);unchanged();
    assert(stops==1u && !snapshot->ram_write_phases && !lease_calls && !sd.writes);
}
static void commit_fault(unsigned fault) {
    uint8_t data[512];data_block(data);reset(100u);stage_four(data);
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
    complete(active_mask);unchanged();
    assert(stops==1u && !snapshot->ram_commits);
    assert(snapshot->proof_commit_attempts==1u && snapshot->ram_write_phases==4u);
    assert(snapshot->proof_replies==4u && snapshot->proof_completed==4u);
    assert(lease_calls==1u && !sd.owned && !sd.locked);
    if(fault==0u) {
        assert(sd.writes==4u && !memcmp(sd.media[image_lba(42u)],data,512u));
        assert(!memcmp(sd.media[950u],"KUIVMUJ1",8u) && snapshot->ram_faults);
    } else if(fault==1u) {assert(!readback_corrupt && sd.writes==4u && snapshot->ram_faults);}
    else if(fault>=4u) assert(!sd.writes);
    unsigned wire_bytes=sd.bytes,calls=lease_calls;complete(active_mask);unchanged();
    assert(sd.bytes==wire_bytes && lease_calls==calls);only_mapped_writes();
}
static void read_fault(unsigned fault) {
    reset(100u);prepare_request(11u,42u,0u,NULL);
    switch(fault) {
    case 0:sd.media[image_lba(42u)][99]^=1u;break;
    case 1:sd.fail_release=true;break;
    case 2:sd.deny_enter=true;break;
    default:sd.fail_read=1u;break;
    }
    complete(active_mask);unchanged();
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
/* Every C payload word is witnessed, even words beyond the first phase's
 * prefix. The worker must not ACK a changed immutable SDK capture. */
static void every_write_word_is_immutable(unsigned argument) {
    uint8_t data[512];data_block(data);reset(100u);
    const unsigned selected_bank=argument/32u,index=argument%32u;
    /* starts parity selects the bank independently of the interrupt mask. */
    if(selected_bank) {
        build(0u,9u,0u,0u,0u,0u);pre(active_mask);physical_complete(9u);
        preserve();complete(active_mask);success_bytes(0u);
    }
    prepare_request(12u,42u,0u,data);assert(bank==selected_bank);
    tx[bank][target_payload_begin+2u+index]^=UINT32_C(0x01010101);
    preserve();complete(active_mask);unchanged();
    assert(stops==1u && !snapshot->ram_write_phases && !lease_calls && !sd.writes);
    assert(!snapshot->ram_commits && !snapshot->proof_commit_attempts);
}
static void silver_context_fault(unsigned argument) {
    const unsigned post=argument/68u,fault=argument%68u;
    uint8_t data[512];data_block(data);reset(100u);
    build(1u,9u,12u,34u,0x02000000u,42u<<24);
    for(unsigned i=0;i<32u;i++) tx[bank][target_payload_begin+2u+i]=le32(data+i*4u);
    if(post) {pre(active_mask);assert(!stops);physical_complete(9u);}
    if(fault>=20u) *field(0xfe8u+(fault-20u)*4u)^=0x400u;
    else switch(fault) {
    case 0:timer1_callback=0x8c1547e6u;break;
    case 1:timer2_callback=0x8c12b768u;break;
    case 2:manager=0x8c29f000u;break;
    case 3:lock_pointer=0x8c11cd28u;break;
    case 4:lock_byte=0u;break;
    case 5:gun=1u;break;
    case 6:trigger=1u;break;
    case 7:busy=1u;break;
    case 8:enable=0u;break;
    case 9:*field(0xfc0u)^=1u;break;
    case 10:*field(0xfc4u)^=32u;break;
    case 11:*field(0xfc8u)^=32u;break;
    case 12:*field(0x10a8u)^=32u;break;
    case 13:*field(0x10acu)^=32u;break;
    case 14:*field(0x10b0u)^=0x3000u;break;
    case 15:*field(0x10b4u)^=1u;break;
    case 16:*field(0x10b8u)^=1u;break;
    case 17:*field(0x10c4u)=1u;break;
    case 18:*field(0x10f4u)=0xdeadc0deu;break;
    default:source=TX;break;
    }
    preserve();if(post) complete(active_mask);else kui_vmu_maple_observer_visit(sr(active_mask));
    unchanged();if(!(stops==1u && !snapshot->ram_write_phases && !lease_calls && !sd.writes))
        fprintf(stderr,"Silver guard argument=%u post=%u fault=%u mask=%u stops=%u phases=%u\n",argument,post,fault,active_mask,stops,snapshot->ram_write_phases);
    assert(stops==1u && !snapshot->ram_write_phases && !lease_calls && !sd.writes);
    assert(!snapshot->ram_commits && !snapshot->proof_commit_attempts);
    if(!post) assert(!mdstar_writes); /* Bad ownership never starts hardware. */
}
static void malformed_or_unrelated_write(unsigned argument) {
    const unsigned selected_bank=argument/12u,fault=argument%12u;
    uint8_t data[512];data_block(data);reset(100u);
    unsigned command=12u,words=34u;
    uint32_t function=0x02000000u,location=42u<<24;
    switch(fault) {
    case 0:words=33u;break;
    case 1:words=35u;break;
    case 2:location|=4u<<8;break;
    case 3:function=0x01000000u;break;
    case 4:location|=1u;break;
    case 5:location|=1u<<16;break;
    case 6:command=13u;words=1u;break;
    case 7:command=13u;words=3u;break;
    case 8:command=13u;words=2u;location|=2u<<8;break;
    default:break;
    }
    build(selected_bank,9u,command,words,function,location);
    if(fault>=9u) {
        /* A2, B1 and D1 are physical-owned, never legal virtual writers. */
        const unsigned port=fault==9u?0u:fault==10u?1u:3u;
        const unsigned unit=fault==9u?2u:1u;
        tx[bank][target_payload_begin-3u]=(port<<16)|words;
        tx[bank][target_payload_begin-2u]=RX+bank*0x6000u+(port*6u+unit)*1024u;
        tx[bank][target_payload_begin-1u]=(words<<24)|(port<<22)|(((port<<6)|unit)<<8)|12u;
    }
    pre(active_mask);
    assert(stops==1u && !snapshot->completion_pending && !snapshot->ram_write_phases);
    assert(!lease_calls && !sd.writes && !mdstar_writes && !snapshot->proof_commit_attempts);
}
static void unsupported_mask(unsigned argument) {
    unsigned post=argument/16u,mask=argument%16u;
    if(mask==6u || mask==15u) return;
    reset(100u);build(0u,9u,12u,34u,0x02000000u,42u<<24);
    if(post) {pre(active_mask);assert(!stops);physical_complete(9u);}
    preserve();if(post) complete(mask);else kui_vmu_maple_observer_visit(sr(mask));
    unchanged();assert(stops==1u && !lease_calls && !sd.writes && !snapshot->ram_write_phases);
    assert(!snapshot->proof_commit_attempts && (!post?!mdstar_writes:1));
}
static void extended_window_boundary(unsigned small_window) {
    if(small_window) reset_window(7u,4u);
    else {
        /* The stage passes these defaults explicitly to worker init. */
        assert(KUI_VMU_MAPLE_OBSERVE_DEFAULT_STOP==UINT32_MAX);
        assert(KUI_VMU_MAPLE_OBSERVE_DEFAULT_POST==UINT32_MAX);
        assert(KUI_VMU_MAPLE_OBSERVE_MAX_STARTS==UINT32_MAX);
        reset_window(KUI_VMU_MAPLE_OBSERVE_DEFAULT_STOP,KUI_VMU_MAPLE_OBSERVE_DEFAULT_POST);
    }
    assert(snapshot->stop_after==(small_window?7u:UINT32_MAX));
    assert(snapshot->post_after==(small_window?4u:UINT32_MAX));
    prepare_request(10u,0u,0u,NULL);finish_request(32u);
    assert(snapshot->starts==1u && snapshot->first_card_start==1u);
    if(!small_window) {
        /* Preserve an actual completed schedule and age only its counter.
         * Both old diagnostic windows must permit the next real schedule. */
        const unsigned previous_limits[]={18000u,216000u};
        for(unsigned i=0;i<2u;i++) {
            kui_vmu_maple_observer_snapshot.starts=previous_limits[i];
            prepare_request(10u,0u,0u,NULL);finish_request(32u);
            assert(snapshot->starts==previous_limits[i]+1u);
            assert(snapshot->starts-snapshot->first_card_start==previous_limits[i] && !stops);
        }
    }
    const unsigned limit=small_window?5u:UINT32_MAX;
    kui_vmu_maple_observer_snapshot.starts=limit-2u;
    prepare_request(10u,0u,0u,NULL);finish_request(32u);
    assert(snapshot->starts==limit-1u && !snapshot->completion_pending);
    const unsigned prepared=snapshot->proof_prepared,completed=snapshot->proof_completed;
    const unsigned replies=snapshot->proof_replies,callbacks=snapshot->completion_calls;
    const unsigned writes=mdstar_writes,reads=rx_reads,leases=lease_calls,wire=sd.bytes;
    build(snapshot->starts&1u,9u,11u,2u,0x02000000u,42u<<24);
    const unsigned unsent_source=source;
    pre(active_mask);
    assert(stops==1u && snapshot->starts==limit && snapshot->initialized==2u);
    assert(snapshot->stop_reason==(small_window?KUI_VMU_OBSERVE_CARD_WINDOW:KUI_VMU_OBSERVE_HARD_LIMIT));
    assert(!snapshot->completion_pending && snapshot->proof_prepared==prepared);
    assert(snapshot->proof_completed==completed && snapshot->proof_replies==replies);
    assert(mdstar_writes==writes && source==unsent_source && rx_reads==reads);
    assert(lease_calls==leases && sd.bytes==wire && !sd.writes);
    assert(le32(kui_vmu_maple_observer_test_response(parent_rx))==UINT32_MAX);
    assert(le32(kui_vmu_maple_observer_test_response(target_rx))==UINT32_MAX);
    /* The stopped, counted final schedule cannot publish a late completion. */
    preserve();complete(active_mask);unchanged();
    assert(snapshot->completion_calls==callbacks && mdstar_writes==writes);
    assert(snapshot->proof_completed==completed && snapshot->proof_replies==replies);
    assert(rx_reads==reads && lease_calls==leases && sd.bytes==wire);
    /* Once saturated, another entry cannot wrap the diagnostic counter or
     * revive the unsent final schedule. Explicit short stops stay latched. */
    pre(active_mask);
    assert(stops==1u && snapshot->starts==limit && !snapshot->completion_pending);
    assert(snapshot->proof_prepared==prepared && snapshot->proof_completed==completed);
    assert(mdstar_writes==writes && rx_reads==reads && lease_calls==leases && sd.bytes==wire);
}
static void isolated(void (*test)(unsigned),unsigned argument) {
    /* Each child models a distinct native worker lifetime, whose private BSS
     * starts zero. A started durable card is never reset or reinitialized. */
    pid_t child=fork();assert(child>=0);
    if(!child) {test(argument);exit(0);}
    int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
static void cold_saved_card(unsigned unused) {
    (void)unused;uint8_t data[512];data_block(data);reset(100u);
    prepare_request(11u,42u,0u,NULL);
    if(reboot_mode==1u) {
        complete(active_mask);unchanged();
        assert(stops==1u && !snapshot->ram_reads && !snapshot->ram_commits && !sd.writes);
        assert(!snapshot->proof_completed && !snapshot->proof_replies);
        assert(snapshot->sd_last_result==KUI_VMU_DURABLE_SD_MARKER);
        return;
    }
    assert(reboot_mode==2u);finish_request(524u);
    assert(!memcmp(kui_vmu_maple_observer_test_response(target_rx)+12u,data,512u));
    assert(snapshot->ram_reads==1u && !snapshot->ram_commits && !sd.writes);
    assert(sd.cids==1u && sd.reads==2u && lease_calls==1u);
    assert(!memcmp(saved_media,sd.media,sizeof(saved_media)));
    /* The cold lifetime can continue writing after the clean fresh seal. */
    for(unsigned i=0;i<512u;i++) data[i]^=0x63u;
    stage_four(data);prepare_request(13u,42u,4u,NULL);finish_request(4u);
    prepare_request(11u,42u,0u,NULL);finish_request(524u);
    assert(!memcmp(kui_vmu_maple_observer_test_response(target_rx)+12u,data,512u));
    assert(snapshot->ram_commits==1u && snapshot->ram_reads==2u);only_mapped_writes();
}
static void saved_media_survives_cold_worker(unsigned torn) {
    int pipefd[2];assert(!pipe(pipefd));pid_t child=fork();assert(child>=0);
    if(!child) {
        assert(!close(pipefd[0]));uint8_t data[512];data_block(data);reset(100u);stage_four(data);
        prepare_request(13u,42u,4u,NULL);
        if(torn) {
            sd.fail_write=4u;complete(active_mask);unchanged();
            assert(stops==1u && !snapshot->ram_commits && sd.writes==4u);
            assert(memcmp(sd.media[image_lba(42u)],data,512u));
        } else {finish_request(4u);assert(snapshot->ram_commits==1u);}
        assert(!memcmp(sd.media[950u],"KUIVMUJ1",8u));
        const uint8_t *bytes=(const uint8_t *)sd.media;size_t at=0;
        while(at<sizeof(sd.media)) {
            ssize_t n=write(pipefd[1],bytes+at,sizeof(sd.media)-at);assert(n>0);at+=(size_t)n;
        }
        assert(!close(pipefd[1]));exit(0);
    }
    assert(!close(pipefd[1]));size_t at=0;
    while(at<sizeof(reboot_media)) {
        ssize_t n=read(pipefd[0],reboot_media+at,sizeof(reboot_media)-at);assert(n>0);at+=(size_t)n;
    }
    assert(!close(pipefd[0]));int status=0;
    assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    /* Each isolated child inherits the parent's untouched worker BSS. The
     * only game lifetime state transferred from the writer is SD media. */
    reboot_mode=1u;isolated(cold_saved_card,0u); /* Pending marker refuses. */
    reboot_mode=2u;isolated(cold_saved_card,0u); /* Recover/checkpoint/new seal. */
    reboot_mode=0u;
}
int main(void) {
    for(active_mask=6u;active_mask<=15u;active_mask+=9u) {
        isolated(happy,0u);isolated(write_capture_guard,0u);
        isolated(extended_window_boundary,0u);isolated(extended_window_boundary,1u);
        for(unsigned fault=0;fault<11u;fault++) isolated(completion_guard,fault);
        for(unsigned fault=0;fault<8u;fault++) isolated(commit_fault,fault);
        for(unsigned fault=0;fault<4u;fault++) isolated(read_fault,fault);
        for(unsigned fault=0;fault<10u;fault++) isolated(configuration_fault,fault);
        for(unsigned at=0;at<64u;at++) isolated(every_write_word_is_immutable,at);
        for(unsigned fault=0;fault<136u;fault++) isolated(silver_context_fault,fault);
        for(unsigned fault=0;fault<24u;fault++) isolated(malformed_or_unrelated_write,fault);
        for(unsigned mask=0;mask<32u;mask++) isolated(unsupported_mask,mask);
        saved_media_survives_cold_worker(0u);saved_media_survives_cold_worker(1u);
    }
    puts("Silver durable worker: filtered DMA, immutable write capture, real-map durable ACK, cold-media recovery/checkpoint/relaunch, restoration/readback/CRC faults and no-publication guards passed");
    return 0;
}
