
    kui_storage_boot_begin();
#ifdef KUI_SD_RUNTIME
    if(kui_update_bank_capability[8]!=1u || kui_update_engine_capability[8]!=KUI_UPDATE_ENGINE_CURRENT) return 1;
#endif
    ui_thread = thd_get_current();
    vid_set_mode(DM_640x480 | DM_MULTIBUFFER, PM_RGB565);
#ifdef KUI_SD_RUNTIME
    /* Mode setup resets the previous framebuffer. Repaint the existing
     * immutable RAM artwork before clock/setup logging or storage preflight;
     * the normal splash/chime lifecycle still starts below. */
    kui_splash_draw(vram_s);
    vid_waitvbl();vid_flip(-1);
#endif
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
    update_boot_start();
    kui_system_settings_default(&system_current);system_pending=system_current;
    kui_music_init(kui_log);kui_disc_identity_init(&disc_identity);
    kui_disc_identity_park(&disc_identity);disc_snapshot=disc_identity;
    safe_video_boot=(controller_buttons()&CONT_Y)!=0;
    kui_settings_default(&settings_current);
    settings_pending = settings_current;
    kui_shell_init(&shell, &settings_current);
    kui_shell_set_system_preferences(&shell,&system_current);
    kui_destination_default(destination_current);
    snprintf(settings_note,sizeof(settings_note),"Loading preferences from storage...");
    pending = 27; busy = true; splash_active=true;
    splash_deadline=timer_ms_gettime64()+3000;
    kui_log("K-UI launcher: Disc Ripper, VMU, Memory, Network, Settings, Diagnostics, GD Play and Music.");
    kui_log("B skips the original startup splash/chime. Home Y cycles menu music volume/off.");
    kui_log("Ripper: A new dump (confirm), X resume latest matching disc, Y verify.");
    kui_log("B returns home while idle; during work it stops and checkpoints.");
    kui_log("New dumps use game-named folders in /Games; duplicates get a number.");
    kui_log("Home/Ripper L/R: previous/next song. Ripper Start: Advanced, destination and settings.");
    kui_log("Newly inserted GD-ROM titles appear while idle; the drive stays ready for 45 seconds.");
    kui_log("The boot disc stays stopped; disc apps start the drive when requested.");
    kui_log("System Settings: video, memory display and menu music. Hold Y on runtime start for safe video.");
    kui_log("Reports say if a dump matches Redump/TOSEC (copy data/known-dumps/*.db to KUI/).");
    kui_log("Capture defaults: CRC32, no automatic readback. Y Verify rereads saved files.");
    kui_log("Saved preferences apply first; explicit /KUI/bench.cfg keys override them.");
    kui_log("Capture/Resume/Verify auto-save a report after ending. Wait for READY.");
    kui_log("Diagnostics R trigger: isolated benchmarks from /KUI/bench.cfg.");
    kui_log("The screen redraws 2x a second while working, which frees CPU (ui_hz=full: off).");
#else
    kui_log("Configured benchmarks use /KUI/bench.cfg; B stops safely.");
    kui_log("Bench SD sections write temporary test files; results auto-save to SD.");
    kui_log("Full capture is available in the updated SD runtime.");
#endif
    /* Code, fonts and assets are already in RAM. Show the boot screen before
     * the bounded STOP; no worker or app can own the drive at this point. Do
     * not read the boot disc merely to populate the optional idle title. */
#ifdef KUI_SD_RUNTIME
    draw_shell();
#else
    draw_boot();
#endif
    (void)kui_disc_stop();
    kui_vmu_lcd_startup();
    /* A slow STOP must not consume the interval in which input can cancel
     * automatic startup, nor expire the chime before its worker starts. */
#ifdef KUI_SD_RUNTIME
    splash_deadline=timer_ms_gettime64()+3000;
#else
    kui_boot_ui_init(&boot_ui,timer_ms_gettime64());
#endif
    kthread_attr_t attrs = {.stack_size = KUI_IO_WORKER_STACK_BYTES, .label = "kui-io"};
    kthread_t *io_worker=thd_create_ex(&attrs,worker,NULL);
