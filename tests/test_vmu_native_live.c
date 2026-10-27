/* SPDX-License-Identifier: GPL-3.0-only */
/* Differential live-witness checks use the full production bind/runtime pair
 * as the oracle. The worker/SPI fixture also runs unchanged in its own suite. */
#define main persistent_regression_main
#include "test_vmu_native_persistent.c"
#undef main
static struct kui_vmu_native_live plan;
struct live_word {uint32_t address,value;unsigned width;};
static struct live_word trace[128];
static unsigned trace_count,alter_read,live_reads;
static bool traced_read(void *unused,uint32_t address,unsigned width,uint32_t *value) {
    (void)unused;++live_reads;
    if(!read_value(NULL,address,width,value)) return false;
    if(trace_count<sizeof(trace)/sizeof(trace[0])) trace[trace_count++]=(struct live_word){address,*value,width};
    if(alter_read==live_reads) *value^=1u;
    return true;
}
static struct kui_vmu_native_runtime_boundary live_boundary_input(unsigned bank,unsigned phase,bool filtered) {
    uint32_t source=physical(input.tx_base)+bank*0x3000u;
    put(0xa05f6c04u,4u,filtered?0x0c700020u:source);
    put(0x0c3010b0u,4u,source|0xa0000000u);put(0x0c3010b4u,4u,bank^phase);
    put(0x0c3010b8u,4u,bank^phase);put(0x0c300fc0u,4u,phase?0u:1u);
    return (struct kui_vmu_native_runtime_boundary){phase,0xf0u,phase?0xf0u:0x60u,
        0x0c012226u,source,filtered?0x0c700020u:source,48u};
}
static int reference(const struct kui_vmu_native_runtime_boundary *b,
    struct kui_vmu_native_runtime_snapshot *out) {
    struct kui_vmu_native_runtime_config c=input.runtime;
    if(!kui_vmu_native_layout_bind(&input.layout,&input.driver,read_value,NULL,&c))
        return KUI_VMU_NATIVE_RUNTIME_INVALID;
    return kui_vmu_native_runtime_check(&input.driver,&c,b,read_value,NULL,out);
}
static void uniform_layout(void) {
    struct kui_vmu_native_layout *l=&input.layout;
    l->rx_layout=KUI_VMU_NATIVE_RX_UNIFORM48;l->inputs=1u;
    memset(l->input_values,0,sizeof(l->input_values));memset(l->indices,0,sizeof(l->indices));
    l->input_values[0]=0xac500004u;l->indices[0]=0xac400000u;l->rx_base=0xac500020u;
    put(l->input_array,4u,l->input_values[0]);put(l->index_array,4u,l->indices[0]);
    put(0x0c300fc8u,4u,l->rx_base);
    for(unsigned i=0;i<48u;i++) {l->rx_slots[i]=l->rx_base+i*1024u;
        put(0x0c300fe8u+i*4u,4u,l->rx_slots[i]);}
}
static void prepare_plan(const struct kui_vmu_native_runtime_boundary *b) {
    struct kui_vmu_native_runtime_config c=input.runtime;
    struct kui_vmu_native_runtime_snapshot actual,expected;
    struct kui_vmu_native_layout_diagnostic diagnostic;
    CHECK(reference(b,&expected)==KUI_VMU_NATIVE_RUNTIME_VALID);
    CHECK(kui_vmu_native_live_prepare(&plan,&input.layout,&input.driver,&c,b,read_value,NULL,
        &actual,&diagnostic)==KUI_VMU_NATIVE_RUNTIME_VALID);
    CHECK(!memcmp(&actual,&expected,sizeof(actual)) && !diagnostic.code);
}
static void no_snapshot_on_failure(const struct kui_vmu_native_runtime_boundary *b) {
    struct kui_vmu_native_runtime_snapshot out,before;memset(&before,0xa5,sizeof(before));out=before;
    CHECK(kui_vmu_native_live_check(&plan,&input.driver,b,traced_read,NULL,&out)!=KUI_VMU_NATIVE_RUNTIME_VALID);
    CHECK(!memcmp(&out,&before,sizeof(out)));
}
int main(void) {
    struct kui_vmu_native_runtime_snapshot out,expected;
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    unsigned full_reads=0u,hot_reads=0u;
    for(unsigned geometry=0;geometry<2u;geometry++) for(unsigned a=0;a<3u;a++)
        for(unsigned bank=0;bank<2u;bank++) for(unsigned phase=0;phase<2u;phase++) {
        fixture();if(geometry) uniform_layout();
        put(input.driver.manager_cell,4u,physical(input.driver.manager_initial)|aliases[a]);
        struct kui_vmu_native_runtime_boundary b=live_boundary_input(bank,phase,false);prepare_plan(&b);
        CHECK(reference(&b,&expected)==0);trace_count=live_reads=alter_read=0u;
        CHECK(kui_vmu_native_live_check(&plan,&input.driver,&b,traced_read,NULL,&out)==0);
        CHECK(!memcmp(&out,&expected,sizeof(out)) && out.bank==bank);
        if(phase) {b=live_boundary_input(bank,phase,true);CHECK(reference(&b,&expected)==0);
            CHECK(kui_vmu_native_live_check(&plan,&input.driver,&b,read_value,NULL,&out)==0);
            CHECK(!memcmp(&out,&expected,sizeof(out)));}
    }
    fixture();struct kui_vmu_native_runtime_boundary b=live_boundary_input(0u,0u,false);prepare_plan(&b);
    bus_reads=0u;CHECK(reference(&b,&out)==0);full_reads=bus_reads;
    trace_count=live_reads=alter_read=0u;bus_reads=0u;
    CHECK(kui_vmu_native_live_check(&plan,&input.driver,&b,traced_read,NULL,&out)==0);hot_reads=bus_reads;
    CHECK(hot_reads+51u==full_reads && hot_reads==98u);
    struct live_word successful[128];unsigned total=trace_count;memcpy(successful,trace,sizeof(successful));
    /* Every live pointer/control/input/slot sampled by the optimized path is
     * also rejected by the old full reference if changed in guest memory. */
    for(unsigned i=0;i<total;i++) {
        put(successful[i].address,successful[i].width,successful[i].value^1u);
        CHECK(reference(&b,&expected)!=KUI_VMU_NATIVE_RUNTIME_VALID);
        trace_count=live_reads=alter_read=0u;no_snapshot_on_failure(&b);
        put(successful[i].address,successful[i].width,successful[i].value);
    }
    for(unsigned i=1u;i<=total;i++) {
        trace_count=live_reads=0u;alter_read=i;no_snapshot_on_failure(&b);CHECK(live_reads==i);
        trace_count=live_reads=alter_read=0u;bus_reads=0u;fail_read=i;
        no_snapshot_on_failure(&b);CHECK(bus_reads==i);fail_read=0u;
    }
    fixture();uniform_layout();b=live_boundary_input(0u,0u,false);prepare_plan(&b);
    trace_count=live_reads=alter_read=0u;CHECK(kui_vmu_native_live_check(&plan,&input.driver,&b,traced_read,NULL,&out)==0);
    total=trace_count;memcpy(successful,trace,sizeof(successful));CHECK(total==78u);
    for(unsigned i=0;i<total;i++) {
        put(successful[i].address,successful[i].width,successful[i].value^1u);
        CHECK(reference(&b,&expected)!=0);trace_count=live_reads=alter_read=0u;no_snapshot_on_failure(&b);
        put(successful[i].address,successful[i].width,successful[i].value);
    }
    for(unsigned field=0u;field<7u;field++) {
        struct kui_vmu_native_runtime_boundary bad=b;
        switch(field) {case 0:bad.phase=2u;break;case 1:bad.worker_sr=0x60u;break;
            case 2:bad.inherited_sr=0x50u;break;case 3:bad.sdk_source_physical+=32u;break;
            case 4:bad.dma_source_physical=0x0c700020u;break;case 5:bad.dma_bytes=13u;break;
            default:bad.dma_bytes=0x3004u;break;}
        CHECK(reference(&bad,&expected)!=0);no_snapshot_on_failure(&bad);
    }
    b=live_boundary_input(0u,1u,true);
    const uint32_t invalid_dma[]={0x0c6fffe0u,0x0c72ffe0u,0xac700020u,0x0c700021u};
    for(unsigned i=0;i<4u;i++) {
        b.dma_source_physical=invalid_dma[i];put(0xa05f6c04u,4u,invalid_dma[i]);
        CHECK(reference(&b,&expected)!=0);no_snapshot_on_failure(&b);
    }
    /* A private proof must allow normal post-binding registration and
     * deregistration, but its full reference and optimized path must agree
     * on every invalid code target. No callback is invoked by either path. */
    for(unsigned a=0;a<3u;a++) for(unsigned phase=0;phase<2u;phase++) {
        fixture();b=live_boundary_input(0u,phase,false);prepare_plan(&b);
        put(input.layout.timer1_callback,4u,aliases[a]|0x0c010000u);
        put(input.layout.timer2_callback,4u,aliases[a]|0x0c20fffeu);
        CHECK(reference(&b,&expected)==0);
        CHECK(kui_vmu_native_live_check(&plan,&input.driver,&b,read_value,NULL,&out)==0);
        CHECK(!memcmp(&out,&expected,sizeof(out)));
        put(input.layout.timer2_callback,4u,0u);
        CHECK(kui_vmu_native_live_check(&plan,&input.driver,&b,read_value,NULL,&out)==0);
    }
    const uint32_t bad_timer[]={1u,0x8c00fffeu,0x8c210000u,0x8c20ffffu,
        0xec087498u,0x6c087498u,0xa05f6c00u,0xac700000u,0x8ce00000u};
    for(unsigned timer=0;timer<2u;timer++) for(unsigned i=0;i<sizeof(bad_timer)/sizeof(bad_timer[0]);i++) {
        fixture();b=live_boundary_input(0u,0u,false);prepare_plan(&b);
        put(timer?input.layout.timer2_callback:input.layout.timer1_callback,4u,bad_timer[i]);
        CHECK(reference(&b,&expected)!=0);trace_count=live_reads=alter_read=0u;no_snapshot_on_failure(&b);
    }
    b=live_boundary_input(0u,1u,true);b.inherited_pr=0xec012226u;
    CHECK(reference(&b,&expected)!=0);no_snapshot_on_failure(&b);
    /* Private proof objects cannot be moved or admitted from serialized data. */
    struct kui_vmu_native_live copied=plan;
    CHECK(kui_vmu_native_live_check(&copied,&input.driver,&b,read_value,NULL,&out)==KUI_VMU_NATIVE_RUNTIME_INVALID);
    fixture();b=live_boundary_input(0u,0u,false);prepare_plan(&b);
    struct kui_vmu_native_live before=plan;struct kui_vmu_native_runtime_config c=input.runtime;
    struct kui_vmu_native_runtime_config old=c;memset(&expected,0xa5,sizeof(expected));out=expected;
    input.layout.rx_slots[1]=input.layout.rx_slots[0];
    CHECK(kui_vmu_native_live_prepare(&plan,&input.layout,&input.driver,&c,&b,read_value,NULL,
        &out,NULL)!=0);
    CHECK(!memcmp(&plan,&before,sizeof(plan)) && !memcmp(&c,&old,sizeof(c)) &&
        !memcmp(&out,&expected,sizeof(out)));
    printf("Native prepared live proof: %u checks; full=%u live=%u guest reads; every live witness differential-tested\n",
        checks,full_reads,hot_reads);
    return 0;
}
