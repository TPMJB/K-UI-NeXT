/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/options.h"
#include <string.h>

/* The only FatFs-touching part of options. Separate from options.c so the
 * parser can be unit-tested on the PC without linking a filesystem. */
static char text[KUI_OPT_FILE_MAX];

bool kui_options_overlay(struct kui_options *opt,const char *path,kui_log_fn log) {
    struct kui_options parsed=*opt;
    FIL file;
    FRESULT r=f_open(&file,path,FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH) {
        log("No %s; keeping current options",path+2);
        return true;
    }
    if(r!=FR_OK) {log("Cannot open %s: FatFs=%u",path+2,(unsigned)r);return false;}
    bool ok=false;
    if(f_size(&file)>sizeof(text)) {
        log("%s is larger than %u bytes; ignored. Copy one file from docs/bench-cfgs/, "
            "not the whole commented example",path+2,(unsigned)sizeof(text));
    } else {
        UINT done=0;
        r=f_read(&file,text,(UINT)f_size(&file),&done);
        if(r!=FR_OK || done!=f_size(&file))
            log("Cannot read %s: FatFs=%u",path+2,(unsigned)r);
        else ok=kui_options_parse(&parsed,text,done,log);
    }
    r=f_close(&file);
    if(r!=FR_OK) {log("Cannot close %s: FatFs=%u",path+2,(unsigned)r);ok=false;}
    if(ok) {*opt=parsed;log("Loaded %s (explicit keys override saved settings)",path+2);}
    return ok;
}
bool kui_options_load(struct kui_options *opt,const char *path,kui_log_fn log) {
    kui_options_default(opt);
    return kui_options_overlay(opt,path,log);
}
