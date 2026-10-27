/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_LOADER_TIMELINE_H
#define KUI_NATIVE_LOADER_TIMELINE_H
#include <stdint.h>

#define KUI_NATIVE_TIMELINE_HZ 781250u
#define KUI_NATIVE_TIMELINE_BIN_TICKS 488281u /* 0.62499968 seconds */
#define KUI_NATIVE_TIMELINE_BINS 32u
#define KUI_NATIVE_TIMELINE_LONGEST 8u
#define KUI_NATIVE_TIMELINE_REQUESTS 16u
#define KUI_NATIVE_TIMELINE_CHAIN_GAP 26042u /* proximity threshold, NOT a frame witness */
enum kui_native_loader_event_flags {
    KUI_NATIVE_TIMELINE_ENTER=1u,
    KUI_NATIVE_TIMELINE_EXIT=2u,
    KUI_NATIVE_TIMELINE_MAPLE=3u,
    KUI_NATIVE_TIMELINE_KIND_MASK=15u,
    KUI_NATIVE_TIMELINE_CLOCK_VALID=0x100u,
    KUI_NATIVE_TIMELINE_CLOCK_BREAK=0x200u,
    KUI_NATIVE_TIMELINE_READ=0x400u,
    KUI_NATIVE_TIMELINE_PENDING=0x800u,
    KUI_NATIVE_TIMELINE_COMPLETE=0x1000u,
    KUI_NATIVE_TIMELINE_FAILED=0x2000u
};
enum kui_native_loader_request_flags {
    KUI_NATIVE_TIMELINE_PARTIAL=1u,
    KUI_NATIVE_TIMELINE_REQUEST_FAILED=2u,
    KUI_NATIVE_TIMELINE_REPLACED=4u,
    KUI_NATIVE_TIMELINE_BAD_CLOCK=8u
};
/* Raw observations. result is the actual GD return at EXIT; its bit pattern
 * represents int32_t. progress is completed bytes. The collector owns no IO.
 * CLOCK_VALID means the wrapper admitted the exact timer profile at this
 * sample. CLOCK_BREAK starts another epoch; no duration crosses epochs.
 * Neither a valid sample nor modulo arithmetic proves an unobserved timer
 * stop/reset did not occur while game code ran. */
struct kui_native_loader_event {
    uint32_t tick,flags,function,result,token,lba,count,progress;
    uint32_t caller_sr,vbr,iprb,chcr,tcr,dmaor,ssr,state;
};
struct kui_native_loader_bin {
    uint32_t epoch,tick,gd_ticks,calls,read_starts,read_completes,maples,invalid;
};
struct kui_native_loader_call {
    struct kui_native_loader_event begin,end;
    uint32_t elapsed,gap,epoch;
};
struct kui_native_loader_request {
    uint32_t epoch,token,lba,count,begin_tick,end_tick,total_ticks,inside_ticks;
    uint32_t outside_ticks,calls,progress,result,flags,chain_length,chain_ticks,chain_inside;
};
struct kui_native_loader_timeline {
    struct kui_native_loader_bin bins[KUI_NATIVE_TIMELINE_BINS];
    struct kui_native_loader_call longest[KUI_NATIVE_TIMELINE_LONGEST];
    struct kui_native_loader_request requests[KUI_NATIVE_TIMELINE_REQUESTS];
    struct kui_native_loader_event entered;
    struct kui_native_loader_request active;
    uint32_t events,valid,invalid,epoch,clock_breaks,unmatched,overflow,saturated;
    uint32_t have_clock,last_tick,origin_tick,epoch_bin,bin_count,have_enter,have_exit,last_exit;
    uint32_t longest_count,request_count,request_next,have_request,last_token;
    uint32_t chain_length,chain_begin,chain_end,chain_inside,chain_epoch;
    uint32_t max_call_ticks,max_gap_ticks,max_chain_length,max_chain_ticks,max_chain_inside;
};
_Static_assert(sizeof(struct kui_native_loader_event)==64u,"timeline input remains sixteen words");
_Static_assert(sizeof(struct kui_native_loader_timeline)<=4096u,"timeline owns at most four KiB");
void kui_native_loader_timeline_reset(struct kui_native_loader_timeline *);
void kui_native_loader_timeline_add(struct kui_native_loader_timeline *,const struct kui_native_loader_event *);
#endif
