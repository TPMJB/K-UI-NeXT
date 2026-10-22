/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/dreamcast/platform.h"
#include "../src/dreamcast/sci_sd_storage.h"
#include "../src/loader/sci_sd_bus.h"
#include "kui/storage_error.h"
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
static unsigned read_calls,fail_read_call;
static int scif_result;
static bool healthy=true,disc_busy;
semaphore_t _g1_ata_sem;

void kui_log(const char *format,...) {(void)format;}
bool kui_disc_read_pending(void *ctx) {(void)ctx;return disc_busy;}
bool kui_mount(FATFS *fs,kui_log_fn log) {(void)fs;(void)log;return true;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE immediate) {
    (void)fs;(void)path;(void)immediate;return FR_OK;
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
enum kui_loader_sd_result kui_sci_sd_acquire(void) {return KUI_LOADER_SD_OK;}
void kui_sci_sd_release(void) {}
enum kui_loader_sd_result kui_loader_sd_init_bus(struct kui_loader_sd *card,
                                                const struct kui_loader_sd_bus *ops) {
    *card=(struct kui_loader_sd){.bus=*ops,.blocks=10000,.high_capacity=true,
        .ready=init_result==KUI_LOADER_SD_OK,.last_command=41,.last_response=0xff};
    return init_result;
}
void kui_loader_sd_shutdown(struct kui_loader_sd *card) {card->ready=false;}
const char *kui_loader_sd_result_name(enum kui_loader_sd_result result) {(void)result;return "test";}
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

static struct kui_storage_errors snapshot(void) {
    struct kui_storage_errors result;kui_storage_errors_get(&result);return result;
}
int main(void) {
    uint8_t data[130u*512u]={0};
    kui_sd_set_params(KUI_STORAGE_SCI,true);
    assert(kui_sd_connect());
    kui_storage_errors_reset();
    assert(media.read(NULL,10,1,data)==0 && snapshot().total==0);
    assert(media.read(NULL,10,1,NULL)==-1 && snapshot().total==0);
    assert(media.read(NULL,10,0,data)==-1 && snapshot().total==0);
    assert(media.read(NULL,9999,2,data)==-1 && snapshot().total==0);

    read_result=KUI_LOADER_SD_CRC;read_calls=0;fail_read_call=2;
    assert(media.read(NULL,10,130,data)==-1);
    struct kui_storage_errors e=snapshot();
    assert(e.total==1 && e.read_errors==1 && e.crc_errors==1);
    assert(e.last_operation==KUI_STORAGE_ERROR_READ && e.last_result==KUI_STORAGE_ERROR_CRC);
    assert(e.last_transport==KUI_STORAGE_SCI && e.last_lba==138 && e.last_count==2);
    assert(e.sd_detail_valid && e.sd_command==18 && e.sd_response==0);

    write_result=KUI_LOADER_SD_CRC;
    assert(media.write(NULL,55,2,data)==-1);
    e=snapshot();assert(e.total==2 && e.write_errors==1 && e.crc_errors==2);
    assert(e.last_lba==55 && e.last_count==2 && e.sd_command==25 && e.sd_response==0x0b);
    sync_result=KUI_LOADER_SD_COMMAND;
    assert(media.sync(NULL)==-1);
    e=snapshot();assert(e.total==3 && e.sync_errors==1 && e.rejected_errors==1);
    assert(e.last_lba==0 && e.last_count==0 && e.sd_command==13 && e.sd_response==0x20);

    read_calls=0;fail_read_call=0;read_result=KUI_LOADER_SD_TIMEOUT;
    assert(media.read(NULL,2,1,data)==-1 && snapshot().timeout_errors==1);
    read_result=KUI_LOADER_SD_NOT_READY;
    assert(media.read(NULL,2,1,data)==-1 && snapshot().total==4);
    read_result=KUI_LOADER_SD_OK;healthy=false;
    assert(media.read(NULL,2,1,data)==-1);
    e=snapshot();assert(e.total==5 && e.io_errors==1 && !e.sd_detail_valid);
    healthy=true;

    kui_storage_errors_reset();
    e=snapshot();assert(e.total==0 && e.last_operation==KUI_STORAGE_ERROR_NONE && !e.sd_detail_valid);
    assert(kui_storage_active()==KUI_STORAGE_SCI && media.read(NULL,2,1,data)==0);
    kui_storage_errors_get(NULL);
    kui_sd_disconnect();
    init_result=KUI_LOADER_SD_TIMEOUT;
    assert(!kui_sd_connect());
    e=snapshot();assert(e.total==1 && e.init_errors==1 && e.timeout_errors==1);
    assert(e.sd_detail_valid && e.sd_command==41 && e.last_lba==0 && e.last_count==0);

    kui_sd_set_params(KUI_STORAGE_SCIF,true);assert(kui_sd_connect());
    kui_storage_errors_reset();scif_result=-7;
    assert(media.read(NULL,91,1,data)==-7 && media.write(NULL,92,1,data)==-7);
    assert(media.sync(NULL)==-7);
    e=snapshot();assert(e.total==3 && e.io_errors==3 && !e.sd_detail_valid);
    assert(e.read_errors==1 && e.write_errors==1 && e.sync_errors==1);
    assert(e.last_transport==KUI_STORAGE_SCIF && e.sd_command==0 && e.sd_response==0);
    kui_sd_disconnect();
    scif_result=0;kui_sd_set_params(KUI_STORAGE_IDE,true);assert(kui_sd_connect());
    kui_storage_errors_reset();
    disc_busy=true;assert(media.read(NULL,3,1,data)==-1 && snapshot().total==0);
    disc_busy=false;assert(media.write(NULL,3,1,data)==-1 && snapshot().total==0);
    assert(media.read(NULL,3,1,data)==-1);
    e=snapshot();assert(e.total==1 && e.timeout_errors==1 && !e.sd_detail_valid);
    assert(e.last_transport==KUI_STORAGE_IDE && e.last_lba==3 && e.last_count==1);
    kui_sd_disconnect();
    assert(!strcmp(kui_storage_error_operation_name(KUI_STORAGE_ERROR_READ),"read"));
    assert(!strcmp(kui_storage_error_result_name(KUI_STORAGE_ERROR_CRC),"CRC mismatch"));
    return 0;
}
