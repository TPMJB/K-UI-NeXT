/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored publication regression; no native or retail code executes. */
#define KUI_WINCE_CDDA_WWP 1
#define KUI_ARMADA_CE_GENERIC_APPLICATION 1
#define KUI_ARMADA_CE_STREAM 1
#define KUI_RETAIL_CE 1
#define KUI_ARMADA_CE_STREAM_PLAYER_TEST 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/loader/armada_ce_stream_player.c"

int main(void) {
    memset(&player,0,sizeof(player));kui_wwp_cdda_drive_status=0u;
    (void)kui_armada_ce_excerpt_queue(NULL,33u,0u,0u,0u);
    assert(!kui_wwp_cdda_drive_status);
    (void)kui_armada_ce_excerpt_queue(NULL,20u,4u,4u,15u);
    assert(!kui_wwp_cdda_drive_status); /* Accepted intent alone is insufficient. */
    player.applied_epoch=player.mail.epoch;
    player.model.state=KUI_ARMADA_CE_STREAM_PREFILL;
    status_model();assert(!kui_wwp_cdda_drive_status);
    player.model.state=KUI_ARMADA_CE_STREAM_FAULT;player.disabled=1u;
    status_model();assert(!kui_wwp_cdda_drive_status); /* Failed first start. */
    player.disabled=0u;
    player.model.state=KUI_ARMADA_CE_STREAM_PLAYING;
    player.model.native_running=true;
    status_model();assert(kui_wwp_cdda_drive_status==3u);
    (void)kui_armada_ce_excerpt_queue(NULL,22u,0u,0u,0u);
    assert(kui_wwp_cdda_drive_status==1u);
    status_model();assert(kui_wwp_cdda_drive_status==1u); /* Exclude stale step. */
    player.applied_epoch=player.mail.epoch;
    player.model.state=KUI_ARMADA_CE_STREAM_PAUSED;
    status_model();assert(kui_wwp_cdda_drive_status==1u);
    (void)kui_armada_ce_excerpt_queue(NULL,23u,0u,0u,0u);
    assert(kui_wwp_cdda_drive_status==3u);
    player.applied_epoch=player.mail.epoch;
    player.model.state=KUI_ARMADA_CE_STREAM_QUIESCING;
    player.model.after_stop=KUI_ARMADA_CE_STREAM_PREFILL;
    status_model();assert(kui_wwp_cdda_drive_status==3u);
    player.model.native_running=false;player.model.state=KUI_ARMADA_CE_STREAM_ENDED;
    status_model();assert(kui_wwp_cdda_drive_status==2u);
    player.disabled=1u;status_model();assert(kui_wwp_cdda_drive_status==9u);
    player.disabled=0u;
    (void)kui_armada_ce_excerpt_queue(NULL,33u,0u,0u,0u);
    assert(!kui_wwp_cdda_drive_status);
    player.applied_epoch=player.mail.epoch;status_model();assert(!kui_wwp_cdda_drive_status);
    (void)kui_armada_ce_excerpt_queue(NULL,23u,0u,0u,0u);
    status_model();assert(!kui_wwp_cdda_drive_status);
    (void)kui_armada_ce_excerpt_queue(NULL,20u,4u,4u,15u);
    player.applied_epoch=player.mail.epoch;player.model.state=KUI_ARMADA_CE_STREAM_PREFILL;
    status_model();assert(!kui_wwp_cdda_drive_status);
    puts("PASS WWP published drive state: cold boot/failed start untouched, native start, pause/resume, stale step, end, fault and stop");
    return 0;
}
