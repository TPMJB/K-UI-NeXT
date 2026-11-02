/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_gd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if !KUI_NATIVE_SESSION_CONTROLS
#error "This test exercises the opt-in native disc lifecycle"
#endif
#define BEGIN UINT32_C(0x8c010000)
#define PARAM BEGIN
#define STATUS (BEGIN+64u)
#define OUTPUT (BEGIN+512u)
static uint8_t ram[8192];
static struct kui_retail_manifest maps[2];
static struct kui_retail_gd service;
static unsigned checks,reads,selected,assertions;
#define CHECK(x) do {++assertions;assert(x);} while(0)
static void put(uint32_t address,uint32_t value) {
    for(unsigned i=0;i<4;i++)ram[address-BEGIN+i]=(uint8_t)(value>>(8u*i));
}
static uint32_t get(uint32_t address) {
    uint32_t result=0;for(unsigned i=0;i<4;i++)result|=(uint32_t)ram[address-BEGIN+i]<<(8u*i);
    return result;
}
static uint8_t *map(void *unused,uint32_t address,uint32_t bytes,int writing) {
    (void)unused;(void)writing;
    if(address<BEGIN || bytes>sizeof(ram) || address-BEGIN>sizeof(ram)-bytes)return NULL;
    return ram+address-BEGIN;
}
static int range(void *unused,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)unused;++checks;
    return lba<45000u || lba>=maps[selected].slots[2].track.end_lba ||
        count>maps[selected].slots[2].track.end_lba-lba || bytes!=2048u?-1:0;
}
static int read_disc(void *unused,uint32_t lba,uint32_t count,uint32_t bytes,void *output) {
    (void)unused;(void)lba;++reads;memset(output,selected?0xb2:0xa1,(size_t)count*bytes);return 0;
}
static int32_t call(uint32_t function,uint32_t a,uint32_t b) {
    return kui_retail_gd_dispatch(&service,a,b,0,function);
}
static int32_t read_request(unsigned count) {
    put(PARAM,45150u);put(PARAM+4u,count);put(PARAM+8u,OUTPUT);put(PARAM+12u,0);
    return call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM);
}
static void init(void) {
    memset(ram,0xcc,sizeof(ram));memset(maps,0,sizeof(maps));selected=0;checks=reads=0;
    for(unsigned i=0;i<2;i++) {
        maps[i].track_count=3;maps[i].session_lba=45000;
        maps[i].slots[0].track=(struct kui_retail_track){.start_lba=0,.end_lba=8,.control=4};
        maps[i].slots[1].track=(struct kui_retail_track){.start_lba=100,.end_lba=108};
        maps[i].slots[2].track=(struct kui_retail_track){.start_lba=45000,.end_lba=45016u+i*16u,.control=4};
    }
    const struct kui_gd_ops ops={NULL,map,range,read_disc};
    CHECK(kui_retail_gd_init(&service,maps[0].slots,3,&ops,BEGIN,BEGIN+sizeof(ram))==0);
    CHECK(service.disc_generation==1 && service.disc_lid==KUI_RETAIL_DISC_READY);
}
static void lifecycle(void) {
    init();int32_t token=read_request(2);CHECK(token>0);
#ifndef KUI_RETAIL_GD_ASYNC
    service.step=1;CHECK(call(KUI_GD_EXEC,0,0)==0 && reads==1 && service.completed_bytes==2048);
#else
    kui_retail_gd_progress_epoch(&service,service.disc_generation,(uint32_t)token,1,0);
    CHECK(service.completed_bytes==2048 && reads==0);
#endif
    uint32_t before=service.disc_generation;uint8_t preserved[4096];memcpy(preserved,ram+512,sizeof(preserved));
    CHECK(kui_retail_gd_disc_open(&service)==0);
    CHECK(service.disc_generation==before+1u && service.disc_changed && !service.pending);
    CHECK(service.command==KUI_GD_DMAREAD && service.status==KUI_GD_FAILED && service.error==KUI_GD_ERROR_UNAVAILABLE);
    struct kui_retail_gd snapshot=service;
    CHECK(kui_retail_gd_disc_close(&service,&maps[1])==-1 && !memcmp(&service,&snapshot,sizeof(service)));
    CHECK(kui_retail_gd_disc_open(&service)==-1 && !memcmp(&service,&snapshot,sizeof(service)));
    CHECK(call(KUI_GD_EXEC,0,0)==0 && !memcmp(preserved,ram+512,sizeof(preserved)));
#ifdef KUI_RETAIL_GD_ASYNC
    kui_retail_gd_progress_epoch(&service,before,(uint32_t)token,2,0);
    CHECK(service.completed_bytes==2048 && service.status==KUI_GD_FAILED);
#endif
    CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==6 && get(STATUS+4u)==0);
    CHECK(service.disc_observed);
    CHECK(kui_retail_gd_disc_absent(&service)==0);
    CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==7 && get(STATUS+4u)==0);
    CHECK(kui_retail_gd_disc_close(&service,&maps[1])==0);selected=1;
    CHECK(service.tracks==maps[1].slots && service.position_lba==45000 && service.sector_bytes==2048);
    CHECK(service.command==KUI_GD_DMAREAD && service.status==KUI_GD_FAILED);
    CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(get(STATUS+4u)==KUI_GD_ERROR_UNAVAILABLE && get(STATUS+8u)==2048);
    CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_NOT_FOUND);
    token=read_request(1);CHECK(token>0);
