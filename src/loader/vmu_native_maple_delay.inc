/* SPDX-License-Identifier: GPL-3.0-only */
/* Diagnostic delay only. No timer setup, PVR/Maple access or VMU state work.
 * 59/76 ticks reproduce previously measured PROFILE C-body intervals;
 * they are not a claim about the unmeasured NORMAL callback duration. */
#define NATIVE_MAPLE_DELAY_SUBMIT_TICKS 59u
#define NATIVE_MAPLE_DELAY_COMPLETE_TICKS 76u
#define NATIVE_MAPLE_DELAY_POLLS 4096u
enum native_maple_delay_status {
    NATIVE_MAPLE_DELAY_OK=0,
    NATIVE_MAPLE_DELAY_BAD_TARGET,
    NATIVE_MAPLE_DELAY_BEGIN_READ,
    NATIVE_MAPLE_DELAY_BEGIN_PROFILE,
    NATIVE_MAPLE_DELAY_COUNTER_READ,
    NATIVE_MAPLE_DELAY_COUNTER_RESET,
    NATIVE_MAPLE_DELAY_END_READ,
    NATIVE_MAPLE_DELAY_END_PROFILE,
    NATIVE_MAPLE_DELAY_FROZEN
};
struct native_maple_delay_result {uint32_t elapsed,polls;};
typedef bool (*native_maple_delay_reader)(void *,bool,
    struct kui_vmu_native_profile_sample *);
static enum native_maple_delay_status native_maple_delay_wait(uint32_t ticks,
    native_maple_delay_reader read,void *context,struct native_maple_delay_result *result) {
    struct kui_vmu_native_profile_sample begin={0},end={0};
    *result=(struct native_maple_delay_result){0};
    if(ticks!=NATIVE_MAPLE_DELAY_SUBMIT_TICKS && ticks!=NATIVE_MAPLE_DELAY_COMPLETE_TICKS)
        return NATIVE_MAPLE_DELAY_BAD_TARGET;
    if(!read(context,true,&begin)) return NATIVE_MAPLE_DELAY_BEGIN_READ;
    if(!kui_vmu_native_profile_sample_valid(&begin)) return NATIVE_MAPLE_DELAY_BEGIN_PROFILE;
    for(unsigned i=0u;i<NATIVE_MAPLE_DELAY_POLLS;i++) {
        ++result->polls;
        if(!read(context,false,&end)) return NATIVE_MAPLE_DELAY_COUNTER_READ;
        result->elapsed=begin.counter-end.counter;
        if(result->elapsed>KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL)
            return NATIVE_MAPLE_DELAY_COUNTER_RESET;
        if(result->elapsed<ticks) continue;
        uint32_t elapsed=result->elapsed;
        if(!read(context,true,&end)) return NATIVE_MAPLE_DELAY_END_READ;
        if(!kui_vmu_native_profile_sample_valid(&end)) return NATIVE_MAPLE_DELAY_END_PROFILE;
        result->elapsed=begin.counter-end.counter;
        if(result->elapsed<elapsed || result->elapsed>KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL)
            return NATIVE_MAPLE_DELAY_COUNTER_RESET;
        return NATIVE_MAPLE_DELAY_OK;
    }
    return NATIVE_MAPLE_DELAY_FROZEN;
}
#ifndef NATIVE_MAPLE_DELAY_HOST_TEST
static bool native_maple_delay_sample(void *unused,bool profile,
    struct kui_vmu_native_profile_sample *sample) {
    (void)unused;
    if(profile) {
        sample->frqcr=*(volatile const uint16_t *)(uintptr_t)UINT32_C(0xffc00000);
        sample->tcor=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xffd80008);
        sample->tcr=*(volatile const uint16_t *)(uintptr_t)UINT32_C(0xffd80010);
        sample->tstr=*(volatile const uint8_t *)(uintptr_t)UINT32_C(0xffd80004);
    }
    sample->counter=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xffd8000c);
    return true;
}
static void native_maple_delay_visit(uint32_t phase,uint32_t ticks) {
    struct native_maple_delay_result result;
    enum native_maple_delay_status status=native_maple_delay_wait(ticks,
        native_maple_delay_sample,NULL,&result);
    if(status!=NATIVE_MAPLE_DELAY_OK) {
        /* Never turn a skipped/frozen clock into an apparently passing
         * timing test. An early SDK clock setup refusal is a separate result. */
        state.runtime_result=UINT32_C(0x4d0000)|(phase<<8)|(uint32_t)status;
        state.fault_at=phase;state.fault_value=result.elapsed;state.fault_expect=ticks;
        fail(0x4du);
    }
}
void kui_vmu_native_maple_delay_submit(void) {
    native_maple_delay_visit(0u,NATIVE_MAPLE_DELAY_SUBMIT_TICKS);
}
void kui_vmu_native_maple_delay_complete(void) {
    native_maple_delay_visit(1u,NATIVE_MAPLE_DELAY_COMPLETE_TICKS);
}
#endif
