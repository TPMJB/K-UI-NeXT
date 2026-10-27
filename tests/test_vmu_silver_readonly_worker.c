/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent exact-Silver scalar fixtures and synthetic physical responses.
 * Link the actual blank-card backend and overlay; no retail payload is used. */
#include "kui/vmu_maple_observe.h"
#include "kui/vmu_readonly_overlay.h"
#include "kui/vmu_image.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if KUI_VMU_READONLY_PROOF == 2
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
static uint32_t parent_rx,target_rx,frame_rx[32];
static unsigned bank,stops,rx_reads,table_reads,manager_reads,tx_words,last_control;
static unsigned frame_count,target_payload_begin,target_payload_words,forbid_target_payload;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned le16(const uint8_t *p) {return (unsigned)p[0]|(unsigned)p[1]<<8;}
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
        assert((address-(RX|P2))%1024u==0u); /* Worker diagnostics read headers only. */
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
    tx_words=frame_count=target_payload_begin=target_payload_words=forbid_target_payload=0u;
    parent_rx=target_rx=0u;
    *field(0xfc4u)=TX|P2;*field(0xfc8u)=RX|P2;
    *field(0x10a8u)=TX|P2;*field(0x10acu)=(TX+0x3000u)|P2;
    *field(0x10f4u)=0xa05f6c00u;
    for(unsigned i=0;i<48u;i++) *field(0xfe8u+4u*i)=(RX+i*1024u)|P2;
#if KUI_VMU_READONLY_PROOF == 2
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
        put32(p,0x03000208u+i*0x100u); /* Synthetic unrelated physical bytes. */
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

static void both_banks_full_discovery_media_root_and_fat(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    static const unsigned commands[]={0u,1u,2u,10u,11u,11u};
    static const unsigned sizes[]={0u,116u,196u,32u,524u,524u};
    assert(KUI_VMU_MAPLE_PROFILE==2 &&
        (KUI_VMU_READONLY_PROOF==1 || KUI_VMU_READONLY_PROOF==2));
    for(unsigned first_bank=0;first_bank<2u;first_bank++) {
        reset(100u);
        for(unsigned step=0;step<6u;step++) {
            const unsigned b=(first_bank+step)&1u,parent=step?9u:1u;
            const unsigned mask=step&1u?6u:15u,words=commands[step]>=10u?2u:0u;
            const uint32_t location=step==4u?0xff000000u:step==5u?0xfe000000u:0u;
            build(b,parent,commands[step],words,0x02000000u,location);
            pre(mask);
            assert(!stops && s->proof_prepared==step+1u && s->proof_completed==step);
            assert(s->completion_pending==frame_count && !rx_reads);
            assert(rx[parent_rx-RX+2u]==0xffu); /* No pre-DMA advertisement. */
            physical_complete(parent);preserve();complete(mask);success_bytes(sizes[step]);
            assert(!stops && s->proof_completed==step+1u && s->proof_parents==step+1u);
            assert(s->completion_matched==step+1u && !s->completion_pending);
            assert(s->proof_replies==step && !s->proof_commit_attempts && !s->proof_flags);
            assert(!s->completion_guard_flags && s->completion_pr_last==RETURN_PR);
            if(step) {
                uint8_t *reply=kui_vmu_maple_observer_test_response(target_rx);
                assert(reply[1]==0u && reply[2]==1u && 4u+4u*reply[3]==sizes[step]);
                assert(le32(reply+4u)==0x02000000u);
                if(commands[step]<10u) {
#if KUI_VMU_READONLY_PROOF == 2
                    static const char name[]="K-UI Read-Only SD";
#else
                    static const char name[]="K-UI Read-Only Blank";
#endif
                    assert(!memcmp(reply+22u,name,sizeof(name)-1u));
                }
            }
            if(step>=4u) {
                uint8_t expected[512],*reply=kui_vmu_maple_observer_test_response(target_rx);
                const unsigned block=step==4u?255u:254u;
                assert(kui_vmu_image_blank_block((uint16_t)block,expected)==0);
                assert(le32(reply)==0x82010008u && le32(reply+8u)==location);
                assert(!memcmp(reply+12u,expected,512u));
                if(block==255u) {
                    for(unsigned i=0;i<16u;i++) assert(reply[12u+i]==0x55u);
                    assert(le16(reply+12u+0x46u)==254u && le16(reply+12u+0x50u)==200u);
                } else {
                    assert(le16(reply+12u)==0xfffcu);
                    assert(le16(reply+12u+241u*2u)==0xfffau);
                    assert(le16(reply+12u+253u*2u)==252u);
                    assert(le16(reply+12u+254u*2u)==0xfffau);
                }
            }
            /* Only the completion diagnostics issue read callbacks; the
             * overlay itself operates on the proved local P2 slot pointers. */
            rx_reads=0u;
        }
        assert(s->proof_info==2u && s->proof_media==1u && s->proof_reads==2u);
        assert(s->proof_last_block==254u && !s->proof_commit_attempts);
        assert(s->completion_masks==((1u<<6)|(1u<<15)));
        assert(s->entry_masks==((1u<<6)|(1u<<15)) && !s->completion_bad_mask);
    }
}

