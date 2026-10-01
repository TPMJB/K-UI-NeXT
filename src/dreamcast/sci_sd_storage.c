/* SPDX-License-Identifier: GPL-3.0-only */
#include "sci_sd_storage.h"

#define READY_TICKS (500u * 12500u)

static uint8_t byte(struct kui_loader_sd *c, uint8_t value) {
    return c->bus.transfer(c->bus.ctx,value,c->slow);
}
static bool ready(struct kui_loader_sd *c) {
    uint32_t start=c->bus.ticks(c->bus.ctx);
    for(unsigned tries=0;tries<1000000u;tries++) {
        if(byte(c,0xff)==0xff) return true;
        if((uint32_t)(c->bus.ticks(c->bus.ctx)-start)>=READY_TICKS) break;
    }
    return false;
}
static uint8_t crc7(const uint8_t packet[5]) {
    uint8_t crc=0;
    for(unsigned i=0;i<5;i++) {
        uint8_t value=packet[i];
        for(unsigned bit=0;bit<8;bit++) {
            crc=(uint8_t)(crc<<1);
            if((crc^value)&0x80u) crc^=9u;
            value=(uint8_t)(value<<1);
        }
    }
    return (uint8_t)((crc<<1)|1u);
}
static enum kui_loader_sd_result command(struct kui_loader_sd *c,uint8_t cmd,uint32_t arg) {
    c->last_command=cmd;c->last_response=0xff;
    if(!ready(c)) return KUI_LOADER_SD_TIMEOUT;
    uint8_t packet[6]={(uint8_t)(0x40u|cmd),(uint8_t)(arg>>24),
        (uint8_t)(arg>>16),(uint8_t)(arg>>8),(uint8_t)arg,0};
    packet[5]=crc7(packet);
    for(unsigned i=0;i<6;i++) (void)byte(c,packet[i]);
    for(unsigned i=0;i<20;i++) {
        uint8_t reply=byte(c,0xff);
        if(!(reply&0x80u)) {
            c->last_response=reply;
            return reply==0?KUI_LOADER_SD_OK:KUI_LOADER_SD_COMMAND;
        }
    }
    return KUI_LOADER_SD_TIMEOUT;
}
static uint16_t crc16(uint16_t crc,uint8_t value) {
    uint32_t x=(crc>>8)^value;x^=x>>4;
    return (uint16_t)((crc<<8)^(x<<12)^(x<<5)^x);
}
static void release(struct kui_loader_sd *c) {
    c->bus.select(c->bus.ctx,false);(void)byte(c,0xff);
}
static enum kui_loader_sd_result status(struct kui_loader_sd *c) {
    enum kui_loader_sd_result result=command(c,13,0);
    if(result!=KUI_LOADER_SD_OK) return result;
    c->last_response=byte(c,0xff);
    return c->last_response==0?KUI_LOADER_SD_OK:KUI_LOADER_SD_COMMAND;
}
enum kui_loader_sd_result kui_sci_sd_sync(struct kui_loader_sd *c) {
    if(!c) return KUI_LOADER_SD_ARGUMENT;
    if(!c->ready) return KUI_LOADER_SD_NOT_READY;
    c->bus.select(c->bus.ctx,true);
    enum kui_loader_sd_result result=status(c);
    release(c);
    if(result!=KUI_LOADER_SD_OK) c->ready=false;
    return result;
}
enum kui_loader_sd_result kui_sci_sd_write(struct kui_loader_sd *c,uint32_t lba,
                      uint32_t count,const uint8_t *data) {
    if(!c || !data || !count) return KUI_LOADER_SD_ARGUMENT;
    if(!c->ready) return KUI_LOADER_SD_NOT_READY;
    if((uint64_t)lba+count>c->blocks ||
       (!c->high_capacity && (uint64_t)lba+count>(UINT64_C(1)<<23)))
        return KUI_LOADER_SD_RANGE;
    c->bus.select(c->bus.ctx,true);
    bool multi=count>1;
    /* Even an uncertain CMD25 reply requires a stop token before deselect. */
    enum kui_loader_sd_result result=command(c,multi?25:24,c->high_capacity?lba:lba<<9);
    for(uint32_t block=0;result==KUI_LOADER_SD_OK && block<count;block++) {
        if(!ready(c)) {result=KUI_LOADER_SD_TIMEOUT;c->last_response=0xff;break;}
        (void)byte(c,multi?0xfc:0xfe);
        uint16_t crc=0;
        for(unsigned i=0;i<512;i++) {
            uint8_t value=*data++;crc=crc16(crc,value);(void)byte(c,value);
        }
        (void)byte(c,(uint8_t)(crc>>8));(void)byte(c,(uint8_t)crc);
        uint8_t reply=0xff;
        for(unsigned i=0;i<20 && reply==0xff;i++) reply=byte(c,0xff);
        if((reply&0x1fu)!=5u) {
            c->last_response=reply;
            result=reply==0xff?KUI_LOADER_SD_TIMEOUT:
                (reply&0x1fu)==0x0bu?KUI_LOADER_SD_CRC:KUI_LOADER_SD_TOKEN;
        }
        if(!ready(c) && result==KUI_LOADER_SD_OK) {
            result=KUI_LOADER_SD_TIMEOUT;c->last_response=0xff;
        }
    }
    if(multi) {
        /* Stop only after the device has released busy; never send a command
         * into a still-programming card. A failed stop poisons this session. */
        bool stopped=false;
        if(ready(c)) {(void)byte(c,0xfd);stopped=ready(c);}
        if(!stopped && result==KUI_LOADER_SD_OK) {
            result=KUI_LOADER_SD_TIMEOUT;c->last_response=0xff;
        }
    }
    if(result==KUI_LOADER_SD_OK) result=status(c);
    release(c);
    if(result!=KUI_LOADER_SD_OK) c->ready=false;
    return result;
}
