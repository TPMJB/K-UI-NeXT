/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/core.h"

enum kui_command_result kui_command(const struct kui_command_ops *o,
    int command, void *params, uint32_t timeout_ms, uint32_t abort_ms) {
    if(!o || !o->now_ms || !o->yield || !o->cancelled || !o->submit ||
       !o->poll || !o->abort || !timeout_ms || !abort_ms)
        return KUI_CMD_INVALID;

    uint64_t start = o->now_ms(o->ctx);
    int handle = 0;
    /* Submission and execution share one deadline, including busy retries. */
    while(handle == 0) {
        if(o->cancelled(o->ctx)) return KUI_CMD_CANCELLED;
        if(o->now_ms(o->ctx) - start >= timeout_ms) return KUI_CMD_TIMEOUT;
        handle = o->submit(o->ctx, command, params);
        if(handle < 0) return KUI_CMD_FAILED;
        if(handle == 0) o->yield(o->ctx);
    }

    enum kui_command_result result;
    for(;;) {
        if(o->cancelled(o->ctx)) { result = KUI_CMD_CANCELLED; break; }
        if(o->now_ms(o->ctx) - start >= timeout_ms) {
            result = KUI_CMD_TIMEOUT; break;
        }
        int status = o->poll(o->ctx, handle);
        if(status == 2) return KUI_CMD_OK;
        if(status == -1 || status == 0) return KUI_CMD_FAILED;
        if(status != 1 && status != 4) {
            /* Unexpected streaming/unknown status may still own the buffer. */
            result = KUI_CMD_FAILED; break;
        }
        o->yield(o->ctx);
    }

    o->abort(o->ctx, handle);
    start = o->now_ms(o->ctx);
    while(o->now_ms(o->ctx) - start < abort_ms) {
        int status = o->poll(o->ctx, handle);
        if(status == 0 || status == 2 || status == -1) return result;
        o->yield(o->ctx);
    }
    return KUI_CMD_RECOVERY_FAILED;
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