#ifdef KUI_RETAIL_GD_ASYNC
    kui_retail_gd_progress_epoch(&service,before,(uint32_t)token,1,0);
    CHECK(service.pending && !service.completed_bytes);
    kui_retail_gd_progress_epoch(&service,service.disc_generation,(uint32_t)token-1u,1,0);
    CHECK(service.pending && !service.completed_bytes);
    kui_retail_gd_progress_epoch(&service,service.disc_generation,(uint32_t)token,1,0);
#else
    CHECK(call(KUI_GD_EXEC,0,0)==0 && reads==2);
    for(unsigned i=0;i<2048;i++)CHECK(ram[512u+i]==0xb2);
#endif
    CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    put(PARAM,1);put(PARAM+4u,OUTPUT);
    token=call(KUI_GD_REQUEST,KUI_GD_GETTOC2,PARAM);CHECK(token>0);
    CHECK(call(KUI_GD_EXEC,0,0)==0 && call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    CHECK((get(OUTPUT+404u)&0x00ffffffu)==maps[1].slots[2].track.end_lba+150u);
    CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==1 && get(STATUS+4u)==0x80);
}
static void no_media_and_refusals(void) {
    init();service.executing=1;struct kui_retail_gd snapshot=service;
    CHECK(kui_retail_gd_disc_open(&service)==-1 && !memcmp(&service,&snapshot,sizeof(service)));
    service.executing=0;service.disc_generation=UINT32_MAX;snapshot=service;
    CHECK(kui_retail_gd_disc_open(&service)==-1 && !memcmp(&service,&snapshot,sizeof(service)));
    service.disc_generation=1;CHECK(kui_retail_gd_disc_open(&service)==0);
    for(unsigned phase=0;phase<2;phase++) {
        if(phase)CHECK(kui_retail_gd_disc_absent(&service)==0);
        unsigned before=checks;int32_t token=read_request(1);CHECK(token>0);
        CHECK(call(KUI_GD_EXEC,0,0)==0 && !reads && checks==before);
        CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED && get(STATUS+4u)==KUI_GD_ERROR_UNAVAILABLE);
        put(PARAM,1);put(PARAM+4u,OUTPUT);
        token=call(KUI_GD_REQUEST,KUI_GD_GETTOC2,PARAM);CHECK(token>0);
        CHECK(call(KUI_GD_EXEC,0,0)==0 && call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
        token=call(KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,0);CHECK(token>0);
        CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
        uint32_t generation=service.disc_generation;
        CHECK(call(KUI_GD_INIT,0,0)==0 && call(KUI_GD_RESET,0,0)==0);
        CHECK(service.disc_generation==generation && service.disc_changed);
        CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==(phase?7u:6u));
    }
    CHECK(kui_retail_gd_disc_close(&service,NULL)==-1);
    maps[1].track_count=0;snapshot=service;
    CHECK(kui_retail_gd_disc_close(&service,&maps[1])==-1 && !memcmp(&service,&snapshot,sizeof(service)));
}
int main(void) {
    lifecycle();no_media_and_refusals();
    printf("PASS native GD disc lifecycle: %u checks; observed lid, absence, immutable map rebind, retired token and generation fences\n",assertions);
    return 0;
}
