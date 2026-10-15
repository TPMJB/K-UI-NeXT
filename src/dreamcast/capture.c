/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/capture_network.h"
#include "kui/network_w5500.h"
#include "kui/network_wifi.h"

/* The overlapped GD-ROM DMA read, in every build since it was proven on hardware (Trips 11-12).
 * Whether a capture uses it is capture_read= (default dma). */
/* IDE and the GD-ROM share G1. Preserve GD DMA, but finish it here
 * before the engine can write the preceding chunk to the ATA slave. */
static bool serialized_read;
static enum kui_read_result serialized_result;
static bool capture_read_begin(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    if(!kui_disc_read_begin(ctx,fad,sectors,out)) return false;
    if(kui_storage_active()==KUI_STORAGE_IDE) {
        serialized_result=kui_disc_read_end(ctx);serialized_read=true;
    }
    return true;
}
static enum kui_read_result capture_read_end(void *ctx) {
    if(serialized_read) {serialized_read=false;return serialized_result;}
    return kui_disc_read_end(ctx);
}
#define KUI_CAPTURE_DMA_OPS capture_read_begin,capture_read_end
#include <kos/timer.h>
#include <string.h>
static struct kui_capture_stats last_stats;
const struct kui_capture_stats *kui_capture_last_stats(void) { return &last_stats; }
static bool cancelled(void *ctx) { (void)ctx;return kui_cancelled(); }
static uint64_t now(void *ctx) { (void)ctx;return timer_ms_gettime64(); }
static uint64_t now_us(void *ctx) { (void)ctx;return timer_us_gettime64(); }
static void clear_network_config(struct kui_network_rip_config *config) {
    volatile uint8_t *p=(volatile uint8_t *)config;
    for(size_t i=0;i<sizeof(*config);i++)p[i]=0;
}
static enum kui_capture_result capture_start(enum kui_capture_mode mode,const char *build,
    const char *destination,bool quick_resume,const char *job_directory,const char *folder_label,bool network,bool cd_toc) {
    memset(&last_stats,0,sizeof(last_stats));
    char parent[KUI_DEST_ROOT_CAP];
    if(!kui_destination_normalize(parent,destination)) {
        kui_log("Capture refused: invalid or too-long destination; no dump writes");
        return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
    }
    char selected[KUI_DEST_JOB_CAP]={0},label[KUI_DEST_TITLE_CAP]={0};
    if(job_directory && *job_directory) {
        if(mode==KUI_CAPTURE_NEW || !kui_destination_job_normalize(selected,job_directory)) {
            kui_log("Capture refused: invalid exact saved job; no dump writes");
            return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
        }
    }
    if(mode==KUI_CAPTURE_NEW && folder_label && *folder_label) {
        if(!memchr(folder_label,0,KUI_DEST_TITLE_CAP)) {
            kui_log("Capture refused: folder label too long; no dump writes");
            return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
        }
        kui_destination_title(label,folder_label);
    }
    /* These local values live through the synchronous capture call; the engine
     * never borrows the UI's mutable destination buffer. */
    struct kui_toc sessions[2];struct kui_capture_plan plan;
    /* Missing configuration is valid; unreadable/rejected configuration must
     * not silently weaken a user's requested verification policy. */
    if(!kui_options_refresh()) {
        kui_log("Capture refused: preferences or bench.cfg could not be loaded; no dump writes");
        return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
    }
    struct kui_capture_output output={.parent=parent,.game_names=true,
        .format=kui_options.capture_format,.folder_label=label[0]?label:NULL};
    kui_disc_timing_reset();
    bool is_cd=false;
    if(!(cd_toc?kui_disc_prepare_capture(sessions,&is_cd):kui_disc_prepare(sessions)) || kui_cancelled())
        return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
    if(is_cd && !cd_toc) {
        kui_log("CD capture refused: choose Advanced > Single-session CD source for a known single-session disc");
        return KUI_CAPTURE_FAILED;
    }
    if(!(is_cd?kui_plan_cd_tracks(&sessions[0],&plan):kui_plan_tracks(sessions,&plan))) {
        kui_log("Unsupported TOC for this GDI profile; no dump files written");return KUI_CAPTURE_FAILED;
    }
    if(is_cd) {
        output.format=KUI_CAPTURE_FORMAT_BIN_CUE;
        kui_log("CD capture: full TOC INDEX01 intervals, BIN/CUE output; no subchannel/index reconstruction");
        kui_log("Single-session scope declared by user; session count is not queried from firmware");
    }
    struct kui_capture_network *remote=NULL;
    if(network) {
        struct kui_network_rip_config config={0};char problem[128]={0};FATFS fs;
        if(!kui_sd_connect())return KUI_CAPTURE_FAILED;
        bool ok=kui_mount(&fs,kui_log);
        if(ok)ok=kui_capture_network_config_load(&config,problem);
        if(f_mount(NULL,"0:",0)!=FR_OK)ok=false;
        kui_sd_disconnect();
        if(!ok) {clear_network_config(&config);kui_log("PC destination refused: %s",problem[0]?problem:"local configuration unavailable");return KUI_CAPTURE_FAILED;}
        const struct kui_net_ports ports={kui_w5500_console_port(),kui_wifi_console_port()};
        remote=kui_capture_network_start(&ports,&config,kui_log,kui_cancelled,NULL,problem);
        clear_network_config(&config);
        if(!remote) {kui_log("PC destination refused: %s",problem);return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;}
        kui_log("Destination: authenticated PC volume; raw tracks are written directly to the PC");
    } else if(!kui_sd_connect()) return KUI_CAPTURE_FAILED;
    /* The first value of each capture_* list in bench.cfg; the defaults are the engine as
     * it has always been. A resumed job keeps the hash mode it started with. */
    struct kui_capture_options options={
        .crc_only=kui_options.capture_crc_only[0],.skip_end_readback=!kui_options.end_readback[0],
        .resume_size_only=(quick_resume && mode==KUI_CAPTURE_RESUME)||kui_options.resume_size[0],
        .sample_every=kui_options.sample_readback[0],
        .read_dma=kui_options.capture_dma[0],.output=&output,
        .retry_limit=kui_options.capture_retry_limit,.strict_sectors=kui_options.capture_strict_sectors,
        .job_directory=selected[0]?selected:NULL};
    struct kui_capture_ops ops={NULL,kui_disc_read_raw,cancelled,now,kui_capture_status,
        kui_log,build,now_us,kui_disc_timing_phase,KUI_CAPTURE_DMA_OPS,&options,&last_stats};
    /* Whole-operation CPU split: how much of this capture the UI thread took. */
    struct kui_cpu_census cpu_before,cpu_after;
    kui_cpu_census_mark(&cpu_before);
    enum kui_capture_result result=kui_capture(&plan,&ops,mode);
    kui_cpu_census_mark(&cpu_after);
    kui_cpu_census_log(kui_log,"operation","(setup+capture+verify)",&cpu_before,&cpu_after);
    if(remote) {
        /* The engine unmounts on every exit. Refuse to leave a stale remote
         * mount registered even after a failed operation. Reports reconnect
         * the local card only after this session has ended. */
        if(f_mount(NULL,"0:",0)!=FR_OK && result==KUI_CAPTURE_COMPLETE)result=KUI_CAPTURE_FAILED;
        if(kui_capture_network_problem(remote)[0])kui_log("PC destination: %s",kui_capture_network_problem(remote));
        if(!kui_capture_network_end(remote) && result==KUI_CAPTURE_COMPLETE)result=KUI_CAPTURE_FAILED;
    } else kui_sd_disconnect();
    kui_disc_timing_report();
    return result;
}
enum kui_capture_result kui_capture_start(enum kui_capture_mode mode,const char *build,
    const char *destination) {
    return capture_start(mode,build,destination,false,NULL,NULL,false,false);
}
enum kui_capture_result kui_capture_start_selected(enum kui_capture_mode mode,const char *build,
    const char *destination,const char *job_directory,const char *folder_label,bool quick_resume) {
    if(quick_resume && mode!=KUI_CAPTURE_RESUME) {
        memset(&last_stats,0,sizeof(last_stats));
        kui_log("Capture refused: Quick Resume requires Resume mode; no dump writes");
        return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
    }
    return capture_start(mode,build,destination,quick_resume,job_directory,folder_label,false,false);
}
enum kui_capture_result kui_capture_start_target(enum kui_capture_mode mode,const char *build,
    const char *destination,const char *job_directory,const char *folder_label,bool quick_resume,bool network,bool cd_toc) {
    if(quick_resume && mode!=KUI_CAPTURE_RESUME) {
        memset(&last_stats,0,sizeof(last_stats));
        kui_log("Capture refused: Quick Resume requires Resume mode; no dump writes");
        return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
    }
    return capture_start(mode,build,destination,quick_resume,job_directory,folder_label,network,cd_toc);
}
enum kui_capture_result kui_capture_resume_quick(const char *build,const char *destination) {
    /* This opt-in changes only this call's local options. Checkpoint identity,
     * size checks and SHA-256 job compatibility stay with the existing engine;
     * no preferences or bench.cfg values are rewritten. */
    return capture_start(KUI_CAPTURE_RESUME,build,destination,true,NULL,NULL,false,false);
}
