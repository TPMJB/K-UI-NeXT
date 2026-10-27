/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_storage.h"
#include "platform.h"

/* The mounted work area must outlive each job while the session is open. */
static FATFS fs;
static bool open_session,held;
static unsigned jobs;

enum kui_games_storage_result kui_games_storage_open(kui_log_fn log) {
    /* FatFs clears fs_type when anything else mounts or unmounts "0:", or
     * when a remount after a media error fails. Every other job disconnects
     * after its unmount, so a cleared session starts again from a new connect. */
    if(open_session && fs.fs_type) return KUI_GAMES_STORAGE_OK;
    open_session=false;
    if(!kui_sd_connect()) return KUI_GAMES_STORAGE_NO_CARD;
    if(!kui_mount(&fs,log)) {kui_sd_disconnect();return KUI_GAMES_STORAGE_NO_MOUNT;}
    open_session=true;
    return KUI_GAMES_STORAGE_OK;
}
bool kui_games_storage_release(void) {
    if(!open_session) return true;
    open_session=false;
    bool ok=f_mount(NULL,"0:",0)==FR_OK;
    kui_sd_disconnect();
    return ok;
}
bool kui_games_storage_done(bool keep) {
    if(!open_session) return true;
    ++jobs;
    return held && keep?true:kui_games_storage_release();
}
void kui_games_storage_hold(bool hold) {held=hold;}
bool kui_games_storage_active(void) {return open_session;}
unsigned kui_games_storage_jobs(void) {return jobs;}
