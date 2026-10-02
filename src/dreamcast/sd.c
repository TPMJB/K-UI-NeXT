/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/media.h"
#include "kui/storage_policy.h"
#include "kui/storage_error.h"
#include "kui/ata.h"
#include "sci_sd_storage.h"
#include "../loader/sci_sd_bus.h"
#include <dc/sd.h>
#include <kos/irq.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <errno.h>
#ifdef KUI_SCI_ASYNC_RUNTIME
#include "sd.h"
#include <stdio.h>
#include <string.h>
static bool sci_probe_poisoned;
/* Diagnostic observations only; the shared reader's control flow is unchanged. */
static enum kui_sd_async_recovery_phase sci_connect_phase;
static enum kui_loader_sd_result sci_connect_result;
static uint8_t sci_connect_command, sci_connect_response;
static bool sci_connect_healthy;
#endif

/* One filesystem owner, one selected device per boot. Explicit benchmark
 * connections do not change that identity or fall back to another device. */
static unsigned requested=KUI_STORAGE_AUTO,selected=KUI_STORAGE_AUTO,active=KUI_STORAGE_AUTO;
static bool check_crc=true,connected,sci_detected,write_attempted,boot_from_card;
static struct kui_loader_sd sci;
static struct kui_ata ata;
static struct kui_ata_bus ata_bus;
static struct kui_loader_sd_bus sci_bus;
static struct kui_storage_errors storage_errors;
/* Must remain initialized data in the loadable payload, not BSS. */
static volatile struct kui_storage_boot_marker boot_marker=KUI_STORAGE_BOOT_INITIALIZER;
extern semaphore_t _g1_ata_sem;

void kui_storage_errors_reset(void) {storage_errors=(struct kui_storage_errors){0};}
void kui_storage_errors_get(struct kui_storage_errors *out) {if(out) *out=storage_errors;}
static void increment(uint32_t *value) {if(*value<UINT32_MAX) ++*value;}
static int failed(enum kui_storage_error_operation operation,
                  enum kui_storage_error_result result,unsigned transport,
                  uint32_t block,uint32_t count,bool sd_detail) {
    increment(&storage_errors.total);
    switch(operation) {
        case KUI_STORAGE_ERROR_READ:increment(&storage_errors.read_errors);break;
        case KUI_STORAGE_ERROR_WRITE:increment(&storage_errors.write_errors);break;
        case KUI_STORAGE_ERROR_SYNC:increment(&storage_errors.sync_errors);break;
        case KUI_STORAGE_ERROR_INIT:increment(&storage_errors.init_errors);break;
        default:break;
    }
    switch(result) {
        case KUI_STORAGE_ERROR_TIMEOUT:increment(&storage_errors.timeout_errors);break;
        case KUI_STORAGE_ERROR_CRC:increment(&storage_errors.crc_errors);break;
        case KUI_STORAGE_ERROR_REJECTED:increment(&storage_errors.rejected_errors);break;
        default:increment(&storage_errors.io_errors);break;
    }
    storage_errors.last_operation=operation;storage_errors.last_result=result;
    storage_errors.last_transport=transport;
    storage_errors.last_lba=block;storage_errors.last_count=count;
    storage_errors.sd_detail_valid=sd_detail;
    storage_errors.sd_command=sd_detail?sci.last_command:0;
    storage_errors.sd_response=sd_detail?sci.last_response:0;
    return -1;
}
static int sci_failed(enum kui_storage_error_operation operation,
                      enum kui_loader_sd_result result,uint32_t block,uint32_t count) {
    if(result==KUI_LOADER_SD_ARGUMENT || result==KUI_LOADER_SD_NOT_READY ||
       result==KUI_LOADER_SD_RANGE) return -1;
    enum kui_storage_error_result reason=KUI_STORAGE_ERROR_IO;
    if(result==KUI_LOADER_SD_TIMEOUT) reason=KUI_STORAGE_ERROR_TIMEOUT;
    else if(result==KUI_LOADER_SD_CRC) reason=KUI_STORAGE_ERROR_CRC;
    else if(result==KUI_LOADER_SD_COMMAND || result==KUI_LOADER_SD_TOKEN)
        reason=KUI_STORAGE_ERROR_REJECTED;
    /* An OK protocol result plus an unhealthy SCI lease has no trustworthy
     * protocol failure detail. Do not attach the previous command to it. */
    return failed(operation,reason,KUI_STORAGE_SCI,block,count,
                  result!=KUI_LOADER_SD_OK && sci.last_command!=0xff);
}
static int ata_failed(enum kui_storage_error_operation operation,uint32_t block,uint32_t count) {
    if(ata.error==KUI_ATA_INVALID || ata.error==KUI_ATA_RANGE ||
       ata.error==KUI_ATA_BUSY) return -1;
    return failed(operation,ata.error==KUI_ATA_TIMEOUT?KUI_STORAGE_ERROR_TIMEOUT:
        KUI_STORAGE_ERROR_IO,KUI_STORAGE_IDE,block,count,false);
}

