/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/media.h"
#include "kui/storage_policy.h"
#include "kui/ata.h"
#include "sci_sd_storage.h"
#include "../loader/sci_sd_bus.h"
#include <dc/sd.h>
#include <kos/irq.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <errno.h>

/* One filesystem owner, one selected device per boot. Explicit benchmark
 * connections do not change that identity or fall back to another device. */
static unsigned requested=KUI_STORAGE_AUTO,selected=KUI_STORAGE_AUTO,active=KUI_STORAGE_AUTO;
static bool check_crc=true,connected,sci_detected,write_attempted;
static struct kui_loader_sd sci;
static struct kui_ata ata;
static struct kui_ata_bus ata_bus;
static struct kui_loader_sd_bus sci_bus;
/* Must remain initialized data in the loadable payload, not BSS. */
static volatile struct kui_storage_boot_marker boot_marker=KUI_STORAGE_BOOT_INITIALIZER;
extern semaphore_t _g1_ata_sem;

const char *kui_storage_name(unsigned transport) {
    switch(transport) {
        case KUI_STORAGE_SCIF:return "SCIF SD";
        case KUI_STORAGE_SCI:return "SCI SD";
        case KUI_STORAGE_IDE:return "IDE/CF";
        default:return "automatic storage";
    }
}
unsigned kui_storage_active(void) {return active;}
unsigned kui_sd_active_sci(void) {return active;}
bool kui_storage_sci_reserved(void) {
    /* A recognized SD card with an invalid filesystem still occupies these
     * pins. Do not send W5500 register commands to it after a failed mount. */
    return sci_detected || selected==KUI_STORAGE_SCI || (connected && active==KUI_STORAGE_SCI);
}
void kui_storage_boot_begin(void) {
    unsigned hint=kui_storage_boot_transport(&boot_marker);
    if(hint<KUI_STORAGE_AUTO) selected=hint;
    boot_marker.transport=KUI_STORAGE_AUTO;
    boot_marker.inverse=~(uint32_t)KUI_STORAGE_AUTO;
}
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
    if(active==KUI_STORAGE_SCIF) return sd_read_blocks(block,count,data);
    if(active==KUI_STORAGE_IDE) {
        if(!ata_lock()) return -1;
        bool ok=kui_ata_read(&ata,block,(uint32_t)count,data);
        sem_signal(&_g1_ata_sem);return ok?0:-1;
    }
    if((uint64_t)block+count>sci.blocks) return -1;
    while(count) {
        uint32_t n=count>KUI_LOADER_SD_MAX_READ_BLOCKS?KUI_LOADER_SD_MAX_READ_BLOCKS:(uint32_t)count;
        if(kui_loader_sd_read_multi(&sci,block,n,data)!=KUI_LOADER_SD_OK || !kui_sci_sd_healthy()) return -1;
        count-=n;block+=n;data+=(size_t)n*512;
    }
    return 0;
}
static int write_blocks(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;
    if(!connected || !data || !valid_count(count)) return -1;
    /* A failed request can still have modified earlier sectors. */
    write_attempted=true;
    if(active==KUI_STORAGE_SCIF) return sd_write_blocks(block,count,data);
    if(active==KUI_STORAGE_SCI)
        return kui_sci_sd_write(&sci,block,(uint32_t)count,data) && kui_sci_sd_healthy()?0:-1;
    if(!ata_lock()) return -1;
    bool ok=kui_ata_write(&ata,block,(uint32_t)count,data);
    sem_signal(&_g1_ata_sem);return ok?0:-1;
}
static int sync_card(void *ctx) {
    (void)ctx;
    if(!connected) return -1;
    if(active==KUI_STORAGE_SCI) return kui_sci_sd_sync(&sci) && kui_sci_sd_healthy()?0:-1;
    if(active==KUI_STORAGE_IDE) {
        if(!ata_lock()) return -1;
        bool ok=kui_ata_sync(&ata);sem_signal(&_g1_ata_sem);return ok?0:-1;
    }
    uint8_t sector[512];
    /* KOS's fresh read waits for the preceding write's busy phase. */
    return sd_read_blocks(0,1,sector);
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
    } else if(transport==KUI_STORAGE_SCI) {
        irq_mask_t mask=irq_disable();
        enum kui_loader_sd_result result=kui_sci_sd_acquire();
        irq_restore(mask);
        if(result==KUI_LOADER_SD_OK) {
            sci_bus=*kui_sci_sd_bus();sci_bus.ticks=sci_ticks;sci_bus.end=sci_end;
            result=kui_loader_sd_init_bus(&sci,&sci_bus);
            ok=result==KUI_LOADER_SD_OK && kui_sci_sd_healthy();
            if(ok) sci_detected=true;
            if(!ok) sci_end(NULL);
        }
        if(!ok) kui_log("SCI SD initialization: %s",kui_loader_sd_result_name(result));
    } else if(transport==KUI_STORAGE_IDE && ata_lock()) {
        ata_bus=*kui_ata_native_bus();ata_bus.now_us=now_us;ata_bus.pause=pause_ata;
        ok=kui_ata_init(&ata,&ata_bus);sem_signal(&_g1_ata_sem);
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
        " (12.5 MHz, bounded programmed I/O, CRC checked)":
        active==KUI_STORAGE_IDE?" (G1 slave, bounded PIO)":check_crc?" (CRC checked)":" (read CRC disabled)");
    return true;
}