static void every_table_entry_at_both_boundaries_and_masks(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    const unsigned masks[]={6u,15u};
    for(unsigned m=0;m<2u;m++) for(unsigned post=0;post<2u;post++)
    for(unsigned entry=0;entry<48u;entry++) {
        reset(100u);build(entry&1u,9u,1u,0u,0u,0u);
        if(post) {pre(masks[m]);assert(!stops);physical_complete(9u);}
        *field(0xfe8u+entry*4u)^=0x400u;
        preserve();if(post) complete(masks[m]);else kui_vmu_maple_observer_visit(sr(masks[m]));
        unchanged();assert(stops==1u && s->initialized==2u && s->proof_bank_fail==1u);
        assert(!s->proof_completed && !s->proof_replies && !s->proof_commit_attempts && !rx_reads);
        assert(s->stop_reason==KUI_VMU_OBSERVE_PROOF_REFUSED);
    }
}

static void corrupt(unsigned fault,unsigned post) {
    switch(fault) {
    case 0:*field(0x10b4u)^=1u;break;
    case 1:*field(0x10b8u)^=1u;break;
    case 2:*field(0x10c4u)=1u;break;
    case 3:gun=1u;break;
    case 4:*field(0xfc0u)=post?1u:0u;break;
    case 5:*field(0x10b0u)^=0x3000u;break;
    case 6:*field(0xfc4u)^=0x20u;break;
    case 7:*field(0xfc8u)^=0x20u;break;
    case 8:*field(0x10a8u)^=0x20u;break;
    case 9:*field(0x10acu)^=0x20u;break;
    case 10:trigger=1u;break;
    case 11:enable=0u;break;
    case 12:busy=1u;break;
    case 13:lock_pointer=0x8c11cd28u;break;
    case 14:lock_byte=0u;break;
    case 15:source=TX;break; /* Other valid bank, inconsistent with pending batch. */
    case 16:manager=0x8c29f000u;break;
    case 17:*field(0x10f4u)=0xdeadc0deu;break;
    case 18:timer1_callback=0x8c1547e6u;break;
    default:timer2_callback=0x8c12b768u;break;
    }
}

static void live_context_and_descriptor_refusals(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    const unsigned masks[]={6u,15u};
    for(unsigned m=0;m<2u;m++) for(unsigned post=0;post<2u;post++)
    for(unsigned fault=0;fault<20u;fault++) {
        reset(100u);build(1u,9u,1u,0u,0u,0u);
        if(post) {pre(masks[m]);assert(!stops);physical_complete(9u);}
        corrupt(fault,post);preserve();
        if(post) complete(masks[m]);else kui_vmu_maple_observer_visit(sr(masks[m]));
        unchanged();assert(stops==1u && !s->proof_completed && !s->proof_replies);
        assert(!s->proof_commit_attempts && !rx_reads);
        assert(s->stop_reason==KUI_VMU_OBSERVE_PROOF_REFUSED);
        if(fault==16u && !post) assert(!manager_reads);
    }
    for(unsigned b=0;b<2u;b++) {
        reset(100u);build(b,9u,1u,0u,0u,0u);tx[b][1]^=0x6000u;
        pre(6u);assert(stops==1u && !s->completion_pending && !s->proof_prepared && !rx_reads);
        reset(100u);build(b,9u,1u,0u,0u,0u);pre(15u);physical_complete(9u);preserve();
        kui_vmu_maple_observer_complete(sr(15u),0x8c0b5362u);unchanged();
        assert(stops==1u && !s->proof_completed && !rx_reads && (s->completion_guard_flags&1u));
    }
}

static void only_six_and_fifteen_are_admitted(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned mask=0;mask<16u;mask++) if(mask!=6u && mask!=15u) {
        reset(100u);build(mask&1u,9u,1u,0u,0u,0u);pre(mask);
        assert(stops==1u && !s->completion_pending && !s->proof_prepared && !rx_reads);
        assert(s->stop_reason==KUI_VMU_OBSERVE_PROOF_REFUSED);
        reset(100u);build(mask&1u,9u,1u,0u,0u,0u);pre(6u);physical_complete(9u);
        preserve();complete(mask);unchanged();
        assert(stops==1u && !s->proof_completed && !s->proof_replies && !rx_reads);
        assert(s->completion_bad_mask==1u && (s->completion_guard_flags&8u));
    }
}

