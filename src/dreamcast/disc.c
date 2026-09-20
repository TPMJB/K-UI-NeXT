/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include <dc/syscalls.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <inttypes.h>
#include <string.h>

/* All firmware-visible objects survive a failed abort. Once poisoned, this
 * adapter will not touch them again, and requires a console reset. */
static bool initialized, poisoned, media_changed;
static cd_read_params_t read_params;
static cd_cmd_toc_params_t toc_params;
static cd_cmd_chk_status_t detail;
static struct { uint8_t before[32]; cd_toc_t data; uint8_t after[32]; } toc;
static _Alignas(32) struct { uint8_t before[32], data[KUI_RAW_BYTES], after[32]; } raw[2];
static _Alignas(32) struct { uint8_t before[32], data[KUI_DATA_BYTES], after[32]; } cooked[2];
static _Alignas(32) struct {
    uint8_t before[32], data[KUI_CAPTURE_CHUNK*KUI_RAW_BYTES], after[32];
} capture_raw[2];

/* One I/O worker; fixed counters, no per-request formatting or SD writes.
 * Raw-call children are exclusive. Command submit/poll/wait/abort timers are
 * children of read1/read2, not extra time to add to TIMING disc. */
struct command_timing {
    uint64_t us,calls,bytes,max_us,submit_us,poll_us,wait_us,abort_us;
    uint64_t submits,polls,waits,aborts,result[KUI_CMD_INVALID+1];
    uint32_t max_fad,max_sectors;
    /* Firmware poll calls by duration (kui_probe_bucket): short ones are the
     * CPU spinning on a busy drive, long ones are the PIO transfer itself. */
    uint64_t poll_n[KUI_PROBE_BUCKETS],poll_hist_us[KUI_PROBE_BUCKETS];
};
static struct optical_timing {
    uint64_t us,requests,bytes,mode_us,modes,mode_failures,buffers_us;
    uint64_t result[3],mismatches,guards,refused,short_transfers;
    struct command_timing read[2];
} optical[2];
static unsigned optical_phase;
static struct command_timing *active_command;
/* PIO advances through exec_server calls. Sleeping after every busy status
 * throttles that service loop to scheduler ticks (about 8 ms on our console).
 * Keep servicing it, yielding the runnable worker every 2 ms for UI/Maple.
 * The portable command loop still checks Stop and its deadline on every poll.
 * Firmware calls themselves cannot be preempted by the software deadline. */
#define PIO_SERVICE_QUANTUM_US 2000u
static unsigned pio_quantum_us=PIO_SERVICE_QUANTUM_US;
static bool fast_pio;
static uint64_t last_pass_us;
/* Bench sweep only: while set, the read being made reports into these instead
 * of the capture counters, and pause_worker spins instead of yielding. */
static struct kui_probe_stats *probe_stats;
static unsigned probe_service_us;
void kui_disc_set_yield_us(unsigned us) { pio_quantum_us=us?us:PIO_SERVICE_QUANTUM_US; }

