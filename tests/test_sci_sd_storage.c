/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/dreamcast/sci_sd_storage.h"
#include <assert.h>
#include <string.h>

struct mock {
    bool selected,multi,writing,busy_forever,status_error,no_command_reply,no_data_reply;
    uint8_t reject,command_reject;
    unsigned packets,stops,blocks,frame_n,data_n,reply_n,busy;
    uint32_t now,address;
    uint8_t frame[6],data[514],reply[2];
};
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
    struct mock *m=ctx;(void)slow;m->now+=125000;
    if(!m->selected) return 0xff;
    if(m->reply_n) {uint8_t r=m->reply[0];m->reply[0]=m->reply[1];--m->reply_n;return r;}
    if(m->busy_forever && m->blocks) return 0;
    if(m->busy) {--m->busy;return 0;}
    if(m->data_n) {
        m->data[m->data_n-1]=value;
        if(++m->data_n==515) {
            uint16_t crc=reference_crc(m->data);
            assert(m->data[512]==(uint8_t)(crc>>8) && m->data[513]==(uint8_t)crc);
            ++m->blocks;m->data_n=0;
            m->reply[0]=m->no_data_reply?0xff:m->reject?m->reject:5;
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
            if(cmd==24 || cmd==25) {
                m->writing=true;m->multi=cmd==25;
                m->reply[0]=m->no_command_reply?0xff:m->command_reject;
                m->address=((uint32_t)m->frame[1]<<24)|((uint32_t)m->frame[2]<<16)|
                    ((uint32_t)m->frame[3]<<8)|m->frame[4];
            } else {assert(cmd==13);m->reply[1]=m->status_error?0x20:0;m->reply_n=2;}
        }
    }
    return 0xff;
}
static struct kui_loader_sd card(struct mock *m) {
    return (struct kui_loader_sd){.bus={.ctx=m,.select=select_card,.transfer=transfer,.ticks=ticks},
        .blocks=10000,.high_capacity=true,.ready=true};
}
int main(void) {
    uint8_t data[1024];for(unsigned i=0;i<sizeof(data);i++) data[i]=(uint8_t)(i*29u+7u);
    struct mock m={0};struct kui_loader_sd c=card(&m);
    assert(kui_sci_sd_write(&c,17,2,data)==KUI_LOADER_SD_OK);
    assert(m.blocks==2 && m.stops==1 && !m.selected && m.address==17 && !m.busy);
    assert(kui_sci_sd_sync(&c)==KUI_LOADER_SD_OK);
    m=(struct mock){0};c=card(&m);c.high_capacity=false;
    assert(kui_sci_sd_write(&c,17,1,data)==KUI_LOADER_SD_OK && m.address==17u*512 && m.stops==0);
    m=(struct mock){.reject=0x0b};c=card(&m);
    assert(kui_sci_sd_write(&c,17,2,data)==KUI_LOADER_SD_CRC && !c.ready && !m.selected && m.stops==1);
    assert(c.last_command==25 && c.last_response==0x0b);
    unsigned packets=m.packets;
    assert(kui_sci_sd_write(&c,17,1,data)==KUI_LOADER_SD_NOT_READY && m.packets==packets);
    assert(kui_sci_sd_sync(&c)==KUI_LOADER_SD_NOT_READY && m.packets==packets);
    m=(struct mock){.reject=0x0d};c=card(&m);
    assert(kui_sci_sd_write(&c,17,1,data)==KUI_LOADER_SD_TOKEN && !c.ready && !m.selected);
    assert(c.last_command==24 && c.last_response==0x0d);
    m=(struct mock){.busy_forever=true};c=card(&m);
    assert(kui_sci_sd_write(&c,17,1,data)==KUI_LOADER_SD_TIMEOUT && !c.ready && !m.selected);
    assert(c.last_command==24 && c.last_response==0xff);
    /* Preserve the first failure even if the subsequent busy/stop phase
     * fails too; never overwrite a CRC rejection with a generic timeout. */
    m=(struct mock){.reject=0x0b,.busy_forever=true};c=card(&m);
    assert(kui_sci_sd_write(&c,17,2,data)==KUI_LOADER_SD_CRC && !c.ready && !m.selected);
    assert(c.last_command==25 && c.last_response==0x0b && m.stops==0);
    m=(struct mock){.command_reject=4};c=card(&m);
    assert(kui_sci_sd_write(&c,17,2,data)==KUI_LOADER_SD_COMMAND && !c.ready && !m.selected);
    assert(c.last_command==25 && c.last_response==4 && m.stops==1 && m.blocks==0);
    m=(struct mock){.no_command_reply=true};c=card(&m);
    assert(kui_sci_sd_write(&c,17,2,data)==KUI_LOADER_SD_TIMEOUT && !c.ready && !m.selected);
    assert(c.last_command==25 && c.last_response==0xff && m.stops==1 && m.blocks==0);
    m=(struct mock){.no_data_reply=true};c=card(&m);
    assert(kui_sci_sd_write(&c,17,2,data)==KUI_LOADER_SD_TIMEOUT && !c.ready && !m.selected);
    assert(c.last_command==25 && c.last_response==0xff && m.stops==1 && m.blocks==1);
    m=(struct mock){.status_error=true};c=card(&m);
    assert(kui_sci_sd_write(&c,17,1,data)==KUI_LOADER_SD_COMMAND && !c.ready && !m.selected);
    assert(c.last_command==13 && c.last_response==0x20);
    m=(struct mock){.status_error=true};c=card(&m);
    assert(kui_sci_sd_sync(&c)==KUI_LOADER_SD_COMMAND && !c.ready && !m.selected);
    assert(c.last_command==13 && c.last_response==0x20);
    m=(struct mock){0};c=card(&m);
    assert(kui_sci_sd_write(&c,9999,2,data)==KUI_LOADER_SD_RANGE && m.packets==0 && !m.selected);
    assert(kui_sci_sd_write(&c,17,0,data)==KUI_LOADER_SD_ARGUMENT && m.packets==0);
    assert(kui_sci_sd_write(&c,17,1,NULL)==KUI_LOADER_SD_ARGUMENT && m.packets==0);
    assert(kui_sci_sd_write(NULL,17,1,data)==KUI_LOADER_SD_ARGUMENT);
    assert(kui_sci_sd_sync(NULL)==KUI_LOADER_SD_ARGUMENT);
    return 0;
}
