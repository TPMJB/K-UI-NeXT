/* SPDX-License-Identifier: GPL-3.0-only */
/* A completed foreground command must not retain the prior executable's
 * serial controls or a paused producer. The completed GD handle/status stay
 * intact; the next request opens the card against its new caller's controls. */
static void native_foreground_complete(void) {
    struct kui_retail_gd *s=&R.shared.service;
    if((s->pending
#if KUI_NATIVE_GD_DMA_STREAM
        && !(NATIVE_BYTE_STREAM(s->command) && !s->outputs[0])
#endif
       ) || s->executing || e.active)return;
    if(e.in_irq || e.release.depth || R.shared.port.fault ||
       e.opened!=(uint32_t)R.shared.port.acquired)goto fault;
    unhook();
    if((e.opened && kui_sci_stream_stop()!=KUI_SCI_STREAM_OK) ||
       kui_sci_stream_busy() || R.shared.stream.state || e.hooked ||
       e.release.armed || R.shared.port.fault)goto fault;
    kui_sci_stream_discard();
    if(e.opened)kui_sci_sd_release();
    if(R.shared.port.acquired || R.shared.port.fault)goto fault;
    e.opened=0u;
    return;
fault:
    R.shared.port.fault=true;
    e.active=0u;e.failed=KUI_GD_ERROR_IO;
    s->error=KUI_GD_ERROR_IO;s->diag.last_error=KUI_GD_ERROR_IO;
    s->status=KUI_GD_FAILED;s->pending=0u;
}