void kui_disc_timing_reset(void) {
    memset(optical,0,sizeof(optical));optical_phase=0;active_command=NULL;
}
void kui_disc_timing_phase(void *ctx,bool capturing) {
    (void)ctx;optical_phase=capturing?1:0;
}
void kui_disc_timing_report(void) {
    for(unsigned p=0;p<2;p++) {
        const struct optical_timing *t=&optical[p];
        if(!t->requests) continue;
        uint64_t children=t->mode_us+t->buffers_us+t->read[0].us+t->read[1].us;
        kui_log("OPTICAL %s: subtimers inside TIMING disc",p?"capture":"setup");
        kui_log("opt policy=%s PIO yield_us=%u",p?"single":"paired",pio_quantum_us);
        kui_log("opt wall_us=%" PRIu64 " requests=%" PRIu64 " bytes=%" PRIu64,t->us,t->requests,t->bytes);
        kui_log("opt mode_us=%" PRIu64 " calls=%" PRIu64 " failed=%" PRIu64,t->mode_us,t->modes,t->mode_failures);
        kui_log("opt buffers_us=%" PRIu64 " other_us=%" PRIu64,t->buffers_us,t->us>=children?t->us-children:0);
        kui_log("opt requests ok=%" PRIu64 " retry=%" PRIu64 " fatal=%" PRIu64,
            t->result[KUI_READ_OK],t->result[KUI_READ_RETRY],t->result[KUI_READ_FATAL]);
        kui_log("opt mismatch=%" PRIu64 " guards=%" PRIu64 " refused=%" PRIu64,t->mismatches,t->guards,t->refused);
        kui_log("opt short=%" PRIu64,t->short_transfers);
        kui_log("opt poll buckets: call <25us/<100us/<400us/<1.6ms/more");
        for(unsigned i=0;i<2;i++) {
            const struct command_timing *c=&t->read[i];
            uint64_t nested=c->submit_us+c->poll_us+c->wait_us+c->abort_us;
            kui_log("opt read%u us=%" PRIu64 " max_us=%" PRIu64,i+1,c->us,c->max_us);
            kui_log("opt read%u calls=%" PRIu64 " bytes=%" PRIu64,i+1,c->calls,c->bytes);
            kui_log("opt read%u max_fad=%" PRIu32 " max_sectors=%" PRIu32,i+1,c->max_fad,c->max_sectors);
            kui_log("opt read%u submit_us=%" PRIu64 " submits=%" PRIu64,i+1,c->submit_us,c->submits);
            kui_log("opt read%u poll_us=%" PRIu64 " polls=%" PRIu64,i+1,c->poll_us,c->polls);
            kui_log("opt read%u wait_us=%" PRIu64 " waits=%" PRIu64,i+1,c->wait_us,c->waits);
            kui_log("opt read%u abort_us=%" PRIu64 " aborts=%" PRIu64,i+1,c->abort_us,c->aborts);
            kui_log("opt read%u other_us=%" PRIu64,i+1,c->us>=nested?c->us-nested:0);
            if(c->polls) {
                kui_log("opt read%u poll_n %" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64,i+1,
                    c->poll_n[0],c->poll_n[1],c->poll_n[2],c->poll_n[3],c->poll_n[4]);
                kui_log("opt read%u poll_ms %" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64,i+1,
                    c->poll_hist_us[0]/1000,c->poll_hist_us[1]/1000,c->poll_hist_us[2]/1000,
                    c->poll_hist_us[3]/1000,c->poll_hist_us[4]/1000);
            }
            kui_log("opt read%u ok=%" PRIu64 " failed=%" PRIu64 " timeout=%" PRIu64,
                i+1,c->result[KUI_CMD_OK],c->result[KUI_CMD_FAILED],c->result[KUI_CMD_TIMEOUT]);
            kui_log("opt read%u cancelled=%" PRIu64 " abort_failed=%" PRIu64 " invalid=%" PRIu64,
                i+1,c->result[KUI_CMD_CANCELLED],c->result[KUI_CMD_RECOVERY_FAILED],c->result[KUI_CMD_INVALID]);
        }
    }
}