const char *kui_storage_name(unsigned transport) {
    switch(transport) {
        case KUI_STORAGE_SCIF:return "SCIF SD";
        case KUI_STORAGE_SCI:return "SCI SD";
        case KUI_STORAGE_IDE:return "IDE/CF";
        default:return "automatic storage";
    }
}
unsigned kui_storage_active(void) {return active;}
unsigned kui_storage_selected(void) {return selected;}
unsigned kui_sd_active_sci(void) {return active;}
bool kui_storage_sci_reserved(void) {
    /* A recognized SD card with an invalid filesystem still occupies these
     * pins. Do not send W5500 register commands to it after a failed mount. */
    return sci_detected || selected==KUI_STORAGE_SCI || (connected && active==KUI_STORAGE_SCI);
}
void kui_storage_boot_begin(void) {
    unsigned hint=kui_storage_boot_transport(&boot_marker);
    boot_from_card=hint<KUI_STORAGE_AUTO;
    if(hint<KUI_STORAGE_AUTO) selected=hint;
    boot_marker.transport=KUI_STORAGE_AUTO;
    boot_marker.inverse=~(uint32_t)KUI_STORAGE_AUTO;
}
bool kui_storage_boot_from_card(void) {return boot_from_card;}
void kui_sd_set_params(unsigned transport,bool crc) {
    if(connected) return;
    requested=transport<=KUI_STORAGE_AUTO?transport:KUI_STORAGE_AUTO;
    check_crc=crc;
}
static uint64_t now_us(void *ctx) {(void)ctx;return timer_us_gettime64();}
static void pause_ata(void *ctx) {(void)ctx;thd_pass();}
static uint32_t sci_ticks(void *ctx) {(void)ctx;return (uint32_t)(timer_us_gettime64()*25u/2u);}
static void sci_end(void *ctx) {
    (void)ctx;irq_mask_t mask=irq_disable();kui_sci_sd_release();irq_restore(mask);
}
static bool ata_lock(void) {
    /* K-UI's BIOS DMA bypasses KOS's semaphore; reject it independently.
     * Captures into IDE complete each GD DMA before starting an ATA write. */
    if(kui_disc_read_pending(NULL)) return false;
    if(sem_wait_timed(&_g1_ata_sem,100)) return false;
    if(kui_disc_read_pending(NULL)) {sem_signal(&_g1_ata_sem);return false;}
    return true;
}
static uint64_t blocks(void *ctx) {
    (void)ctx;
    if(active==KUI_STORAGE_IDE) return ata.ready?ata.sectors:0;
    if(active==KUI_STORAGE_SCI) return sci.ready?sci.blocks:0;
    uint64_t bytes=sd_get_size();return bytes==UINT64_MAX?0:bytes/512;
}
static bool valid_count(size_t count) {
    /* Pointer advancement must fit on SH-4; the protocol count is also
     * 32-bit when this adapter is compiled on a wider host. */
    if(!count || count>SIZE_MAX/512u) return false;
#if SIZE_MAX > UINT32_MAX
    if(count>UINT32_MAX) return false;
#endif
    return true;
}
static int read_blocks(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;
    if(!connected || !data || !valid_count(count)) return -1;
    if(active==KUI_STORAGE_SCIF) {
        int result=sd_read_blocks(block,count,data);
        if(result) (void)failed(KUI_STORAGE_ERROR_READ,KUI_STORAGE_ERROR_IO,
            active,block,(uint32_t)count,false);
        return result;
    }
    if(active==KUI_STORAGE_IDE) {
        if(!ata_lock()) return -1;
        bool ok=kui_ata_read(&ata,block,(uint32_t)count,data);
        sem_signal(&_g1_ata_sem);
        return ok?0:ata_failed(KUI_STORAGE_ERROR_READ,block,(uint32_t)count);
    }
    if((uint64_t)block+count>sci.blocks) return -1;
    while(count) {
        uint32_t n=count>KUI_LOADER_SD_MAX_READ_BLOCKS?KUI_LOADER_SD_MAX_READ_BLOCKS:(uint32_t)count;
        enum kui_loader_sd_result result=kui_loader_sd_read_multi(&sci,block,n,data);
        if(result!=KUI_LOADER_SD_OK || !kui_sci_sd_healthy())
            return sci_failed(KUI_STORAGE_ERROR_READ,result,block,n);
        count-=n;block+=n;data+=(size_t)n*512;
    }
    return 0;
}
static int write_blocks(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;
    if(!connected || !data || !valid_count(count)) return -1;
    /* A failed request can still have modified earlier sectors. */
    write_attempted=true;
    if(active==KUI_STORAGE_SCIF) {
        int result=sd_write_blocks(block,count,data);
        if(result) (void)failed(KUI_STORAGE_ERROR_WRITE,KUI_STORAGE_ERROR_IO,
            active,block,(uint32_t)count,false);
        return result;
    }
    if(active==KUI_STORAGE_SCI) {
        enum kui_loader_sd_result result=kui_sci_sd_write(&sci,block,(uint32_t)count,data);
        return result==KUI_LOADER_SD_OK && kui_sci_sd_healthy()?0:
            sci_failed(KUI_STORAGE_ERROR_WRITE,result,block,(uint32_t)count);
    }
    if(!ata_lock()) return -1;
    bool ok=kui_ata_write(&ata,block,(uint32_t)count,data);
    sem_signal(&_g1_ata_sem);
    return ok?0:ata_failed(KUI_STORAGE_ERROR_WRITE,block,(uint32_t)count);
}
static int sync_card(void *ctx) {
    (void)ctx;
    if(!connected) return -1;
    if(active==KUI_STORAGE_SCI) {
        enum kui_loader_sd_result result=kui_sci_sd_sync(&sci);
        return result==KUI_LOADER_SD_OK && kui_sci_sd_healthy()?0:
            sci_failed(KUI_STORAGE_ERROR_SYNC,result,0,0);
    }
    if(active==KUI_STORAGE_IDE) {
        if(!ata_lock()) return -1;
        bool ok=kui_ata_sync(&ata);sem_signal(&_g1_ata_sem);
        return ok?0:ata_failed(KUI_STORAGE_ERROR_SYNC,0,0);
    }
    uint8_t sector[512];
    /* KOS's fresh read waits for the preceding write's busy phase. */
    int result=sd_read_blocks(0,1,sector);
    if(result) (void)failed(KUI_STORAGE_ERROR_SYNC,KUI_STORAGE_ERROR_IO,active,0,0,false);
    return result;
}
void kui_sd_disconnect(void) {
    if(!connected) return;
    kui_media_set(NULL);
    if(active==KUI_STORAGE_SCIF) sd_shutdown();
    else if(active==KUI_STORAGE_SCI) {kui_loader_sd_shutdown(&sci);sci_end(NULL);}
    else {
        if(write_attempted && ata.ready && sync_card(NULL))
            kui_log("IDE/CF final sync failed; do not remove the device yet");
        kui_ata_shutdown(&ata);
    }
    connected=false;
}
bool kui_sd_raw_read_ops(struct kui_media_ops *out) {
    if(!out) return false;
    *out=(struct kui_media_ops){0};
    if(!connected) return false;
    *out=(struct kui_media_ops){.blocks=blocks,.read=read_blocks};
    return true;
}
static bool open_device(unsigned transport) {
    bool ok=false;
    if(transport==KUI_STORAGE_SCIF) {
        sd_init_params_t params={SD_IF_SCIF,check_crc};
        ok=sd_init_ex(&params)==0;
        if(!ok) (void)failed(KUI_STORAGE_ERROR_INIT,KUI_STORAGE_ERROR_IO,transport,0,0,false);
    } else if(transport==KUI_STORAGE_SCI) {
        irq_mask_t mask=irq_disable();
        enum kui_loader_sd_result result=kui_sci_sd_acquire();
        irq_restore(mask);
#ifdef KUI_SCI_ASYNC_RUNTIME
        sci_connect_phase=KUI_SD_ASYNC_RECOVERY_ACQUIRE;sci_connect_result=result;
        sci_connect_command=sci_connect_response=0xff;sci_connect_healthy=false;
#endif
        if(result==KUI_LOADER_SD_OK) {
            sci_bus=*kui_sci_sd_bus();sci_bus.ticks=sci_ticks;sci_bus.end=sci_end;
            result=kui_loader_sd_init_bus(&sci,&sci_bus);
#ifdef KUI_SCI_ASYNC_RUNTIME
            sci_connect_phase=KUI_SD_ASYNC_RECOVERY_INITIALIZE;sci_connect_result=result;
            sci_connect_command=sci.last_command;sci_connect_response=sci.last_response;
            sci_connect_healthy=kui_sci_sd_healthy();
#endif
            ok=result==KUI_LOADER_SD_OK && kui_sci_sd_healthy();
            if(ok) sci_detected=true;
            if(!ok) {
                (void)sci_failed(KUI_STORAGE_ERROR_INIT,result,0,0);
                sci_end(NULL);
            }
        }
        if(!ok) kui_log("SCI SD initialization: %s",kui_loader_sd_result_name(result));
    } else if(transport==KUI_STORAGE_IDE && ata_lock()) {
        ata_bus=*kui_ata_native_bus();ata_bus.now_us=now_us;ata_bus.pause=pause_ata;
        ok=kui_ata_init(&ata,&ata_bus);sem_signal(&_g1_ata_sem);
        if(!ok) (void)ata_failed(KUI_STORAGE_ERROR_INIT,0,0);
        if(!ok) kui_log("IDE/CF initialization failed: ATA=%u",(unsigned)ata.error);
    }
    if(!ok) return false;
    active=transport;connected=true;write_attempted=false;
    struct kui_media_ops ops={NULL,blocks,read_blocks,write_blocks,sync_card};
    kui_media_set(&ops);
    return true;
}
static bool open_volume(void *ctx,unsigned transport) {
    (void)ctx;
    if(!open_device(transport)) return false;
    FATFS fs;
    bool ok=kui_mount(&fs,kui_log);
    if(f_mount(NULL,"0:",0)!=FR_OK) ok=false;
    if(!ok) kui_sd_disconnect();
    return ok;
}
bool kui_sd_connect(void) {
#ifdef KUI_SCI_ASYNC_RUNTIME
    if(sci_probe_poisoned) {
        kui_log("SCI probe recovery not verified; restart before accessing storage.");
        return false;
    }
#endif
    if(connected) return true;
    bool ok;
    if(requested==KUI_STORAGE_AUTO) {
        const struct kui_storage_probe_ops ops={NULL,open_volume};
        ok=kui_storage_discover(&selected,&ops);
    } else ok=open_device(requested);
    if(!ok) {
        kui_log("Storage unavailable: %s; no device switch after selection",
            kui_storage_name(requested==KUI_STORAGE_AUTO?selected:requested));
        return false;
    }
    kui_log("Storage: %s%s",kui_storage_name(active),active==KUI_STORAGE_SCI?
        " (12.5 MHz, bounded sector DMA with PIO fallback, CRC checked)":
        active==KUI_STORAGE_IDE?" (G1 slave, bounded PIO)":check_crc?" (CRC checked)":" (read CRC disabled)");
    return true;
}

