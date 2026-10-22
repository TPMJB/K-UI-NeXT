/* SPDX-License-Identifier: GPL-3.0-only */
/* The production persistent worker and independent SPI oracle are shared with
 * the integration regression. This test varies admitted workloads, not the
 * worker's implementation or its ownership callbacks. */
#define main persistent_regression_main
#include "test_vmu_native_persistent.c"
#undef main

struct stress_cost {
    unsigned submit_reads,complete_reads,free_reads,work,wire,reads,writes,statuses;
};
static void fragment_heap(unsigned nodes) {
    CHECK(nodes && nodes<KUI_VMU_NATIVE_HEAP_FREE_LIMIT);
    for(unsigned i=0;i<nodes;i++) {
        uint32_t at=input.lease.begin+i*64u;
        CHECK(at+32u<input.lease.header-64u);
        put(at,4u,i+1u==nodes?input.lease.owner.sentinel:at+64u);
        put(at+4u,4u,1u);
    }
    put(input.lease.owner.sentinel,4u,input.lease.begin);
    put(input.lease.owner.cursor_cell,4u,input.lease.owner.sentinel);
    /* A distinct live allocation, adjacent to but not touching the worker. */
    put(input.lease.header-60u,4u,2u);
    CHECK(lease_valid());
}
static uint32_t stress_rx(unsigned port) {return rx_at(port*6u);}
static unsigned stress_packets(unsigned controllers,unsigned command) {
    CHECK(controllers>=1u && controllers<=4u);
    return controllers+(command!=0u);
}
static void stress_stage(unsigned controllers,unsigned command,unsigned block,unsigned phase,
    const uint8_t *payload,unsigned padding) {
    unsigned packets=stress_packets(controllers,command);
    CHECK(padding+packets<=KUI_VMU_VOLATILE_CHAIN_ENTRIES);
    stage_batch(0u,0u);
    uint32_t source=physical(input.tx_base)+packet_bank*KUI_VMU_NATIVE_TX_BANK_BYTES;
    memset(memory+source-0x0c000000u,0,KUI_VMU_NATIVE_TX_BANK_BYTES);
    unsigned offset=0u,ordinal=0u;
    for(unsigned i=0;i<padding;i++,offset+=4u) put(source+offset,4u,0x700u);
    for(unsigned port=0;port<controllers;port++) {
        unsigned destination=port*64u+32u;
        memset(kui_vmu_native_passive_test_rx(stress_rx(port),1024u),0xa5,1024u);
        uint32_t control=(port<<16)|1u;
        if(++ordinal==packets) control|=UINT32_C(0x80000000);
        put(source+offset,4u,control);put(source+offset+4u,4u,stress_rx(port));
        put(source+offset+8u,4u,UINT32_C(0x01000009)|(port*64u<<16)|(destination<<8));
        put(source+offset+12u,4u,UINT32_C(0x01000000));offset+=16u;
        if(!port && command) {
            unsigned words=command==1u || command==2u?0u:command==12u?34u:2u;
            uint32_t target_control=words;
            if(++ordinal==packets) target_control|=UINT32_C(0x80000000);
            memset(rx_bytes(1),0xa5,1024u);
            put(source+offset,4u,target_control);put(source+offset+4u,4u,rx_at(1));
            put(source+offset+8u,4u,(words<<24)|0x100u|command);
            if(words) put(source+offset+12u,4u,UINT32_C(0x02000000));
            if(words>1u) put(source+offset+16u,4u,(block<<24)|(phase<<8));
            if(command==12u) {
                CHECK(payload);memcpy(memory+source+offset+20u-0x0c000000u,payload,128u);
            }
            offset+=12u+words*4u;
        }
    }
    CHECK(ordinal==packets && offset<=KUI_VMU_NATIVE_TX_BANK_BYTES);
}
static void stress_complete(unsigned controllers) {
    put(0x0c3010b4u,4u,packet_bank^1u);put(0x0c3010b8u,4u,packet_bank^1u);
    put(0x0c300fc0u,4u,0u);frame.sr=0x600001f1u;
    for(unsigned port=0;port<controllers;port++) {
        unsigned destination=port*64u+32u;
        put(stress_rx(port),4u,UINT32_C(0x03000008)|(destination<<16)|(port*64u<<8));
        put(stress_rx(port)+4u,4u,UINT32_C(0x01000000));
    }
    put(0xa05f6c18u,4u,0u);
}
static struct stress_cost stress_transfer(unsigned controllers,unsigned command,unsigned block,
    unsigned phase,const uint8_t *payload,unsigned padding,unsigned frees) {
    struct stress_cost cost={0};
    stress_stage(controllers,command,block,phase,payload,padding);
    unsigned wire=sd.bytes,reads=sd.reads,writes=sd.writes,statuses=sd.statuses;
    memset(&measured_work,0,sizeof(measured_work));submit();cost.submit_reads=measured_submit_reads;
    CHECK(state.descriptors==stress_packets(controllers,command));
    CHECK(issued_plan.physical_count==controllers && issued_plan.virtual_count==(command!=0u));
    CHECK(issued_plan.padding_count==padding && issued_plan.output_bytes==controllers*16u);
    put(0xa05f6c18u,4u,1u);
    uint32_t saved_r4=frame.r4;frame.r4=input.lease.header-32u;
    unsigned before=bus_reads;
    for(unsigned i=0;i<frees;i++) kui_vmu_native_readonly_release_check(&frame);
    cost.free_reads=bus_reads-before;frame.r4=saved_r4;
    stress_complete(controllers);
    uint8_t unrelated[3][1024];
    for(unsigned port=1;port<controllers;port++)
        memcpy(unrelated[port-1u],kui_vmu_native_passive_test_rx(stress_rx(port),1024u),1024u);
    complete_return();cost.complete_reads=measured_complete_reads;idle();
    for(unsigned port=1;port<controllers;port++)
        CHECK(!memcmp(unrelated[port-1u],kui_vmu_native_passive_test_rx(stress_rx(port),1024u),1024u));
    CHECK(value_at(0xa05f6c04u)==physical(input.tx_base)+packet_bank*KUI_VMU_NATIVE_TX_BANK_BYTES);
    cost.work=full_work();cost.wire=sd.bytes-wire;cost.reads=sd.reads-reads;
    cost.writes=sd.writes-writes;cost.statuses=sd.statuses-statuses;
    return cost;
}
static void print_cost(const char *kind,unsigned descriptors,unsigned nodes,unsigned frees,
    unsigned padding,struct stress_cost c) {
    printf("stress %-10s descriptors=%u free_nodes=%u free_calls=%u NOPs=%u submit=%u complete=%u free_guard=%u work=%u SDbytes=%u sectorsR=%u sectorsW=%u status=%u\n",
        kind,descriptors,nodes,frees,padding,c.submit_reads,c.complete_reads,c.free_reads,c.work,
        c.wire,c.reads,c.writes,c.statuses);
}
static void stress_rejected_batches(void) {
    for(unsigned corruption=0;corruption<4u;corruption++) {
        setup();fragment_heap(256u);
        stress_stage(corruption==1u?2u:1u,corruption==1u?0u:1u,0u,0u,NULL,0u);
        uint32_t source=physical(input.tx_base);
        if(corruption==0u) {
            /* A second virtual request cannot reuse the same exact RX slot. */
            put(source+16u,4u,0u);
            memcpy(memory+source+28u-0x0c000000u,memory+source+16u-0x0c000000u,12u);
            put(source+28u,4u,UINT32_C(0x80000000));
        } else if(corruption==1u) {
            /* Two physical descriptors cannot alias the same parent RX. */
            put(source+16u,4u,UINT32_C(0x80000001));
            put(source+20u,4u,stress_rx(0u));
            put(source+24u,4u,UINT32_C(0x01002009));
        } else if(corruption==2u) {
            /* Accessory queries are admitted; physical SETCOND is not. */
            put(source+20u,4u,rx_at(2u));put(source+24u,4u,0x20eu);
        } else {
            stress_stage(4u,1u,0u,0u,NULL,251u);
            /* One extra NOP exceeds the hard 256-entry walk bound. */
            memmove(memory+source+4u-0x0c000000u,memory+source-0x0c000000u,
                KUI_VMU_NATIVE_TX_BANK_BYTES-4u);
            put(source,4u,0x700u);
        }
        uint8_t before[3][1024];
        for(unsigned unit=0;unit<3u;unit++) memcpy(before[unit],rx_bytes(unit),1024u);
        reject_submit();
        CHECK(!lease_calls && !sd.bytes && !sd.writes && !durable_card.commits);
        CHECK(value_at(0xa05f6c18u)==0u && value_at(0xa05f6c04u)==source);
        for(unsigned unit=0;unit<3u;unit++) CHECK(!memcmp(before[unit],rx_bytes(unit),1024u));
    }
}
static void stress_pending_release_protection(void) {
    setup();fragment_heap(256u);stress_stage(4u,0u,0u,0u,NULL,0u);submit();
    put(0xa05f6c18u,4u,1u);frame.r4=input.lease.payload;
    uint8_t before[1024];memcpy(before,rx_bytes(0u),sizeof(before));
    if(setjmp(terminal)==0) {kui_vmu_native_readonly_release_check(&frame);CHECK(false);}
    CHECK(terminal_error && state.pending && !state.proof && !sd.bytes && !lease_calls);
    CHECK(!memcmp(before,rx_bytes(0u),sizeof(before)));
    CHECK(value_at(0xa05f6c18u)==1u);
}
int main(void) {
    static const unsigned fragments[]={1u,8u,64u,256u};
    static const unsigned frequencies[]={0u,1u,8u,64u};
    uint8_t payload[512];memset(payload,0x73,sizeof(payload));
    for(unsigned n=0;n<sizeof(fragments)/sizeof(fragments[0]);n++) {
        setup();fragment_heap(fragments[n]);
        for(unsigned controllers=1u;controllers<=4u;controllers++) {
            struct stress_cost c=stress_transfer(controllers,0u,0u,0u,NULL,0u,0u);
            CHECK(!c.wire && !c.reads && !c.writes);
            print_cost("controller",controllers,fragments[n],0u,0u,c);
        }
        for(unsigned f=0;f<sizeof(frequencies)/sizeof(frequencies[0]);f++) {
            struct stress_cost c=stress_transfer(4u,0u,0u,0u,NULL,0u,frequencies[f]);
            CHECK(!c.wire);print_cost("free-burst",4u,fragments[n],frequencies[f],0u,c);
        }
        struct stress_cost info=stress_transfer(4u,1u,0u,0u,NULL,0u,0u);
        CHECK(!info.wire);print_cost("INFO",5u,fragments[n],0u,0u,info);
        struct stress_cost read=stress_transfer(4u,11u,42u,0u,NULL,0u,0u);
        CHECK(read.reads==2u && !read.writes);print_cost("READ",5u,fragments[n],0u,0u,read);
        for(unsigned phase=0;phase<4u;phase++) {
            struct stress_cost write=stress_transfer(4u,12u,42u,phase,payload+phase*128u,0u,0u);
            CHECK(!write.wire);print_cost("WRITEphase",5u,fragments[n],0u,0u,write);
        }
        struct stress_cost sync=stress_transfer(4u,13u,42u,4u,NULL,0u,0u);
        CHECK(sync.writes==4u && sync.statuses==8u && durable_card.commits==1u);
        print_cost("SYNC",5u,fragments[n],0u,0u,sync);
    }
    setup();fragment_heap(1u);
    struct stress_cost padded=stress_transfer(4u,11u,42u,0u,NULL,251u,0u);
    CHECK(state.nops==251u && state.descriptors==5u && !padded.writes);
    print_cost("max-padding",5u,1u,0u,251u,padded);
    unsigned first=state.completed;
    for(unsigned i=0;i<256u;i++) {
        struct stress_cost read=stress_transfer(4u,11u,i,0u,NULL,0u,0u);
        CHECK(read.reads==1u && !read.writes && read.wire);
    }
    CHECK(state.completed==first+256u && durable_card.reads==257u);
    stress_rejected_batches();
    stress_pending_release_protection();
    printf("Native workload stress: %u checks passed; 1..5 descriptors, 1..256 free nodes, 0..64 free calls, maximum padding, all256 SD reads and bounded refusals\n",checks);
    return 0;
}
