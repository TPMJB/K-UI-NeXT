/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/drive_controls.h"
#include <string.h>

static const unsigned retry_values[]={0,1,2,4,8};
const char *kui_drive_speed_name(unsigned selection) {
    static const char *const names[]={"Unchanged","Maximum","1x requested","2x requested",
        "4x requested","6x requested","8x requested","10x requested","12x requested"};
    return selection<KUI_DRIVE_SPEED_CHOICES?names[selection]:"Invalid";
}
const char *kui_drive_retry_name(unsigned selection) {
    static const char *const names[]={"Unchanged","0","1","2","4","8"};
    return selection<KUI_DRIVE_RETRY_CHOICES?names[selection]:"Invalid";
}
const char *kui_drive_control_result_name(enum kui_drive_control_result result) {
    static const char *const names[]={"verified","invalid request","drive unavailable",
        "mode query failed","mode change rejected","mode readback differs"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"invalid result";
}
bool kui_drive_options_valid(const struct kui_drive_options *options) {
    return options && options->speed<KUI_DRIVE_SPEED_CHOICES &&
        options->retries<KUI_DRIVE_RETRY_CHOICES;
}
static bool valid_ops(const struct kui_drive_control_ops *ops) {
    return ops && ops->safe && ops->query && ops->set;
}
static bool equal(const struct kui_drive_mode *a,const struct kui_drive_mode *b) {
    return a->speed==b->speed && a->standby==b->standby &&
        a->read_flags==b->read_flags && a->read_retry==b->read_retry;
}
static bool valid_mode(const struct kui_drive_mode *mode) {
    return mode->speed<=7 && mode->standby<=65535 &&
        mode->read_flags<=255 && mode->read_retry<=255;
}
static void attempt_begin(struct kui_drive_controls *state,const struct kui_drive_mode *mode) {
    state->attempt_requested=*mode;
    memset(&state->attempt_observed,0,sizeof(state->attempt_observed));
    state->attempt_observed_valid=false;
    state->attempt_result=KUI_DRIVE_CONTROL_INVALID;
}
static enum kui_drive_control_result apply_mode(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops,const struct kui_drive_mode *mode,bool remember) {
    if(remember)attempt_begin(state,mode);
    enum kui_drive_control_result result;
    if(!ops->safe(ops->ctx))result=KUI_DRIVE_CONTROL_UNSAFE;
    else {
        /* Even a rejected command can have partially changed a setting. The
         * entry snapshot remains restoration authority until the session ends. */
        state->modified=true;
        if(!ops->set(ops->ctx,mode))result=KUI_DRIVE_CONTROL_SET_FAILED;
        else if(!ops->safe(ops->ctx))result=KUI_DRIVE_CONTROL_UNSAFE;
        else if(!ops->query(ops->ctx,&state->observed))result=KUI_DRIVE_CONTROL_QUERY_FAILED;
        else {
            if(remember) {
                state->attempt_observed=state->observed;
                state->attempt_observed_valid=true;
            }
            result=equal(mode,&state->observed)?KUI_DRIVE_CONTROL_OK:KUI_DRIVE_CONTROL_MISMATCH;
        }
    }
    if(remember)state->attempt_result=result;
    return result;
}
static enum kui_drive_control_result apply(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops,const struct kui_drive_mode *mode) {
    return apply_mode(state,ops,mode,true);
}
static struct kui_drive_mode selected_mode(const struct kui_drive_controls *state,
    const struct kui_drive_options *options) {
    struct kui_drive_mode mode=state->original;
    if(options->speed)mode.speed=options->speed-1;
    if(options->retries)mode.read_retry=retry_values[options->retries-1];
    return mode;
}
enum kui_drive_control_result kui_drive_controls_trial_begin(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops) {
    if(!state || state->active || !valid_ops(ops))return KUI_DRIVE_CONTROL_INVALID;
    if(!ops->safe(ops->ctx))return KUI_DRIVE_CONTROL_UNSAFE;
    memset(state,0,sizeof(*state));
    state->attempt_result=KUI_DRIVE_CONTROL_QUERY_FAILED;
    if(!ops->query(ops->ctx,&state->original) || !valid_mode(&state->original))
        return KUI_DRIVE_CONTROL_QUERY_FAILED;
    state->requested=state->observed=state->original;
    attempt_begin(state,&state->original);
    state->attempt_observed=state->original;
    state->attempt_observed_valid=true;
    state->attempt_result=KUI_DRIVE_CONTROL_OK;
    state->active=state->snapshot_valid=state->verified=state->trial=true;
    return KUI_DRIVE_CONTROL_OK;
}
enum kui_drive_control_result kui_drive_controls_select(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops,const struct kui_drive_options *options) {
    if(!state || !state->active || !state->trial || !state->snapshot_valid ||
       !state->verified || !valid_ops(ops) || !kui_drive_options_valid(options))
        return KUI_DRIVE_CONTROL_INVALID;
    if(!ops->safe(ops->ctx)) {state->verified=false;return KUI_DRIVE_CONTROL_UNSAFE;}
    struct kui_drive_mode desired=selected_mode(state,options),previous=state->requested;
    if(equal(&desired,&previous)) {
        attempt_begin(state,&desired);
        state->attempt_observed=state->observed;
        state->attempt_observed_valid=true;
        state->attempt_result=KUI_DRIVE_CONTROL_OK;
        return KUI_DRIVE_CONTROL_OK;
    }
    state->verified=false;
    enum kui_drive_control_result result=apply(state,ops,&desired);
    if(result==KUI_DRIVE_CONTROL_OK) {
        state->requested=desired;state->verified=true;return result;
    }
    /* SET failure may have partially changed the firmware. No caller may skip
     * this profile and read under the previous policy until rollback verifies. */
    if(!ops->safe(ops->ctx) || apply_mode(state,ops,&previous,false)!=KUI_DRIVE_CONTROL_OK)
        return KUI_DRIVE_CONTROL_UNSAFE;
    state->verified=true;
    return result;
}
enum kui_drive_control_result kui_drive_controls_begin(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops,const struct kui_drive_options *options) {
    if(!state || state->active || !valid_ops(ops) || !kui_drive_options_valid(options))
        return KUI_DRIVE_CONTROL_INVALID;
    if(!ops->safe(ops->ctx))return KUI_DRIVE_CONTROL_UNSAFE;
    memset(state,0,sizeof(*state));
    state->attempt_result=KUI_DRIVE_CONTROL_QUERY_FAILED;
    /* The historical default path issues neither REQ_MODE nor SET_MODE. */
    if(!options->speed && !options->retries) {
        state->attempt_result=KUI_DRIVE_CONTROL_OK;
        state->active=state->verified=true;return KUI_DRIVE_CONTROL_OK;
    }
    if(!ops->query(ops->ctx,&state->original))return KUI_DRIVE_CONTROL_QUERY_FAILED;
    if(!valid_mode(&state->original))return KUI_DRIVE_CONTROL_QUERY_FAILED;
    state->requested=selected_mode(state,options);
    state->active=state->snapshot_valid=true;
    enum kui_drive_control_result result=apply(state,ops,&state->requested);
    state->verified=result==KUI_DRIVE_CONTROL_OK;return result;
}
enum kui_drive_control_result kui_drive_controls_reapply(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops) {
    if(!state || !state->active || !valid_ops(ops))return KUI_DRIVE_CONTROL_INVALID;
    if(!state->verified)return KUI_DRIVE_CONTROL_UNSAFE;
    if(!state->modified && !state->trial)return KUI_DRIVE_CONTROL_OK;
    enum kui_drive_control_result result=apply(state,ops,&state->requested);
    state->verified=result==KUI_DRIVE_CONTROL_OK;return result;
}
enum kui_drive_control_result kui_drive_controls_end(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops) {
    if(!state || !valid_ops(ops))return KUI_DRIVE_CONTROL_INVALID;
    enum kui_drive_control_result result=KUI_DRIVE_CONTROL_OK;
    if(state->active && state->modified)result=apply(state,ops,&state->original);
    state->active=state->modified=state->snapshot_valid=state->verified=state->trial=false;
    return result;
}