#ifdef KUI_SCI_ASYNC_RUNTIME
const char *kui_sd_async_recovery_name(enum kui_sd_async_recovery_phase phase) {
    switch(phase) {
        case KUI_SD_ASYNC_RECOVERY_ACQUIRE:return "acquire";
        case KUI_SD_ASYNC_RECOVERY_INITIALIZE:return "init";
        case KUI_SD_ASYNC_RECOVERY_READ:return "read";
        default:return "not attempted";
    }
}
static enum kui_sci_async_status async_reason(const struct kui_sci_async_probe_result *p) {
    return p->operation_status==KUI_SCI_ASYNC_OK?p->status:p->operation_status;
}
/* Eight front-of-card sectors exercise ordinary metadata; the remaining
 * samples cover the rest of the addressable card, including its final LBA.
 * Fewer than 16 sectors are supported without duplicates or zero divisors. */
static uint32_t async_sample_lbas(uint64_t blocks,uint32_t lbas[KUI_SD_ASYNC_STRESS_SECTORS]) {
    uint64_t addressable=blocks;
    if(addressable>UINT64_C(0x100000000)) addressable=UINT64_C(0x100000000);
    uint32_t count=addressable<KUI_SD_ASYNC_STRESS_SECTORS?
        (uint32_t)addressable:KUI_SD_ASYNC_STRESS_SECTORS;
    uint32_t front=count<8u?count:8u;
    for(uint32_t i=0;i<front;++i) lbas[i]=i;
    uint32_t spread=count-front;
    for(uint32_t i=0;i<spread;++i)
        lbas[front+i]=spread==1u?8u:8u+(uint32_t)((addressable-9u)*i/(spread-1u));
    return count;
}

