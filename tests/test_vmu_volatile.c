/* SPDX-License-Identifier: GPL-3.0-only */
/* Volatile transaction behavior using an immutable, logical-block-only base.
 * No external write callback exists; every write must remain in owned RAM. */
#include "kui/vmu_volatile.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct kui_vmu_volatile card;
static unsigned base_reads;
static bool base_fail;
static uint8_t response[524];
static size_t received;

static void base_pattern(unsigned block,uint8_t out[512]) {
    for(unsigned i=0;i<512u;i++) out[i]=(uint8_t)(block*29u+i*7u+(i>>8));
}
static void write_pattern(unsigned seed,uint8_t out[512]) {
    for(unsigned i=0;i<512u;i++) out[i]=(uint8_t)(seed*13u+i*11u+(i>>5));
}
static int read_base(void *context,uint16_t block,uint8_t out[512]) {
    assert(context==&base_reads && block<256u);++base_reads;
    if(base_fail) return -1;
    base_pattern(block,out);return 0;
}
static void start(void) {
    base_reads=0u;base_fail=false;
    assert(kui_vmu_volatile_init(&card,read_base,&base_reads)==0);
    assert(card.ready && !card.faulted && !base_reads && !card.cached_blocks);
    assert(sizeof(card)<=10u*1024u);
}
static size_t request(uint8_t out[140],unsigned cmd,unsigned block,unsigned phase,
    const uint8_t *data) {
    memset(out,0,140u);out[0]=(uint8_t)cmd;out[1]=1u;
    if(cmd==1u || cmd==2u) return 4u;
    out[3]=(uint8_t)(cmd==12u?34u:2u);out[7]=2u;
    out[9]=(uint8_t)phase;out[11]=(uint8_t)block;
    if(cmd==12u && data) memcpy(out+12u,data+phase*128u,128u);
    return cmd==12u?140u:12u;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void handled(const uint8_t *frame,size_t bytes,unsigned expected) {
    memset(response,0xa5,sizeof(response));received=999u;
    assert(kui_vmu_volatile_handle(&card,frame,bytes,response,sizeof(response),&received)==1);
    assert(received==expected && received==4u+4u*response[3]);
    assert(response[1]==0u && response[2]==1u);
    for(size_t i=received;i<sizeof(response);i++) assert(response[i]==0xa5u);
}
static void phase(unsigned block,unsigned n,const uint8_t data[512]) {
    uint8_t frame[140];size_t bytes=request(frame,12u,block,n,data);
    handled(frame,bytes,4u);assert(le32(response)==0x00010007u);
}
static void sync_block(unsigned block,unsigned n) {
    uint8_t frame[140];size_t bytes=request(frame,13u,block,n,NULL);
    handled(frame,bytes,4u);assert(le32(response)==0x00010007u);
}
static void read_compare(unsigned block,const uint8_t expected[512]) {
    uint8_t frame[140];size_t bytes=request(frame,11u,block,0u,NULL);
    handled(frame,bytes,524u);assert(le32(response)==0x82010008u);
    assert(!memcmp(response+4u,frame+4u,8u));assert(!memcmp(response+12u,expected,512u));
}
static void commit(unsigned block,const uint8_t data[512]) {
    for(unsigned n=0;n<4u;n++) phase(block,n,data);
    sync_block(block,4u);
}

static void identity_base_reads_and_read_status(void) {
    uint8_t frame[140],expected[512];start();
    handled(frame,request(frame,1u,0u,0u,NULL),116u);
    assert(response[0]==5u && !memcmp(response+22u,"K-UI Volatile RAM Card",21u));
    assert(le32(response+4u)==KUI_VMU_EMU_FUNC_MEMCARD);
    handled(frame,request(frame,2u,0u,0u,NULL),196u);
    assert(response[0]==6u && !memcmp(response+116u,"K-UI volatile RAM writes; all changes lost on reset",49u));
    handled(frame,request(frame,10u,0u,0u,NULL),32u);
    static const uint8_t media[]={
        8,0,1,7,0,0,0,2,255,0,0,0,255,0,254,0,
        1,0,253,0,13,0,0,0,200,0,31,0,0,0,0,0};
    assert(!memcmp(response,media,sizeof(media)) && !base_reads);
    base_pattern(42u,expected);read_compare(42u,expected);sync_block(42u,1u);
    assert(base_reads==1u && card.base_reads==1u && card.reads==1u && !card.cache_reads);
    assert(!card.ram_commits && !card.cached_blocks && !card.write_phases);
}

static void phase_retries_committed_reads_and_overwrite(void) {
    uint8_t data[512],old[512],replacement[512];start();
    write_pattern(41u,data);write_pattern(87u,replacement);base_pattern(42u,old);
    phase(42u,0u,data);read_compare(42u,old);
    assert(card.protocol.phases==1u && !card.ram_commits && !card.cached_blocks);
    phase(42u,1u,data);phase(42u,0u,data);assert(card.protocol.phases==3u);
    phase(42u,2u,data);phase(42u,1u,data);assert(card.protocol.phases==7u);
    phase(42u,2u,data);phase(42u,3u,data);read_compare(42u,old);
    assert(card.protocol.phases==15u && base_reads==2u && !card.ram_commits);
    sync_block(42u,4u);assert(card.ram_commits==1u && card.cached_blocks==1u);
    sync_block(42u,4u);assert(card.ram_commits==1u); /* Lost final ACK retry. */
    read_compare(42u,data);sync_block(42u,1u);
    assert(base_reads==2u && card.cache_reads==1u && card.write_phases==7u);
    phase(42u,0u,replacement);read_compare(42u,data);
    for(unsigned n=1;n<4u;n++) phase(42u,n,replacement);
    sync_block(42u,4u);read_compare(42u,replacement);
    assert(card.cached_blocks==1u && card.ram_commits==2u && base_reads==2u);
    /* A changed/new phase0 deliberately cancels the previous transaction. */
    phase(43u,0u,data);phase(43u,1u,data);phase(43u,0u,replacement);
    assert(card.protocol.phases==1u);
    for(unsigned n=1;n<4u;n++) phase(43u,n,replacement);
    sync_block(43u,4u);read_compare(43u,replacement);
    assert(card.cached_blocks==2u && card.ram_commits==3u && base_reads==2u);
}

static void capacity_and_exact_four_byte_outputs(void) {
    const unsigned commands[]={1u,2u,10u,11u,12u,13u};
    const unsigned needed[]={116u,196u,32u,524u,4u,4u};
    uint8_t frame[140],guard[526],data[512];write_pattern(1u,data);start();
    for(unsigned k=0;k<6u;k++) {
        size_t bytes=request(frame,commands[k],42u,commands[k]==13u?4u:0u,data);
        if(commands[k]==10u) bytes=request(frame,10u,0u,0u,NULL);
        for(unsigned capacity=0;capacity<needed[k];capacity++) {
            struct kui_vmu_volatile before=card;unsigned reads=base_reads;
            memset(guard,0xa5,sizeof(guard));received=999u;
            assert(kui_vmu_volatile_handle(&card,frame,bytes,guard+1u,capacity,&received)==KUI_VMU_VOLATILE_CAPACITY);
            assert(!received && base_reads==reads && !memcmp(&card,&before,sizeof(card)));
            for(unsigned i=0;i<sizeof(guard);i++) assert(guard[i]==0xa5u);
        }
    }
    for(unsigned n=0;n<4u;n++) {
        size_t bytes=request(frame,12u,42u,n,data);memset(guard,0xa5,sizeof(guard));
        assert(kui_vmu_volatile_handle(&card,frame,bytes,guard+1u,4u,&received)==1 && received==4u);
        assert(guard[0]==0xa5u && guard[5]==0xa5u && le32(guard+1u)==0x00010007u);
    }
    size_t bytes=request(frame,13u,42u,4u,NULL);memset(guard,0xa5,sizeof(guard));
    assert(kui_vmu_volatile_handle(&card,frame,bytes,guard+1u,4u,&received)==1 && received==4u);
    assert(guard[0]==0xa5u && guard[5]==0xa5u && le32(guard+1u)==0x00010007u);
    assert(card.ram_commits==1u && card.cached_blocks==1u && !base_reads);
}

static void fill_cache(void) {
    uint8_t data[512];
    for(unsigned block=0;block<16u;block++) {write_pattern(block,data);commit(block,data);}
    assert(card.cached_blocks==16u && card.ram_commits==16u && !base_reads);
}
static void full_cache_no_eviction_or_seventeenth_phase_ack(void) {
    uint8_t data[512],frame[140];write_pattern(99u,data);
    for(unsigned n=0;n<4u;n++) {
        start();fill_cache();commit(3u,data);read_compare(3u,data);
        assert(card.cached_blocks==16u && card.ram_commits==17u && !base_reads);
        size_t bytes=request(frame,12u,16u,n,data);
        struct kui_vmu_volatile before=card;
        memset(response,0xa5,sizeof(response));received=999u;
        assert(kui_vmu_volatile_handle(&card,frame,bytes,response,3u,&received)==KUI_VMU_VOLATILE_CAPACITY);
        assert(!received && !memcmp(&card,&before,sizeof(card)));
        assert(kui_vmu_volatile_handle(&card,frame,bytes,response,4u,&received)==KUI_VMU_VOLATILE_FULL);
        assert(!received && card.faulted && card.full_faults==1u && !base_reads);
        assert(card.ram_commits==17u && card.cached_blocks==16u && card.write_phases==before.write_phases);
        assert(!memcmp(&card.protocol,&before.protocol,sizeof(card.protocol)));
        assert(!memcmp(card.cache,before.cache,sizeof(card.cache)));
        for(unsigned i=0;i<sizeof(response);i++) assert(response[i]==0xa5u);
    }
    start();fill_cache();size_t bytes=request(frame,12u,16u,0u,data);assert(bytes==140u);
    kui_vmu_emulation_t before=card.protocol;
    assert(kui_vmu_volatile_admit(&card,le32(frame),le32(frame+4u),le32(frame+8u))==KUI_VMU_VOLATILE_FULL);
    assert(card.faulted && card.full_faults==1u && !memcmp(&before,&card.protocol,sizeof(before)) && !base_reads);
}

static void refused(const uint8_t *frame,size_t bytes) {
    struct kui_vmu_volatile before=card;unsigned reads=base_reads;
    memset(response,0xa5,sizeof(response));received=999u;
    assert(kui_vmu_volatile_handle(&card,frame,bytes,response,sizeof(response),&received)==KUI_VMU_VOLATILE_REFUSED);
    assert(!received && !card.faulted && card.refused==before.refused+1u && reads==base_reads);
    before.refused++;assert(!memcmp(&card,&before,sizeof(card)));
    for(unsigned i=0;i<sizeof(response);i++) assert(response[i]==0xa5u);
}
static void strict_fields_and_metadata_admission(void) {
    uint8_t frame[144],data[512];write_pattern(8u,data);start();
    for(unsigned cmd=0;cmd<256u;cmd++) {
        if(cmd==1u || cmd==2u || cmd==10u || cmd==11u || cmd==12u || cmd==13u) continue;
        size_t bytes=request(frame,11u,0u,0u,NULL);frame[0]=(uint8_t)cmd;refused(frame,bytes);
    }
    const unsigned commands[]={1u,2u,10u,11u,12u,13u};
    for(unsigned k=0;k<6u;k++) {
        unsigned cmd=commands[k];size_t bytes=request(frame,cmd,0u,cmd==13u?4u:0u,data);
        for(unsigned field=1u;field<=2u;field++) for(unsigned value=0;value<256u;value++) {
            if(value==(field==1u?1u:0u)) continue;
            request(frame,cmd,0u,cmd==13u?4u:0u,data);frame[field]=(uint8_t)value;refused(frame,bytes);
        }
        for(unsigned words=0;words<256u;words++) {
            if(words==(cmd<3u?0u:cmd==12u?34u:2u)) continue;
            request(frame,cmd,0u,cmd==13u?4u:0u,data);frame[3]=(uint8_t)words;refused(frame,bytes);
        }
        for(size_t n=0;n<=bytes+4u;n++) {
            if(n==bytes) continue;
            request(frame,cmd,0u,cmd==13u?4u:0u,data);memset(frame+140u,0,4u);
            uint8_t *short_frame=malloc(n?n:1u);assert(short_frame);memcpy(short_frame,frame,n);
            refused(short_frame,n);free(short_frame);
        }
        if(cmd<3u) continue;
        const unsigned malformed[]={4u,5u,6u,7u,8u,10u};
        for(unsigned i=0;i<sizeof(malformed)/sizeof(malformed[0]);i++) {
            request(frame,cmd,0u,cmd==13u?4u:0u,data);frame[malformed[i]]^=1u;refused(frame,bytes);
        }
        for(unsigned n=0;n<256u;n++) {
            if((cmd==12u && n<4u) || (cmd==13u && (n==1u || n==4u)) || ((cmd==10u || cmd==11u) && !n)) continue;
            request(frame,cmd,0u,cmd==13u?4u:0u,data);frame[9]=(uint8_t)n;refused(frame,bytes);
        }
    }
    request(frame,12u,42u,0u,data);
    struct kui_vmu_volatile before=card;
    assert(kui_vmu_volatile_admit(&card,le32(frame),le32(frame+4u),le32(frame+8u))==1);
    assert(!memcmp(&before,&card,sizeof(card)) && !base_reads);
    /* LCD-shaped CMD12 is rejected before protocol staging. */
    frame[3]=50u;frame[7]=4u;
    assert(kui_vmu_volatile_admit(&card,le32(frame),le32(frame+4u),le32(frame+8u))==0);
    assert(!card.protocol.active && !card.write_phases && !card.ram_commits && !base_reads);
}

static void protocol_order_and_sync_errors_are_terminal(void) {
    uint8_t data[512],changed[512],frame[140];write_pattern(1u,data);write_pattern(2u,changed);
    for(unsigned fault=0;fault<8u;fault++) {
        start();size_t bytes=0;
        switch(fault) {
        case 0:bytes=request(frame,12u,42u,1u,data);break;
        case 1:phase(42u,0u,data);bytes=request(frame,12u,42u,2u,data);break;
        case 2:phase(42u,0u,data);bytes=request(frame,12u,43u,1u,data);break;
        case 3:phase(42u,0u,data);phase(42u,1u,data);bytes=request(frame,12u,42u,1u,changed);break;
        case 4:phase(42u,0u,data);phase(42u,1u,data);phase(42u,2u,data);bytes=request(frame,13u,42u,4u,NULL);break;
        case 5:bytes=request(frame,13u,42u,1u,NULL);break;
        case 6:{uint8_t old[512];base_pattern(42u,old);read_compare(42u,old);bytes=request(frame,13u,43u,1u,NULL);break;}
        default:for(unsigned n=0;n<4u;n++) phase(42u,n,data);bytes=request(frame,13u,43u,4u,NULL);break;
        }
        unsigned reads=base_reads;uint32_t phases=card.write_phases;
        memset(response,0xa5,sizeof(response));received=999u;
        assert(kui_vmu_volatile_handle(&card,frame,bytes,response,4u,&received)==KUI_VMU_VOLATILE_INTERNAL);
        assert(!received && card.faulted && !card.ram_commits && !card.cached_blocks);
        assert(card.write_phases==phases && base_reads==reads);
        for(unsigned i=0;i<sizeof(response);i++) assert(response[i]==0xa5u);
        bytes=request(frame,1u,0u,0u,NULL);
        assert(kui_vmu_volatile_handle(&card,frame,bytes,response,sizeof(response),&received)==KUI_VMU_VOLATILE_INTERNAL);
        assert(!received && base_reads==reads);
    }
}
static void base_failure_preserves_ram_and_stops_reads(void) {
    uint8_t data[512],frame[140];write_pattern(4u,data);start();commit(42u,data);
    uint8_t cached[sizeof(card.cache)];memcpy(cached,card.cache,sizeof(cached));base_fail=true;
    size_t bytes=request(frame,11u,43u,0u,NULL);received=999u;
    assert(kui_vmu_volatile_handle(&card,frame,bytes,response,sizeof(response),&received)==KUI_VMU_VOLATILE_INTERNAL);
    assert(!received && card.faulted && base_reads==1u && card.ram_commits==1u);
    assert(!memcmp(card.cache,cached,sizeof(cached)));
    bytes=request(frame,11u,42u,0u,NULL);memset(response,0xa5,sizeof(response));
    assert(kui_vmu_volatile_handle(&card,frame,bytes,response,sizeof(response),&received)==KUI_VMU_VOLATILE_INTERNAL);
    assert(!received && base_reads==1u);
    for(unsigned i=0;i<sizeof(response);i++) assert(response[i]==0xa5u);
}
static void invalid_api_arguments(void) {
    uint8_t frame[140];start();size_t bytes=request(frame,1u,0u,0u,NULL);received=999u;
    assert(kui_vmu_volatile_init(NULL,read_base,NULL)==KUI_VMU_VOLATILE_ARGUMENT);
    assert(kui_vmu_volatile_admit(NULL,0u,0u,0u)==KUI_VMU_VOLATILE_ARGUMENT);
    assert(kui_vmu_volatile_handle(NULL,frame,bytes,response,sizeof(response),&received)==KUI_VMU_VOLATILE_ARGUMENT && !received);
    assert(kui_vmu_volatile_handle(&card,NULL,bytes,response,sizeof(response),&received)==KUI_VMU_VOLATILE_ARGUMENT);
    assert(kui_vmu_volatile_handle(&card,frame,bytes,NULL,sizeof(response),&received)==KUI_VMU_VOLATILE_ARGUMENT);
    assert(kui_vmu_volatile_handle(&card,frame,bytes,response,sizeof(response),NULL)==KUI_VMU_VOLATILE_ARGUMENT);
    assert(kui_vmu_volatile_init(&card,NULL,NULL)==KUI_VMU_VOLATILE_ARGUMENT && !card.ready);
    assert(kui_vmu_volatile_admit(&card,0u,0u,0u)==KUI_VMU_VOLATILE_ARGUMENT && !base_reads);
}

int main(void) {
    identity_base_reads_and_read_status();phase_retries_committed_reads_and_overwrite();
    capacity_and_exact_four_byte_outputs();full_cache_no_eviction_or_seventeenth_phase_ack();
    strict_fields_and_metadata_admission();protocol_order_and_sync_errors_are_terminal();
    base_failure_preserves_ram_and_stops_reads();invalid_api_arguments();
    puts("VMU volatile wrapper: phase/sync retries, committed RAM reads, strict admission and16-slot terminal limits passed");
    return 0;
}
