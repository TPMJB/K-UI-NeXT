
    kui_storage_boot_begin();
#ifdef KUI_SD_RUNTIME
    if(kui_update_bank_capability[8]!=1u || kui_update_engine_capability[8]!=KUI_UPDATE_ENGINE_CURRENT) return 1;
#endif
    ui_thread = thd_get_current();
    vid_set_mode(DM_640x480 | DM_MULTIBUFFER, PM_RGB565);
    kui_log("Running " KUI_RELEASE_SHORT " " KUI_ROLE " build " KUI_BUILD_ID);
    kui_clock_start(kui_log);
    kui_log("Video: %ux%u %s %s, buffered",
        (unsigned)vid_mode->width, (unsigned)vid_mode->height,
        vid_mode->cable_type == CT_VGA ? "VGA" :
            (vid_mode->flags & VID_PAL ? "PAL" : "NTSC"),
        vid_mode->flags & VID_INTERLACE ? "interlaced" : "progressive");
#ifndef KUI_SD_RUNTIME
    kui_boot_ui_init(&boot_ui,timer_ms_gettime64());
    kui_log("CD menu: B stays here; X opens recovery; startup boot begins after 3 seconds.");
    kui_log(kui_storage_boot_from_card()?"Card boot: automatic loader override bypassed.":
        "CD autoboot: optional /KUI/boot.kui first; Start K-UI bypasses it.");
    kui_log("Choose Auto, SCIF, SCI or IDE/CF. A retries after inserting an SD card.");
    kui_log("Boot images are read-only. Built-in diagnostics label and confirm writes.");
#endif
    kui_log("Diagnostic code and fonts are loaded entirely in RAM.");
    kui_log("Replace boot CD with a known-good retail GD-ROM; close lid.");
#ifdef KUI_SD_RUNTIME
    kui_log("Diagnostics page: A disc samples; X SD test; Y save log.");
#else
    kui_log("Open Diagnostics for disc samples, storage write/read checks, or log saving.");
#endif
    kui_log("Use a spare test card for write tests. No formatting; existing files preserved.");
#ifdef KUI_SD_RUNTIME
    kui_update_boot_require_engine(KUI_UPDATE_ENGINE_CURRENT);
