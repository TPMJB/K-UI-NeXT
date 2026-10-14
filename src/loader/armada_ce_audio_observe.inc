/* SPDX-License-Identifier: GPL-3.0-only */
/* Included only by the separate CE SCI-background diagnostic resident. */
#if KUI_ARMADA_CE_EXCERPT
#include "kui/armada_ce_excerpt_entry.h"
static struct kui_armada_ce_excerpt_request armada_ce_excerpt_request;
static void armada_ce_audio_record(uint32_t function,uint32_t command,
                                    int32_t result) {
#if KUI_ARMADA_CE_STREAM
    /* Reset revokes source tickets as well as the emulated GD handle. Stop
     * is a scalar mailbox command; the native worker quiesces its own lease. */
    if((function==KUI_GD_INIT || function==KUI_GD_RESET) && result==0) {
        function=KUI_GD_REQUEST;command=KUI_GD_STOP;result=1;
    } else if(function==KUI_GD_REQUEST && command==KUI_GD_COMMAND_INIT && result>0) {
        command=KUI_GD_STOP;
    }
#endif
    /* outputs belongs to the positively accepted request, never to a
     * rejected caller. Keep the existing data-delivery callback first. */
    if(function!=KUI_GD_REQUEST || result<=0 ||
       (command!=KUI_RETAIL_GD_PLAY && command!=KUI_RETAIL_GD_PLAY2 &&
        command!=KUI_RETAIL_GD_PAUSE && command!=KUI_RETAIL_GD_RELEASE && command!=KUI_GD_STOP)) return;
    armada_ce_excerpt_request.first=service.outputs[0];
    armada_ce_excerpt_request.last=service.outputs[1];
    armada_ce_excerpt_request.repeat=service.outputs[2];
    armada_ce_excerpt_request.deferred_available=kui_retail_ce_deferred[0]==0u;
    const struct kui_armada_ce_excerpt_entry_snapshot *s=kui_armada_ce_bind_probe(
        function,command,result,(uint32_t)(uintptr_t)&armada_ce_excerpt_request);
    if(s->deferred && !kui_retail_ce_deferred[0]) {
        kui_retail_ce_deferred[1]=0;
        __asm__ __volatile__("" : : : "memory");
        kui_retail_ce_deferred[0]=s->deferred;
    }
}
#if KUI_ARMADA_CE_STREAM
static void armada_ce_audio_kick(uint32_t function) {
    if(kui_retail_ce_deferred[0] || (function!=KUI_GD_EXEC && function!=KUI_GD_CHECK)) return;
    const struct kui_armada_ce_excerpt_entry_snapshot *s=kui_armada_ce_bind_probe(
        function,0,0,0);
    if(s->deferred) {
        kui_retail_ce_deferred[1]=0;
        __asm__ __volatile__("" : : : "memory");
        kui_retail_ce_deferred[0]=s->deferred;
    }
}
#endif
static void armada_ce_audio_report(void) {
    const struct kui_armada_ce_excerpt_entry_snapshot *s=kui_armada_ce_bind_probe(UINT32_MAX,0,0,0);
#if KUI_ARMADA_CE_STREAM
    if(!s->report_labels[0]) return;
    /* Text lives in the existing high controller, keeping the low reader
     * within its unchanged service-stack reservation. */
    static const uint8_t order[6]={0u,5u,1u,2u,3u,4u};
    for(unsigned i=0;i<6u;i++)
        retail_display_values((const char *)(uintptr_t)s->report_labels[i],
            (const uint32_t *)(const void *)s+order[i]*5u,5);
#else
    retail_display_values("GD BIND READ QUEUE PIO SKIP",s->binding,5);
    retail_display_values("BIND WHY NODE VALUE EXPECT DETAIL",s->fault,5);
    retail_display_values("SR FPSCR CURRENT OWNER DEVICE",s->context,5);
    retail_display_values("EXCERPT ATTEMPT PLAY REFUSED HR",s->playback,5);
    retail_display_values("BUFFER CAPS BYTES VOICES CRC",s->buffer,5);
    retail_display_values("CMD FIRST LAST PLAYCUR WRITECUR",s->cursor,5);
#endif
}
#if KUI_ARMADA_CE_STREAM
static void armada_ce_audio_history(unsigned samples) {
    /* Terminal execution has stopped: copy authored history only, without
     * sampling guest memory, clocks or native sound methods. */
    const struct kui_armada_ce_excerpt_entry_snapshot *s=kui_armada_ce_bind_probe(UINT32_MAX,0,0,0);
    if(!s->report_labels[7]) return;
    retail_display_line((const char *)(uintptr_t)s->report_labels[samples?8u:7u]);
    retail_display_values((const char *)(uintptr_t)s->report_labels[6],s->trace.counts,5);
    for(unsigned i=0;i<KUI_ARMADA_CE_STREAM_TRACE_ROWS;i++)
        retail_display_values((const char *)(uintptr_t)s->report_labels[9],
            samples?s->trace.samples[i]:s->trace.events[i],5);
}
#endif
#elif KUI_ARMADA_CE_RAM_BIND
#include "kui/armada_ce_bind_probe.h"
static void armada_ce_audio_record(uint32_t function, uint32_t command,
                                    int32_t result) {
    if(function == KUI_GD_REQUEST && result > 0 &&
       (command == KUI_RETAIL_GD_PLAY || command == KUI_RETAIL_GD_PLAY2)) {
        (void)kui_armada_ce_bind_probe(function,command,result,1u);
    }
}
static void armada_ce_audio_report(void) {
    const struct kui_armada_ce_bind_snapshot *s=kui_armada_ce_bind_probe(0u,0u,0,0u);
    retail_display_values("TTB PTEH PLAYS CCR MASK LO",s->cpu,5);
    retail_display_values("N SLOT BLOCK AT BLOCK ST MASK HI",s->nominal,5);
    retail_display_values("R SLOT BLOCK AT BLOCK ST NEG",s->real,5);
    retail_display_values("PICK VA SLOT BLOCK AT BLOCK ST",s->candidate,5);
}
#else
static struct kui_armada_ce_audio_observation armada_ce_audio;

static void armada_ce_audio_record(uint32_t function, uint32_t command,
                                    int32_t result) {
    kui_armada_ce_audio_observe(&armada_ce_audio, function, command, result,
                               &service);
}

static void armada_ce_audio_report(void) {
    /* Static output preserves the existing small CE hook-stack budget.
     * The terminal return has stopped all game execution. No guest pointer,
     * audio register, card read, clock sample, or queue write is involved. */
    static uint32_t gd[5];
    retail_display_values("PLAY PLAY2 PAUSE RELEASE STOP", armada_ce_audio.accepted, 5);
    gd[0] = armada_ce_audio.rejected;
    gd[1] = armada_ce_audio.status_requests;
    gd[2] = armada_ce_audio.drive_calls;
    gd[3] = armada_ce_audio.last_play_command;
    retail_display_values("REJECTED STAT DRIVE PLAYCMD", gd, 4);
    /* Controls issued before terminal return never erase the last PLAY.
     * All three parameters came from an accepted, validated request. */
    retail_display_values("START END REPEAT", armada_ce_audio.play_parameters, 3);
    gd[0] = service.diag.last_function;
    gd[1] = service.diag.last_command;
    gd[2] = (uint32_t)service.diag.last_result;
    gd[3] = service.diag.last_error;
    gd[4] = service.pending;
    retail_display_values("FN CMD RESULT ERROR PENDING", gd, 5);
}
#endif
