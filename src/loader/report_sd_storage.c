/* SPDX-License-Identifier: GPL-3.0-only */
/* Terminal-report-only derivative of src/dreamcast/sci_sd_storage.c.
 * Exact one-sector CMD24 and CMD13 status framing; no block-transfer path,
 * multiwrite, initialization, retry, DMA or hardware timer access. */
#include "kui/report_sd_storage.h"

#define READY_TICKS (500u*12500u)
static enum kui_loader_sd_result validate(const struct kui_loader_sd *c) {
    if(!c || !c->bus.select || !c->bus.transfer || !c->bus.ticks ||
       c->bus.begin || c->bus.end || c->bus.transfer_block || !c->blocks ||
       c->blocks>(UINT64_C(1)<<32) ||
       (!c->high_capacity && c->blocks>(UINT64_C(1)<<23)))return KUI_LOADER_SD_ARGUMENT;
    return c->ready && !c->slow?KUI_LOADER_SD_OK:KUI_LOADER_SD_NOT_READY;
}
static uint8_t byte(struct kui_loader_sd *c,uint8_t value) {
    return c->bus.transfer(c->bus.ctx,value,false);
}
static bool ready(struct kui_loader_sd *c) {
    uint32_t start=c->bus.ticks(c->bus.ctx);
    /* ticks is bounded nominal byte-work, not elapsed console time. */
    for(unsigned tries=0;tries<1000000u;tries++) {
        if(byte(c,0xffu)==0xffu)return true;
        if((uint32_t)(c->bus.ticks(c->bus.ctx)-start)>=READY_TICKS)break;
    }
    return false;
}
static uint8_t crc7(const uint8_t packet[5]) {
    uint8_t crc=0u;
    for(unsigned i=0;i<5u;i++) {
        uint8_t value=packet[i];
        for(unsigned bit=0;bit<8u;bit++) {
            crc=(uint8_t)(crc<<1);if((crc^value)&0x80u)crc^=9u;
            value=(uint8_t)(value<<1);
        }
    }
    return (uint8_t)((crc<<1)|1u);
}
static enum kui_loader_sd_result command(struct kui_loader_sd *c,uint8_t cmd,uint32_t arg) {
    c->last_command=cmd;c->last_response=0xffu;
    if(!ready(c))return KUI_LOADER_SD_TIMEOUT;
    uint8_t packet[6]={(uint8_t)(0x40u|cmd),(uint8_t)(arg>>24),
        (uint8_t)(arg>>16),(uint8_t)(arg>>8),(uint8_t)arg,0};
    packet[5]=crc7(packet);
    for(unsigned i=0;i<6u;i++)(void)byte(c,packet[i]);
    for(unsigned i=0;i<20u;i++) {
        uint8_t response=byte(c,0xffu);
        if(!(response&0x80u)) {
            c->last_response=response;
            return response?KUI_LOADER_SD_COMMAND:KUI_LOADER_SD_OK;
        }
    }
    return KUI_LOADER_SD_TIMEOUT;
}
static void release(struct kui_loader_sd *c) {
    c->bus.select(c->bus.ctx,false);(void)byte(c,0xffu);
}
static enum kui_loader_sd_result status(struct kui_loader_sd *c) {
    enum kui_loader_sd_result result=command(c,13u,0u);
    if(result!=KUI_LOADER_SD_OK)return result;
    c->last_response=byte(c,0xffu);
    return c->last_response?KUI_LOADER_SD_COMMAND:KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_report_sd_sync(struct kui_loader_sd *c) {
    enum kui_loader_sd_result result=validate(c);if(result)return result;
    c->bus.select(c->bus.ctx,true);result=status(c);release(c);
    if(result)c->ready=false;
    return result;
}
enum kui_loader_sd_result kui_report_sd_write(struct kui_loader_sd *c,uint32_t lba,
    const uint8_t data[512]) {
    if(!data)return KUI_LOADER_SD_ARGUMENT;
    enum kui_loader_sd_result result=validate(c);if(result)return result;
    if((uint64_t)lba>=c->blocks)return KUI_LOADER_SD_RANGE;
    c->bus.select(c->bus.ctx,true);
    result=command(c,24u,c->high_capacity?lba:lba<<9);
    if(result==KUI_LOADER_SD_OK) {
        if(!ready(c)) {result=KUI_LOADER_SD_TIMEOUT;c->last_response=0xffu;}
        else {
            (void)byte(c,0xfeu);uint16_t crc=0u;
            for(unsigned i=0;i<512u;i++) {
                uint8_t value=data[i];uint32_t x=(crc>>8)^value;x^=x>>4;
                crc=(uint16_t)((crc<<8)^(x<<12)^(x<<5)^x);(void)byte(c,value);
            }
            (void)byte(c,(uint8_t)(crc>>8));(void)byte(c,(uint8_t)crc);
            uint8_t response=0xffu;
            for(unsigned i=0;i<20u && response==0xffu;i++)response=byte(c,0xffu);
            if((response&0x1fu)!=5u) {
                c->last_response=response;
                result=response==0xffu?KUI_LOADER_SD_TIMEOUT:
                    (response&0x1fu)==0x0bu?KUI_LOADER_SD_CRC:KUI_LOADER_SD_TOKEN;
            }
            /* Preserve a first CRC/data rejection if later busy also fails. */
            if(!ready(c) && result==KUI_LOADER_SD_OK) {
                result=KUI_LOADER_SD_TIMEOUT;c->last_response=0xffu;
            }
        }
    }
    if(result==KUI_LOADER_SD_OK)result=status(c);
    release(c);if(result)c->ready=false;
    return result;
}
