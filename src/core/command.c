/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/core.h"

static bool usable(const struct kui_command_ops *o, uint32_t timeout_ms, uint32_t abort_ms) {
    return o && o->now_ms && o->yield && o->cancelled && o->submit && o->poll && o->abort &&
           timeout_ms && abort_ms;
}

/* Abort and recover, then report `result`. Shared by the blocking and split paths so there is
 * one recovery policy, not two. */
static enum kui_command_result recover(const struct kui_command_ops *o, int handle,
                                       uint32_t abort_ms, enum kui_command_result result) {
    o->abort(o->ctx, handle);
    uint64_t start = o->now_ms(o->ctx);
    while(o->now_ms(o->ctx) - start < abort_ms) {
        int status = o->poll(o->ctx, handle);
        if(status == 0 || status == 2 || status == -1) return result;
        o->yield(o->ctx);
    }
    return KUI_CMD_RECOVERY_FAILED;
}

enum kui_command_result kui_command_begin(const struct kui_command_ops *o,
    int command, void *params, uint32_t timeout_ms, uint32_t abort_ms,
    struct kui_command_async *out) {
    if(!usable(o, timeout_ms, abort_ms) || !out) return KUI_CMD_INVALID;
    out->handle = 0; out->live = false;
    out->timeout_ms = timeout_ms; out->abort_ms = abort_ms;
    out->start = o->now_ms(o->ctx);
    /* Submission and execution share one deadline, including busy retries. */
    while(out->handle == 0) {
        if(o->cancelled(o->ctx)) return KUI_CMD_CANCELLED;
        if(o->now_ms(o->ctx) - out->start >= timeout_ms) return KUI_CMD_TIMEOUT;
        out->handle = o->submit(o->ctx, command, params);
        if(out->handle < 0) return KUI_CMD_FAILED;
        if(out->handle == 0) o->yield(o->ctx);
    }
    out->live = true;   /* from here the firmware may own params and the buffer until end */
    return KUI_CMD_OK;
}

bool kui_command_ready(const struct kui_command_ops *o, struct kui_command_async *a) {
    if(!usable(o, 1, 1) || !a || !a->live) return true;   /* nothing to wait for */
    int status = o->poll(o->ctx, a->handle);
    /* Anything that is not "still running" is finished as far as waiting goes; end sorts out
     * whether it succeeded. */
    return status != 1 && status != 4;
}

enum kui_command_result kui_command_end(const struct kui_command_ops *o,
    struct kui_command_async *a) {
    if(!a) return KUI_CMD_INVALID;
    if(!usable(o, a->timeout_ms ? a->timeout_ms : 1, a->abort_ms ? a->abort_ms : 1))
        return KUI_CMD_INVALID;
    if(!a->live) return KUI_CMD_FAILED;   /* never begun, or already ended */
    a->live = false;
    enum kui_command_result result;
    for(;;) {
        if(o->cancelled(o->ctx)) { result = KUI_CMD_CANCELLED; break; }
        if(o->now_ms(o->ctx) - a->start >= a->timeout_ms) { result = KUI_CMD_TIMEOUT; break; }
        int status = o->poll(o->ctx, a->handle);
        if(status == 2) return KUI_CMD_OK;
        if(status == -1 || status == 0) return KUI_CMD_FAILED;
        if(status != 1 && status != 4) {
            /* Unexpected streaming/unknown status may still own the buffer. */
            result = KUI_CMD_FAILED; break;
        }
        o->yield(o->ctx);
    }
    return recover(o, a->handle, a->abort_ms, result);
}

enum kui_command_result kui_command(const struct kui_command_ops *o,
    int command, void *params, uint32_t timeout_ms, uint32_t abort_ms) {
    struct kui_command_async async;
    enum kui_command_result r = kui_command_begin(o, command, params, timeout_ms, abort_ms, &async);
    return r == KUI_CMD_OK ? kui_command_end(o, &async) : r;
}

const char *kui_command_name(enum kui_command_result result) {
    switch(result) {
        case KUI_CMD_OK: return "OK";
        case KUI_CMD_FAILED: return "FAILED";
        case KUI_CMD_TIMEOUT: return "TIMEOUT";
        case KUI_CMD_CANCELLED: return "CANCELLED";
        case KUI_CMD_RECOVERY_FAILED: return "ABORT FAILED: RESET REQUIRED";
        default: return "INVALID COMMAND";
    }
}
