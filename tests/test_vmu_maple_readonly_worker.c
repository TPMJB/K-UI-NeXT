/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact-owner P1 bookkeeping and P2 DMA arenas, without MMIO or physical SD. */
#include "kui/vmu_maple_observe.h"
#include "kui/vmu_readonly_overlay.h"
#include "kui/vmu_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MANAGER UINT32_C(0x8c1212a8)
#define RX_BASE UINT32_C(0x0c0e0080)
#define TX_BASE UINT32_C(0x0c0ec0a0)

static uint8_t rx[48u*1024u],rx_before[sizeof(rx)];
static uint32_t tx[2][0x3000u/4u],tx_before[2][0x3000u/4u];
static uint32_t sdk[0x10f8u/4u];
static uint32_t source,manager,lock_pointer,lock_byte,gun,trigger,enable,busy;
static unsigned bank,stops,rx_reads,tx_words,last_control,target_command;
static uint32_t target_rx,parent_rx;
#if KUI_VMU_READONLY_PROOF >= 2
void kui_vmu_test_sd_reset(void);
void kui_vmu_test_sd_configure(struct kui_vmu_maple_observe_config *);
#endif
#if KUI_VMU_READONLY_PROOF == 3
static uint8_t *private_chain;
static unsigned mdstar_writes;
static unsigned deny_mdstar_write;
uint32_t kui_vmu_maple_observer_test_private_chain(uint8_t *bytes,size_t capacity) {
    assert(bytes && !((uintptr_t)bytes&31u) && capacity==512u);
    private_chain=bytes;return UINT32_C(0x0cfde000);
}
void kui_vmu_maple_observer_test_write(uint32_t address,uint32_t value) {
    assert(address==UINT32_C(0xa05f6c04));++mdstar_writes;
    if(mdstar_writes!=deny_mdstar_write) source=value;
}
#endif

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(i*8u));
}
static uint32_t *field(unsigned offset) {assert(!(offset&3u));return &sdk[offset/4u];}

uint8_t *kui_vmu_maple_observer_test_response(uint32_t physical) {
    if(physical<RX_BASE || physical-RX_BASE>=sizeof(rx)) return NULL;
    return rx+physical-RX_BASE;
}
uint32_t kui_vmu_maple_observer_test_read(uint32_t address) {
    if(address==0xa05f6c04u) return source;
    if(address==0xa05f6c10u) return trigger;
    if(address==0xa05f6c14u) return enable;
    if(address==0xa05f6c18u) return busy;
    if(address==0x8c1223a4u) return manager;
    if(address==0x8c0c0964u) return lock_pointer;
    if(address==0xac0c0968u) return lock_byte;
    if(address==0x8c118398u) return gun;
    if(address>=MANAGER && address<MANAGER+sizeof(sdk)) {
        assert(!(address&3u));return sdk[(address-MANAGER)/4u];
    }
    if(address>=(RX_BASE|0xa0000000u) && address<(RX_BASE|0xa0000000u)+sizeof(rx)) {
        assert(!(address&3u));++rx_reads;
        return le32(rx+address-(RX_BASE|0xa0000000u));
    }
    for(unsigned b=0;b<2u;b++) {
        uint32_t start=(TX_BASE+b*0x3000u)|0xa0000000u;
        if(address>=start && address<start+sizeof(tx[b])) {
            assert(!(address&3u));return tx[b][(address-start)/4u];
        }
    }
    assert(!"Unexpected SDK alias, MMIO or source read");return 0;
}
void kui_vmu_maple_observer_test_stop(void) {++stops;}

static void reset(void) {
    struct kui_vmu_maple_observe_config config={
        KUI_VMU_MAPLE_OBSERVE_MAGIC,KUI_VMU_MAPLE_OBSERVE_VERSION,
#if KUI_VMU_READONLY_PROOF == 3
        sizeof(config),100u,100u,{0},0,0,0};
#else
        sizeof(config),100u,20u,{0},0,0,0};
#endif
    memset(sdk,0,sizeof(sdk));memset(tx,0,sizeof(tx));memset(rx,0xa5,sizeof(rx));
    manager=MANAGER;lock_pointer=0xac0c0968u;lock_byte=0x80u;
    gun=trigger=busy=0u;enable=1u;bank=0;stops=rx_reads=0;
    *field(0xfc4)=0xac0ec0a0u;*field(0xfc8)=0xac0e0080u;
    *field(0x10a8)=0xac0ec0a0u;*field(0x10ac)=0xac0ef0a0u;
    *field(0x10f4)=0xa05f6c00u;
    for(unsigned i=0;i<48u;i++) *field(0xfe8u+i*4u)=0xac0e0080u+i*1024u;
#if KUI_VMU_READONLY_PROOF >= 2
    kui_vmu_test_sd_reset();kui_vmu_test_sd_configure(&config);
#endif
#if KUI_VMU_READONLY_PROOF == 3
    private_chain=NULL;mdstar_writes=deny_mdstar_write=0;
