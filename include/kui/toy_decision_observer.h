/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_DECISION_OBSERVER_H
#define KUI_TOY_DECISION_OBSERVER_H
#include <stdint.h>
#include "kui/retail_gd.h"
#ifndef KUI_TOY_DECISION_PROFILE
#define KUI_TOY_DECISION_PROFILE 0
#endif
#if KUI_TOY_DECISION_PROFILE < 0 || KUI_TOY_DECISION_PROFILE > 2
#error Toy decision profile must be 0, 1 or 2
#endif
#if KUI_TOY_DECISION_PROFILE
#if KUI_TOY_PILOT_DATA_PIPELINE_PROFILE != 2 || KUI_TOY_PILOT_DATA_SCHEDULE_MODE != 2 || \
    KUI_TOY_PILOT_DATA_BUDGET_PROFILE != 4 || \
    KUI_TOY_DEMAND_TEST_PROFILE || KUI_TOY_FMV_IDLE_REARM || KUI_TOY_PILOT_WORD_REVERSE_PROFILE
#error Toy decision profiles require unchanged AP, without earlier test alternatives
#endif
#define KUI_TOY_DECISION_BINS 8u
#define KUI_TOY_DECISION_BIN_TICKS 1562500u
#define KUI_TOY_DECISION_BAD_CLOCK 1u
#define KUI_TOY_DECISION_SKIPPED_BINS 2u
#define KUI_TOY_DECISION_CROSS_BIN 4u
#define KUI_TOY_DECISION_SATURATED 8u
struct kui_toy_decision_bin {
    uint32_t sequence,epoch,first_tick,last_tick,flags;
    uint32_t gd_calls,gd_valid,data_calls,bytes,gd_ticks;
    uint32_t gd_max,gap_samples,gap_ticks,gap_max,service_calls;
    uint32_t service_valid,service_ticks,service_max,wait_steps,credit_steps;
    uint32_t wait_ticks,credit_ticks,other_ticks,zero_services,call_blocks;
    uint32_t irq_blocks,errors,retries,mask_low,mask_high;
    uint32_t mask15,mask_bl,route_own,route_other,priority;
    uint32_t updater_entries;
    uint32_t accepted_data,completed_data,read_modes,max_request;
};
_Static_assert(sizeof(struct kui_toy_decision_bin)==160u,"Toy decision bin ABI");
struct kui_toy_decision_report {
    uint32_t magic,version,enabled,frozen,head,count,epoch,invalid_clocks;
    uint32_t first_vbr,last_vbr,expected_vbr,nested_calls;
    uint32_t last_command,last_lba,last_count,last_sector_bytes,expired_bins;
    uint32_t freeze_tick,freeze_valid,last_sample_age;
    struct kui_toy_decision_bin bins[KUI_TOY_DECISION_BINS];
};
/* Interval classes include finite loop/admission work, not pure wire time.
 * Existing AP timer-budget samples are reused; no new per-step MMIO reads. */
struct kui_toy_decision_service {
    uint32_t calls,elapsed_ticks,max,wait_steps,credit_steps;
    uint32_t wait_ticks,credit_ticks,other_ticks,zero_calls;
};
void kui_toy_decision_gd_begin(const struct kui_retail_gd *,uint32_t,uint32_t);
void kui_toy_decision_gd_end(const struct kui_retail_gd *,uint32_t,uint32_t,int32_t);
int kui_toy_decision_service_active(void);
void kui_toy_decision_service_commit(const struct kui_toy_decision_service *);
void kui_toy_decision_freeze(void);
const struct kui_toy_decision_report *kui_toy_decision_snapshot(void);
uint32_t kui_toy_pilot_sci_decision_vbr(void);
#ifdef KUI_TOY_DECISION_HOST_TEST
uint32_t kui_toy_decision_host_read(uint32_t,unsigned);
uint32_t kui_toy_decision_host_vbr(void);
uint32_t kui_toy_decision_host_sr(void);
uint32_t kui_toy_decision_host_updater(void);
#endif
#endif
#endif
