/* SPDX-License-Identifier: GPL-3.0-only */
/* Query-only accessory passthrough and virtual RESET in the actual worker. */
#define main persistent_regression_main
#include "test_vmu_native_persistent.c"
#undef main
static void peripheral_stage(unsigned cmd,uint32_t function) {
    stage_batch(1u,0u);
    uint32_t source=physical(input.tx_base)+packet_bank*0x3000u;
    put(source+28u,4u,0x00010000u); /* MAIN B ceases to be LAST. */
    unsigned words=cmd==9u?1u:0u;
    put(source+40u,4u,0x80000000u|words);put(source+44u,4u,rx_at(2u));
    put(source+48u,4u,(words<<24)|0x200u|cmd);
    if(words) put(source+52u,4u,function);
    memset(rx_bytes(2u),0xa5,1024u);
}
static void peripheral_begin(unsigned cmd,uint32_t function) {
    if(!live_plan.ready) transfer_batch(0u,0u,0u,NULL);
    peripheral_stage(cmd,function);submit();
    /* A physical accessory and virtual enumeration are deliberately outside
     * the controller-only lane, even after its launch proof has warmed. */
    CHECK(!controller_pair.pending && overlay.pending);
    CHECK(issued_plan.frame_count==4u && issued_plan.physical_count==3u && issued_plan.virtual_count==1u);
    CHECK(issued_plan.output_bytes==(cmd==9u?44u:40u));
    CHECK(value_at(0xa05f6c04u)==issued_physical());
    physical_complete();put(rx_at(0u),4u,0x03220008u); /* Real A2 remains advertised. */
}
static void peripheral_reply(unsigned cmd,uint32_t function,unsigned error) {
    unsigned code=error?error:cmd==1u?5u:cmd==2u?6u:8u;
    unsigned words=error?0u:cmd==1u?28u:cmd==2u?48u:3u;
    put(rx_at(2u),4u,(words<<24)|0x00020000u|code);
    if(words) put(rx_at(2u)+4u,4u,function);
}
int main(void) {
    uint8_t expected[1024],before[1024],staged[512];memset(staged,0x73,sizeof(staged));
    const uint32_t functions[]={0x01000000u,0x02000000u,0x04000000u,0x08000000u,0x00010000u};
    for(unsigned cmd=1u;cmd<=2u;cmd++) {
        setup();peripheral_begin(cmd,0x06000000u);peripheral_reply(cmd,0x06000000u,0u);
        memcpy(expected,rx_bytes(2u),sizeof(expected));complete_return();
        CHECK(!memcmp(expected,rx_bytes(2u),sizeof(expected)) && rx_bytes(0u)[2]==0x23u);
        CHECK(!lease_calls && !sd.bytes);idle();
    }
    for(unsigned f=0;f<sizeof(functions)/sizeof(functions[0]);f++)
        for(unsigned kind=0;kind<5u;kind++) {
            setup();peripheral_begin(9u,functions[f]);
            if(kind==4u) put(rx_at(2u),4u,UINT32_MAX);
            else peripheral_reply(9u,functions[f],kind?0xfbu+kind:0u);
            memcpy(expected,rx_bytes(2u),sizeof(expected));complete_return();
            CHECK(!memcmp(expected,rx_bytes(2u),sizeof(expected)) && !sd.bytes && !lease_calls);
        }
    /* Wrong endpoint/function/type/length refuses before any virtual response
     * or parent-bit publication, while preserving the physical bytes. */
    for(unsigned mutation=0;mutation<7u;mutation++) {
        setup();peripheral_begin(9u,0x04000000u);peripheral_reply(9u,0x04000000u,0u);
        uint32_t header=value_at(rx_at(2u));
        if(mutation==0u) put(rx_at(2u),4u,header^0x100u);
        if(mutation==1u) put(rx_at(2u),4u,header^0x10000u);
        if(mutation==2u) put(rx_at(2u)+4u,4u,0x02000000u);
        if(mutation==3u) put(rx_at(2u),4u,header&0xffffffu);
        if(mutation==4u) put(rx_at(2u),4u,(header&0xffffff00u)|7u);
        if(mutation==5u) put(rx_at(2u),4u,0x010200fdu);
        if(mutation==6u) put(state.source+52u,4u,0x02000000u);
        memcpy(expected,rx_bytes(2u),sizeof(expected));memcpy(before,rx_bytes(1u),sizeof(before));
        reject_complete();CHECK(!memcmp(expected,rx_bytes(2u),sizeof(expected)) &&
            !memcmp(before,rx_bytes(1u),sizeof(before)) && rx_bytes(0u)[2]==0x22u);
        CHECK(!sd.bytes && !lease_calls && !durable_card.commits);
    }
    setup();stage_block(42u,staged);unsigned reads=sd.reads,writes=sd.writes;
    CHECK(durable_card.protocol.phases==15u);
    transfer_batch(3u,0u,0u,NULL);
    CHECK(value_at(rx_at(1u))==0x00010007u && !durable_card.protocol.phases &&
        !durable_card.protocol.killed && !durable_card.commits && sd.reads==reads && sd.writes==writes);
    read_compare(42u,sd.media[image_lba(42u)]);
    const unsigned prohibited[]={3u,4u,10u,11u,12u,13u,14u};
    for(unsigned i=0;i<sizeof(prohibited)/sizeof(prohibited[0]);i++) {
        setup();peripheral_stage(prohibited[i],0x02000000u);
        memcpy(before,rx_bytes(1u),sizeof(before));memcpy(expected,rx_bytes(2u),sizeof(expected));
        reject_submit();CHECK(!state.pending && !sd.bytes && !lease_calls &&
            value_at(0xa05f6c04u)==physical(input.tx_base) && value_at(0xa05f6c18u)==0u &&
            !memcmp(before,rx_bytes(1u),sizeof(before)) && !memcmp(expected,rx_bytes(2u),sizeof(expected)));
    }
    /* KILL and malformed RESET stay refused before DMA/storage. */
    setup();stage_batch(4u,0u);reject_submit();CHECK(!sd.bytes && !lease_calls);
    setup();stage_batch(3u,0u);uint32_t source=physical(input.tx_base);
    put(source+16u,4u,1u);put(source+24u,4u,0x01000103u);reject_submit();CHECK(!sd.bytes && !lease_calls);
    printf("Native peripheral queries and RESET: %u checks; typed replies preserved, invalid batches atomic, no physical writes\n",checks);
    return 0;
}