static uint64_t now(void *ctx) { (void)ctx; return timer_ms_gettime64(); }
static void pause_worker(void *ctx) {
    (void)ctx;
    uint64_t start=timer_us_gettime64();
    if(probe_service_us) {
        /* Sweep only: stands in for the CPU being busy elsewhere between
         * firmware service calls, so the drive is left unserviced that long. */
        while(timer_us_gettime64()-start<probe_service_us) { }
    } else if(fast_pio) {
        if(start-last_pass_us<pio_quantum_us) return;
        thd_pass();
        last_pass_us=timer_us_gettime64();
    } else thd_sleep(1);
    if(active_command) {active_command->wait_us+=timer_us_gettime64()-start;++active_command->waits;}
    if(probe_stats) {probe_stats->pause_us+=timer_us_gettime64()-start;++probe_stats->pauses;}
}
static bool cancelled(void *ctx) { (void)ctx; return kui_cancelled(); }
static int submit(void *ctx, int command, void *params) {
    (void)ctx;
    uint64_t start=(active_command||probe_stats)?timer_us_gettime64():0;
    int handle = syscall_gdrom_send_command((cd_cmd_code_t)command, params);
    syscall_gdrom_exec_server();
    if(active_command||probe_stats) {
        uint64_t d=timer_us_gettime64()-start;
        if(active_command) {active_command->submit_us+=d;++active_command->submits;}
        if(probe_stats) probe_stats->submit_us+=d;
    }
    return handle;
}
static int poll(void *ctx, int handle) {
    (void)ctx;
    uint64_t start=(active_command||probe_stats)?timer_us_gettime64():0;
    syscall_gdrom_exec_server();
    int status=syscall_gdrom_check_command(handle, &detail);
    if(active_command||probe_stats) {
        uint64_t d=timer_us_gettime64()-start;unsigned b=kui_probe_bucket(d);
        if(active_command) {
            active_command->poll_us+=d;++active_command->polls;
            ++active_command->poll_n[b];active_command->poll_hist_us[b]+=d;
        }
        if(probe_stats) {++probe_stats->polls;++probe_stats->poll_n[b];probe_stats->poll_us[b]+=d;}
    }
    return status;
}
static void abort_command(void *ctx, int handle) {
    (void)ctx;uint64_t start=active_command?timer_us_gettime64():0;
    syscall_gdrom_abort_command(handle);
    if(active_command) {active_command->abort_us+=timer_us_gettime64()-start;++active_command->aborts;}
}
static bool command(int code, void *params, uint32_t timeout) {
    if(poisoned || kui_cancelled()) {
        if(active_command) ++active_command->result[poisoned?KUI_CMD_RECOVERY_FAILED:KUI_CMD_CANCELLED];
        return false;
    }
    memset(&detail, 0, sizeof(detail));
    fast_pio=code==CD_CMD_PIOREAD;
    last_pass_us=timer_us_gettime64();
    struct kui_command_ops ops = {NULL, now, pause_worker, cancelled, submit, poll, abort_command};
    enum kui_command_result result = kui_command(&ops, code, params, timeout, 1000);
    fast_pio=false;
    if(active_command) ++active_command->result[result];
    if(result == KUI_CMD_OK) return true;
    kui_log("CMD %d %s", code, kui_command_name(result));
    kui_log("  sense=%" PRId32 "/%" PRId32 " size=%lu ATA=%d",
        detail.err1, detail.err2, (unsigned long)detail.size, detail.ata);
    if(result == KUI_CMD_RECOVERY_FAILED) poisoned = true;
    if(detail.err1 == 6 || detail.err1 == 2) media_changed = true;
    return false;
}

static bool set_mode(int size, int track_type) {
    if(poisoned || kui_cancelled()) return false;
    cd_sec_mode_params_t mode = {0,
        size == 2352 ? CDROM_READ_WHOLE_SECTOR : CDROM_READ_DATA_AREA,
        track_type, size};
    int r = syscall_gdrom_sector_mode(&mode);
    if(r != 0) kui_log("Set sector mode failed: %d", r);
    return r == 0;
}
static bool read_sector(void *data, uint32_t fad) {
    read_params = (cd_read_params_t){.start_sec = fad, .num_sec = 1,
                                   .buffer = data, .is_test = 0};
    return command(CD_CMD_PIOREAD, &read_params, 5000);
}

static bool sample(const struct kui_track *track, uint32_t fad) {
    uint32_t lba;
    if(!kui_fad_to_lba(fad, &lba)) return false;
    kui_log("Track %02u FAD=%" PRIu32 " LBA=%" PRIu32, track->number, fad, lba);
    if(!set_mode(2352, 0)) return false;
    /* Two different initial fills reveal bytes left untouched by a short
     * transfer. Guards detect nearby overwrite. Repeat equality is evidence
     * of coverage/stability, not a full CD integrity or reference check. */
    for(unsigned i = 0; i < 2; ++i) {
        memset(&raw[i], i ? 0x5a : 0xa5, sizeof(raw[i]));
        if(!read_sector(raw[i].data, fad)) return false;
        uint8_t fill = i ? 0x5a : 0xa5;
        if(!kui_guard_is(raw[i].before, 32, fill) ||
           !kui_guard_is(raw[i].after, 32, fill)) {
            kui_log("RAW GUARD CORRUPTION: RESET REQUIRED"); poisoned = true;
            return false;
        }
    }
    if(memcmp(raw[0].data, raw[1].data, KUI_RAW_BYTES)) {
        kui_log("Raw reads differ: underfill, jitter or unstable data"); return false;
    }
    kui_log("  raw repeat/guards OK CRC32=%08" PRIx32,
        kui_crc32(0, raw[0].data, KUI_RAW_BYTES));
    if(!(track->control & 4)) {
        kui_log("  audio sample (no offset/subchannel validation)"); return true;
    }
    int offset = kui_data_offset(raw[0].data);
    if(offset < 0) { kui_log("  unsupported/malformed data-sector layout"); return false; }
    kui_log("  raw mode=%u header=%02x:%02x:%02x", raw[0].data[15],
        raw[0].data[12], raw[0].data[13], raw[0].data[14]);
    if(!set_mode(2048, offset == 16 ? 1024 : 2048)) return false;
    for(unsigned i = 0; i < 2; ++i) {
        memset(&cooked[i], i ? 0x5a : 0xa5, sizeof(cooked[i]));
        if(!read_sector(cooked[i].data, fad)) return false;
        uint8_t fill = i ? 0x5a : 0xa5;
        if(!kui_guard_is(cooked[i].before, 32, fill) ||
           !kui_guard_is(cooked[i].after, 32, fill)) {
            kui_log("COOKED GUARD CORRUPTION: RESET REQUIRED"); poisoned = true;
            return false;
        }
    }
    if(memcmp(cooked[0].data, cooked[1].data, KUI_DATA_BYTES) ||
       memcmp(raw[0].data + offset, cooked[0].data, KUI_DATA_BYTES)) {
        kui_log("  raw/cooked payload mismatch"); return false;
    }
    kui_log("  cooked repeat/guards and raw payload MATCH");
    return true;
}