static bool async_dma_active(void *ctx) {
    return kui_sci_async_work_sample(ctx)>0;
}

static void async_sustained(uint8_t baseline[KUI_SD_ASYNC_STRESS_SECTORS][512],
        uint8_t verify[512],struct kui_sd_async_result *out,
        bool (*cancelled)(void *),void *cancel_ctx) {
    struct kui_sci_async_reader reader={0};
    enum kui_sci_async_status status=kui_sci_async_open(&reader,&sci,&out->probe);
    if(status!=KUI_SCI_ASYNC_OK) return;
    uint64_t started=timer_us_gettime64();
    bool heartbeat_started=kui_sci_async_heartbeat_start(async_dma_active,&reader);
    if(!heartbeat_started) {
        status=KUI_SCI_ASYNC_BUSY;goto close;
    }
    uint32_t seen=0,work=UINT32_C(0x6b756973);
    for(uint32_t trial=0;trial<KUI_SD_ASYNC_STRESS_READ_LIMIT;++trial) {
        if(cancelled && cancelled(cancel_ctx)) {status=KUI_SCI_ASYNC_CANCELLED;break;}
        if(timer_us_gettime64()-started>=out->target_us) {
            out->duration_complete=true;break;
        }
        /* Alternate ascending and descending passes through the bounded
         * sample set; every sample is used once before any is repeated. */
        uint32_t index=trial%out->baseline_sectors;
        if((trial/out->baseline_sectors)&1u) index=out->baseline_sectors-1u-index;
        uint64_t read_start=timer_us_gettime64();
        status=kui_sci_async_begin(&reader,out->baseline_lbas[index],false);
        bool stopping=false;
        if(status==KUI_SCI_ASYNC_OK) status=KUI_SCI_ASYNC_PENDING;
        while(status==KUI_SCI_ASYNC_PENDING) {
            if(!stopping && cancelled && cancelled(cancel_ctx)) {
                stopping=true;status=kui_sci_async_cancel(&reader);
                if(status!=KUI_SCI_ASYNC_PENDING) break;
            }
            status=kui_sci_async_poll(&reader);++out->poll_calls;
            if(status==KUI_SCI_ASYNC_PENDING) {
                /* Only DMA count progress during this work is credited by
                 * the engine. Completion between sample and record cannot
                 * become a false overlap measurement. */
                uint32_t before=kui_sci_async_work_sample(&reader);
                for(unsigned j=0;j<16u;++j) {
                    work^=work<<13;work^=work>>17;work^=work<<5;
                }
                kui_sci_async_work_record(&reader,before,16u,work);
            }
        }
        if(status==KUI_SCI_ASYNC_OK)
            status=kui_sci_async_finish(&reader,verify,baseline[index]);
        uint64_t elapsed=timer_us_gettime64()-read_start;
        out->read_elapsed_us+=elapsed;
        if(elapsed>out->max_read_us) out->max_read_us=elapsed;
        if(status!=KUI_SCI_ASYNC_OK) break;
        ++out->read_cycles;++out->baseline_reads[index];
        if(!(seen&(UINT32_C(1)<<index))) {
            seen|=UINT32_C(1)<<index;++out->distinct_lbas_verified;
        }
        /* Leave natural timer preemption enabled across a bounded burst.
         * Yielding after every short poll can align DMA windows between
         * scheduler ticks and weaken the interrupt-during-DMA observation.
         * The engine bounds every read and cancellation remains checked in
         * every pending poll; this does not mask IRQs or change timer rates. */
        if((out->read_cycles%64u)==0) {thd_pass();++out->worker_yields;}
    }
    out->stress_elapsed_us=timer_us_gettime64()-started;
    if(status==KUI_SCI_ASYNC_OK && out->stress_elapsed_us>=out->target_us)
        out->duration_complete=true;
    out->iteration_limit=out->read_cycles==KUI_SD_ASYNC_STRESS_READ_LIMIT && !out->duration_complete;
    /* A failed close takes priority over cancellation or a data failure.
     * The wrapper's recovery/poison policy uses the engine's ownership proof. */
close:
    if(heartbeat_started) kui_sci_async_heartbeat_end(&out->heartbeat);
    enum kui_sci_async_status closed=kui_sci_async_close(&reader);
    out->probe.timer_irq_instrumented=out->heartbeat.installed;
    if(closed!=KUI_SCI_ASYNC_OK) status=closed;
    if(out->probe.status!=KUI_SCI_ASYNC_RESTORE) out->probe.status=status;
    if(out->probe.operation_status==KUI_SCI_ASYNC_OK)
        out->probe.operation_status=status;
}