static void real_a1_and_bad_parent_never_get_overwritten(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned mask=6u;mask<=15u;mask+=9u) for(unsigned conflict=0;conflict<4u;conflict++) {
        reset(100u);build(conflict&1u,9u,1u,0u,0u,0u);pre(mask);physical_complete(9u);
        uint8_t *parent=kui_vmu_maple_observer_test_response(parent_rx);
        uint8_t *target=kui_vmu_maple_observer_test_response(target_rx);
        if(conflict==0u) parent[2]|=1u;
        else if(conflict==1u) put32(target,0x1c010005u);
        else if(conflict==2u) put32(target,0x0000ff00u); /* Only exact FFFFFFFF is admitted. */
        else put32(parent+4u,0x02000000u); /* Parent must actually be a controller. */
        preserve();complete(mask);unchanged();
        assert(stops==1u && !s->proof_completed && !s->proof_replies && !rx_reads);
        assert(!s->proof_commit_attempts);
        assert(conflict==3u?(s->proof_flags&KUI_VMU_OVERLAY_BAD_PARENT_REPLY):s->proof_conflicts==1u);
    }
}

static void unsupported_commands_stop_before_any_target_payload_read(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    const unsigned commands[]={12u,13u,7u,3u,4u,9u};
    const unsigned words[]={34u,2u,0u,0u,0u,1u};
    for(unsigned mask=6u;mask<=15u;mask+=9u) for(unsigned i=0;i<6u;i++) {
        reset(100u);build(i&1u,9u,commands[i],words[i],0x02000000u,0u);
        forbid_target_payload=1u;pre(mask);
        assert(stops==1u && !s->completion_pending && !s->proof_prepared && !rx_reads);
        assert(!s->proof_replies && !s->proof_commit_attempts);
        assert(s->proof_flags&0x08000000u); /* Early refusal precedes all A1 payload reads. */
        assert(s->stop_reason==KUI_VMU_OBSERVE_PROOF_REFUSED);
    }
}

static void unrelated_writes_stop_before_any_payload_read(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned mask=6u;mask<=15u;mask+=9u) for(unsigned fault=0;fault<3u;fault++) {
        reset(100u);build(fault&1u,9u,0u,0u,0u,0u);
        memset(tx[bank],0,sizeof(tx[bank]));tx_words=frame_count=0u;
        const unsigned words=fault==1u?2u:34u,command=fault==1u?13u:12u;
        if(fault<2u) {
            packet(0u,0u,command,words,0x01000000u,0u);
            target_payload_begin=3u;target_payload_words=words;
        } else {
            packet(0u,0u,9u,1u,0x01000000u,0u);
            const unsigned first=tx_words;
            packet(1u,1u,command,words,0x02000000u,0u);
            target_payload_begin=first+3u;target_payload_words=words;
        }
        for(unsigned port=1u;port<4u;port++) packet(port,0u,1u,0u,0u,0u);
        tx[bank][last_control]|=0x80000000u;
        forbid_target_payload=1u;pre(mask);
        assert(stops==1u && !s->completion_pending && !s->proof_prepared && !rx_reads);
        assert((s->proof_flags&0x08000000u) && !s->proof_commit_attempts);
    }
}

static void captured_source_metadata_cannot_change_before_publication(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned mask=6u;mask<=15u;mask+=9u) for(unsigned fault=0;fault<9u;fault++) {
        reset(100u);build(fault&1u,9u,11u,2u,0x02000000u,0xff000000u);
        pre(mask);physical_complete(9u);
        switch(fault) {
        case 0:tx[bank][0]|=0x80000000u;break;
        case 1:tx[bank][1]+=1024u;break;
        case 2:tx[bank][2]^=1u;break;
        case 3:tx[bank][4]=0x80000700u;break; /* First padding word. */
        case 4:tx[bank][last_control]&=0x7fffffffu;break;
        case 5:tx[bank][target_payload_begin]^=0x02000000u;break;
        case 6:tx[bank][target_payload_begin+1u]^=0x01000000u;break;
        case 7:tx[bank][3]^=0x01000000u;break; /* Parent GETCOND function. */
        default:tx[bank][target_payload_begin-3u]^=1u;break;
        }
        preserve();complete(mask);unchanged();
        assert(stops==1u && !s->proof_completed && !s->proof_replies && !rx_reads);
        assert(s->proof_bank_fail==1u && (s->completion_guard_flags&0x100u));
    }
}

