/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/report_sd_storage.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

struct mock {
    bool selected,multi,writing,busy_forever,status_error,no_command_reply,no_data_reply;
    bool initial_busy,clock_stuck;unsigned short_after,transfers;
    uint8_t reject,command_reject;
    unsigned packets,stops,blocks,frame_n,data_n,reply_n,busy;
    uint32_t now,address;
    uint8_t frame[6],data[514],reply[2];
    unsigned block_calls,block_bytes,fail_block_call,fail_block_after,fault_calls;
    unsigned bad_crc_call,crc_rejections;
    bool block_fault;
    uint64_t wire_hash;
};
static bool use_blocks,slow_path;
static uint16_t reference_crc(const uint8_t *p) {
    uint16_t crc=0;
    for(unsigned i=0;i<512;i++) for(int bit=7;bit>=0;bit--) {
        bool flip=((crc>>15)^((p[i]>>bit)&1u))!=0;
        crc=(uint16_t)(crc<<1);if(flip) crc^=0x1021;
    }
    return crc;
}
static void select_card(void *ctx,bool selected) {((struct mock *)ctx)->selected=selected;}
static uint32_t ticks(void *ctx) {return ((struct mock *)ctx)->now;}
static uint8_t transfer(void *ctx,uint8_t value,bool slow) {
    struct mock *m=ctx;if(!m->clock_stuck)m->now+=125000u;++m->transfers;
    assert(!slow);
    m->wire_hash=(m->wire_hash^(value|((unsigned)m->selected<<8)|
                              ((unsigned)slow<<9)))*UINT64_C(1099511628211);
    if(m->block_fault) {++m->fault_calls;return 0xff;}
    if(!m->selected) return 0xff;
    if(m->initial_busy)return 0u;
    if(m->reply_n) {uint8_t r=m->reply[0];m->reply[0]=m->reply[1];--m->reply_n;return r;}
    if(m->busy_forever && m->blocks) return 0;
    if(m->busy) {--m->busy;return 0;}
    if(m->data_n) {
        if(m->short_after && m->data_n>m->short_after) {m->block_fault=true;return 0xffu;}
        m->data[m->data_n-1]=value;
        if(++m->data_n==515) {
            uint16_t crc=reference_crc(m->data);
            bool crc_ok=m->data[512]==(uint8_t)(crc>>8) && m->data[513]==(uint8_t)crc;
            assert(crc_ok || (m->bad_crc_call && m->block_calls==m->bad_crc_call));
            if(!crc_ok) ++m->crc_rejections;
            ++m->blocks;m->data_n=0;
            m->reply[0]=!crc_ok?0x0b:m->no_data_reply?0xff:m->reject?m->reject:5;
            m->reply_n=1;m->busy=m->no_data_reply?0:3;
            if(!m->multi) m->writing=false;
        }
        return 0xff;
    }
    if(m->writing && (value==0xfc || value==0xfe)) {m->data_n=1;return 0xff;}
    if(m->writing && value==0xfd) {m->writing=false;++m->stops;return 0xff;}
    if(m->frame_n || (value&0xc0u)==0x40u) {
        m->frame[m->frame_n++]=value;
        if(m->frame_n==6) {
            ++m->packets;m->frame_n=0;m->reply[0]=0;m->reply_n=1;
            unsigned cmd=m->frame[0]&63u;
            assert(cmd==24u || cmd==13u);
            if(cmd==24) {
                m->writing=true;m->multi=cmd==25;
                m->reply[0]=m->no_command_reply?0xff:m->command_reject;
                m->address=((uint32_t)m->frame[1]<<24)|((uint32_t)m->frame[2]<<16)|
                    ((uint32_t)m->frame[3]<<8)|m->frame[4];
            } else {assert(cmd==13);m->reply[1]=m->status_error?0x20:0;m->reply_n=2;}
        }
    }
    return 0xff;
}
static bool transfer_block(void *ctx,const uint8_t *tx,uint8_t *rx,
                           size_t count,bool slow,uint16_t *crc_out) {
    struct mock *m=ctx;
    assert(tx && !rx && count==512 && !slow && crc_out);
    ++m->block_calls;
    for(size_t i=0;i<count;i++) {
        if(m->block_calls==m->fail_block_call && i==m->fail_block_after) {
            m->block_fault=true;return false;
        }
        (void)transfer(ctx,tx[i],false);++m->block_bytes;
    }
    *crc_out=reference_crc(tx)^(m->block_calls==m->bad_crc_call);
    return true;
}
static struct kui_loader_sd card(struct mock *m) {
    return (struct kui_loader_sd){.bus={.ctx=m,.select=select_card,.transfer=transfer,.ticks=ticks,
        .transfer_block=use_blocks?transfer_block:NULL},
        .blocks=10000,.high_capacity=true,.ready=true,.slow=slow_path};
}
static void no_begin(void *context) {(void)context;assert(false);}
static void check_refusal(struct kui_loader_sd *c,const uint8_t data[512],enum kui_loader_sd_result result) {
    struct mock *m=c->bus.ctx;unsigned transfers=m->transfers;
    assert(kui_report_sd_write(c,17u,data)==result && m->transfers==transfers && !m->selected);
    assert(kui_report_sd_sync(c)==result && m->transfers==transfers && !m->selected);
}
int main(void) {
    uint8_t data[512];for(unsigned i=0;i<512u;i++)data[i]=(uint8_t)(i*29u+7u);
    use_blocks=false;slow_path=false;
    struct mock m={0};struct kui_loader_sd c=card(&m);
    assert(kui_report_sd_write(&c,17u,data)==KUI_LOADER_SD_OK);
    assert(m.blocks==1u && m.stops==0u && !m.selected && m.address==17u && !m.busy);
    assert(!m.block_calls && !m.block_bytes && m.packets==2u && c.last_command==13u);
    assert(!memcmp(m.data,data,512u));
    assert(kui_report_sd_sync(&c)==KUI_LOADER_SD_OK && m.packets==3u);
    m=(struct mock){0};c=card(&m);c.high_capacity=false;
    assert(kui_report_sd_write(&c,17u,data)==KUI_LOADER_SD_OK && m.address==17u*512u);
    for(unsigned kind=0u;kind<10u;kind++) {
        m=(struct mock){0};c=card(&m);enum kui_loader_sd_result expected=KUI_LOADER_SD_TIMEOUT;
        if(kind==0u) {m.reject=0x0bu;expected=KUI_LOADER_SD_CRC;}
        if(kind==1u) {m.reject=0x0du;expected=KUI_LOADER_SD_TOKEN;}
        if(kind==2u)m.busy_forever=true;
        if(kind==3u) {m.reject=0x0bu;m.busy_forever=true;expected=KUI_LOADER_SD_CRC;}
        if(kind==4u) {m.command_reject=4u;expected=KUI_LOADER_SD_COMMAND;}
        if(kind==5u)m.no_command_reply=true;
        if(kind==6u)m.no_data_reply=true;
        if(kind==7u) {m.status_error=true;expected=KUI_LOADER_SD_COMMAND;}
        if(kind==8u)m.short_after=17u;
        if(kind==9u) {m.initial_busy=true;m.clock_stuck=true;}
        assert(kui_report_sd_write(&c,17u,data)==expected);
        assert(!c.ready && !m.selected && !m.stops && !m.block_calls && !m.block_bytes);
        assert(m.transfers<1001000u && m.packets<=2u);
        if(kind==0u || kind==3u)assert(c.last_response==0x0bu);
        if(kind==8u)assert(m.block_fault && m.data_n==18u && !memcmp(m.data,data,17u));
        if(kind==9u)assert(m.transfers==1000001u && !m.packets);
        unsigned transfers=m.transfers;
        assert(kui_report_sd_write(&c,17u,data)==KUI_LOADER_SD_NOT_READY);
        assert(kui_report_sd_sync(&c)==KUI_LOADER_SD_NOT_READY && m.transfers==transfers);
    }
    m=(struct mock){.status_error=true};c=card(&m);
    assert(kui_report_sd_sync(&c)==KUI_LOADER_SD_COMMAND && !c.ready && !m.selected);
    m=(struct mock){.initial_busy=true,.clock_stuck=true};c=card(&m);
    assert(kui_report_sd_sync(&c)==KUI_LOADER_SD_TIMEOUT && !c.ready && !m.selected);
    assert(m.transfers==1000001u);
    m=(struct mock){0};c=card(&m);c.bus.begin=no_begin;check_refusal(&c,data,KUI_LOADER_SD_ARGUMENT);
    c=card(&m);c.bus.end=no_begin;check_refusal(&c,data,KUI_LOADER_SD_ARGUMENT);
    c=card(&m);c.bus.transfer_block=transfer_block;check_refusal(&c,data,KUI_LOADER_SD_ARGUMENT);
    c=card(&m);c.bus.select=NULL;check_refusal(&c,data,KUI_LOADER_SD_ARGUMENT);
    c=card(&m);c.bus.transfer=NULL;check_refusal(&c,data,KUI_LOADER_SD_ARGUMENT);
    c=card(&m);c.bus.ticks=NULL;check_refusal(&c,data,KUI_LOADER_SD_ARGUMENT);
    c=card(&m);c.slow=true;check_refusal(&c,data,KUI_LOADER_SD_NOT_READY);
    c=card(&m);c.ready=false;check_refusal(&c,data,KUI_LOADER_SD_NOT_READY);
    c=card(&m);c.blocks=0u;check_refusal(&c,data,KUI_LOADER_SD_ARGUMENT);
    c=card(&m);c.blocks=(UINT64_C(1)<<32)+1u;check_refusal(&c,data,KUI_LOADER_SD_ARGUMENT);
    c=card(&m);c.high_capacity=false;c.blocks=(UINT64_C(1)<<23)+1u;
    check_refusal(&c,data,KUI_LOADER_SD_ARGUMENT);
    c=card(&m);assert(kui_report_sd_write(&c,10000u,data)==KUI_LOADER_SD_RANGE && !m.transfers);
    assert(kui_report_sd_write(&c,17u,NULL)==KUI_LOADER_SD_ARGUMENT && !m.transfers);
    assert(kui_report_sd_write(NULL,17u,data)==KUI_LOADER_SD_ARGUMENT);
    assert(kui_report_sd_sync(NULL)==KUI_LOADER_SD_ARGUMENT);
    static const uint16_t expected[]={0x0000u,0x7fa1u,0x31c3u};
    for(unsigned vector=0;vector<3u;vector++) {
        memset(data,vector==1u?0xff:0u,512u);if(vector==2u)memcpy(data+503u,"123456789",9u);
        assert(reference_crc(data)==expected[vector]);m=(struct mock){0};c=card(&m);
        assert(kui_report_sd_write(&c,17u,data)==KUI_LOADER_SD_OK);
        assert(m.data[512u]==(uint8_t)(expected[vector]>>8) && m.data[513u]==(uint8_t)expected[vector]);
    }
    puts("terminal report CMD24 protocol model passed");return 0;
}
