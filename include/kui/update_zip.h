/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_UPDATE_ZIP_H
#define KUI_UPDATE_ZIP_H
#include "kui/update.h"
#define KUI_UPDATE_ZIP_PATH_BYTES 192u
#define KUI_UPDATE_ZIP_STAGE_PATH "0:/KUI/system/update-stage/current.kup"
/* Mounted/exclusive FatFs operation. Extracts exactly one embedded .kup from
 * a canonical Updates ZIP into an owned private cache. Never installs code or
 * changes activation. Caller must inspect/install the returned package using
 * the normal reviewed pin. Re-run for INSTALL to detect changed ZIP contents.
 * Stored/deflate ZIP32 only; bounded directory, input and wrapping dictionary.
 * All failures leave out empty and try to remove this operation's partial. */
enum kui_update_result kui_update_zip_prepare(const char *path,
    char out[KUI_UPDATE_ZIP_PATH_BYTES],struct kui_app_status *,kui_log_fn,
    kui_cancel_fn,kui_app_progress_fn);
#endif
