/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual opt-in low reader against the existing card/DMA model.
 * The cache implementation and its private-stack entry are tested separately. */
#include "kui/native_readahead.h"
#include "kui/native_sdk_sci_runtime.h"
#include <assert.h>
extern const struct kui_native_ra_low_ops kui_native_ra_reader_ops;
static uint32_t clock_counter=UINT32_C(0x70000000),eri_slot,rxi_slot;
void kui_retail_sdk_sci_entry(void) {}
uint32_t kui_retail_async_test_read32(uint32_t address) {
    if(address==UINT32_C(0xffd8000c)) {
        uint32_t result=clock_counter;clock_counter-=19u;return result;
    }
    if(address==KUI_NATIVE_SDK_SCI_OWNER_VBR+KUI_NATIVE_SDK_SCI_ERI_SLOT_OFFSET)return eri_slot;
    assert(address==KUI_NATIVE_SDK_SCI_OWNER_VBR+KUI_NATIVE_SDK_SCI_RXI_SLOT_OFFSET);
    return rxi_slot;
}
volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
#define main original_async_main
#define kui_sci_sd_acquire fixture_sci_sd_acquire
#include "test_retail_async.c"
#undef kui_sci_sd_acquire
#undef main

uint32_t kui_retail_async_vmu_test_sr(void) {return 0xf0u;}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    enum kui_loader_sd_result result=fixture_sci_sd_acquire();
    if(result==KUI_LOADER_SD_OK)R.shared.port.acquired=true;
    return result;
}
void kui_sci_sd_release(void) {
    CHECK(kui_retail_hook_active==1u && R.shared.port.acquired &&
        !kui_sci_stream_busy() && !R.shared.stream.state &&
        !R.engine.hooked && !R.engine.release.armed);
    R.shared.port.acquired=false;
}

static void setup_sdk(unsigned how,bool ready) {
    cooked_fixture=true;setup(KUI_NATIVE_SDK_SCI_OWNER_VBR,how,48u,false);mode(2048u);
    kui_retail_hook_active=kui_retail_hook_fault=0u;
    R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]=UINT32_C(0x8c1add0c);
    R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]=KUI_NATIVE_SDK_SCI_OWNER_VBR;
    eri_slot=rxi_slot=(uint32_t)(uintptr_t)kui_retail_sdk_sci_entry;
    if(ready)R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=KUI_NATIVE_SDK_SCI_COOKIE;
}
static void complete(int32_t token) {
    CHECK(finish(token,0u,1u)==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);
    CHECK(!R.engine.hooked && !R.engine.release.armed && !hw.vbr_sets);
}

