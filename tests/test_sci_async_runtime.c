/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/dreamcast/platform.h"
#include "../src/dreamcast/sci_sd_storage.h"
#include "../src/loader/sci_sd_bus.h"
#include "kui/storage_error.h"
#include "../src/dreamcast/sd.h"
#include <stdio.h>
#include "kui/ata.h"
#include <dc/sd.h>
#include <kos/irq.h>
#include <kos/sem.h>
#include <assert.h>
#include <string.h>

static struct kui_media_ops media;
static const struct kui_loader_sd_bus bus={0};
static const struct kui_ata_bus ata_bus={0};
static enum kui_loader_sd_result init_result,read_result,write_result,sync_result;
static unsigned read_calls,fail_read_call,init_calls,single_calls,probe_calls,fail_single,mismatch_single,unmounts;
static enum kui_sci_async_status probe_status,operation_status;
static bool quarantined,fail_reinit,fail_reacquire;
static bool stop,probe_started=true,probe_restored=true;
static int scif_result;
static bool healthy=true,disc_busy;
static bool stress_mode,stress_cancel,heartbeat_available=true,heartbeat_lost;
static bool speed_mode,speed_file;
static unsigned quantum_calls,multi_blocks;
/* CMD18 measurement mocks: calls, injected outcome and one corrupt block. */
static unsigned capture_calls,resume_calls;
static uint32_t capture_lba,resume_lba;
static enum kui_sci_async_status capture_result;
static bool capture_corrupt;
/* Streaming pass mock: blocks left in the current stream and its runs. */
static uint32_t stream_left,stream_runs,stream_corrupt_lba;
static unsigned async_opens,async_begins,async_polls,async_finishes,async_cancels,async_closes;
static unsigned yield_calls,work_calls,cancel_at_poll,cancel_at_baseline;
static uint64_t mock_now,card_blocks=10000;
static uint32_t current_lba,polled,baseline_lba_seen[KUI_SD_ASYNC_STRESS_SECTORS];
static unsigned baseline_seen;
static struct kui_sci_async_probe_result *engine_result;
static bool (*heartbeat_active)(void *);
static void *heartbeat_ctx;
static bool heartbeat_started;
static uint8_t sector_byte(uint32_t lba) {return (uint8_t)(0x81u+(lba^(lba>>8)^(lba>>16)^(lba>>24)));}
semaphore_t _g1_ata_sem;

