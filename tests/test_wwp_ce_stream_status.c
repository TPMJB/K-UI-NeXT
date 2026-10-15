/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored DRIVE-status bridge and ordinary GD wire formatting. */
#define KUI_WINCE_CDDA_WWP 1
#define KUI_RETAIL_CE 1
#define KUI_ARMADA_CE_STREAM 1
#define main retail_gd_fixture_main
#include "test_retail_gd.c"
#undef main

/* No controller function is linked into this fixture: the low bridge can
 * only read this authored publication, including before the first PLAY. */
volatile uint32_t kui_wwp_cdda_drive_status;
#include "../src/loader/wwp_ce_stream_status.inc"

static int32_t status_call(uint32_t function,uint32_t a,uint32_t b) {
    uint32_t drive_status=service.drive_status;
    if(function==KUI_GD_DRIVE) {
        uint32_t status=wwp_ce_stream_drive_status();
        if(status) service.drive_status=status;
    }
    int32_t result=call(function,a,b);
    if(function==KUI_GD_DRIVE) service.drive_status=drive_status;
    return result;
}
int main(void) {
    reset();
    service.position_lba=45004u;service.drive_status=1u;
    CHECK(!kui_wwp_cdda_drive_status);
    CHECK(status_call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==1u);
    CHECK(service.position_lba==45004u && service.drive_status==1u && !ctx.reads);
    const uint32_t states[]={3u,1u,2u,9u};
    for(unsigned i=0;i<4u;i++) {
        kui_wwp_cdda_drive_status=states[i];
        CHECK(status_call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==states[i]);
        CHECK(get(STATUS+4u)==0x80u);
        CHECK(service.position_lba==45004u && service.drive_status==1u && !ctx.reads);
    }

    /* DATA and metadata keep their existing service state. Only the DRIVE
     * poll that controls World Party's PLAY retry receives the projection. */
    kui_wwp_cdda_drive_status=3u;
    read_params(45005u,1u,OUTPUT);
    int32_t token=status_call(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM);
    CHECK(token>0 && status_call(KUI_GD_EXEC,0,0)==0);
    CHECK(status_call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    CHECK(ctx.reads==1u && ctx.sectors==1u);
    CHECK(service.position_lba==45005u && service.drive_status==1u);
    CHECK(ram[OUTPUT-BEGIN]==pattern(45005u,0u));
    CHECK(status_call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==3u);
    CHECK(service.position_lba==45005u && service.drive_status==1u);
    ctx.deny=STATUS;
    CHECK(status_call(KUI_GD_DRIVE,STATUS,0)==-1);
    CHECK(service.drive_status==1u && service.position_lba==45005u);
    ctx.deny=0;
    put(PARAM,1u);put(PARAM+4u,14u);put(PARAM+8u,OUTPUT);
    token=status_call(KUI_GD_REQUEST,KUI_RETAIL_GD_GETSCD,PARAM);
    CHECK(token>0 && status_call(KUI_GD_EXEC,0,0)==0);
    CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    CHECK(ram[OUTPUT-BEGIN+1u]==0x15u && ram[OUTPUT-BEGIN+5u]==3u);
    for(unsigned i=0;i<4u;i++) put(PARAM+i*4u,STATUS+32u+i*4u);
    token=status_call(KUI_GD_REQUEST,KUI_RETAIL_GD_REQ_STAT,PARAM);
    CHECK(token>0 && status_call(KUI_GD_EXEC,0,0)==0);
    CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    CHECK(get(STATUS+32u)==1u && get(STATUS+36u)==3u);
    CHECK(get(STATUS+40u)==(0x14000000u|(45005u+150u)));

    kui_wwp_cdda_drive_status=0u;
    CHECK(status_call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==1u);
    put(PARAM,2u);put(PARAM+4u,2u);put(PARAM+8u,15u);
    token=status_call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,PARAM);
    CHECK(token>0 && status_call(KUI_GD_EXEC,0,0)==0);
    CHECK(status_call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    CHECK(!kui_wwp_cdda_drive_status && ctx.reads==1u && service.drive_status==1u);
    /* Existing observer publishes the sole STOP after successful reset.
     * The synchronous DRIVE projection leaves no cached audio override. */
    const uint32_t resets[]={KUI_GD_INIT,KUI_GD_RESET};
    for(unsigned i=0;i<2u;i++) {
        CHECK(status_call(resets[i],0,0)==0 && service.drive_status==1u);
        CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==1u);
    }
    printf("WWP DRIVE word: %u checks pass; cold boot, active states, bad output, reset and preserved DATA state\n",assertions);
    return 0;
}