static void hard_limit_counts_final_unsent_schedule(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    reset(2u);build(0u,1u,0u,0u,0u,0u);pre(6u);physical_complete(1u);
    preserve();complete(6u);success_bytes(0u);
    assert(s->completion_matched==1u && s->proof_completed==1u);
    build(1u,9u,1u,0u,0u,0u);pre(15u);
    assert(stops==1u && s->starts==2u && s->stop_reason==KUI_VMU_OBSERVE_HARD_LIMIT);
    assert(!s->completion_pending && s->proof_prepared==1u && s->proof_completed==1u);
    const unsigned prior_rx_reads=rx_reads;
    preserve();complete(15u);unchanged();
    assert(rx_reads==prior_rx_reads && s->completion_calls==1u && !s->proof_replies);
}

static void extended_defaults_and_init_ceiling(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    assert(KUI_VMU_MAPLE_OBSERVE_DEFAULT_STOP==36000u);
    assert(KUI_VMU_MAPLE_OBSERVE_DEFAULT_POST==18000u);
    assert(KUI_VMU_MAPLE_OBSERVE_MAX_STARTS==36000u);
    reset_window(KUI_VMU_MAPLE_OBSERVE_DEFAULT_STOP,KUI_VMU_MAPLE_OBSERVE_DEFAULT_POST);
    assert(s->initialized==1u && s->stop_after==36000u && s->post_after==18000u);
    struct kui_vmu_maple_observe_config config={
        KUI_VMU_MAPLE_OBSERVE_MAGIC,KUI_VMU_MAPLE_OBSERVE_VERSION,
        sizeof(config),36000u,18000u,{0},0,0,0
    };
#if KUI_VMU_READONLY_PROOF == 2
    kui_vmu_test_sd_configure(&config);
#endif
    assert(kui_vmu_maple_observer_init(&config)==0);
    assert(s->stop_after==36000u && s->post_after==18000u);
    config.stop_after=36001u;assert(kui_vmu_maple_observer_init(&config)==-1);
    config.stop_after=36000u;config.post_after=36001u;
    assert(kui_vmu_maple_observer_init(&config)==-1);
}

static void post_window_counts_exact_final_unsent_schedule(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    reset_window(50u,4u);
    /* The first card request is start 3. A post window of 4 ends at start 7;
     * starts 3..6 complete, while the final counted schedule is never issued. */
    for(unsigned n=1u;n<=7u;n++) {
        const unsigned parent=n==1u?1u:9u,target=n==3u?10u:0u;
        const unsigned mask=n&1u?6u:15u,prior_rx_reads=rx_reads;
        build(n&1u,parent,target,target?2u:0u,0x02000000u,0u);pre(mask);
        assert(s->starts==n && s->post_after==4u);
        assert(s->first_card_start==(n<3u?0u:3u));
        if(n==7u) {
            assert(stops==1u && s->stop_reason==KUI_VMU_OBSERVE_CARD_WINDOW);
            assert(!s->completion_pending && s->proof_prepared==6u && s->proof_completed==6u);
            assert(rx_reads==prior_rx_reads && rx[parent_rx-RX+2u]==0xffu);
            preserve();complete(mask);unchanged();
            assert(s->completion_calls==6u && s->completion_matched==6u && rx_reads==prior_rx_reads);
        } else {
            assert(!stops && s->stop_reason==KUI_VMU_OBSERVE_RUNNING);
            assert(s->completion_pending==frame_count && s->proof_prepared==n);
            physical_complete(parent);preserve();complete(mask);success_bytes(target?32u:0u);
            assert(s->proof_completed==n && s->completion_matched==n);
        }
    }
    assert(s->proof_parents==6u && s->proof_replies==1u && s->proof_media==1u);
    assert(!s->proof_reads && !s->proof_commit_attempts);
}

int main(void) {
    both_banks_full_discovery_media_root_and_fat();
    every_table_entry_at_both_boundaries_and_masks();
    live_context_and_descriptor_refusals();only_six_and_fifteen_are_admitted();
    real_a1_and_bad_parent_never_get_overwritten();
    unsupported_commands_stop_before_any_target_payload_read();
    unrelated_writes_stop_before_any_payload_read();
    captured_source_metadata_cannot_change_before_publication();
    hard_limit_counts_final_unsent_schedule();
    extended_defaults_and_init_ceiling();post_window_counts_exact_final_unsent_schedule();
    puts("Silver readonly worker: both banks, blank root/FAT, exact masks and zero-mutation refusals passed");
    return 0;
}
