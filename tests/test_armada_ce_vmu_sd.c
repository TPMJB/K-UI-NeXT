/* SPDX-License-Identifier: GPL-3.0-only */
/* Real provider, SD protocol and store over the independent SPI card oracle. */
#include "armada_ce_vmu_sd_fixture.h"

static void no_more_io(void) {
    uint8_t output[512];memset(output,0xa5,sizeof(output));unsigned bytes=sd.bytes;
    CHECK(kui_armada_ce_vmu_sd_read(&endpoint,0u,output)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_armada_ce_vmu_sd_commit(&endpoint,0u,output)==KUI_VMU_EMU_IO_ERROR);
    CHECK(sd.bytes==bytes && endpoint.faulted);idle();
}
static void read_and_commit(void) {
    setup();uint8_t output[512],data[512];
    for(unsigned b=0;b<256u;b++) {
        memset(output,0xa5,sizeof(output));
        CHECK(kui_armada_ce_vmu_sd_read(&endpoint,(uint16_t)b,output)==KUI_VMU_EMU_IO_OK);
        CHECK(!memcmp(output,sd.media[image_lba(b)],512u));idle();
    }
    CHECK(endpoint.bound && endpoint.reads==256u && !sd.writes && csds==512u);
    const unsigned blocks[]={0u,127u,128u,255u};
    for(unsigned j=0;j<4u;j++) {
        for(unsigned i=0;i<512u;i++) data[i]=(uint8_t)(i+j*13u);
        unsigned b=blocks[j];
        CHECK(kui_armada_ce_vmu_sd_commit(&endpoint,(uint16_t)b,data)==KUI_VMU_EMU_IO_OK);idle();
        CHECK(!memcmp(sd.media[image_lba(b)],data,512u));
        CHECK(endpoint.handoff.block_crc[b]==kui_retail_crc32(0,data,512u) && !endpoint.handoff.seal);
        CHECK(kui_armada_ce_vmu_sd_read(&endpoint,(uint16_t)b,output)==KUI_VMU_EMU_IO_OK);
        CHECK(!memcmp(output,data,512u));idle();
    }
    CHECK(endpoint.commits==4u && sd.writes==16u && sd.statuses>=16u && endpoint.sector_writes==16u);
}
static void admission_and_identity(void) {
    uint8_t data[512]={0},output[512];
    setup();struct kui_armada_ce_vmu_sd_ops platform=ops();
    CHECK(kui_armada_ce_vmu_sd_init(&endpoint,&handoff,&platform)==-1);
    CHECK(kui_armada_ce_vmu_sd_read(&endpoint,256u,output)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_armada_ce_vmu_sd_read(&endpoint,0u,endpoint.bounce)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_armada_ce_vmu_sd_commit(&endpoint,0u,NULL)==KUI_VMU_EMU_IO_ERROR && !sd.bytes);
    worker=0u;CHECK(kui_armada_ce_vmu_sd_read(&endpoint,0u,output)==KUI_VMU_EMU_IO_ERROR);
    worker=1u;fence=1u;CHECK(kui_armada_ce_vmu_sd_read(&endpoint,0u,output)==KUI_VMU_EMU_IO_ERROR);
    fence=0u;guard_refused=1u;CHECK(kui_armada_ce_vmu_sd_read(&endpoint,0u,output)==KUI_VMU_EMU_IO_BUSY);
    guard_refused=0u;begin_result=1u;
    CHECK(kui_armada_ce_vmu_sd_read(&endpoint,0u,output)==KUI_VMU_EMU_IO_BUSY);
    CHECK(!endpoint.faulted && !sd.bytes && !end_calls && !port_claims);idle();
    begin_result=0u;CHECK(kui_armada_ce_vmu_sd_read(&endpoint,0u,output)==KUI_VMU_EMU_IO_OK);idle();
    for(unsigned reason=0;reason<5u;reason++) {
        setup();
        if(reason==0u) sd.cid[0]^=1u;
        if(reason==1u) csd_bad=1u;
        if(reason==2u) csd_crc_bad=1u;
        if(reason==3u) sd.media[950][0]=1u;
        if(reason==4u) port_refused=1u;
        CHECK(kui_armada_ce_vmu_sd_commit(&endpoint,0u,data)==KUI_VMU_EMU_IO_ERROR);
        CHECK(!sd.writes && !endpoint.commits && !endpoint.bound);no_more_io();
    }
    setup();sd.media[100][0]^=1u;memset(output,0xa5,sizeof(output));
    CHECK(kui_armada_ce_vmu_sd_read(&endpoint,0u,output)==KUI_VMU_EMU_IO_ERROR);
    for(unsigned i=0;i<512u;i++) CHECK(output[i]==0xa5u);
    no_more_io();
    setup();cid_after_write=1u;
    CHECK(kui_armada_ce_vmu_sd_commit(&endpoint,0u,data)==KUI_VMU_EMU_IO_ERROR);
    CHECK(sd.writes==1u && !endpoint.commits);no_more_io();
    setup();cid_after_read=1u;memset(output,0xa5,sizeof(output));
    CHECK(kui_armada_ce_vmu_sd_read(&endpoint,0u,output)==KUI_VMU_EMU_IO_ERROR);
    CHECK(sd.reads==2u && !endpoint.reads);
    for(unsigned i=0;i<512u;i++) CHECK(output[i]==0xa5u);
    no_more_io();
}
static void acquired_failures(void) {
    uint8_t data[512];memset(data,0x33,sizeof(data));
    for(unsigned phase=1u;phase<=4u;phase++) for(unsigned fault=0u;fault<2u;fault++) {
        setup();if(fault) sd.fail_status=phase;else sd.fail_write=phase;
        uint32_t old=endpoint.handoff.block_crc[7];
        CHECK(kui_armada_ce_vmu_sd_commit(&endpoint,7u,data)==KUI_VMU_EMU_IO_ERROR);
        CHECK(!endpoint.commits && endpoint.handoff.block_crc[7]==old);no_more_io();
    }
    for(unsigned fault=0;fault<3u;fault++) {
        setup();if(fault==0u) port_release_failure=1u;
        if(fault==1u) end_failure=1u;
        if(fault==2u) sd.fail_release=true;
        uint32_t old=endpoint.handoff.block_crc[42];
        CHECK(kui_armada_ce_vmu_sd_commit(&endpoint,42u,data)==KUI_VMU_EMU_IO_ERROR);
        CHECK(sd.writes==4u && !memcmp(sd.media[image_lba(42u)],data,512u));
        CHECK(endpoint.handoff.block_crc[42]==old && !endpoint.commits && !endpoint.bound);no_more_io();
    }
    for(unsigned fault=0;fault<3u;fault++) {
        setup();if(fault==0u) port_release_failure=1u;
        if(fault==1u) end_failure=1u;
        if(fault==2u) sd.fail_release=true;
        uint8_t output[512];memset(output,0xa5,sizeof(output));
        CHECK(kui_armada_ce_vmu_sd_read(&endpoint,42u,output)==KUI_VMU_EMU_IO_ERROR);
        CHECK(!endpoint.reads && !endpoint.bound);
        for(unsigned i=0;i<512u;i++) CHECK(output[i]==0xa5u);
        no_more_io();
    }
    setup();sd.fail_read=1u;
    CHECK(kui_armada_ce_vmu_sd_commit(&endpoint,0u,data)==KUI_VMU_EMU_IO_ERROR);
    CHECK(!sd.writes);no_more_io();
    setup();sd.fail_busy=1u;
    CHECK(kui_armada_ce_vmu_sd_commit(&endpoint,0u,data)==KUI_VMU_EMU_IO_ERROR);
    CHECK(sd.writes==1u && !endpoint.commits);no_more_io();
}
int main(void) {
    read_and_commit();admission_and_identity();acquired_failures();
    printf("CE SD provider: %u checks; real CID/CSD/CRC, CMD17/CMD24 redo barriers/readback, "
        "PIO-only callbacks, release-before-success and latched faults passed\n",checks);
    return 0;
}
