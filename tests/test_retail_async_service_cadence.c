/* SPDX-License-Identifier: GPL-3.0-only */
/* Compare identical finite GD/IRQ schedules against real reader revisions.
 * A round is a caller opportunity, not a Dreamcast video frame or timing
 * measurement. Unlike finish(), this never supplies unlimited interrupts. */
#if KUI_NATIVE_READER_DIAGNOSTIC
#include <assert.h>
#include <stdint.h>
uint32_t kui_retail_async_test_read32(uint32_t address) {
    static uint32_t counter;
    assert(address==UINT32_C(0xffd8000c));
    counter-=19u;return counter;
}
#endif
#define main original_async_main
#include "test_retail_async.c"
#undef main

struct counts {unsigned request,check,exec,irq;};
static struct counts maximum;
static unsigned delivered(void) {return R.engine.stats.call_blocks+R.engine.stats.irq_blocks;}
static void update(unsigned *p,unsigned v) {if(v>*p)*p=v;}
static int32_t call(uint32_t function,int32_t token) {
    unsigned before=delivered();
    int32_t result=gd(function,function==KUI_GD_CHECK?(uint32_t)token:0u,
        function==KUI_GD_CHECK?STATUS:0u);
    update(function==KUI_GD_EXEC?&maximum.exec:&maximum.check,delivered()-before);
    return result;
}
static void scenario(unsigned how,bool cooked,unsigned delay,unsigned schedule) {
    cooked_fixture=cooked;setup(GAME_VBR,how,48,false);mode(2048);
    memset(&maximum,0,sizeof(maximum));m.delay=delay;
    unsigned before=delivered();int32_t token=request(45001u,64u,OUTPUT);
    maximum.request=delivered()-before;
    unsigned rounds=0,irq_entries=0;
    int32_t status=KUI_GD_PROCESSING;
    while(R.engine.active) {
        /* At most one in-flight reception finishes between caller visits.
         * Time within an entry remains the existing model's register delay. */
        elapse();
        kui_retail_hook_sr=schedule==2u?0u:0xf0u;
        if(schedule==2u && hooked() && kui_sci_stream_busy()) {
            before=delivered();CHECK(!kui_retail_async_irq());
            update(&maximum.irq,delivered()-before);++irq_entries;
        }
        if(schedule==1u)CHECK(call(KUI_GD_EXEC,token)==0);
        status=call(KUI_GD_CHECK,token);
        if(schedule==3u && status==KUI_GD_PROCESSING)status=call(KUI_GD_CHECK,token);
        CHECK(++rounds<4096u);
    }
    if(status!=KUI_GD_COMPLETED)status=call(KUI_GD_CHECK,token);
    CHECK(status==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);
    CHECK(!kui_sci_stream_stats()->polled);
    printf("{\"how\":%u,\"cooked\":%u,\"delay\":%u,\"schedule\":%u,"
        "\"rounds\":%u,\"irq_entries\":%u,\"request_max\":%u,\"check_max\":%u,"
        "\"exec_max\":%u,\"irq_max\":%u,\"exact_output\":true}\n",
        how,cooked,delay,schedule,rounds,irq_entries,maximum.request,
        maximum.check,maximum.exec,maximum.irq);
}
static void hot_interrupt(unsigned how) {
    cooked_fixture=true;setup(GAME_VBR,how,48,false);mode(2048);
    int32_t token=request(45001u,64u,OUTPUT);m.delay=0;elapse();
    unsigned before=delivered();CHECK(!kui_retail_async_irq());
    unsigned blocks=delivered()-before;
    CHECK(finish(token,8,1)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
    printf("{\"hot_irq\":true,\"how\":%u,\"blocks_in_one_irq\":%u,\"exact_output\":true}\n",how,blocks);
}
int main(void) {
    for(unsigned how=X;how<=Y;++how)
        for(unsigned cooked=0;cooked<2u;++cooked)
            for(unsigned delay=0;delay<3u;++delay)
                for(unsigned schedule=0;schedule<4u;++schedule)
                    scenario(how,cooked,delay==0u?0u:delay==1u?1000u:5000u,schedule);
    hot_interrupt(X);hot_interrupt(Y);
    return 0;
}