#endif
    assert(kui_vmu_maple_observer_init(&config)==0);
    assert(kui_vmu_maple_observer_snapshot.proof_mode==KUI_VMU_READONLY_PROOF);
}

static void packet(unsigned port,unsigned unit,unsigned command,unsigned words,
    uint32_t function,uint32_t location) {
    unsigned address=(port<<6)|unit;
    uint32_t physical=RX_BASE+bank*0x6000u+(port*6u+(unit==32u?0u:1u))*1024u;
    last_control=tx_words;tx[bank][tx_words++]=(port<<16)|words;
    tx[bank][tx_words++]=physical;
    tx[bank][tx_words++]=(words<<24)|(port<<22)|(address<<8)|command;
    for(unsigned i=0;i<words;i++) tx[bank][tx_words++]=i==0u?function:i==1u?location:0x5a5a5a5au;
    put32(kui_vmu_maple_observer_test_response(physical),UINT32_MAX);
    if(port==0u && unit==32u) parent_rx=physical;
    if(port==0u && unit==1u) target_rx=physical;
}

static void build(unsigned selected_bank,unsigned parent_command,unsigned sub_command,
    unsigned sub_words,uint32_t function,uint32_t location) {
    bank=selected_bank;source=TX_BASE+bank*0x3000u;tx_words=0;target_rx=0;target_command=sub_command;
    memset(tx[bank],0,sizeof(tx[bank]));
    *field(0x10b0)=source|0xa0000000u;*field(0x10b4)=*field(0x10b8)=bank;
    *field(0xfc0)=1u;
    packet(0u,32u,parent_command,parent_command==9u?1u:0u,0x01000000u,0u);
    if(sub_command) {
        for(unsigned i=0;i<5u;i++) tx[bank][tx_words++]=0x00000700u;
        packet(0u,1u,sub_command,sub_words,function,location);
    }
    for(unsigned port=1u;port<4u;port++) packet(port,32u,1u,0u,0u,0u);
    tx[bank][last_control]|=0x80000000u;
}
static void preserve(void) {memcpy(rx_before,rx,sizeof(rx));memcpy(tx_before,tx,sizeof(tx));}
static void unchanged(void) {
    assert(!memcmp(rx_before,rx,sizeof(rx)));
    assert(!memcmp(tx_before,tx,sizeof(tx)));
}
static void pre(void) {
    preserve();kui_vmu_maple_observer_visit(0x60000161u);unchanged();
}
static void physical_complete(unsigned parent_command) {
    uint8_t *r=kui_vmu_maple_observer_test_response(parent_rx);
    unsigned words=parent_command==9u?3u:28u,code=parent_command==9u?8u:5u;
    put32(r,(words<<24)|(0x20u<<16)|code);put32(r+4u,0x01000000u);
    *field(0xfc0)=0u;*field(0x10b4)=*field(0x10b8)=bank^1u;
}
static void complete(void) {
    kui_vmu_maple_observer_complete(0x600001f1u,KUI_VMU_MAPLE_OBSERVE_COMPLETION_PR);
}
static void success_bytes(unsigned response_bytes) {
    assert(!memcmp(tx_before,tx,sizeof(tx)));
    for(unsigned i=0;i<sizeof(rx);i++) {
        if(i==parent_rx-RX_BASE+2u) continue;
        if(target_rx && i>=target_rx-RX_BASE && i<target_rx-RX_BASE+response_bytes) continue;
        assert(rx[i]==rx_before[i]);
    }
    assert(kui_vmu_maple_observer_test_response(parent_rx)[2]==0x21u);
}

static void alternating_bank_discovery_media_read(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    reset();
    const unsigned commands[]={0u,1u,10u,11u},bytes[]={0u,116u,32u,524u};
    for(unsigned step=0;step<4u;step++) {
        unsigned words=commands[step]>=10u?2u:0u,parent=step?9u:1u;
        build(step&1u,parent,commands[step],words,0x02000000u,step==3u?0xff000000u:0u);
        pre();assert(!stops && s->proof_prepared==step+1u && s->proof_completed==step);
        assert(s->completion_pending==(step?5u:4u));
        assert(kui_vmu_maple_observer_test_response(parent_rx)[2]==0xffu); /* No early advertisement. */
        physical_complete(parent);preserve();complete();success_bytes(bytes[step]);
        assert(!stops && s->proof_completed==step+1u && s->proof_parents==step+1u);
        assert(s->completion_matched==step+1u && !s->completion_pending);
        assert(s->proof_replies==step && !s->proof_commit_attempts && !s->proof_flags);
        if(step) {
            uint8_t *r=kui_vmu_maple_observer_test_response(target_rx);
            assert(r[1]==0u && r[2]==1u && 4u+4u*r[3]==bytes[step]);
            assert(le32(r+4u)==0x02000000u);
        }
    }
    uint8_t expected[512];assert(kui_vmu_image_blank_block(255u,expected)==0);
    uint8_t *root=kui_vmu_maple_observer_test_response(target_rx);
    assert(le32(root)==0x82010008u && le32(root+8u)==0xff000000u);
    assert(!memcmp(root+12u,expected,512u));
    for(unsigned i=0;i<16u;i++) assert(root[12u+i]==0x55u);
    assert(s->proof_info==1u && s->proof_media==1u && s->proof_reads==1u);
    assert(s->proof_last_block==255u && s->proof_bytes==672u && !s->proof_commit_attempts);
    assert(s->starts==4u && s->descriptors==19u && s->completion_rx_headers==19u);
    assert(s->nop_words==15u && !s->completion_orphan && !s->completion_overwritten);
    assert(!s->completion_guard_flags && s->completion_sr_last==0x600001f1u);
    assert(s->completion_pr_last==0x8c0b5362u && s->entry_sr_last==0x60000161u);
}