bool kui_disc_prepare(struct kui_toc sessions[2]) {
    if(poisoned) { kui_log("Drive unavailable after failed recovery; reset console"); return false; }
    if(!initialized) { kui_drive_init_bus(); initialized = true; }
    bool ready = false;
    /* Two additional INIT attempts for disc-change sense only. */
    for(unsigned attempt = 0; attempt < 3 && !kui_cancelled(); ++attempt) {
        if(command(CD_CMD_INIT, NULL, 10000)) { ready = true; break; }
        if(poisoned || detail.err1 != 6) break;
    }
    if(!ready) { kui_log("Disc initialization did not complete"); return false; }
    media_changed = false;
    for(unsigned area = 0; area < 2; ++area) {
        memset(&toc, 0xa5, sizeof(toc));
        toc_params = (cd_cmd_toc_params_t){(cd_area_t)area, &toc.data};
        if(!command(CD_CMD_GETTOC2, &toc_params, 5000)) return false;
        if(!kui_guard_is(toc.before, 32, 0xa5) || !kui_guard_is(toc.after, 32, 0xa5)) {
            kui_log("TOC GUARD CORRUPTION: RESET REQUIRED"); poisoned = true; return false;
        }
        kui_log("%s TOC first=%08" PRIx32 " last=%08" PRIx32 " end=%08" PRIx32,
            area ? "HIGH" : "LOW", toc.data.first, toc.data.last, toc.data.leadout_sector);
        if(!kui_parse_toc(toc.data.entry, toc.data.first, toc.data.last,
                           toc.data.leadout_sector, &sessions[area])) {
            kui_log("TOC invalid or unsupported; no sample reads issued"); return false;
        }
        for(unsigned j = 0; j < sessions[area].count; ++j) {
            const struct kui_track *t = &sessions[area].tracks[j];
            kui_log("  T%02u ctrl=%x start=%" PRIu32 " next=%" PRIu32,
                t->number, t->control, t->start, t->end);
        }
    }
    return true;
}

