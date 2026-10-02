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
semaphore_t _g1_ata_sem;

void kui_log(const char *format,...) {(void)format;}
bool kui_disc_read_pending(void *ctx) {(void)ctx;return disc_busy;}
bool kui_mount(FATFS *fs,kui_log_fn log) {(void)fs;(void)log;return true;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE immediate) {
    (void)path;(void)immediate;if(!fs) ++unmounts;return FR_OK;
}
void kui_media_set(const struct kui_media_ops *ops) {media=ops?*ops:(struct kui_media_ops){0};}
irq_mask_t irq_disable(void) {return 0;}
void irq_restore(irq_mask_t state) {(void)state;}
int sem_wait_timed(semaphore_t *sem,unsigned timeout) {(void)sem;(void)timeout;return 0;}
void sem_signal(semaphore_t *sem) {(void)sem;}
void thd_pass(void) {}
uint64_t timer_us_gettime64(void) {return 0;}
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
    *card=(struct kui_loader_sd){.bus=*ops,.blocks=10000,.high_capacity=true,
        .ready=init_result==KUI_LOADER_SD_OK,.last_command=41,.last_response=0xff};
    return init_result;
}
void kui_loader_sd_shutdown(struct kui_loader_sd *card) {card->ready=false;}
const char *kui_loader_sd_result_name(enum kui_loader_sd_result result) {return result==KUI_LOADER_SD_TIMEOUT?"card timeout":"test";}
enum kui_loader_sd_result kui_loader_sd_read_multi(struct kui_loader_sd *card,
    uint32_t block,uint32_t count,void *out) {
    (void)block;(void)count;(void)out;
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
    assert(card->ready && lba==0 && count==1 && out && unmounts);
    ++single_calls;card->last_command=17;card->last_response=0;
    if(single_calls==fail_single) return KUI_LOADER_SD_CRC;
    memset(out,single_calls==mismatch_single?0x82:0x81,512);
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
const char *kui_sci_async_status_name(enum kui_sci_async_status status) {
    return status==KUI_SCI_ASYNC_TIMEOUT?"timeout":status==KUI_SCI_ASYNC_RESTORE?"restore failed":
        status==KUI_SCI_ASYNC_UNSUPPORTED?"unsupported state":"mock result";
}
static bool cancelled(void *ctx) {(void)ctx;return stop;}
static void prepare(void) {
    single_calls=probe_calls=fail_single=mismatch_single=0;
    probe_started=probe_restored=true;probe_status=operation_status=KUI_SCI_ASYNC_OK;
    quarantined=stop=fail_reinit=fail_reacquire=false;init_result=KUI_LOADER_SD_OK;
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
    else assert(!"unknown mode");
    kui_sd_async_probe(&out,cancelled,NULL);
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
    puts("PASS SCI async runtime: CRC baseline, recovery, partial reinit, no-touch rejection, poisoned I/O lock");
    return 0;
}
