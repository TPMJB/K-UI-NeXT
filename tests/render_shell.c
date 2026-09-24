/* SPDX-License-Identifier: GPL-3.0-only */
/* Host preview uses the same embedded artwork/font and renderer as hardware.
 * Modes: home, ripper, confirm, settings, diagnostics, complete, partial,
 * stopped, destination, keyboard, advanced, ripper-settings, video, vmu,
 * memory, network, idle, reset, home-vmu, home-memory, home-network,
 * home-music, home-gd, music, gd-play, gd-confirm, quick-resume, dma-fallback,
 * music-queued, advanced-destination, clock, clock-confirm, defaults,
 * vmu-restore, vmu-restore-confirm, crc-scan, scan-folder, home-games,
 * games, games-detail, games-error, games-advanced, games-probe,
 * games-probe-loading, games-image-probe, games-image-probe-loading,
 * games-retail, games-retail-loading, games-retail-invalid. */
#include "kui/shell.h"
#include <stdio.h>
#include <string.h>

static uint16_t frame[640*480];
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    struct kui_settings preferences={true,false,true};
    struct kui_shell shell; kui_shell_init(&shell,&preferences);
    const char *logs[]={"SD exFAT, 249997312 sectors, cluster=131072 bytes",
        "Volume start=2048 (MBR)","Disc: MDK2", "Track 04: audio",
        "Checkpoint saved. Partial job preserved.","Capture result: stopped",
        "Report saved: /KUI/probes/p0012/diagnostics.txt"};
    struct kui_shell_view view={.build="a1b2c3d4e5f6",.phase=2,.track=4,.tracks=31,
        .rate_kib=1012,.done=421ull*1048576,.total=1133ull*1048576,
        .committed=421ull*1048576,.elapsed_ms=426000,
        .phase_elapsed_ms=426000,.progress_age_ms=100,
        .memory_valid=true,.memory_used=2800*1024,.memory_physical=16384*1024,
        .memory_peak=2816*1024,.log_lines=logs,.log_count=7,.total_log_lines=174,
        .job_dir="/Games/MDK2 (2)",.disc_title="MDK2",.inserted_title="MDK2",
        .gdi_name="MDK2.gdi",.music_title="Neon Circuit",.music_enabled=true,
        .music_playing=true,.music_volume=75};
    struct kui_app_status status={.complete=true,.passed=true};
    view.music_cache_bytes=4674286;
    if(!strcmp(argv[1],"home-games")) shell.home_selected=8;
    else if(!strcmp(argv[1],"games")) {
        shell.page=KUI_SHELL_GAMES;shell.games_listing.count=8;shell.games_listing.has_more=true;
        const char *names[]={"Fighting","Dead or Alive 2","Resident Evil - Code Veronica","MDK2",
            "Armada","Grandia II","Sword of the Berserk","A very long game name that clips safely at the right margin"};
        for(unsigned i=0;i<8;i++) snprintf(shell.games_listing.entries[i].name,sizeof(shell.games_listing.entries[i].name),"%s",names[i]);
        shell.games_listing.entries[0].directory=true;shell.games_selected=1;
        strcpy(shell.games_listing.message,"Choose a GDI image to inspect.");
    } else if(!strcmp(argv[1],"games-detail") || !strcmp(argv[1],"games-error")) {
        shell.page=KUI_SHELL_GAMES_DETAIL;
        strcpy(shell.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
        struct kui_games_detail *d=&shell.games_detail;
        strcpy(d->path,shell.games_selected_path);
        d->valid=strcmp(argv[1],"games-error")!=0;d->bytes=1185765648;d->tracks=3;d->data_tracks=2;d->audio_tracks=1;
        d->boot_bytes=123456;d->boot_lba=45166;
        strcpy(d->title,"DEAD OR ALIVE 2");strcpy(d->product,"T-3601N");strcpy(d->region,"JUE");
        strcpy(d->boot_file,"1ST_READ.BIN");strcpy(d->message,"Track file missing: track03.bin");
    } else if(!strcmp(argv[1],"games-advanced")) {
        shell.page=KUI_SHELL_GAMES_ADVANCED;shell.games_advanced_selected=2;
    } else if(!strcmp(argv[1],"games-probe") || !strcmp(argv[1],"games-probe-loading")) {
        shell.page=KUI_SHELL_GAMES_PROBE_CONFIRM;
        if(!strcmp(argv[1],"games-probe-loading")) {
            view.busy=true;view.app_status=&status;status.complete=false;
            strcpy(status.message,"Validating the probe package");
        }
    } else if(!strcmp(argv[1],"games-image-probe") || !strcmp(argv[1],"games-image-probe-loading")) {
        shell.page=KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM;
        strcpy(shell.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
        strcpy(shell.games_detail.path,shell.games_selected_path);shell.games_detail.valid=true;
        if(!strcmp(argv[1],"games-image-probe-loading")) {
            view.busy=true;view.app_status=&status;status.complete=false;
            strcpy(status.message,"Mapping selected image files");
        }
    }
    else if(!strcmp(argv[1],"games-retail") || !strcmp(argv[1],"games-retail-loading") ||
            !strcmp(argv[1],"games-retail-invalid")) {
        shell.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;
        strcpy(shell.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
        struct kui_games_detail *d=&shell.games_detail;
        strcpy(d->path,shell.games_selected_path);d->valid=true;d->tracks=3;
        strcpy(d->title,"DEAD OR ALIVE 2");strcpy(d->boot_file,"1ST_READ.BIN");
        d->boot_bytes=123456;d->boot_lba=45166;
        if(!strcmp(argv[1],"games-retail-loading")) {
            view.busy=true;view.app_status=&status;status.complete=false;
            strcpy(status.message,"Preparing experimental DOA2 launch...");
        }
        if(!strcmp(argv[1],"games-retail-invalid")) strcpy(d->title,"ARMADA");
    }
    else if(!strcmp(argv[1],"audio-cd")) {
        shell.page=KUI_SHELL_CD_AUDIO;shell.cd_audio.loaded=true;shell.cd_audio.count=12;
        shell.cd_audio.playing=true;shell.cd_audio.current=3;shell.cd_selected=2;
        view.music_title="Audio CD track 3";
        for(unsigned i=0;i<12;i++) {shell.cd_audio.tracks[i].number=i+1;shell.cd_audio.tracks[i].seconds=181+i*2;}
        strcpy(shell.cd_audio.message,"Playing track 3 from audio CD.");
    } else if(!strcmp(argv[1],"vmu-actions") || !strcmp(argv[1],"vmu-delete") || !strcmp(argv[1],"vmu-copy")) {
        shell.page=KUI_SHELL_VMU_ACTIONS;shell.vmu.present=true;shell.vmu.count=1;
        strcpy(shell.vmu.entries[0].name,"MDK2_SAVE");shell.vmu.entries[0].bytes=4096;
        shell.vmu_copy_slot=1;shell.confirm_vmu_delete=!strcmp(argv[1],"vmu-delete");
        shell.confirm_vmu_copy=!strcmp(argv[1],"vmu-copy");
    } else if(!strcmp(argv[1],"safe-area")) {
        shell.page=KUI_SHELL_SETTINGS;shell.system_selected=8;shell.system_draft.screen_inset=1;
    } else if(!strcmp(argv[1],"system-tools") || !strcmp(argv[1],"restart")) {
        shell.page=KUI_SHELL_SYSTEM_TOOLS;shell.confirm_restart=!strcmp(argv[1],"restart");
        view.app_status=&status;strcpy(status.message,"Settings flash backup verified on SD.");
        status.line_count=1;strcpy(status.lines[0],"/KUI/backups/system/flash-0001.bin");
    } else if(!strcmp(argv[1],"salvage") || !strcmp(argv[1],"salvage-confirm") || !strcmp(argv[1],"salvage-working")) {
        shell.page=KUI_SHELL_SALVAGE;shell.salvage_selected=3;shell.salvage_zero_fill=true;
        shell.confirm_salvage=!strcmp(argv[1],"salvage-confirm");
        view.app_status=&status;status.passed=false;strcpy(status.message,"Incomplete: 3 unresolved sectors remain.");
        if(!strcmp(argv[1],"salvage-working")) {
            view.busy=true;status.complete=false;status.line_count=6;status.done=5;status.total=12;
            strcpy(status.message,"Retrying unresolved sectors");
            const char *lines[]={"Track 3/31  FAD 45150  attempts 3","Targets 12  recovered 5  remaining 7",
                "Recovery pass 2/5  First pass: complete","/KUI/salvage/job-0001",
                "Unresolved zeros are NOT a verified game dump.","Separate recovery job; normal captures unchanged."};
            for(unsigned i=0;i<6;i++) snprintf(status.lines[i],KUI_APP_LINE_CAP,"%s",lines[i]);
        }
    } else if(!strcmp(argv[1],"retry")) {
        shell.page=KUI_SHELL_RIPPER;view.busy=true;view.retries=11;view.retry_attempt=3;view.retry_limit=10;view.retry_fad=45150;
    } else if(!strcmp(argv[1],"music-clear")) {
        shell.page=KUI_SHELL_MUSIC;shell.confirm_music_clear=true;
    } else if(!strcmp(argv[1],"clock") || !strcmp(argv[1],"clock-confirm")) {
        shell.page=KUI_SHELL_CLOCK;
        const struct kui_datetime d={2026,9,23,20,15,31};kui_shell_set_clock(&shell,&d,NULL);
        shell.clock_selected=2;shell.confirm_clock=!strcmp(argv[1],"clock-confirm");
    } else if(!strcmp(argv[1],"defaults")) {
        shell.page=KUI_SHELL_SETTINGS;shell.system_selected=7;shell.confirm_defaults=true;
    } else if(!strcmp(argv[1],"vmu-restore") || !strcmp(argv[1],"vmu-restore-confirm")) {
        shell.page=KUI_SHELL_VMU_RESTORE;shell.vmu_slot=5;shell.backups.total=12;shell.backups.count=8;
        for(unsigned i=0;i<8;i++) {
            snprintf(shell.backups.entries[i].name,16,"SAVE_%02u",i);
            snprintf(shell.backups.entries[i].folder,16,"v%04u",i+1);
        }
        shell.backup_selected=2;strcpy(shell.restore_name,"MDK2_SAVE");shell.restore_bytes=4096;
        strcpy(shell.backups.status.message,"Select a backup, then choose the target VMU.");
        shell.confirm_vmu_restore=!strcmp(argv[1],"vmu-restore-confirm");
    } else if(!strcmp(argv[1],"crc-scan")) {
        shell.page=KUI_SHELL_CRC_SCAN;strcpy(shell.browse_path,"/Games/ARMADA");
        view.app_status=&status;status.done=status.total=1187764704;status.line_count=7;
        strcpy(status.message,"Mode 1 checks passed; full catalogue match includes audio");
        strcpy(status.lines[0],"Track 5 / 5");
        strcpy(status.lines[1],"Data sectors 220386  Audio sectors 284616");
        strcpy(status.lines[2],"Bad sectors 0  Unsupported 0");
        strcpy(status.lines[3],"CRC mismatches 0  SHA mismatches 0");
        strcpy(status.lines[4],"TOSEC: FULL TRACK MATCH");
        strcpy(status.lines[5],"Armada v1.000 (1999)(Metro3D)(US)[!]");
        strcpy(status.lines[6],"No manifest; only FULL TRACK MATCH checks all audio too.");
    } else if(!strcmp(argv[1],"scan-folder")) {
        shell.page=KUI_SHELL_DESTINATION;shell.browse_for_scan=true;
        strcpy(shell.browse_path,"/Games/MDK2");
    } else if(!strcmp(argv[1],"home-music")) shell.home_selected=7;
    else if(!strcmp(argv[1],"home-gd")) shell.home_selected=6;
    else if(!strcmp(argv[1],"gd-play") || !strcmp(argv[1],"gd-confirm")) {
        shell.page=KUI_SHELL_GD_PLAY;shell.confirm_gd_boot=!strcmp(argv[1],"gd-confirm");
    } else if(!strcmp(argv[1],"quick-resume")) {
        shell.page=KUI_SHELL_ADVANCED;shell.advanced_selected=3;shell.confirm_quick_resume=true;
    } else if(!strcmp(argv[1],"music")) {
        shell.page=KUI_SHELL_MUSIC;shell.music_listing.count=8;shell.music_listing.has_more=true;
        const char *names[]={"Albums","Neon Circuit.wav","Orbital Drift.wav","Midnight Vector.wav",
            "Chrome Horizon.wav","Menu.wav","Long descriptive music filename that should clip safely.wav","Harbor Lights.ogg"};
        for(unsigned i=0;i<8;i++) snprintf(shell.music_listing.entries[i].name,
            sizeof(shell.music_listing.entries[i].name),"%s",names[i]);
        shell.music_listing.entries[0].directory=true;shell.music_selected=3;
        strcpy(shell.music_listing.message,"Choose a WAV or Ogg file to play.");
    } else if(!strcmp(argv[1],"settings")) {shell.page=KUI_SHELL_SETTINGS;shell.system_selected=2;}
    else if(!strcmp(argv[1],"ripper-settings")) shell.page=KUI_SHELL_RIPPER_SETTINGS;
    else if(!strcmp(argv[1],"video")) {
        shell.page=KUI_SHELL_SETTINGS;view.video_trial=true;view.video_seconds=7;
        shell.system_draft.video_mode=KUI_VIDEO_PAL50;
    } else if(!strcmp(argv[1],"vmu")) {
        shell.page=KUI_SHELL_VMU;shell.vmu.count=8;shell.vmu.total=12;
        shell.vmu.present=true;shell.vmu.free_blocks=62;shell.vmu_selected=2;
        const char *names[]={"SONIC2__S01","MDK2___SAVE","BIOHAZARD_CV","BERSERK_SYS",
            "CRAZY_TAXI","SHENMUE_000","SOULCALIBUR","JETSETRADIO"};
        for(unsigned i=0;i<8;i++) {
            snprintf(shell.vmu.entries[i].name,16,"%s",names[i]);
            shell.vmu.entries[i].bytes=16384;
        }
        snprintf(shell.vmu.status.message,128,"12 saves found. Select a save to back up.");
    } else if(!strcmp(argv[1],"memory")) {
        shell.page=KUI_SHELL_MEMORY;view.app_status=&status;
        snprintf(status.message,128,"Allocated region passed");
        status.done=status.total=22ull*1024*1024;status.line_count=6;
        const char *lines[]={"Owned test region: 4096 KiB","Read coverage: 23068672 bytes; passes 70/70",
            "Errors: 0; first mismatch stops the test","6 full pattern/address passes",
            "64 walking-bit passes at 4 KiB block edges","CPU accesses; other RAM and VRAM are not tested"};
        for(unsigned i=0;i<6;i++) snprintf(status.lines[i],80,"%s",lines[i]);
    } else if(!strcmp(argv[1],"network")) {
        shell.page=KUI_SHELL_NETWORK;view.app_status=&status;status.passed=false;
        snprintf(status.message,128,"No Ethernet adapter detected");status.line_count=4;
        const char *lines[]={"Broadband and LAN adapters were checked.","No DHCP request or traffic sent.",
            "No reachable network is asserted.","Connect an adapter and inspect again."};
        for(unsigned i=0;i<4;i++) snprintf(status.lines[i],80,"%s",lines[i]);
    } else if(!strcmp(argv[1],"home-vmu")) shell.home_selected=1;
    else if(!strcmp(argv[1],"home-memory")) shell.home_selected=2;
    else if(!strcmp(argv[1],"home-network")) shell.home_selected=3;
    else if(!strcmp(argv[1],"diagnostics")) shell.page=KUI_SHELL_DIAGNOSTICS;
    else if(!strcmp(argv[1],"advanced") || !strcmp(argv[1],"advanced-destination")) {
        shell.page=KUI_SHELL_ADVANCED;
        if(!strcmp(argv[1],"advanced-destination")) shell.advanced_selected=4;
    }
    else if(!strcmp(argv[1],"destination")) {
        shell.page=KUI_SHELL_DESTINATION;
        const char *folders[]={"Action","Adventure","Driving","Fighting","Imports",
            "Puzzle","Role-playing","Sports"};
        shell.listing.count=8; shell.listing.has_more=true; shell.browser_selected=3;
        for(unsigned i=0;i<8;i++) snprintf(shell.listing.entries[i].name,
            sizeof(shell.listing.entries[i].name),"%s",folders[i]);
    } else if(!strcmp(argv[1],"keyboard")) {
        shell.page=KUI_SHELL_KEYBOARD; shell.keyboard_selected=42;
        snprintf(shell.keyboard,sizeof(shell.keyboard),"/Games/Fighting");
    }
    else if(strcmp(argv[1],"home")) {
        shell.page=KUI_SHELL_RIPPER;
        if(!strcmp(argv[1],"confirm")) shell.confirm_new=true;
        else if(!strcmp(argv[1],"complete") || !strcmp(argv[1],"partial")) {
            view.phase=4; view.outcome=KUI_SHELL_OUTCOME_COMPLETE;
            view.done=view.total; view.committed=view.total;
            view.track=view.tracks; view.rate_kib=0;
            view.inserted_title="Sword of the Berserk";
            view.reference_checked=true;
            view.reference.result=!strcmp(argv[1],"complete")?
                KUI_KNOWN_FULL_MATCH:KUI_KNOWN_DATA_MATCH;
        } else if(!strcmp(argv[1],"idle")) {
            view.phase=0;view.done=view.total=view.committed=0;view.rate_kib=0;
            view.track=view.tracks=0;view.elapsed_ms=0;
        } else if(!strcmp(argv[1],"dma-fallback")) {
            view.busy=true;view.dma_degraded=true;view.rate_kib=690;
        } else if(!strcmp(argv[1],"music-queued")) {
            view.busy=true;view.music_change_pending=true;
        } else if(!strcmp(argv[1],"reset")) {
            view.outcome=KUI_SHELL_OUTCOME_FAILED;view.drive_reset_required=true;
            view.message="Capture timed out; drive abort failed. Restart required.";
        } else if(!strcmp(argv[1],"stopped")) view.outcome=KUI_SHELL_OUTCOME_STOPPED;
        else view.busy=true;
    }
    kui_shell_draw(frame,&shell,&view,NULL,NULL);
    FILE *out=fopen(argv[2],"wb"); if(!out) return 1;
    if(fprintf(out,"P6\n640 480\n255\n")<0) return 1;
    for(unsigned i=0;i<640*480;i++) {
        unsigned v=frame[i];
        unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),
            (unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
        if(fwrite(rgb,1,3,out)!=3) return 1;
    }
    return fclose(out)!=0;
}
