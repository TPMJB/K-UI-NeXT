/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/options.h"
#include <string.h>

/* The only FatFs-touching part of options. Separate from options.c so the
 * parser can be unit-tested on the PC without linking a filesystem. */

static char text[KUI_OPT_FILE_MAX];

bool kui_options_load(struct kui_options *opt, const char *path, kui_log_fn log) {
    kui_options_default(opt);
    FIL file;
    FRESULT r = f_open(&file, path, FA_READ);
    if(r == FR_NO_FILE || r == FR_NO_PATH) {
        log("No %s; using default options", path + 2);
        return true;
    }
    if(r != FR_OK) { log("Cannot open %s: FatFs=%u", path + 2, (unsigned)r); return false; }
    bool ok = false;
    if(f_size(&file) > sizeof(text)) {
        log("%s is larger than %u bytes; ignored", path + 2, (unsigned)sizeof(text));
    } else {
        UINT done = 0;
        r = f_read(&file, text, (UINT)f_size(&file), &done);
        if(r != FR_OK || done != f_size(&file))
            log("Cannot read %s: FatFs=%u", path + 2, (unsigned)r);
        else if(kui_options_parse(opt, text, done, log)) {
            log("Loaded %s", path + 2);
            ok = true;
        }
    }
    f_close(&file);
    if(!ok) kui_options_default(opt);
    return ok;
}
