/* SPDX-License-Identifier: GPL-3.0-only */
/* Pilot-only protocol adapter. Called on the serialized GD stack. Sound
 * worker/SDK work remains in its normal hook. The protected low base owns
 * request validation and guest maps; the opt-in high reader owns data work. */
#include "kui/toy_pilot_gd.h"
#include "kui/toy_pilot_gd_status.h"
#include "kui/toy_loader_trace.h"
#include <stddef.h>
#if KUI_TOY_PILOT_SHARED_SCI
#include "toy_pilot_sci.h"
#endif

extern uint32_t kui_toy_pilot_request(uint32_t,uint32_t,uint32_t,uint32_t);
extern const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void);
typedef int32_t (*base_fn)(uint32_t,uint32_t,uint32_t,uint32_t);

/* Terminal evidence only. CHECK breadcrumbs retain the guest arguments and
 * the protected base's exact result; capture never acknowledges a handle. */
volatile uint32_t kui_toy_pilot_native_diagnostics[8];
static struct kui_retail_gd *diagnostic_service;

void kui_toy_pilot_gd_capture(uint32_t owner,uint32_t firstworktoken,uint32_t flags) {
    struct kui_retail_gd *s=diagnostic_service;
    kui_toy_pilot_native_diagnostics[0]=owner;
    kui_toy_pilot_native_diagnostics[1]=firstworktoken;
    kui_toy_pilot_native_diagnostics[2]=s?s->command:UINT32_MAX;
    kui_toy_pilot_native_diagnostics[3]=s?s->token:UINT32_MAX;
    if(s) flags|=(s->pending?1u:0u)|(s->executing?2u:0u)|
        ((uint32_t)(uint8_t)s->status<<8)|((s->error&255u)<<16);
    kui_toy_pilot_native_diagnostics[4]=flags;
}

