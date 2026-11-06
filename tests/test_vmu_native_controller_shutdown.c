/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual persistent/controller C over the existing RAM/MMIO/SD fixture.
 * The supplied original is never rewritten; its exact source contract is
 * tested separately from the synthetic full-context cancellation fixture. */
#define KUI_RETAIL_ASYNC_TEST 1
#if KUI_SHUTDOWN_SPLIT_FIXTURE
#define KUI_VMU_NATIVE_CACHED_CODE 1
#define KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION 1
#define KUI_NATIVE_SPLIT_CONTROLLER_GENERAL 1
#define KUI_NATIVE_GENERAL_LEAN_LAYOUT 1
#define KUI_NATIVE_VMU_ONLY 1
#define KUI_NATIVE_VMU_LITE 1
#else
#define KUI_VMU_NATIVE_BACKGROUND 1
#define KUI_NATIVE_READER_DIAGNOSTIC 1
#define KUI_NATIVE_COOP_PUMP 1
#endif
#define KUI_CONTROLLER_TEST_MAIN controller_baseline_main
#include "test_vmu_native_controller.c"
static unsigned region_reads,pump_visits,restorations;
void *kui_vmu_native_reader_test_region(uint32_t address) {
    (void)address;++region_reads;return NULL;
}
uint32_t kui_native_cooperative_test_vbr(void) {return 0x8c00f400u;}
uint16_t kui_native_cooperative_test_iprb(void) {return 0xa500u;}
void kui_native_cooperative_test_restore(uint32_t vbr,uint16_t iprb) {
    (void)vbr;(void)iprb;++restorations;
}
int kui_native_cooperative_test_visit(void) {++pump_visits;return 0;}
static uint8_t original[0xc00000u];static size_t original_bytes;
static struct kui_vmu_native_discovery original_driver;
static unsigned source_refusals,context_refusals,cancellations,write_only_reads,stopped_callbacks,stopped_rx_reads;
static uint32_t original_half(uint32_t address) {
    CHECK(physical(address)>=0x0c010000u && physical(address)-0x0c010000u<=original_bytes-2u);
    unsigned at=physical(address)-0x0c010000u;
    return original[at]|(uint32_t)original[at+1u]<<8;
}
static uint32_t original_literal(uint32_t pc,unsigned width) {
    uint32_t op=original_half(pc),at=width==4u?((pc+4u)&~3u)+4u*(op&255u):pc+4u+2u*(op&255u);
    CHECK(physical(at)>=0x0c010000u && physical(at)-0x0c010000u<=original_bytes-width);
    unsigned offset=physical(at)-0x0c010000u;uint32_t value=0u;
    memcpy(&value,original+offset,width);return value;
}
static void copy_original(uint32_t address,unsigned bytes) {
    CHECK(physical(address)>=0x0c010000u && physical(address)-0x0c010000u<=original_bytes-bytes);
    memcpy(memory+physical(address)-0x0c000000u,original+physical(address)-0x0c010000u,bytes);
}
static void exact_source(void) {
    fixture();memcpy(memory+0x10000u,original,original_bytes);config.driver=original_driver;
    CHECK(controller_shutdown_source());
}
static unsigned cached_source_reads;
static bool stale_p1_source(uint32_t address,unsigned width,uint32_t *value) {
    uint32_t at=physical(address);
    if((address&0xe0000000u)!=0x80000000u || at<0x0c010000u ||
       at-0x0c010000u>original_bytes-width) return false;
    ++cached_source_reads;*value=0u;memcpy(value,original+at-0x0c010000u,width);return true;
}
static void source_cases(void) {
    exact_source();uint32_t entry=original_driver.consumer+0xa2u;
    /* Every bit of every fixed instruction, including branches/delays and
     * epilogue, is independently required. Load displacements have semantic
     * role checks, so they are tested separately from their opcode/register. */
    for(unsigned i=0;i<82u;i++) {
        uint32_t op=original_half(entry+2u*i);
        unsigned first=((op&0xf000u)==0xd000u || (op&0xf000u)==0x9000u)?8u:0u;
        for(unsigned bit=first;bit<16u;bit++) {
            uint32_t at=physical(entry)+2u*i;uint16_t changed=(uint16_t)(op^(1u<<bit));
            memcpy(memory+at-0x0c000000u,&changed,2u);
            CHECK(!controller_shutdown_source());++source_refusals;
            uint16_t before=(uint16_t)op;memcpy(memory+at-0x0c000000u,&before,2u);
        }
    }
    const uint8_t long_offsets[]={14u,20u,24u,26u,82u,94u,106u};
    for(unsigned i=0;i<sizeof(long_offsets);i++) {
        uint32_t pc=entry+long_offsets[i],op=original_half(pc),pool=((pc+4u)&~3u)+4u*(op&255u);
        uint32_t value=original_literal(pc,4u);
        for(unsigned bit=0;bit<32u;bit++) {
            put(pool,4u,value^(1u<<bit));CHECK(!controller_shutdown_source());++source_refusals;
        }
        put(pool,4u,value);
    }
    const uint8_t word_offsets[]={46u,58u,86u,90u,98u,102u,114u,134u};
    for(unsigned i=0;i<sizeof(word_offsets);i++) {
        uint32_t pc=entry+word_offsets[i],pool=pc+4u+2u*(original_half(pc)&255u);
        uint16_t value=(uint16_t)original_literal(pc,2u);
        for(unsigned bit=0;bit<16u;bit++) {
            uint16_t changed=(uint16_t)(value^(1u<<bit));memcpy(memory+physical(pool)-0x0c000000u,&changed,2u);
            CHECK(!controller_shutdown_source());++source_refusals;
        }
        memcpy(memory+physical(pool)-0x0c000000u,&value,2u);
    }
    uint32_t timers[2]={original_literal(entry+14u,4u),original_literal(entry+20u,4u)};
    for(unsigned i=0;i<2u;i++) for(unsigned half=0;half<(i?3u:5u);half++) {
        uint16_t value=(uint16_t)original_half(timers[i]+2u*half);
        unsigned first=(!i&&!half)?8u:0u;
        for(unsigned bit=first;bit<16u;bit++) {
            uint16_t changed=(uint16_t)(value^(1u<<bit));
            memcpy(memory+physical(timers[i])+2u*half-0x0c000000u,&changed,2u);
            CHECK(!controller_shutdown_source());++source_refusals;
        }
        memcpy(memory+physical(timers[i])+2u*half-0x0c000000u,&value,2u);
    }
    uint32_t timer_pool=((timers[0]+4u)&~3u)+4u*(original_half(timers[0])&255u);
    for(unsigned bit=0;bit<32u;bit++) {
        put(timer_pool,4u,0xffd8000cu^(1u<<bit));CHECK(!controller_shutdown_source());++source_refusals;
    }
    put(timer_pool,4u,0xffd8000cu);CHECK(controller_shutdown_source());
    config.driver.family=2u;CHECK(!controller_shutdown_source());++source_refusals;
    config.driver.family=99u;CHECK(!controller_shutdown_source());++source_refusals;
    config.driver=original_driver;config.driver.consumer+=2u;CHECK(!controller_shutdown_source());++source_refusals;
    config.driver=original_driver;config.driver.image_bytes=physical(entry)-0x0c010000u+163u;
    CHECK(!controller_shutdown_source());++source_refusals;
    /* P1 still contains the pristine original while uncached physical RAM
     * has changed. A cached reproof would falsely accept all three cases. */
    exact_source();read_override=stale_p1_source;cached_source_reads=0u;
    const uint32_t changed_addresses[]={entry,entry+0x140u,timers[0]+2u};
    for(unsigned i=0;i<3u;i++) {
        unsigned at=physical(changed_addresses[i])-0x0c000000u;
        memory[at]^=1u;CHECK(!controller_shutdown_source());++source_refusals;
        memory[at]^=1u;CHECK(controller_shutdown_source());
    }
    CHECK(!cached_source_reads);read_override=NULL;
}
static void bind_source_to_fixture(void) {
    /* Keep the actual original code and timer helpers. Only the manager-cell
     * literal is rebound to the independently initialized synthetic live SDK.
     * Source admission on the unmodified original is exercised above. */
    uint32_t entry=original_driver.consumer+0xa2u;copy_original(entry,164u);
    const uint8_t longs[]={14u,20u,24u,26u,82u,94u,106u};
    const uint8_t words[]={46u,58u,86u,90u,98u,102u,114u,134u};
    for(unsigned i=0;i<sizeof(longs);i++) {
        uint32_t pc=entry+longs[i],pool=((pc+4u)&~3u)+4u*(original_half(pc)&255u);
        copy_original(pool,4u);
        if(i<2u) {
            uint32_t target=original_literal(pc,4u);copy_original(target,i?6u:10u);
            if(!i) copy_original(((target+4u)&~3u)+4u*(original_half(target)&255u),4u);
        } else if(i==2u) put(pool,4u,config.driver.manager_cell);
    }
    for(unsigned i=0;i<sizeof(words);i++) {
        uint32_t pc=entry+words[i];copy_original(pc+4u+2u*(original_half(pc)&255u),2u);
    }
    config.driver.consumer=original_driver.consumer;kui_vmu_native_passive_consumer=config.driver.consumer;
    CHECK(controller_shutdown_source());
}
static void forbid_write_only(uint32_t address,unsigned width) {
    if(address==0xa05f6c8cu) {CHECK(width==4u);++write_only_reads;fail_read=bus_reads+1u;}
}
static void stopped_hardware(void) {
    put(0xa05f6c18u,4u,0u);put(0xa05f6c14u,4u,0u);put(0xa05f6c10u,4u,0u);
    put(0xa05f6c8cu,4u,0x6155404fu);put(0xa05f6c80u,4u,0xc3500000u);put(0xa05f6c04u,4u,0x0cff0000u);
}
static void shutdown_controllers_done(void) {
    controllers_done(1u,0u);
    put(rx_at(0u),4u,value_at(rx_at(0u))&~((uint32_t)VIRTUAL<<16));
}
static void pending_case(unsigned captured,unsigned bank) {
    uniform_warm();bind_source_to_fixture();state.completed=state.submitted=(state.completed&~1u)|bank;
    controllers(1u,0u);
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
    if(captured) kui_vmu_native_passive_split_submit(&frame);else
#else
    CHECK(!captured);
#endif
    submit();
    CHECK(controller_pair.pending==(captured?NATIVE_CONTROLLER_CAPTURED:NATIVE_CONTROLLER_PREPARED));
    shutdown_controllers_done();stopped_hardware();read_observer=forbid_write_only;
}
static void complete_cancel(void) {
    uint8_t before[KUI_VMU_NATIVE_RX_SLOTS][1024];save_all_rx(before);
    uint32_t hardware[6];memcpy(hardware,mmio,sizeof(hardware));
    unsigned completed=state.completed,submits=state.submitted,ads=overlay.stats.advertisements;
    unsigned regions=region_reads,pumps=pump_visits,restores=restorations,bytes=sd.bytes;
    unsigned calls=lease_calls,backs=overlay.stats.backend_calls;
    complete_return();
    CHECK(controller_pair.pending==NATIVE_CONTROLLER_STOPPED && !state.pending && state.completed==completed+1u && state.submitted==submits);
    same_all_rx(before);CHECK(!memcmp(hardware,mmio,sizeof(hardware)));
    CHECK(overlay.stats.advertisements==ads && overlay.stats.backend_calls==backs);
    CHECK(region_reads==regions && pump_visits==pumps && restorations==restores);
    CHECK(sd.bytes==bytes && lease_calls==calls && !write_only_reads);++cancellations;
}
static void no_stopped_rx(uint32_t address,unsigned width) {
    forbid_write_only(address,width);
    for(unsigned i=0;i<KUI_VMU_NATIVE_RX_SLOTS;i++) if(config.runtime.rx_slots[i] &&
       physical(address)>=physical(config.runtime.rx_slots[i]) &&
       physical(address)-physical(config.runtime.rx_slots[i])<1024u) {
        ++stopped_rx_reads;fail_read=bus_reads+1u;
    }
}
static void stopped_stage(unsigned command,unsigned bank) {
    /* The older fixture uses completed as its bank selector. Temporarily
     * select the original SDK bank independently, then restore our counters
     * before calling production code: stopped SDK cycles are not our DMA. */
    uint32_t completed=state.completed,submitted=state.submitted;
    state.completed=bank;stage_batch(command,command==12u?42u:0u);
    state.completed=completed;state.submitted=submitted;
    CHECK(packet_bank==bank && !mmio[2]);read_observer=no_stopped_rx;
}
static void stopped_callback(unsigned phase) {
    uint8_t before[KUI_VMU_NATIVE_RX_SLOTS][1024];save_all_rx(before);
    uint32_t hardware[6];memcpy(hardware,mmio,sizeof(hardware));
    uint32_t completed=state.completed,submitted=state.submitted;
    unsigned regions=region_reads,pumps=pump_visits,restores=restorations,bytes=sd.bytes;
    unsigned calls=lease_calls,ads=overlay.stats.advertisements,replies=overlay.stats.replies;
    if(setjmp(terminal)==0) {
        if(phase) kui_vmu_native_passive_complete(&frame);
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
        else kui_vmu_native_passive_split_submit(&frame);
#else
        else kui_vmu_native_passive_submit(&frame);
#endif
    } else {fprintf(stderr,"Stopped callback refused: phase%u detail%u check%08x\n",phase,terminal_error,state.runtime_result);CHECK(false);}
    CHECK(controller_pair.pending==NATIVE_CONTROLLER_STOPPED && !state.pending);
    CHECK(state.completed==completed && state.submitted==submitted && completed==submitted);
    same_all_rx(before);CHECK(!memcmp(hardware,mmio,sizeof(hardware)) && !stopped_rx_reads);
    CHECK(region_reads==regions && pump_visits==pumps && restorations==restores);
    CHECK(sd.bytes==bytes && lease_calls==calls && overlay.stats.advertisements==ads && overlay.stats.replies==replies);
    ++stopped_callbacks;
}
static void stopped_cycles(void) {
    const unsigned commands[]={0u,1u,12u,0u};
    for(unsigned cycle=0;cycle<4u;cycle++) {
        stopped_stage(commands[cycle],cycle&1u);stopped_callback(0u);
        /* MDEN0 ignores the SDK's start request: no physical_complete() and
         * no authored RX reply. Only the original server's phase fields move. */
        put(config.driver.manager_initial+0x10b4u,4u,packet_bank^1u);
        put(config.driver.manager_initial+0x10b8u,4u,packet_bank^1u);
        put(config.driver.manager_initial+0xfc0u,4u,0u);frame.sr=complete_sr;
        stopped_callback(1u);
        /* Repeated idle callbacks are likewise transparent, not unmatched
         * private DMA completions. */
        stopped_callback(1u);
    }
}
static void idle_finish_entry(void) {
    for(unsigned prior_capture=0;prior_capture<2u;prior_capture++) {
        uniform_warm();bind_source_to_fixture();
        CHECK(live_plan.ready && !controller_pair.pending && !state.pending);
        if(prior_capture) {
            controllers(1u,0u);submit();shutdown_controllers_done();complete_return();
        } else {
            CHECK(!controller_pair.count);
            controller_pair.parent=CONTROLLER_FRAMES;
        }
        uint32_t completed=state.completed,submitted=state.submitted;
        stopped_hardware();stopped_stage(prior_capture?1u:0u,prior_capture);
        stopped_callback(0u);
        CHECK(state.completed==completed && state.submitted==submitted);
        put(config.driver.manager_initial+0x10b4u,4u,packet_bank^1u);
        put(config.driver.manager_initial+0x10b8u,4u,packet_bank^1u);
        put(config.driver.manager_initial+0xfc0u,4u,0u);frame.sr=complete_sr;
        stopped_callback(1u);stopped_cycles();
    }
}
static void success_cases(void) {
    for(unsigned captured=0;captured<(KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION?2u:1u);captured++) for(unsigned bank=0;bank<2u;bank++) {
        pending_case(captured,bank);complete_cancel();stopped_cycles();
        /* Cancellation does not bless a stopped bus for new submissions.
         * Once the original SDK restores it, normal publication resumes. */
        put(0xa05f6c14u,4u,1u);stopped_callback(1u);
        CHECK(controller_pair.pending==NATIVE_CONTROLLER_STOPPED);
        read_observer=forbid_write_only;
        put(0xa05f6c14u,4u,1u);stage_batch(0u,0u);controllers(1u,0u);submit();shutdown_controllers_done();
        uint8_t before[5][1024];save_rx(before);complete_return();before[0][2]|=VIRTUAL;
        same_rx(before);
    }
    for(unsigned bit=0;bit<32u;bit++) if(!(0xffff130fu&(1u<<bit))) {
        pending_case(0u,0u);mmio[4]^=1u<<bit;complete_cancel();
    }
    /* Write-only protection readback is deliberately unknowable. Any read
     * fails through the observer; these backing values cannot affect proof. */
    const uint32_t arbitrary[]={0u,UINT32_MAX,0xdeadbeefu};
    for(unsigned i=0;i<3u;i++) {
        pending_case(0u,0u);mmio[5]=arbitrary[i];complete_cancel();
    }
}
static void shutdown_refused(const char *kind,unsigned index) {
    if(setjmp(terminal)==0) {
        kui_vmu_native_passive_complete(&frame);
        fprintf(stderr,"Unexpected shutdown admission: %s %u\n",kind,index);CHECK(false);
    }
    CHECK(terminal_error && state.pending && !state.proof);
}
static void refusal_cases(void) {
    for(unsigned reg=0;reg<5u;reg++) for(unsigned bit=0;bit<32u;bit++) {
        if(reg==4u && !(0xffff130fu&(1u<<bit))) continue;
        pending_case(0u,0u);mmio[reg]^=1u<<bit;
        uint8_t before[KUI_VMU_NATIVE_RX_SLOTS][1024];save_all_rx(before);
        shutdown_refused("MMIO bit",reg*32u+bit);same_all_rx(before);no_storage();++context_refusals;
    }
    for(unsigned which=0;which<20u;which++) {
        pending_case(0u,0u);
        switch(which) {
        case 0:frame.pr+=2u;break;
        case 1:frame.sr&=~0xf0u;break;
        case 2:live_sr=0u;break;
        case 3:bad_guard=1u;break;
        case 4:put(config.driver.manager_cell,4u,config.driver.manager_initial+4u);break;
        case 5:put(config.driver.lock_cell,4u,kui_vmu_native_lock_pointer(&config.driver)+1u);break;
        case 6:put(kui_vmu_native_lock_pointer(&config.driver),1u,0u);break;
        case 7:put(config.driver.manager_initial+0x10b4u,4u,7u);break;
        case 8:put(config.driver.manager_initial+0x10b0u,4u,state.source);break;
        case 9:put(config.layout.input_array,4u,1u);break;
        case 10:put(config.driver.manager_initial+0xfe8u+packet_bank*24u*4u,4u,0u);break;
        case 11:put(config.lease.header+4u,4u,0u);break;
        case 12:put(config.driver.completion_slot,4u,0u);break;
        case 13:put(state.source,4u,0u);break;
        case 14:put(rx_at(0u)+4u,4u,0x02000000u);break;
        case 15:config.driver.family=2u;break;
        case 16:put(config.lease.begin,4u,config.lease.header);CHECK(poll_lease_valid() && !lease_valid());break;
        case 17:live_plan.ready=0u;break;
        case 18:overlay.pending=1u;break;
        default: {
            uint32_t entry=original_driver.consumer+0xa2u;
            uint16_t value=(uint16_t)(original_half(entry)^1u);
            memcpy(memory+physical(entry)-0x0c000000u,&value,2u);break;
        }
        }
        uint8_t before[KUI_VMU_NATIVE_RX_SLOTS][1024];save_all_rx(before);
        shutdown_refused("owner context",which);same_all_rx(before);no_storage();++context_refusals;
    }
    /* Filtered virtual-card/storage transfer never receives cancellation. */
    uniform_warm();bind_source_to_fixture();stage_batch(1u,0u);submit();
    CHECK(overlay.pending && !controller_pair.pending);physical_complete();stopped_hardware();
    uint8_t before[KUI_VMU_NATIVE_RX_SLOTS][1024];save_all_rx(before);
    shutdown_refused("filtered transfer",0u);same_all_rx(before);no_storage();++context_refusals;
}
static void stopped_refusals(void) {
    for(volatile unsigned which=0;which<12u;which++) {
        pending_case(0u,0u);complete_cancel();stopped_stage(0u,0u);
        switch(which) {
        case 0:put(0xa05f6c04u,4u,0x0c400020u);break;
        case 1:put(0xa05f6c14u,4u,2u);break;
        case 2:put(0xa05f6c18u,4u,1u);break;
        case 3:put(config.driver.completion_slot,4u,0u);break;
        case 4:put(config.lease.begin,4u,config.lease.header);CHECK(poll_lease_valid() && !lease_valid());break;
        case 5:put(config.driver.manager_initial+0xfe8u+23u*4u,4u,0u);break;
        case 6:put(value_at(0xa05f6c04u),4u,0x100u);break;
        case 7:put(value_at(0xa05f6c04u)+4u,4u,physical(config.lease.payload));break;
        case 8:put(config.layout.timer1_callback,4u,0x8ce00000u);break;
        case 9:put(config.driver.lock_cell,4u,kui_vmu_native_lock_pointer(&config.driver)^1u);break;
        case 10: {
            uint32_t entry=original_driver.consumer+0xa2u;
            uint16_t word=(uint16_t)(original_half(entry)^1u);
            memcpy(memory+physical(entry)-0x0c000000u,&word,2u);break;
        }
        default:frame.r14+=4u;break;
        }
        uint8_t before[KUI_VMU_NATIVE_RX_SLOTS][1024];save_all_rx(before);
        if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
        CHECK(terminal_error==0x46u && !state.pending && state.submitted==state.completed);
        same_all_rx(before);CHECK(!stopped_rx_reads);no_storage();++context_refusals;
    }
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f);
    original_bytes=fread(original,1u,sizeof(original),f);CHECK(!ferror(f) && feof(f));fclose(f);
    CHECK(kui_vmu_native_discover(original,original_bytes,0x8c010000u,&original_driver)==KUI_VMU_NATIVE_RESOLVED);
    source_cases();success_cases();refusal_cases();stopped_refusals();idle_finish_entry();
    printf("Stopped lifecycle: %u submit/idle callbacks, no RX reads or private DMA-count changes\n",stopped_callbacks);
    printf("Controller shutdown: %u checks; %u original source-role refusals, %u cancellation boundary refusals, %u cancellations; no RX/MMIO/storage/TICK publication\n",checks,source_refusals,context_refusals,cancellations);
    return 0;
}