enum kui_read_result kui_disc_read_raw(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    (void)ctx;
    struct optical_timing *t=&optical[optical_phase];
    uint64_t request_start=timer_us_gettime64(),start;
    enum kui_read_result result=KUI_READ_FATAL;
    ++t->requests;
    if(!out || !sectors || sectors>KUI_CAPTURE_CHUNK || fad<150 || fad>0xffffff ||
       sectors>0x1000000u-fad || poisoned || media_changed || kui_cancelled()) {++t->refused;goto done;}
    start=timer_us_gettime64();bool mode_ok=set_mode(2352,0);
    t->mode_us+=timer_us_gettime64()-start;++t->modes;
    if(!mode_ok) {++t->mode_failures;goto done;}
    size_t bytes=(size_t)sectors*KUI_RAW_BYTES;
    /* Identification keeps its established paired samples and disc identity.
     * Capture issues one sequential request; failures still use the core's
     * bounded retry policy. Saved-file CRC/SHA verification is unchanged. */
    unsigned reads=optical_phase?1:2;
    for(unsigned i=0;i<reads;i++) {
        uint8_t fill=i?0x5a:0xa5;
        start=timer_us_gettime64();
        memset(&capture_raw[i],fill,sizeof(capture_raw[i]));
        read_params=(cd_read_params_t){.start_sec=fad,.num_sec=sectors,.buffer=capture_raw[i].data,.is_test=0};
        t->buffers_us+=timer_us_gettime64()-start;
        struct command_timing *c=&t->read[i];active_command=c;
        start=timer_us_gettime64();bool read_ok=command(CD_CMD_PIOREAD,&read_params,5000);
        uint64_t duration=timer_us_gettime64()-start;active_command=NULL;
        c->us+=duration;++c->calls;
        if(duration>c->max_us) {c->max_us=duration;c->max_fad=fad;c->max_sectors=sectors;}
        /* A failed abort may leave firmware owning these static objects. */
        if(poisoned) goto done;
        start=timer_us_gettime64();
        bool guard_ok=kui_guard_is(capture_raw[i].before,32,fill) &&
           kui_guard_is(capture_raw[i].after,32,fill) &&
           kui_guard_is(capture_raw[i].data+bytes,sizeof(capture_raw[i].data)-bytes,fill);
        t->buffers_us+=timer_us_gettime64()-start;
        if(!guard_ok) {
            ++t->guards;poisoned=true;kui_log("CAPTURE GUARD CORRUPTION: RESET REQUIRED");goto done;
        }
        if(!read_ok) {result=media_changed || kui_cancelled()?KUI_READ_FATAL:KUI_READ_RETRY;goto done;}
        /* KOS defines status.size as transferred bytes. This check detects a
         * reported short transfer without rereading CDDA or guessing from its
         * contents (legitimate audio can match any buffer fill pattern). */
        if(detail.size!=bytes) {
            ++t->short_transfers;
            kui_log("Transfer size mismatch FAD=%" PRIu32 " got=%lu want=%lu",
                fad,(unsigned long)detail.size,(unsigned long)bytes);
            result=KUI_READ_RETRY;goto done;
        }
        c->bytes+=bytes;
    }
    start=timer_us_gettime64();
    bool mismatch=reads==2 && memcmp(capture_raw[0].data,capture_raw[1].data,bytes)!=0;
    if(!mismatch) memcpy(out,capture_raw[0].data,bytes);
    t->buffers_us+=timer_us_gettime64()-start;
    if(mismatch) {
        ++t->mismatches;kui_log("Raw repeat mismatch at FAD=%" PRIu32,fad);result=KUI_READ_RETRY;
    } else {
        t->bytes+=bytes;result=KUI_READ_OK;
    }
done:
    ++t->result[result];t->us+=timer_us_gettime64()-request_start;return result;
}

/* --- bench-only optical probe ------------------------------------------------
 * The same PIO command, mode setup, guards and error handling as capture, so a
 * sweep measures the real path, but into its own buffer sized for the largest
 * sweep read and reporting into caller-owned counters. The guard covers the
 * before/after words and up to KUI_PROBE_GUARD_TAIL bytes past the request, so
 * a transfer that overruns is still caught; only that tail is refilled, not the
 * whole buffer, because a memset that grows with the read size would itself
 * look like drive time and skew the very comparison the sweep is for. */
#define KUI_PROBE_GUARD_TAIL 4096u
static _Alignas(32) struct {
    uint8_t before[32], data[KUI_OPT_SWEEP_CHUNK_MAX*KUI_RAW_BYTES], after[32];
} probe_raw;