static unsigned deliveries,quiesces;
static uint32_t delivered_wait,handled=1u,quiesce_ok=1u;
static bool stop_speculation;
static uint32_t auxiliary(uint32_t event,uint32_t arg,
    kui_retail_async_region_type *region,uint32_t reserved) {
    CHECK(region==&R && !reserved);
    if(event==KUI_NATIVE_RA_DELIVER) {
        ++deliveries;delivered_wait=arg;
        return handled;
    }
    CHECK(event==KUI_NATIVE_RA_QUIESCE && !arg);
    CHECK(kui_retail_hook_active==1u && !R.shared.service.pending &&
        !R.shared.service.executing && !R.engine.active && !R.engine.in_irq);
    ++quiesces;
    if(stop_speculation) {
        kui_native_ra_reader_ops.unhook();
        CHECK(kui_native_ra_reader_ops.stop()==KUI_SCI_STREAM_OK);
    }
    return quiesce_ok;
}
static void bind_auxiliary(void) {
    R.vector600[KUI_NATIVE_RA_ENTRY_INDEX]=(uint32_t)(uintptr_t)auxiliary;
    deliveries=quiesces=0u;handled=quiesce_ok=1u;stop_speculation=false;
}
static void descriptor(void) {
    const struct kui_native_ra_low_ops *ops=&kui_native_ra_reader_ops;
    CHECK(ops->magic==KUI_NATIVE_RA_OPS_MAGIC && ops->version==KUI_NATIVE_RA_VERSION &&
        ops->bytes==sizeof(*ops));
    CHECK(ops->region_storage==&kui_retail_async_region_storage && ops->deliver &&
        ops->hook && ops->unhook && ops->fetch==kui_sci_stream_fetch &&
        ops->poll==kui_sci_stream_poll && ops->take==kui_sci_stream_take &&
        ops->stop==kui_sci_stream_stop && ops->busy==kui_sci_stream_busy &&
        ops->feed==kui_retail_cursor_feed);
}
static void deliver_handoff(void) {
    setup_sdk(X,true);bind_auxiliary();
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(deliveries && !delivered_wait && !R.engine.cursor.done &&
        !R.engine.stats.call_blocks && !kui_sci_stream_busy());
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
    CHECK(delivered_wait==20u && !R.engine.cursor.done && !R.engine.stats.call_blocks);
    handled=0u;complete(token); /* Invalid/unhandled auxiliary falls back. */
    CHECK(R.engine.stats.call_blocks==256u && !quiesces);
}
static void speculative_irq_and_loan(void) {
    setup_sdk(X,true);int32_t token=request(45001u,64u,OUTPUT);complete(token);
    bind_auxiliary();
    CHECK(kui_native_ra_reader_ops.fetch(R.engine.cursor.block+1u,65536u,false)==KUI_SCI_STREAM_OK);
    CHECK(kui_sci_stream_busy() && !R.engine.active && !R.shared.service.pending);
    kui_native_ra_reader_ops.hook();
    CHECK(R.engine.hooked && kui_retail_sdk_sci_dispatch(0x4e0u)==0u);
    CHECK(deliveries==1u && !delivered_wait && R.engine.hooked && kui_sci_stream_busy());
    struct kui_loader_sd copied;
    stop_speculation=true;
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(quiesces==1u && !kui_sci_stream_busy() && !R.engine.hooked &&
        !R.engine.opened && !R.shared.port.acquired &&
        R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_WRITER);
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(!kui_retail_hook_active && R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_IDLE);
    CHECK(quiesces==1u && deliveries==1u); /* Restart belongs to a later TICK. */
}
static void real_demand_and_refused_quiesce(void) {
    setup_sdk(X,true);bind_auxiliary();handled=0u;
    int32_t token=request(45001u,64u,OUTPUT);
    struct kui_loader_sd copied;
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_BUSY);
    CHECK(!quiesces && !kui_retail_hook_active && R.shared.service.pending);
    complete(token);
    quiesce_ok=0u;
    CHECK(!kui_sci_stream_busy());
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_BUSY);
    CHECK(quiesces==1u && !kui_retail_hook_active &&
        R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_IDLE);
    R.vector600[KUI_NATIVE_RA_ENTRY_INDEX]=0u;
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
}
static void pre_quiesce_guards(void) {
    for(unsigned cause=0u;cause<6u;cause++) {
        setup_sdk(X,true);bind_auxiliary();handled=0u;
        int32_t token=request(45001u,64u,OUTPUT);complete(token);
        switch(cause) {
        case 0u:R.shared.service.initialized=0u;break;
        case 1u:R.shared.service.pending=1u;break;
        case 2u:R.shared.service.executing=1u;break;
        case 3u:R.engine.active=1u;break;
        case 4u:R.engine.in_irq=1u;break;
        default:R.engine.release.depth=1u;break;
        }
        kui_retail_async_region_type before=R;
        struct kui_loader_sd copied={0},unchanged=copied;
        unsigned clocks=card.clocks;
        CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_BUSY);
        CHECK(!quiesces && !kui_retail_hook_active && card.clocks==clocks &&
            !memcmp(&before,&R,sizeof(R)) && !memcmp(&copied,&unchanged,sizeof(copied)));
    }
}
int main(void) {
    for(unsigned how=X;how<=Y;how++) {
        setup_sdk(how,true);int32_t token=request(45001u,64u,OUTPUT);complete(token);
    }
    descriptor();deliver_handoff();
    speculative_irq_and_loan();real_demand_and_refused_quiesce();pre_quiesce_guards();
    printf("native read-ahead low bridge: %u checks; null/fallback, handled delivery, inactive speculative IRQ, quiesce and demand/loan exclusion passed\n",checks);
    return 0;
}
