/* SPDX-License-Identifier: GPL-3.0-only */
/* The real GD service accepts CMD17 before its modeled image callback fails.
 * Terminal snapshots do not reinterpret that execution failure as rejection. */
#define main original_retail_gd_regression
#include "test_retail_gd.c"
#undef main
#include "native_vmu_io_diagnostic.h"

static void accepted_read_failure_snapshot(void) {
    reset();read_params(45000u,20u,OUTPUT);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM)>0);
    CHECK(!service.diag.reject_reason);
    CHECK(call(KUI_GD_EXEC,0u,0u)==0 && service.completed_bytes==4096u);
    ctx.fail_at=2u;
    CHECK(call(KUI_GD_EXEC,0u,0u)==0 && service.error==KUI_GD_ERROR_IO);
    CHECK(service.diag.last_function==2u && service.diag.last_command==0x11u);
    struct kui_loader_sd sd={.ready=true,.last_command=18u,.last_response=0x0bu};
    uint32_t words[KUI_NATIVE_VMU_IO_DIAGNOSTIC_WORDS+2u];
    for(unsigned flags=0u;flags<16u;flags++) {
        for(unsigned i=0u;i<sizeof(words)/sizeof(words[0]);i++) words[i]=0x9c7a531du;
        sd.ready=flags&1u;sd.slow=flags&2u;
        kui_native_vmu_io_snapshot(words+1u,&service,&sd,KUI_LOADER_SD_CRC,
            KUI_GAME_IO,!!(flags&4u),!!(flags&8u),KUI_GD_EXEC);
        CHECK(words[0]==0x9c7a531du && words[10]==0x9c7a531du);
        CHECK(words[1]==2u && words[2]==0x11u && words[3]==KUI_LOADER_SD_CRC);
        CHECK(words[4]==KUI_GAME_IO && words[5]==(0x000b1200u|flags));
        CHECK(words[6]==45000u && words[7]==20u && words[8]==OUTPUT && words[9]==4096u);
    }
}
static void rejected_request_is_not_reported_as_image_execution(void) {
    const uint32_t denied[]={PARAM,OUTPUT};
    const uint32_t why[]={KUI_RETAIL_GD_REJECT_PARAMETERS,KUI_RETAIL_GD_REJECT_DESTINATION};
    for(unsigned i=0u;i<2u;i++) {
        reset();read_params(45000u,20u,OUTPUT);ctx.deny=denied[i];
        CHECK(call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM)==0);
        CHECK(service.diag.reject_reason==why[i] && !ctx.reads && !service.error);
        struct kui_loader_sd sd={.ready=true};uint32_t words[9];
        kui_native_vmu_io_snapshot(words,&service,&sd,KUI_LOADER_SD_OK,KUI_GAME_IO,
            false,false,KUI_GD_REQUEST);
        CHECK(words[0]==0u && words[1]==0x11u && words[3]==UINT32_MAX);
    }
}
int main(void) {
    accepted_read_failure_snapshot();rejected_request_is_not_reported_as_image_execution();
    printf("VMU I/O terminal: %u checks; accepted DMAREAD execution errors, partial progress, packed state and request/memory refusal distinction\n",assertions);
    return 0;
}