static void async_run(struct kui_sd_async_result *out,
        bool (*cancelled)(void *),void *cancel_ctx,bool sustained) {
    /* Dedicated storage, never a guest destination or the bus DMA buffer.
     * Only the serialized storage worker may call either diagnostic. */
    static uint8_t baseline[KUI_SD_ASYNC_STRESS_SECTORS][512] __attribute__((aligned(32)));
    static uint8_t verify[512] __attribute__((aligned(32)));
    if(!out) return;
    *out=(struct kui_sd_async_result){0};
    out->sustained=sustained;
    out->target_us=sustained?KUI_SD_ASYNC_STRESS_US:0;
    out->recovery_command=out->recovery_response=0xff;
    out->probe.status=KUI_SCI_ASYNC_UNSUPPORTED;
    if(!connected || active!=KUI_STORAGE_SCI || selected!=KUI_STORAGE_SCI ||
       !sci.ready || sci.slow || !sci.blocks || !kui_sci_sd_healthy()) {
        snprintf(out->message,sizeof(out->message),"Requires the boot-selected SCI card; no device switch attempted.");
        return;
    }
    if(cancelled && cancelled(cancel_ctx)) {
        out->probe.status=KUI_SCI_ASYNC_CANCELLED;
        snprintf(out->message,sizeof(out->message),"Stopped before reading the baseline.");
        return;
    }
    if(f_mount(NULL,"0:",0)!=FR_OK) {
        out->probe.status=KUI_SCI_ASYNC_BUSY;
        snprintf(out->message,sizeof(out->message),"Volume could not be unmounted; probe did not start.");
        return;
    }
    uint32_t samples=sustained?async_sample_lbas(sci.blocks,out->baseline_lbas):1u;
    enum kui_loader_sd_result read=KUI_LOADER_SD_OK;
    for(uint32_t i=0;i<samples;++i) {
        /* Read-only CMD17, including ordinary CRC verification, twice before
         * experimenting. No DMA experiment starts with a partial baseline. */
        if(i && cancelled && cancelled(cancel_ctx)) {
            out->probe.status=KUI_SCI_ASYNC_CANCELLED;goto recovery;
        }
        uint32_t lba=out->baseline_lbas[i];
        read=kui_loader_sd_read(&sci,lba,1,baseline[i]);
        if(read==KUI_LOADER_SD_OK) read=kui_loader_sd_read(&sci,lba,1,verify);
        if(read!=KUI_LOADER_SD_OK || !kui_sci_sd_healthy() || memcmp(baseline[i],verify,512)) {
            out->probe.status=KUI_SCI_ASYNC_MISMATCH;
            snprintf(out->message,sizeof(out->message),"Baseline read not verified; no async transfer attempted.");
            goto unsafe;
        }
        ++out->baseline_sectors;
        out->baseline_crcs[i]=kui_crc32(0,baseline[i],512);
        out->baseline_crc32=kui_crc32(out->baseline_crc32,baseline[i],512);
        bool unique=true;
        for(uint32_t j=0;j<i;++j)
            if(!memcmp(baseline[i],baseline[j],512)) {unique=false;break;}
        if(unique) ++out->distinct_payloads;
    }
    out->baseline_verified=true;
    if(sustained) async_sustained(baseline,verify,out,cancelled,cancel_ctx);
    else (void)kui_sci_async_probe_run(&sci,0,baseline[0],cancelled,cancel_ctx,&out->probe);
    if(out->probe.status==KUI_SCI_ASYNC_RESTORE ||
       (out->probe.started && !out->probe.safe_restored)) {
        enum kui_sci_async_status reason=async_reason(&out->probe);
        if(reason==KUI_SCI_ASYNC_OK) reason=KUI_SCI_ASYNC_RESTORE;
        snprintf(out->message,sizeof(out->message),"SCI %s. Storage locked until restart.",
            kui_sci_async_status_name(reason));
        goto unsafe;
    }
    if(out->probe.started && out->probe.status!=KUI_SCI_ASYNC_OK) {
        /* Local register restoration cannot establish the protocol state of
         * an interrupted sector. Discard it, release, then initialize anew. */
        sci.ready=false;kui_sd_disconnect();
        out->recovery_reinitialized=true;
        bool reopened=kui_sd_connect();
        out->recovery_phase=sci_connect_phase;out->recovery_result=sci_connect_result;
        out->recovery_bus_healthy=sci_connect_healthy;
        out->recovery_command=sci_connect_command;out->recovery_response=sci_connect_response;
        out->recovery_command_valid=sci_connect_phase==KUI_SD_ASYNC_RECOVERY_INITIALIZE && sci_connect_command!=0xff;
        if(!reopened) {
            snprintf(out->message,sizeof(out->message),"SCI %s; reinit %s failed. Restart required.",
                kui_sci_async_status_name(async_reason(&out->probe)),
                kui_sd_async_recovery_name(out->recovery_phase));
            goto unsafe;
        }
    }
recovery:
    /* Cancellation never skips the recovery proof. It precedes all report
     * writes and uses the unmodified normal reader with CRC enabled. */
    read=kui_loader_sd_read(&sci,0,1,verify);
    out->recovery_phase=KUI_SD_ASYNC_RECOVERY_READ;out->recovery_result=read;
    out->recovery_command=sci.last_command;out->recovery_response=sci.last_response;
    out->recovery_command_valid=sci.last_command!=0xff;
    out->recovery_bus_healthy=kui_sci_sd_healthy();
    out->recovery_data_match=read==KUI_LOADER_SD_OK && !memcmp(baseline[0],verify,512);
    out->recovery_verified=read==KUI_LOADER_SD_OK && out->recovery_bus_healthy && out->recovery_data_match;
    if(!out->recovery_verified) {
        snprintf(out->message,sizeof(out->message),"SCI %s; normal read recovery failed. Restart required.",
            kui_sci_async_status_name(async_reason(&out->probe)));
        goto unsafe;
    }
    if(out->sustained && (out->heartbeat.ownership_lost ||
       (out->heartbeat.installed && !out->heartbeat.restored)))
        snprintf(out->message,sizeof(out->message),"Timer observer restoration unconfirmed; normal read recovery verified.");
    else if(out->iteration_limit)
        snprintf(out->message,sizeof(out->message),"Read limit reached before 60 seconds; normal read recovery verified.");
    else snprintf(out->message,sizeof(out->message),"%s; normal read recovery verified.",
        kui_sci_async_status_name(out->probe.status));
    return;
unsafe:
    /* No retry, fallback, filesystem write or background reconnect after
     * uncertain cleanup. Restart is required to rebuild ownership safely. */
    out->restart_required=true;
    sci_probe_poisoned=true;sci.ready=false;kui_media_set(NULL);connected=false;
}
void kui_sd_async_probe(struct kui_sd_async_result *out,
        bool (*cancelled)(void *),void *cancel_ctx) {
    async_run(out,cancelled,cancel_ctx,false);
}
void kui_sd_async_stress(struct kui_sd_async_result *out,
        bool (*cancelled)(void *),void *cancel_ctx) {
    async_run(out,cancelled,cancel_ctx,true);
}
#endif