enum kui_read_result kui_disc_read_probe(void *ctx,uint32_t fad,unsigned sectors,unsigned service_us,
        const uint8_t **out,struct kui_probe_stats *stats) {
    (void)ctx;
    if(!out||!stats||!sectors||sectors>KUI_OPT_SWEEP_CHUNK_MAX||fad<150||fad>0xffffff||
       sectors>0x1000000u-fad||poisoned||media_changed||kui_cancelled()) return KUI_READ_FATAL;
    if(!set_mode(2352,0)) return KUI_READ_FATAL;
    size_t bytes=(size_t)sectors*KUI_RAW_BYTES;
    size_t tail=sizeof(probe_raw.data)-bytes;
    if(tail>KUI_PROBE_GUARD_TAIL) tail=KUI_PROBE_GUARD_TAIL;
    memset(probe_raw.before,0xa5,sizeof(probe_raw.before));
    memset(probe_raw.after,0xa5,sizeof(probe_raw.after));
    memset(probe_raw.data+bytes,0xa5,tail);
    read_params=(cd_read_params_t){.start_sec=fad,.num_sec=sectors,.buffer=probe_raw.data,.is_test=0};
    probe_stats=stats;probe_service_us=service_us;
    uint64_t start=timer_us_gettime64();
    bool read_ok=command(CD_CMD_PIOREAD,&read_params,5000);
    uint64_t duration=timer_us_gettime64()-start;
    probe_stats=NULL;probe_service_us=0;
    stats->cmd_us+=duration;stats->last_us=duration;
    /* A failed abort may leave firmware owning this static object. */
    if(poisoned) return KUI_READ_FATAL;
    bool guard_ok=kui_guard_is(probe_raw.before,32,0xa5)&&kui_guard_is(probe_raw.after,32,0xa5)&&
        kui_guard_is(probe_raw.data+bytes,tail,0xa5);
    if(!guard_ok) {poisoned=true;kui_log("PROBE GUARD CORRUPTION: RESET REQUIRED");return KUI_READ_FATAL;}
    if(!read_ok) return media_changed||kui_cancelled()?KUI_READ_FATAL:KUI_READ_RETRY;
    if(detail.size!=bytes) {
        kui_log("Transfer size mismatch FAD=%" PRIu32 " got=%lu want=%lu",
            fad,(unsigned long)detail.size,(unsigned long)bytes);
        return KUI_READ_RETRY;
    }
    *out=probe_raw.data;
    return KUI_READ_OK;
}

/* The GD-ROM DMA probe is EXPERIMENTAL and opt-in at build time: `make diagnostic
 * KUI_EXPERIMENTAL=1`. It is bench-only hardware probing that has never run on a console and
 * that failed to build twice on this project's CI (register allocation in KOS's cache helper, then
 * my own wrong macro test that let it back in), so it is not compiled into the
 * default build: nothing experimental may be able to break the build the capture engine ships
 * in. The host tests build it (test-disc is compiled with the define). */
#ifdef KUI_EXPERIMENTAL_DMA
/* Data-cache maintenance for the DMA probe: one `ocbp` (write back, then invalidate) or `ocbi`
 * (invalidate) per 32-byte line, each with a single register operand. KOS's own
 * arch_dcache_purge_range/inval_range were used first and did not compile under the CI's GCC
 * 15.2 ("asm operand has impossible constraints"): their per-line helper is one inline asm with
 * EIGHT memory operands plus a register, which the allocator could not satisfy once inlined into
 * a function with many live values, as the probe is. Nothing here needs that generality, and a
 * one-register asm cannot fail that way. Kept out of line so the asm never sees the probe's
 * register pressure at all. The host tests substitute KOS's header with a recording double. */
#ifndef KUI_ON_CONSOLE
#include <arch/cache.h>   /* host tests only: a recording double. The console does not use KOS's header here. */
#endif
#ifdef KUI_ON_CONSOLE
__attribute__((noinline)) static void cache_purge(uintptr_t start,size_t bytes) {
    uintptr_t end=start+bytes;
    for(start&=~(uintptr_t)31;start<end;start+=32) __asm__ __volatile__("ocbp @%0"::"r"(start):"memory");
}
__attribute__((noinline)) static void cache_inval(uintptr_t start,size_t bytes) {
    uintptr_t end=start+bytes;
    for(start&=~(uintptr_t)31;start<end;start+=32) __asm__ __volatile__("ocbi @%0"::"r"(start):"memory");
}
#else
static void cache_purge(uintptr_t start,size_t bytes) { arch_dcache_purge_range(start,bytes); }
static void cache_inval(uintptr_t start,size_t bytes) { arch_dcache_inval_range(start,bytes); }
#endif