#if KUI_TOY_PILOT_LOADER_TRACE
static int32_t __attribute__((noinline)) dispatch_body(struct kui_retail_gd *s,
#else
int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *s,
#endif
    uint32_t r4,uint32_t r5,uint32_t r7,uintptr_t base_address) {
    diagnostic_service=s;
    base_fn base=(base_fn)base_address;
#if KUI_TOY_PILOT_SHARED_SCI
    /* Revoke the old destination before the low service changes its token
     * or drops pending. A data ABORT cannot revoke the audio mailbox. */
    if(r7==KUI_GD_INIT || r7==KUI_GD_RESET ||
       (r7==KUI_GD_ABORT && r4 && r4==s->token &&
        kui_toy_pilot_gd_data_pending(s)))
        kui_toy_pilot_sci_cancel(s);
#if KUI_TOY_PILOT_ASYNC_CDDA
    /* The raw receiver can be pending while there is no data handle.
     * Scalar and audio CHECK/EXEC calls therefore give the same bounded
     * engine service opportunity. Projection and handle acknowledgement
     * remain owned by their existing protocol paths below. */
    if(r7==KUI_GD_EXEC || r7==KUI_GD_CHECK)
        (void)kui_toy_pilot_sci_service(s);
#endif
    /* The low CHECK wrapper can synthesize a synchronous paced EXEC. Route
     * both calls, including a terminal CHECK, directly through the high
     * async core. Its EXEC never invokes the low ops.read callback. */
    if(kui_toy_pilot_gd_data_owned(s) &&
       (r7==KUI_GD_EXEC || r7==KUI_GD_CHECK)) {
#if !KUI_TOY_PILOT_ASYNC_CDDA
        (void)kui_toy_pilot_sci_service(s);
#endif
        int32_t result=kui_retail_gd_dispatch(s,r4,r5,0,r7);
        if(r7==KUI_GD_CHECK) {
            kui_toy_pilot_native_diagnostics[5]=r4;
            kui_toy_pilot_native_diagnostics[6]=r5;
            kui_toy_pilot_native_diagnostics[7]=(uint32_t)result;
        }
        return result;
    }
#endif
    if(r7==KUI_GD_INIT || r7==KUI_GD_RESET)
        (void)kui_toy_pilot_request(KUI_TOY_PILOT_RESET,0,0,0);
    /* An abort of our outstanding audio handle must revoke the hardware
     * mailbox too. Do not revoke for an unrelated or already consumed token. */
    if(r7==KUI_GD_ABORT && r4 && r4==s->token && kui_toy_pilot_gd_audio_pending(s))
        (void)kui_toy_pilot_request(KUI_GD_STOP,0,0,0);
    const struct kui_toy_pilot_snapshot *p=kui_toy_pilot_snapshot();
    if(r7==KUI_GD_EXEC && kui_toy_pilot_gd_audio_pending(s)) {
        uint32_t error=!p || p->fault?KUI_GD_ERROR_UNAVAILABLE:
            p->generation!=s->count?KUI_GD_ERROR_CANCELLED:0u;
        if(error) { s->pending=0;s->error=error;s->status=KUI_GD_FAILED; }
        else (void)base(0,0,0,KUI_GD_EXEC);
        (void)kui_toy_pilot_gd_project(s,p);
        return 0;
    }
    /* Project immediately before responses; the game can query between
     * finite-bank service visits. Data reads keep their own position/state. */
    /* All protocol status bytes are nonzero. Zero means this call did not
     * execute a pending scalar response; its terminal output stays intact. */
    uint32_t audio=0u;
    if(r7==KUI_GD_DRIVE || (r7==KUI_GD_EXEC && s->pending &&
       (s->command==KUI_RETAIL_GD_REQ_STAT || s->command==KUI_RETAIL_GD_GETSCD)))
        audio=kui_toy_pilot_gd_project(s,p);
    int32_t result=base(r4,r5,0,r7);
#if KUI_TOY_PILOT_SHARED_SCI
    /* Submission retains the low core's complete source/destination checks.
     * Only its accepted positive handle may start background data work. */
    if(r7==KUI_GD_REQUEST && result>0 && kui_toy_pilot_gd_data_pending(s))
        (void)kui_toy_pilot_sci_pump(s);
#endif
    if(r7==KUI_GD_CHECK) {
        kui_toy_pilot_native_diagnostics[5]=r4;
        kui_toy_pilot_native_diagnostics[6]=r5;
        kui_toy_pilot_native_diagnostics[7]=(uint32_t)result;
    }
    if(audio && r7==KUI_GD_EXEC && s->command==KUI_RETAIL_GD_GETSCD &&
       s->status==KUI_GD_COMPLETED && !s->error && s->completed_bytes>=2u) {
        /* The ordinary encoder has checked the complete destination. Reuse
         * its map, retaining alias/range/protected-owner checks for this
         * two-byte status update. No direct guest-pointer conversion. Patch
         * only the response completed by this EXEC; later server visits
         * leave the terminal handle's result intact until CHECK consumes it. */
        uint32_t address=(s->destination&0x00ffffffu)|0x8c000000u;
        uint8_t *out=s->ops.map(s->ops.context,address,2u,1);
        if(out) out[1]=(uint8_t)audio;
    }
    if(r7==KUI_GD_REQUEST && result>0 &&
       (r4==KUI_RETAIL_GD_PLAY || r4==KUI_RETAIL_GD_PLAY2 ||
        r4==KUI_RETAIL_GD_PAUSE || r4==KUI_RETAIL_GD_RELEASE ||
        r4==KUI_GD_STOP || r4==KUI_GD_COMMAND_INIT)) {
        uint32_t command=r4==KUI_GD_COMMAND_INIT?KUI_TOY_PILOT_RESET:r4;
        s->count=kui_toy_pilot_request(command,s->outputs[0],s->outputs[1],s->outputs[2]);
        if(!s->count) {
            s->error=KUI_GD_ERROR_UNAVAILABLE;s->status=KUI_GD_FAILED;s->pending=0;
        }
    }
    return result;
}

#if KUI_TOY_PILOT_LOADER_TRACE
int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *s,
    uint32_t r4,uint32_t r5,uint32_t r7,uintptr_t base_address) {
    kui_toy_loader_trace_begin(s,r7,r4,r5);
    int32_t result=dispatch_body(s,r4,r5,r7,base_address);
    kui_toy_loader_trace_end(s,r7,r4,r5,result);
    return result;
}
#endif