static void every_table_entry_rechecked_before_and_after_dma(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned post=0;post<2u;post++) for(unsigned entry=0;entry<48u;entry++) {
        reset();build(entry&1u,9u,1u,0u,0u,0u);
        if(post) {pre();assert(!stops);physical_complete(9u);}
        *field(0xfe8u+entry*4u)^=0x400u;
        preserve();if(post) complete();else kui_vmu_maple_observer_visit(0x60000161u);
        unchanged();assert(stops==1u && s->proof_bank_fail==1u && !s->proof_completed);
        assert(!s->proof_replies && !s->proof_commit_attempts && !rx_reads);
        assert(s->stop_reason==KUI_VMU_OBSERVE_PROOF_REFUSED);
    }
}

static void live_context_rejections_preserve_all_rx(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned post=0;post<2u;post++) for(unsigned fault=0;fault<16u;fault++) {
        reset();build(1u,9u,1u,0u,0u,0u);
        if(post) {pre();assert(!stops);physical_complete(9u);}
        switch(fault) {
        case 0: *field(0x10b4)^=1u;break;
        case 1: *field(0x10b8)^=1u;break;
        case 2: *field(0x10c4)=1u;break;
        case 3: gun=1u;break;
        case 4: *field(0xfc0)^=1u;break;
        case 5: *field(0x10b0)^=0x3000u;break;
        case 6: *field(0xfc4)^=0x20u;break;
        case 7: *field(0xfc8)^=0x20u;break;
        case 8: *field(0x10a8)^=0x20u;break;
        case 9: *field(0x10ac)^=0x20u;break;
        case 10: trigger=1u;break;
        case 11: enable=0u;break;
        case 12: busy=1u;break;
        case 13: lock_pointer=0x8c0c0968u;break;
        case 14: lock_byte=0u;break;
        default: source=TX_BASE;break;
        }
        preserve();if(post) complete();else kui_vmu_maple_observer_visit(0x60000161u);
        unchanged();assert(stops==1u && !s->proof_completed && !s->proof_replies);
        assert(!s->proof_commit_attempts && !rx_reads);
    }
    /* Per-packet RX must match the active bank, not merely be valid RAM. */
    reset();build(1u,9u,1u,0u,0u,0u);tx[1][1]-=0x6000u;
    pre();assert(stops==1u && s->proof_bank_fail==1u && !s->completion_pending);
    assert(!s->proof_prepared && !s->proof_replies && !s->proof_commit_attempts);
}

static void physical_conflicts_and_mutations_never_publish(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned conflict=0;conflict<2u;conflict++) {
        reset();build(0u,9u,1u,0u,0u,0u);pre();physical_complete(9u);
        if(conflict==0u) kui_vmu_maple_observer_test_response(parent_rx)[2]|=1u;
        else put32(kui_vmu_maple_observer_test_response(target_rx),0x1c010005u);
        preserve();complete();unchanged();
        assert(stops==1u && s->proof_conflicts==1u && !s->proof_completed);
        assert(!s->proof_replies && !s->proof_commit_attempts && !rx_reads);
    }
    const unsigned commands[]={12u,13u,7u,3u,4u,12u};
    for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
        unsigned words=i==0u?34u:i==1u?2u:i==5u?50u:0u;
        reset();build(0u,9u,commands[i],words,i==5u?0x04000000u:0x02000000u,0u);
        pre();assert(stops==1u && !s->completion_pending && !s->proof_prepared);
        assert(!s->proof_replies && !s->proof_commit_attempts && !rx_reads);
        assert(s->proof_flags&KUI_VMU_OVERLAY_BAD_REQUEST);
        assert(s->stop_reason==KUI_VMU_OBSERVE_PROOF_REFUSED);
    }
}

int main(void) {
    alternating_bank_discovery_media_read();
    every_table_entry_rechecked_before_and_after_dma();
    live_context_rejections_preserve_all_rx();
    physical_conflicts_and_mutations_never_publish();
    puts("VMU readonly worker: alternating SDK banks, virtual discovery/root read,48-slot gates and zero-mutation refusals passed");
    return 0;
}
