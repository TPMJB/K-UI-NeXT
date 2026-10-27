/* SPDX-License-Identifier: GPL-3.0-only */
/* Real A1 VMU traffic and virtual A2 RESET in the actual worker. */
#define main persistent_regression_main
#include "test_vmu_native_persistent.c"
#undef main
static void peripheral_stage(unsigned cmd,uint32_t function) {
    stage_batch(1u,0u);
    uint32_t source=physical(input.tx_base)+packet_bank*0x3000u;
    put(source+28u,4u,0x00010000u); /* MAIN B ceases to be LAST. */
    unsigned words=cmd==9u?1u:0u;
    put(source+40u,4u,0x80000000u|words);put(source+44u,4u,rx_at(1u));
    put(source+48u,4u,(words<<24)|0x100u|cmd);
    if(words) put(source+52u,4u,function);
    memset(rx_bytes(1u),0xa5,1024u);
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
    physical_complete();put(rx_at(0u),4u,0x03210008u); /* Real A1 remains advertised. */
}
static void peripheral_reply(unsigned cmd,uint32_t function,unsigned error) {
    unsigned code=error?error:cmd==1u?5u:cmd==2u?6u:8u;
    unsigned words=error?0u:cmd==1u?28u:cmd==2u?48u:3u;
    put(rx_at(1u),4u,(words<<24)|0x00010000u|code);
    if(words) put(rx_at(1u)+4u,4u,function);
}
static unsigned vmu_words(unsigned command,uint32_t function) {
    if(command==3u) return 0u;
    if(command==12u) return function==0x04000000u?50u:34u;
    return command==14u && function==0x04000000u?49u:2u;
}
static void vmu_begin(unsigned command,uint32_t function) {
    if(!live_plan.ready) transfer_batch(0u,0u,0u,NULL);
    stage_batch(1u,0u);
    uint32_t source=physical(input.tx_base)+packet_bank*0x3000u;
    unsigned words=vmu_words(command,function);
    put(source+28u,4u,0x00010000u);
    put(source+40u,4u,0x80000000u|words);put(source+44u,4u,rx_at(1u));
    put(source+48u,4u,(words<<24)|0x100u|command);
    for(unsigned i=0;i<words;i++) put(source+52u+i*4u,4u,0x9e370000u+i*0x10203u);
    if(words) put(source+52u,4u,function);
    if(words>1u && !(command==14u && function==0x04000000u))
        put(source+56u,4u,function==0x02000000u && command!=10u?
            0x2a000000u|(command==13u?0x400u:command==12u?0x200u:0u):0u);
    memset(rx_bytes(1u),0xa5,1024u);
    uint8_t before[0x3000];memcpy(before,memory+source-0x0c000000u,sizeof(before));
    submit();CHECK(overlay.pending && !controller_pair.pending);
    CHECK(issued_plan.virtual_address==2u && issued_plan.physical_count==3u && issued_plan.virtual_count==1u);
    CHECK(issued_plan.output_bytes==40u+4u*words);
    CHECK(!memcmp(issued_chain+40u,before+52u,4u*words));
    CHECK(!memcmp(before,memory+source-0x0c000000u,sizeof(before)));
    physical_complete();put(rx_at(0u),4u,0x03210008u);
}
static void vmu_reply(unsigned command) {
    unsigned words=command==10u?7u:command==11u?130u:0u;
    put(rx_at(1u),4u,(words<<24)|0x00010000u|(words?8u:7u));
    if(words) put(rx_at(1u)+4u,4u,0x02000000u);
    if(command==11u) put(rx_at(1u)+8u,4u,0x2a000000u);
}
static void physical_vmu_passthrough(void) {
    const struct {unsigned command;uint32_t function;} requests[]={
        {3u,0u},{10u,0x02000000u},{11u,0x02000000u},{12u,0x02000000u},
        {13u,0x02000000u},{12u,0x04000000u},{14u,0x04000000u},{14u,0x08000000u}};
    for(unsigned r=0;r<sizeof(requests)/sizeof(requests[0]);r++) {
        for(unsigned result=0;result<6u;result++) {
            setup();vmu_begin(requests[r].command,requests[r].function);vmu_reply(requests[r].command);
            if(result==1u) put(rx_at(1u),4u,UINT32_MAX);
            else if(result>=2u && result<=4u) put(rx_at(1u),4u,0x000100fau+result);
            else if(result==5u && requests[r].command!=3u) {
                put(rx_at(1u),4u,0x010100fbu);put(rx_at(1u)+4u,4u,0x20000000u);
            }
            uint8_t before[1024];memcpy(before,rx_bytes(1u),sizeof(before));
            complete_return();CHECK(!memcmp(before,rx_bytes(1u),sizeof(before)));
            CHECK(rx_bytes(0u)[2]==0x23u && rx_bytes(2u)[2]==2u);
            CHECK(!sd.bytes && !lease_calls && !durable_card.commits);idle();
        }
        for(unsigned mutation=0;mutation<9u;mutation++) {
            setup();vmu_begin(requests[r].command,requests[r].function);vmu_reply(requests[r].command);
            uint32_t header=value_at(rx_at(1u));
            if(mutation==0u) put(rx_at(1u),4u,header^0x100u);
            else if(mutation==1u) put(rx_at(1u),4u,header^0x30000u);
            else if(mutation==2u) put(rx_at(1u),4u,header^0x01000000u);
            else if(mutation==3u) put(rx_at(1u),4u,(header&0xffffff00u)|6u);
            else if(mutation==4u) {put(rx_at(1u),4u,0x010100fbu);put(rx_at(1u)+4u,4u,0x40000000u);}
            else if(mutation==5u) put(state.source+48u,4u,value_at(state.source+48u)^0x100u);
            else if(mutation==7u && (requests[r].command==10u || requests[r].command==11u))
                put(rx_at(1u)+4u,4u,0x04000000u);
            else if(mutation==8u && requests[r].command==11u) put(rx_at(1u)+8u,4u,0x2b000000u);
            else if(mutation>=7u) {put(rx_at(1u),4u,0x010100fbu);put(rx_at(1u)+4u,4u,0u);}
            else if(vmu_words(requests[r].command,requests[r].function)) {
                uint32_t last=state.source+52u+4u*(vmu_words(requests[r].command,requests[r].function)-1u);
                put(last,4u,value_at(last)^1u);
            }
            else put(state.source+48u,4u,0u);
            uint8_t physical[1024],virtual[1024],parent[1024];
            memcpy(physical,rx_bytes(1u),1024u);memcpy(virtual,rx_bytes(2u),1024u);memcpy(parent,rx_bytes(0u),1024u);
            reject_complete();CHECK(!memcmp(physical,rx_bytes(1u),1024u) && !memcmp(virtual,rx_bytes(2u),1024u) &&
                !memcmp(parent,rx_bytes(0u),1024u));CHECK(!sd.bytes && !lease_calls && !overlay.stats.backend_calls);
        }
    }
}
int main(void) {
    physical_vmu_passthrough();
    uint8_t expected[1024],before[1024],staged[512];memset(staged,0x73,sizeof(staged));
    const uint32_t functions[]={0x01000000u,0x02000000u,0x04000000u,0x08000000u,0x00010000u};
    for(unsigned cmd=1u;cmd<=2u;cmd++) {
        setup();peripheral_begin(cmd,0x06000000u);peripheral_reply(cmd,0x06000000u,0u);
        memcpy(expected,rx_bytes(1u),sizeof(expected));complete_return();
        CHECK(!memcmp(expected,rx_bytes(1u),sizeof(expected)) && rx_bytes(0u)[2]==0x23u);
        CHECK(!lease_calls && !sd.bytes);idle();
    }
    for(unsigned f=0;f<sizeof(functions)/sizeof(functions[0]);f++)
        for(unsigned kind=0;kind<5u;kind++) {
            setup();peripheral_begin(9u,functions[f]);
            if(kind==4u) put(rx_at(1u),4u,UINT32_MAX);
            else peripheral_reply(9u,functions[f],kind?0xfbu+kind:0u);
            memcpy(expected,rx_bytes(1u),sizeof(expected));complete_return();
            CHECK(!memcmp(expected,rx_bytes(1u),sizeof(expected)) && !sd.bytes && !lease_calls);
        }
    /* Wrong endpoint/function/type/length refuses before any virtual response
     * or parent-bit publication, while preserving the physical bytes. */
    for(unsigned mutation=0;mutation<7u;mutation++) {
        setup();peripheral_begin(9u,0x04000000u);peripheral_reply(9u,0x04000000u,0u);
        uint32_t header=value_at(rx_at(1u));
        if(mutation==0u) put(rx_at(1u),4u,header^0x100u);
        if(mutation==1u) put(rx_at(1u),4u,header^0x10000u);
        if(mutation==2u) put(rx_at(1u)+4u,4u,0x02000000u);
        if(mutation==3u) put(rx_at(1u),4u,header&0xffffffu);
        if(mutation==4u) put(rx_at(1u),4u,(header&0xffffff00u)|7u);
        if(mutation==5u) put(rx_at(1u),4u,0x010100fdu);
        if(mutation==6u) put(state.source+52u,4u,0x02000000u);
        memcpy(expected,rx_bytes(1u),sizeof(expected));memcpy(before,rx_bytes(2u),sizeof(before));
        reject_complete();CHECK(!memcmp(expected,rx_bytes(1u),sizeof(expected)) &&
            !memcmp(before,rx_bytes(2u),sizeof(before)) && rx_bytes(0u)[2]==0x21u);
        CHECK(!sd.bytes && !lease_calls && !durable_card.commits);
    }
    setup();stage_block(42u,staged);unsigned reads=sd.reads,writes=sd.writes;
    CHECK(durable_card.protocol.phases==15u);
    transfer_batch(3u,0u,0u,NULL);
    CHECK(value_at(rx_at(2u))==0x00020007u && !durable_card.protocol.phases &&
        !durable_card.protocol.killed && !durable_card.commits && sd.reads==reads && sd.writes==writes);
    read_compare(42u,sd.media[image_lba(42u)]);
    const unsigned prohibited[]={4u,10u,11u,12u,13u,14u};
    for(unsigned i=0;i<sizeof(prohibited)/sizeof(prohibited[0]);i++) {
        setup();peripheral_stage(prohibited[i],0x02000000u);
        memcpy(before,rx_bytes(2u),sizeof(before));memcpy(expected,rx_bytes(1u),sizeof(expected));
        reject_submit();CHECK(!state.pending && !sd.bytes && !lease_calls &&
            value_at(0xa05f6c04u)==physical(input.tx_base) && value_at(0xa05f6c18u)==0u &&
            !memcmp(before,rx_bytes(2u),sizeof(before)) && !memcmp(expected,rx_bytes(1u),sizeof(expected)));
    }
    /* KILL and malformed RESET stay refused before DMA/storage. */
    setup();stage_batch(4u,0u);reject_submit();CHECK(!sd.bytes && !lease_calls);
    setup();stage_batch(3u,0u);uint32_t source=physical(input.tx_base);
    put(source+16u,4u,1u);put(source+24u,4u,0x01000203u);reject_submit();CHECK(!sd.bytes && !lease_calls);
    printf("Native A1 VMU/A2 transport: %u checks; full memory/LCD payloads and typed replies preserved, invalid batches atomic, no virtual storage writes\n",checks);
    return 0;
}