/* --- bench-only GD-ROM DMA probe --------------------------------------------------
 * EXPERIMENTAL. Whether this drive can DMA raw 2352-byte sectors, what it costs, and how
 * much CPU it leaves free are not known; this exists to find out, and is reached only by
 * the bench's `sweep_mode=dma`. It goes through the same command layer as every other
 * read here (deadlines, cancellation, abort, poisoning) and POLLS for completion. It does
 * not use an interrupt: this runtime does not initialize KOS's CD-ROM subsystem (no
 * INIT_CDROM), so nothing installs the DMA-end handler, and what that handler does in KOS
 * is call exec_server and read the command status, which the polling loop does anyway. If
 * a drive never completes a DMA that way the probe says so and an interrupt-driven
 * variant is the next thing to try.
 *
 * Between polls the worker SLEEPS (thd_sleep, ~10 ms resolution) rather than yielding, so
 * the CPU is genuinely free while the drive transfers, and a completion can be noticed up
 * to one scheduler tick late: the rate includes that latency, about 8% at 32 sectors and
 * 2% at 128.
 *
 * Three hazards this handles that a copy of KOS's own path would not:
 *  - A sector is 2352 bytes, a multiple of 32 only for an even count: the transfer must be.
 *  - KOS invalidates the cache over cnt * 2048 bytes (its default sector size, which this
 *    code never sets); these sectors are 2352, so the whole range is invalidated here.
 *  - The guard pattern is written by the CPU, so it is purged (written back) before the
 *    DMA, or dirty lines could later be written back over what the drive wrote.
 * After any failure to complete, DMA stays off until reboot: an unfinished transfer may
 * still own the buffer, and an untested bus state is not one to keep poking. */
static _Alignas(32) struct {
    uint8_t before[32], data[KUI_OPT_SWEEP_CHUNK_MAX*KUI_RAW_BYTES], after[32];
} probe_dma_raw;
static bool dma_broken;
static void *dma_address(void *p) {
#ifdef KUI_ON_CONSOLE
    return (void *)((uintptr_t)p & 0x1fffffffu);   /* the DMA engine addresses physical memory */
#else
    return p;
#endif
}

/* The same DMA read, split so the CPU can write the previous chunk to the SD card while the
 * drive fills this one. Measured: a DMA read costs the CPU under 1% (Trip 6a), so the whole
 * disc time can hide behind the SD write. The caller MUST call end after a successful begin:
 * until it does, the firmware owns read_params and the buffer it was given.
 * Bench-only, like the blocking probe; the capture engine does not use it yet. */
static struct kui_command_async dma_async;
static uint8_t *dma_target;
static size_t dma_target_bytes;
static bool dma_in_flight;

bool kui_disc_read_begin(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    (void)ctx;
    if(!out||!sectors||(sectors&1u)||sectors>KUI_OPT_SWEEP_CHUNK_MAX||fad<150||fad>0xffffff||
       sectors>0x1000000u-fad||poisoned||media_changed||dma_broken||dma_in_flight||
       ((uintptr_t)out&31u)||kui_cancelled()) return false;
    if(!set_mode(2352,0)) return false;
    dma_target=out;dma_target_bytes=(size_t)sectors*KUI_RAW_BYTES;
    /* The CPU must not hold dirty lines over this range: the drive writes it behind our back. */
    cache_purge((uintptr_t)out,dma_target_bytes);
    cache_inval((uintptr_t)out,dma_target_bytes);
    read_params=(cd_read_params_t){.start_sec=fad,.num_sec=sectors,
        .buffer=dma_address(out),.is_test=0};
    struct kui_command_ops ops={NULL,now,pause_worker,cancelled,submit,poll,abort_command};
    memset(&detail,0,sizeof(detail));
    if(kui_command_begin(&ops,CD_CMD_DMAREAD,&read_params,5000,1000,&dma_async)!=KUI_CMD_OK) return false;
    dma_in_flight=true;
    return true;
}
bool kui_disc_read_pending(void *ctx) {
    (void)ctx;
    if(!dma_in_flight) return false;
    struct kui_command_ops ops={NULL,now,pause_worker,cancelled,submit,poll,abort_command};
    return !kui_command_ready(&ops,&dma_async);
}
enum kui_read_result kui_disc_read_end(void *ctx) {
    (void)ctx;
    if(!dma_in_flight) return KUI_READ_FATAL;
    struct kui_command_ops ops={NULL,now,pause_worker,cancelled,submit,poll,abort_command};
    enum kui_command_result r=kui_command_end(&ops,&dma_async);
    dma_in_flight=false;
    /* Read what the drive wrote, not whatever the cache kept. */
    cache_inval((uintptr_t)dma_target,dma_target_bytes);
    if(r==KUI_CMD_RECOVERY_FAILED) {poisoned=true;dma_broken=true;return KUI_READ_FATAL;}
    if(r!=KUI_CMD_OK) {
        dma_broken=true;
        kui_log("GD-ROM DMA read did not complete (%s); DMA stays off until reboot",kui_command_name(r));
        return media_changed||kui_cancelled()?KUI_READ_FATAL:KUI_READ_RETRY;
    }
    return KUI_READ_OK;
}