void kui_log(const char *format,...) {(void)format;}
bool kui_disc_read_pending(void *ctx) {(void)ctx;return disc_busy;}
bool kui_mount(FATFS *fs,kui_log_fn log) {
    (void)log;
    if(speed_file) {fs->database=100;fs->csize=8;}
    return true;
}
FRESULT f_open(FIL *fp,const TCHAR *path,BYTE mode) {
    assert(speed_mode && path && strstr(path,"runtime.kui") && mode==FA_READ);
    if(!speed_file) return FR_NO_FILE;
    fp->obj.sclust=10;return FR_OK;
}
FRESULT f_close(FIL *fp) {(void)fp;return FR_OK;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE immediate) {
    (void)path;(void)immediate;if(!fs) ++unmounts;return FR_OK;
}
void kui_media_set(const struct kui_media_ops *ops) {media=ops?*ops:(struct kui_media_ops){0};}
irq_mask_t irq_disable(void) {return 0;}
void irq_restore(irq_mask_t state) {(void)state;}
int sem_wait_timed(semaphore_t *sem,unsigned timeout) {(void)sem;(void)timeout;return 0;}
void sem_signal(semaphore_t *sem) {(void)sem;}
void thd_pass(void) {++yield_calls;}
uint64_t timer_us_gettime64(void) {return mock_now;}
int sd_init_ex(const sd_init_params_t *params) {assert(params->interface==SD_IF_SCIF);return scif_result;}
void sd_shutdown(void) {}
uint64_t sd_get_size(void) {return 10000u*512u;}
int sd_read_blocks(uint32_t block,size_t count,uint8_t *data) {
    (void)block;(void)count;(void)data;return scif_result;
}
int sd_write_blocks(uint32_t block,size_t count,const uint8_t *data) {
    (void)block;(void)count;(void)data;return scif_result;
}
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) {return &bus;}
bool kui_sci_sd_healthy(void) {return healthy;}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {return fail_reacquire?KUI_LOADER_SD_UNSUPPORTED:KUI_LOADER_SD_OK;}
void kui_sci_sd_release(void) {}
enum kui_loader_sd_result kui_loader_sd_init_bus(struct kui_loader_sd *card,
                                                const struct kui_loader_sd_bus *ops) {
    ++init_calls;
    if(fail_reinit) init_result=KUI_LOADER_SD_TIMEOUT;
    *card=(struct kui_loader_sd){.bus=*ops,.blocks=card_blocks,.high_capacity=true,
        .ready=init_result==KUI_LOADER_SD_OK,.last_command=41,.last_response=0xff};
    return init_result;
}
void kui_loader_sd_shutdown(struct kui_loader_sd *card) {card->ready=false;}
const char *kui_loader_sd_result_name(enum kui_loader_sd_result result) {return result==KUI_LOADER_SD_TIMEOUT?"card timeout":"test";}
enum kui_loader_sd_result kui_loader_sd_read_multi(struct kui_loader_sd *card,
    uint32_t block,uint32_t count,void *out) {
    if(speed_mode) {
        assert(!async_opens && count && count<=128u);
        for(uint32_t i=0;i<count;++i) memset((uint8_t *)out+(size_t)i*512u,sector_byte(block+i),512);
        multi_blocks+=count;
    }
    ++read_calls;card->last_command=18;card->last_response=0;
    return !fail_read_call || read_calls==fail_read_call?read_result:KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_sci_sd_write(struct kui_loader_sd *card,uint32_t block,
                                         uint32_t count,const uint8_t *data) {
    (void)block;(void)count;(void)data;
    card->last_command=25;card->last_response=write_result==KUI_LOADER_SD_CRC?0x0b:0;
    return write_result;
}
enum kui_loader_sd_result kui_sci_sd_sync(struct kui_loader_sd *card) {
    card->last_command=13;card->last_response=0x20;return sync_result;
}
const struct kui_ata_bus *kui_ata_native_bus(void) {return &ata_bus;}
bool kui_ata_init(struct kui_ata *ata,const struct kui_ata_bus *ops) {
    *ata=(struct kui_ata){.bus=ops,.ready=true,.sectors=10000};return true;
}
bool kui_ata_read(struct kui_ata *ata,uint64_t block,uint32_t count,void *data) {
    (void)block;(void)count;(void)data;ata->error=KUI_ATA_TIMEOUT;return false;
}
bool kui_ata_write(struct kui_ata *ata,uint64_t block,uint32_t count,const void *data) {
    (void)block;(void)count;(void)data;ata->error=KUI_ATA_RANGE;return false;
}
bool kui_ata_sync(struct kui_ata *ata) {ata->error=KUI_ATA_OK;return true;}
void kui_ata_shutdown(struct kui_ata *ata) {ata->ready=false;}


enum kui_loader_sd_result kui_loader_sd_read(struct kui_loader_sd *card,
        uint32_t lba,uint32_t count,void *out) {
    assert(card->ready && (uint64_t)lba<card->blocks && count==1 && out && unmounts);
    if(!stress_mode) assert(lba==0);
    if(stress_mode && !async_opens && (single_calls%2u)==0 && baseline_seen<KUI_SD_ASYNC_STRESS_SECTORS)
        baseline_lba_seen[baseline_seen++]=lba;
    ++single_calls;card->last_command=17;card->last_response=0;
    if(single_calls==fail_single) return KUI_LOADER_SD_CRC;
    memset(out,(uint8_t)(sector_byte(lba)+(single_calls==mismatch_single?1u:0u)),512);
    return KUI_LOADER_SD_OK;
}
enum kui_sci_async_status kui_sci_async_probe_run(const struct kui_loader_sd *card,
        uint32_t lba,const uint8_t baseline[512],bool (*cancelled)(void *),void *ctx,
        struct kui_sci_async_probe_result *out) {
    assert(card->ready && lba==0 && single_calls>=2 && baseline[0]==0x81 && baseline[511]==0x81);
    (void)cancelled;(void)ctx;++probe_calls;
    *out=(struct kui_sci_async_probe_result){.status=probe_status,.started=probe_started,
        .operation_status=operation_status,.dma_quarantined=quarantined,.safe_restored=probe_restored};
    return probe_status;
}
enum kui_sci_async_status kui_sci_async_open(struct kui_sci_async_reader *reader,
        const struct kui_loader_sd *card,struct kui_sci_async_probe_result *out) {
    assert((stress_mode || speed_mode) && card->ready && (speed_mode || single_calls==2u*baseline_seen));
    ++async_opens;engine_result=out;
    *out=(struct kui_sci_async_probe_result){.status=probe_status,.started=probe_started,
        .operation_status=operation_status,.dma_quarantined=quarantined,.safe_restored=probe_restored};
    if(probe_status!=KUI_SCI_ASYNC_OK && !probe_started) return probe_status;
    reader->generation=1;return KUI_SCI_ASYNC_OK;
}
enum kui_sci_async_status kui_sci_async_begin(struct kui_sci_async_reader *reader,
        uint32_t lba,bool slow) {
    assert(reader->generation && !slow && !stress_cancel);
    bool found=speed_mode;
    for(unsigned i=0;i<baseline_seen;++i) if(baseline_lba_seen[i]==lba) found=true;
    assert(found);current_lba=lba;polled=0;++async_begins;
    ++engine_result->fast.attempted;++engine_result->fast.dma_started;
    return KUI_SCI_ASYNC_OK;
}
enum kui_sci_async_status kui_sci_async_begin_stream(struct kui_sci_async_reader *reader,
        uint32_t lba,uint32_t count) {
    assert(reader->generation && speed_mode && !stream_left && resume_calls==1);
    assert(count && count<=KUI_LOADER_SD_MAX_READ_BLOCKS && (uint64_t)lba+count<=card_blocks);
    current_lba=lba;stream_left=count;polled=0;++stream_runs;
    return KUI_SCI_ASYNC_OK;
}
enum kui_sci_async_status kui_sci_async_poll(struct kui_sci_async_reader *reader) {
    assert(reader->generation);++async_polls;
    if(probe_status!=KUI_SCI_ASYNC_OK) return probe_status;
    if(++polled<3u) return KUI_SCI_ASYNC_PENDING;
    if(stream_left) ++engine_result->streaming.dma_irqs;
    else ++engine_result->fast.dma_irqs;
    return KUI_SCI_ASYNC_OK;
}
enum kui_sci_async_status kui_sci_async_finish(struct kui_sci_async_reader *reader,
        uint8_t dst[512],const uint8_t expected[512]) {
    assert(reader->generation && polled==3 && dst && (expected || speed_mode));
    if(stream_left) {
        assert(!expected);
        memset(dst,sector_byte(current_lba)^(current_lba==stream_corrupt_lba?1u:0u),512);
        ++engine_result->streaming.passed;++current_lba;--stream_left;polled=0;mock_now+=400;
        return KUI_SCI_ASYNC_OK;
    }
    ++async_finishes;
    if(speed_mode) {
        assert(!expected);memset(dst,sector_byte(current_lba),512);
        ++engine_result->fast.passed;mock_now+=500;return KUI_SCI_ASYNC_OK;
    }
    for(unsigned i=0;i<512;++i) assert(expected[i]==sector_byte(current_lba));
    engine_result->guards_ok=engine_result->crc_ok=engine_result->baseline_ok=true;
    engine_result->baseline_checked=true;
    mock_now+=UINT64_C(500000);
    if(stress_cancel) return KUI_SCI_ASYNC_CANCELLED;
    memcpy(dst,expected,512);++engine_result->fast.passed;
    return KUI_SCI_ASYNC_OK;
}
enum kui_sci_async_status kui_sci_async_cancel(struct kui_sci_async_reader *reader) {
    assert(reader->generation);++async_cancels;stress_cancel=true;
    return KUI_SCI_ASYNC_PENDING;
}
enum kui_sci_async_status kui_sci_async_close(struct kui_sci_async_reader *reader) {
    assert(reader->generation);++async_closes;reader->generation=0;
    engine_result->safe_restored=probe_restored;
    engine_result->handlers_restored=engine_result->registers_restored=probe_restored;
    return probe_restored?KUI_SCI_ASYNC_OK:KUI_SCI_ASYNC_RESTORE;
}
enum kui_sci_async_status kui_sci_async_stream_capture(struct kui_sci_async_reader *reader,
        uint32_t lba,void *buffer,uint32_t bytes,struct kui_sci_async_stream *out) {
    assert(reader->generation && speed_mode && buffer && !((uintptr_t)buffer&31u));
    assert(bytes==KUI_SD_ASYNC_CAPTURE_BYTES && async_finishes>=KUI_SD_ASYNC_COMPARE_BLOCKS);
    ++capture_calls;capture_lba=lba;
    *out=(struct kui_sci_async_stream){.status=capture_result,.lba=lba,.bytes=bytes,
        .received=bytes,.complete=1,.blocks=30,.gaps=30,.gap_min=3,.gap_max=9,.gap_total=150};
    for(uint32_t i=0;i<out->blocks;++i)
        memset((uint8_t *)buffer+(size_t)i*512u,sector_byte(lba+i),512);
    if(capture_corrupt) ((uint8_t *)buffer)[512*7+100]^=1u;
    if(capture_result!=KUI_SCI_ASYNC_OK) {
        engine_result->status=engine_result->operation_status=capture_result;
        probe_status=capture_result;
    }
    return capture_result;
}
enum kui_sci_async_status kui_sci_async_stream_resume(struct kui_sci_async_reader *reader,
        uint32_t lba,uint32_t count,uint8_t *dst,struct kui_sci_async_resume *out) {
    assert(reader->generation && speed_mode && capture_calls && dst);
    assert(count==KUI_SD_ASYNC_RESUME_BLOCKS);
    ++resume_calls;resume_lba=lba;
    *out=(struct kui_sci_async_resume){.status=KUI_SCI_ASYNC_OK,.lba=lba,.requested=count,.blocks=count};
    for(uint32_t i=0;i<count;++i) memset(dst+(size_t)i*512u,sector_byte(lba+i),512);
    return KUI_SCI_ASYNC_OK;
}
void kui_sci_async_set_framing_quantum(struct kui_sci_async_reader *reader,unsigned bytes) {
    assert(reader->generation && speed_mode && bytes==4096u);++quantum_calls;
}
uint32_t kui_sci_async_work_sample(struct kui_sci_async_reader *reader) {
    assert(reader->generation);return polled && polled<3u?256u:0;
}
void kui_sci_async_work_record(struct kui_sci_async_reader *reader,uint32_t before,
        uint32_t iterations,uint32_t checksum) {
    assert(reader->generation && iterations==16u && checksum);++work_calls;
    if(before) {++engine_result->fast.overlap_batches;engine_result->fast.overlap_iterations+=iterations;}
    if(heartbeat_started) assert(heartbeat_active(heartbeat_ctx)==(before>0));
}
bool kui_sci_async_heartbeat_start(bool (*active)(void *),void *ctx) {
    assert(async_opens && !async_begins);heartbeat_active=active;heartbeat_ctx=ctx;
    heartbeat_started=heartbeat_available;return heartbeat_available;
}
static void heartbeat_snapshot(struct kui_sci_async_heartbeat_result *out) {
    assert(!async_closes);
    *out=(struct kui_sci_async_heartbeat_result){.installed=heartbeat_started,
        .restored=!heartbeat_lost,.ownership_lost=heartbeat_lost,
        .total_ticks=async_begins,.dma_ticks=work_calls,.max_gap_us=10000};
}
void kui_sci_async_heartbeat_end(struct kui_sci_async_heartbeat_result *out) {
    assert(heartbeat_started);heartbeat_snapshot(out);heartbeat_started=false;
}
const char *kui_sci_async_status_name(enum kui_sci_async_status status) {
    return status==KUI_SCI_ASYNC_TIMEOUT?"timeout":status==KUI_SCI_ASYNC_RESTORE?"restore failed":
        status==KUI_SCI_ASYNC_UNSUPPORTED?"unsupported state":"mock result";
}
static bool cancelled(void *ctx) {
    (void)ctx;return stop || (cancel_at_poll && async_polls>=cancel_at_poll) ||
        (cancel_at_baseline && single_calls>=cancel_at_baseline);
}
static void prepare(void) {
    single_calls=probe_calls=fail_single=mismatch_single=0;
    probe_started=probe_restored=true;probe_status=operation_status=KUI_SCI_ASYNC_OK;
    quarantined=stop=fail_reinit=fail_reacquire=false;init_result=KUI_LOADER_SD_OK;
    stress_mode=stress_cancel=heartbeat_lost=heartbeat_started=false;
    heartbeat_available=true;async_opens=async_begins=async_polls=async_finishes=async_cancels=async_closes=0;
    yield_calls=work_calls=cancel_at_poll=cancel_at_baseline=baseline_seen=0;mock_now=0;
    speed_mode=speed_file=false;quantum_calls=multi_blocks=read_calls=0;
    capture_calls=resume_calls=capture_lba=resume_lba=0;
    capture_result=KUI_SCI_ASYNC_OK;capture_corrupt=false;
    stream_left=stream_runs=stream_corrupt_lba=0;
}
/* The same blocks through both readers: the file's first cluster when found,
 * otherwise the data area. Matching CRCs are required for a pass. */
static void test_speed(void) {
    struct kui_sd_async_result out;
    prepare();speed_mode=speed_file=true;kui_sd_async_speed(&out,cancelled,NULL);
    assert(out.speed && !out.sustained && out.speed_file_found);
    assert(out.speed_lba==164 && out.speed_blocks==KUI_SD_ASYNC_SPEED_BLOCKS);
    assert(out.speed_match && out.speed_async_blocks==KUI_SD_ASYNC_SPEED_BLOCKS);
    assert(out.speed_normal_crc==out.speed_async_crc && multi_blocks==KUI_SD_ASYNC_SPEED_BLOCKS);
    assert(read_calls==KUI_SD_ASYNC_SPEED_BLOCKS/128u && quantum_calls==1);
    assert(async_opens==1 && async_begins==KUI_SD_ASYNC_SPEED_BLOCKS && async_closes==1);
    assert(out.speed_async_us==UINT64_C(500)*KUI_SD_ASYNC_SPEED_BLOCKS);
    assert(out.probe.status==KUI_SCI_ASYNC_OK && out.recovery_verified && !out.restart_required);
    assert(single_calls==3 && !probe_calls && !out.recovery_reinitialized);
    /* CMD18 measurements on the same, already verified blocks. */
    assert(out.stream_ran && out.resume_ran && out.stream_match && out.resume_match);
    assert(capture_calls==1 && resume_calls==1 && capture_lba==164);
    assert(resume_lba==164+KUI_SD_ASYNC_RESUME_OFFSET && out.stream.blocks==30);
    assert(out.resume.blocks==KUI_SD_ASYNC_RESUME_BLOCKS);
    /* Then the same 2048 blocks as CMD18 streams of 128, matching the
     * ordinary pass's CRC32. */
    assert(out.speed_stream_match && out.speed_stream_blocks==KUI_SD_ASYNC_SPEED_BLOCKS);
    assert(out.speed_stream_crc==out.speed_normal_crc && stream_runs==KUI_SD_ASYNC_SPEED_BLOCKS/128u);
    assert(out.speed_stream_us==UINT64_C(400)*KUI_SD_ASYNC_SPEED_BLOCKS && !stream_left);

    /* A streamed block that differs from the ordinary pass is reported. */
    prepare();speed_mode=speed_file=true;stream_corrupt_lba=164+700;
    kui_sd_async_speed(&out,cancelled,NULL);
    assert(out.speed_match && !out.speed_stream_match && out.speed_stream_blocks==KUI_SD_ASYNC_SPEED_BLOCKS);
    assert(out.probe.status==KUI_SCI_ASYNC_OK && out.recovery_verified);

    /* A captured block that differs from the async copy is reported. */
    prepare();speed_mode=speed_file=true;capture_corrupt=true;
    kui_sd_async_speed(&out,cancelled,NULL);
    assert(out.speed_match && out.stream_ran && !out.stream_match && out.resume_match);
    assert(out.probe.status==KUI_SCI_ASYNC_OK && out.recovery_verified);

    /* A failed measurement fails the run and goes through reinitialization;
     * the resume pass is not attempted after it. */
    prepare();speed_mode=speed_file=true;capture_result=KUI_SCI_ASYNC_COMMAND;
    kui_sd_async_speed(&out,cancelled,NULL);
    assert(out.speed_match && out.stream_ran && !out.resume_ran && !resume_calls && !stream_runs);
    assert(out.probe.status==KUI_SCI_ASYNC_COMMAND && out.recovery_reinitialized && out.recovery_verified);
    kui_sd_disconnect();prepare();assert(kui_sd_connect());

    prepare();speed_mode=true;kui_sd_async_speed(&out,cancelled,NULL);
    assert(!out.speed_file_found && out.speed_lba==0 && out.speed_match && out.recovery_verified);

    prepare();speed_mode=true;card_blocks=1000;
    kui_sd_disconnect();assert(kui_sd_connect());
    speed_file=true;kui_sd_async_speed(&out,cancelled,NULL);
    /* A range past the card's end is never read; fall back to block 0. */
    assert(!out.speed_file_found && out.speed_lba==0 && out.speed_blocks==1000 && out.recovery_verified);
    assert(out.speed_async_blocks==1000 && out.speed_match);
    kui_sd_disconnect();card_blocks=10000;prepare();assert(kui_sd_connect());
}
static void stress_prepare(void) {prepare();stress_mode=true;}
static void test_stress(void) {
    struct kui_sd_async_result out;
    stress_prepare();kui_sd_async_stress(&out,cancelled,NULL);
    assert(out.sustained && out.duration_complete && !out.iteration_limit);
    assert(out.baseline_verified && out.recovery_verified && !out.restart_required);
    assert(out.baseline_sectors==16 && out.distinct_lbas_verified==16 && out.distinct_payloads>1);
    assert(out.read_cycles==120 && out.poll_calls==360 && out.worker_yields==1);
    assert(async_opens==1 && async_begins==120 && async_finishes==120 && async_closes==1);
    assert(yield_calls==1 && single_calls==33 && !probe_calls);
    assert(out.stress_elapsed_us==KUI_SD_ASYNC_STRESS_US && out.read_elapsed_us==KUI_SD_ASYNC_STRESS_US);
    assert(out.max_read_us==500000 && out.target_us==KUI_SD_ASYNC_STRESS_US);
    assert(out.heartbeat.installed && out.heartbeat.restored && out.probe.timer_irq_instrumented);
    unsigned reads=0;
    for(unsigned i=0;i<16;++i) {
        if(i<8) assert(out.baseline_lbas[i]==i);
        if(i) assert(out.baseline_lbas[i]>out.baseline_lbas[i-1]);
        reads+=out.baseline_reads[i];
    }
    assert(reads==120 && out.baseline_lbas[15]==9999 && !out.recovery_reinitialized);

    stress_prepare();cancel_at_poll=1;kui_sd_async_stress(&out,cancelled,NULL);
    assert(out.probe.status==KUI_SCI_ASYNC_CANCELLED && out.recovery_verified && !out.restart_required);
    assert(async_cancels==1 && async_finishes==1 && async_closes==1 && !out.read_cycles);
    assert(out.recovery_reinitialized && out.worker_yields==0);

    stress_prepare();cancel_at_baseline=4;kui_sd_async_stress(&out,cancelled,NULL);
    assert(out.probe.status==KUI_SCI_ASYNC_CANCELLED && out.recovery_verified && !out.restart_required);
    assert(!async_opens && single_calls==5 && out.baseline_sectors==2 && !out.baseline_verified);

    stress_prepare();probe_status=KUI_SCI_ASYNC_BUSY;probe_started=probe_restored=false;
    kui_sd_async_stress(&out,cancelled,NULL);
    assert(out.probe.status==KUI_SCI_ASYNC_BUSY && out.recovery_verified && !out.restart_required);
    assert(async_opens==1 && !async_begins && !async_closes && single_calls==33);

    stress_prepare();heartbeat_available=false;kui_sd_async_stress(&out,cancelled,NULL);
    assert(out.probe.status==KUI_SCI_ASYNC_BUSY && out.recovery_verified && !out.restart_required);
    assert(async_opens==1 && !async_begins && async_closes==1);
    assert(!out.heartbeat.installed && !out.probe.timer_irq_instrumented);

    stress_prepare();heartbeat_lost=true;kui_sd_async_stress(&out,cancelled,NULL);
    assert(out.recovery_verified && !out.restart_required && !out.heartbeat.restored);
    assert(out.heartbeat.ownership_lost && out.probe.safe_restored && out.probe.handlers_restored);
    assert(strstr(out.message,"Timer observer restoration unconfirmed") && single_calls==33);

    /* Exercise compact cards and the inclusive UINT32_MAX final LBA without
     * allocating by capacity or letting any sample wrap around. */
    static const uint64_t capacities[]={1,8,9,16,UINT64_C(0x100000000),UINT64_MAX};
    for(unsigned c=0;c<sizeof(capacities)/sizeof(capacities[0]);++c) {
        kui_sd_disconnect();card_blocks=capacities[c];stress_prepare();assert(kui_sd_connect());
        kui_sd_async_stress(&out,cancelled,NULL);
        uint32_t count=card_blocks<16?(uint32_t)card_blocks:16u;
        assert(out.baseline_sectors==count && out.distinct_lbas_verified==count);
        assert(out.duration_complete && out.recovery_verified && !out.restart_required);
        uint32_t last=card_blocks>UINT32_MAX?UINT32_MAX:(uint32_t)card_blocks-1u;
        assert(out.baseline_lbas[count-1u]==last);
        for(unsigned i=1;i<count;++i) assert(out.baseline_lbas[i]>out.baseline_lbas[i-1]);
    }
    kui_sd_disconnect();card_blocks=10000;prepare();assert(kui_sd_connect());
}
int main(int argc,char **argv) {
    /* Discover SCI once, then ensure wrapper/recovery never falls back. */
    scif_result=-1;kui_sd_set_params(KUI_STORAGE_AUTO,true);
    assert(kui_sd_connect() && kui_storage_selected()==KUI_STORAGE_SCI);
    struct kui_sd_async_result out;
    prepare();kui_sd_async_probe(&out,cancelled,NULL);
    assert(out.baseline_verified && out.recovery_verified && !out.restart_required);
    assert(!out.recovery_reinitialized && single_calls==3 && probe_calls==1);
    assert(media.read && media.write);

    prepare();probe_status=KUI_SCI_ASYNC_TIMEOUT;
    unsigned initialized=init_calls;
    kui_sd_async_probe(&out,cancelled,NULL);
    assert(out.recovery_reinitialized && out.recovery_verified && !out.restart_required);
    assert(init_calls==initialized+1 && single_calls==3 && probe_calls==1);

    prepare();probe_status=KUI_SCI_ASYNC_BUSY;probe_started=probe_restored=false;
    initialized=init_calls;kui_sd_async_probe(&out,cancelled,NULL);
    assert(!out.probe.started && out.recovery_verified && !out.restart_required);
    assert(init_calls==initialized && single_calls==3);

    prepare();stop=true;kui_sd_async_probe(&out,cancelled,NULL);
    assert(out.probe.status==KUI_SCI_ASYNC_CANCELLED && !out.restart_required);
    assert(!single_calls && !probe_calls);
    test_stress();
    test_speed();
    prepare();
    const char *mode=argc>1?argv[1]:"restore";
    if(!strcmp(mode,"restore")) {probe_status=KUI_SCI_ASYNC_RESTORE;probe_restored=false;probe_started=false;}
    else if(!strcmp(mode,"quarantine")) {
        probe_status=KUI_SCI_ASYNC_RESTORE;operation_status=KUI_SCI_ASYNC_TIMEOUT;
        quarantined=true;probe_restored=false;
    }
    else if(!strcmp(mode,"reinit") || !strcmp(mode,"reacquire")) {
        probe_status=operation_status=KUI_SCI_ASYNC_UNSUPPORTED;
        fail_reinit=!strcmp(mode,"reinit");fail_reacquire=!strcmp(mode,"reacquire");
    }
    else if(!strcmp(mode,"recovery")) fail_single=3;
    else if(!strcmp(mode,"baseline")) fail_single=1;
    else if(!strcmp(mode,"baseline-mismatch")) mismatch_single=2;
    else if(!strcmp(mode,"stress-quarantine")) {
        stress_mode=true;probe_status=KUI_SCI_ASYNC_RESTORE;operation_status=KUI_SCI_ASYNC_TIMEOUT;
        quarantined=true;probe_restored=false;
    }
    else if(!strcmp(mode,"stress-recovery")) {stress_mode=true;fail_single=33;}
    else if(!strcmp(mode,"stress-baseline")) {stress_mode=true;fail_single=17;}
    else if(!strcmp(mode,"stress-baseline-mismatch")) {stress_mode=true;mismatch_single=18;}
    else assert(!"unknown mode");
    if(stress_mode) kui_sd_async_stress(&out,cancelled,NULL);
    else kui_sd_async_probe(&out,cancelled,NULL);
    assert(out.restart_required && !out.recovery_verified);
    assert(!media.read && !media.write && !kui_sd_connect());
    if(!strcmp(mode,"restore") || !strcmp(mode,"quarantine") || !strcmp(mode,"reinit") || !strcmp(mode,"reacquire"))
        assert(single_calls==2 && probe_calls==1);
    if(!strcmp(mode,"reinit") || !strcmp(mode,"reacquire")) {
        assert(out.probe.operation_status==KUI_SCI_ASYNC_UNSUPPORTED && out.recovery_reinitialized);
        assert(strstr(out.message,"unsupported state") && strstr(out.message,"Restart required"));
        if(fail_reinit) {
            assert(out.recovery_phase==KUI_SD_ASYNC_RECOVERY_INITIALIZE);
            assert(out.recovery_result==KUI_LOADER_SD_TIMEOUT && out.recovery_command_valid);
            assert(out.recovery_command==41 && out.recovery_response==0xff);
            assert(strstr(out.message,"reinit init failed"));
        } else {
            assert(out.recovery_phase==KUI_SD_ASYNC_RECOVERY_ACQUIRE);
            assert(out.recovery_result==KUI_LOADER_SD_UNSUPPORTED && !out.recovery_command_valid);
            assert(out.recovery_command==0xff && out.recovery_response==0xff);
            assert(strstr(out.message,"reinit acquire failed"));
        }
    }
    if(!strcmp(mode,"quarantine")) assert(strstr(out.message,"timeout") && strstr(out.message,"restart"));
    if(!strcmp(mode,"restore")) assert(strstr(out.message,"restore failed"));
    if(!strcmp(mode,"baseline") || !strcmp(mode,"baseline-mismatch")) assert(!probe_calls);
    if(!strcmp(mode,"stress-quarantine"))
        assert(async_closes==1 && single_calls==32 && !out.recovery_verified && out.probe.dma_quarantined);
    if(!strcmp(mode,"stress-baseline") || !strcmp(mode,"stress-baseline-mismatch"))
        assert(!async_opens && !out.baseline_verified);
    puts("PASS SCI async runtime: CRC baseline, recovery, partial reinit, no-touch rejection, poisoned I/O lock");
    return 0;
}