enum kui_read_result kui_disc_read_probe_dma(void *ctx,uint32_t fad,unsigned sectors,
        const uint8_t **out,struct kui_probe_stats *stats) {
    (void)ctx;
    if(!out||!stats||!sectors||(sectors&1u)||sectors>KUI_OPT_SWEEP_CHUNK_MAX||fad<150||fad>0xffffff||
       sectors>0x1000000u-fad||poisoned||media_changed||dma_broken||kui_cancelled()) return KUI_READ_FATAL;
    if(!set_mode(2352,0)) return KUI_READ_FATAL;
    size_t bytes=(size_t)sectors*KUI_RAW_BYTES;   /* a multiple of 32: sectors is even */
    size_t tail=sizeof(probe_dma_raw.data)-bytes;
    if(tail>KUI_PROBE_GUARD_TAIL) tail=KUI_PROBE_GUARD_TAIL;
    memset(probe_dma_raw.before,0xa5,sizeof(probe_dma_raw.before));
    memset(probe_dma_raw.after,0xa5,sizeof(probe_dma_raw.after));
    memset(probe_dma_raw.data+bytes,0xa5,tail);
    cache_purge((uintptr_t)probe_dma_raw.before,sizeof(probe_dma_raw.before));
    cache_purge((uintptr_t)probe_dma_raw.data+bytes,tail);
    cache_purge((uintptr_t)probe_dma_raw.after,sizeof(probe_dma_raw.after));
    cache_inval((uintptr_t)probe_dma_raw.data,bytes);
    read_params=(cd_read_params_t){.start_sec=fad,.num_sec=sectors,
        .buffer=dma_address(probe_dma_raw.data),.is_test=0};
    probe_stats=stats;probe_service_us=0;
    uint64_t start=timer_us_gettime64();
    bool read_ok=command(CD_CMD_DMAREAD,&read_params,5000);
    uint64_t duration=timer_us_gettime64()-start;
    probe_stats=NULL;
    stats->cmd_us+=duration;stats->last_us=duration;stats->reported_bytes=detail.size;
    cache_inval((uintptr_t)probe_dma_raw.data,bytes);   /* read what the drive wrote, not old lines */
    if(!read_ok && !kui_cancelled()) {
        dma_broken=true;
        kui_log("GD-ROM DMA read did not complete; DMA stays off until reboot");
    }
    /* A failed abort may leave firmware owning this static object. */
    if(poisoned) {dma_broken=true;return KUI_READ_FATAL;}
    bool guard_ok=kui_guard_is(probe_dma_raw.before,32,0xa5)&&kui_guard_is(probe_dma_raw.after,32,0xa5)&&
        kui_guard_is(probe_dma_raw.data+bytes,tail,0xa5);
    if(!guard_ok) {
        poisoned=true;dma_broken=true;
        kui_log("DMA PROBE GUARD CORRUPTION: RESET REQUIRED");return KUI_READ_FATAL;
    }
    if(!read_ok) return media_changed||kui_cancelled()?KUI_READ_FATAL:KUI_READ_RETRY;
    /* detail.size is recorded, not enforced: whether a DMA read reports it is unknown, and
     * the CRC comparison against a PIO read is what proves the data. */
    *out=probe_dma_raw.data;
    return KUI_READ_OK;
}
#endif /* KUI_EXPERIMENTAL_DMA */

void kui_disc_probe(void) {
    kui_log("DISC PROBE: insert a known-good retail GD-ROM first");
    struct kui_toc sessions[2];
    if(!kui_disc_prepare(sessions)) return;
    unsigned passed = 0;
    for(unsigned area = 0; area < 2; ++area) {
        for(unsigned i = 0; i < sessions[area].count; ++i) {
            const struct kui_track *track = &sessions[area].tracks[i];
            uint32_t points[3];
            unsigned count = kui_sample_points(track, points);
            for(unsigned j = 0; j < count; ++j) {
                if(kui_cancelled() || !sample(track, points[j])) {
                    kui_log("DISC PROBE INCOMPLETE: %u samples passed", passed); return;
                }
                ++passed;
            }
        }
    }
    kui_log("DISC PROBE PASS: both TOCs, %u samples", passed);
    kui_log("Samples only; full-disc accuracy and compatibility unproven");
}
