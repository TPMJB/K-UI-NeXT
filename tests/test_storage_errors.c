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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static struct kui_media_ops media;
static const struct kui_loader_sd_bus bus={0};
static const struct kui_ata_bus ata_bus={0};
static enum kui_loader_sd_result init_result,read_result,write_result,sync_result;
static unsigned read_calls,fail_read_call,single_read_calls,multi_read_calls;
static struct {
    uint32_t block,count;
    void *out;
    unsigned command;
} read_requests[4];
static int scif_result;
static bool healthy=true,disc_busy;
static struct kui_sci_sd_fault first_fault;
static char log_text[8192];
static size_t log_used;
semaphore_t _g1_ata_sem;

void kui_log(const char *format,...) {
    va_list args;va_start(args,format);
    int written=vsnprintf(log_text+log_used,sizeof(log_text)-log_used,format,args);
    va_end(args);
    assert(written>=0 && (size_t)written+1u<sizeof(log_text)-log_used);
    log_used+=(size_t)written;log_text[log_used++]='\n';log_text[log_used]=0;
}
static void clear_log(void) {log_used=0;log_text[0]=0;}
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
void kui_sci_sd_fault_get(struct kui_sci_sd_fault *out) {*out=first_fault;}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {return KUI_LOADER_SD_OK;}
void kui_sci_sd_release(void) {}
enum kui_loader_sd_result kui_loader_sd_init_bus(struct kui_loader_sd *card,
                                                const struct kui_loader_sd_bus *ops) {
    *card=(struct kui_loader_sd){.bus=*ops,.blocks=10000,.high_capacity=true,
        .ready=init_result==KUI_LOADER_SD_OK,.last_command=41,.last_response=0xff};
    return init_result;
}
void kui_loader_sd_shutdown(struct kui_loader_sd *card) {card->ready=false;}
const char *kui_loader_sd_result_name(enum kui_loader_sd_result result) {
    switch(result) {
        case KUI_LOADER_SD_OK:return "OK";
        case KUI_LOADER_SD_TIMEOUT:return "card timeout";
        case KUI_LOADER_SD_COMMAND:return "SD command rejected";
        case KUI_LOADER_SD_CRC:return "SD CRC16 mismatch";
        default:return "test";
    }
}
static uint8_t block_pattern(uint32_t block) {return (uint8_t)(block^(block>>8)^0x81u);}
static enum kui_loader_sd_result read_request(struct kui_loader_sd *card,
    uint32_t block,uint32_t count,void *out,unsigned command) {
    if(read_calls<sizeof(read_requests)/sizeof(read_requests[0])) {
        read_requests[read_calls].block=block;read_requests[read_calls].count=count;
        read_requests[read_calls].out=out;read_requests[read_calls].command=command;
    }
    ++read_calls;card->last_command=(uint8_t)command;card->last_response=0;
    enum kui_loader_sd_result result=!fail_read_call || read_calls==fail_read_call?
        read_result:KUI_LOADER_SD_OK;
    if(result==KUI_LOADER_SD_OK)
        for(uint32_t i=0;i<count;++i)
            memset((uint8_t *)out+(size_t)i*512u,block_pattern(block+i),512);
    return result;
}
enum kui_loader_sd_result kui_loader_sd_read(struct kui_loader_sd *card,
    uint32_t block,uint32_t count,void *out) {
    assert(count==1);++single_read_calls;
    return read_request(card,block,count,out,17);
}
enum kui_loader_sd_result kui_loader_sd_read_multi(struct kui_loader_sd *card,
    uint32_t block,uint32_t count,void *out) {
    assert(count>1 && count<=KUI_LOADER_SD_MAX_READ_BLOCKS);++multi_read_calls;
    return read_request(card,block,count,out,18);
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
static void reset_reads(void) {
    read_calls=single_read_calls=multi_read_calls=fail_read_call=0;
    read_result=KUI_LOADER_SD_OK;
    memset(read_requests,0,sizeof(read_requests));
}
static void check_blocks(const uint8_t *data,uint32_t block,uint32_t count) {
    for(uint32_t i=0;i<count;++i)
        for(unsigned j=0;j<512;++j) assert(data[(size_t)i*512u+j]==block_pattern(block+i));
}
static void test_read_policy(uint8_t data[130u*512u]) {
    reset_reads();
    assert(media.read(NULL,17,1,data)==0);
    assert(read_calls==1 && single_read_calls==1 && multi_read_calls==0);
    assert(read_requests[0].command==17 && read_requests[0].block==17 &&
        read_requests[0].count==1 && read_requests[0].out==data);
    check_blocks(data,17,1);

    reset_reads();
    assert(media.read(NULL,29,2,data)==0);
    assert(read_calls==1 && !single_read_calls && multi_read_calls==1);
    assert(read_requests[0].command==18 && read_requests[0].count==2);
    check_blocks(data,29,2);

    reset_reads();
    assert(media.read(NULL,41,129,data)==0);
    assert(read_calls==2 && single_read_calls==1 && multi_read_calls==1);
    assert(read_requests[0].command==18 && read_requests[0].block==41 &&
        read_requests[0].count==128 && read_requests[0].out==data);
    assert(read_requests[1].command==17 && read_requests[1].block==169 &&
        read_requests[1].count==1 && read_requests[1].out==data+128u*512u);
    check_blocks(data,41,129);

    reset_reads();read_result=KUI_LOADER_SD_TIMEOUT;fail_read_call=1;
    memset(data,0xa5,130u*512u);
    assert(media.read(NULL,10,129,data)==-1);
    assert(read_calls==1 && !single_read_calls && multi_read_calls==1);
    struct kui_storage_errors e=snapshot();
    assert(e.last_lba==10 && e.last_count==128 && e.sd_command==18 &&
        e.last_result==KUI_STORAGE_ERROR_TIMEOUT);
    for(size_t i=0;i<130u*512u;++i) assert(data[i]==0xa5);

    reset_reads();read_result=KUI_LOADER_SD_CRC;fail_read_call=2;
    assert(media.read(NULL,10,129,data)==-1);
    assert(read_calls==2 && single_read_calls==1 && multi_read_calls==1);
    e=snapshot();assert(e.last_lba==138 && e.last_count==1 && e.sd_command==17 &&
        e.last_result==KUI_STORAGE_ERROR_CRC);
    check_blocks(data,10,128);
    for(size_t i=128u*512u;i<130u*512u;++i) assert(data[i]==0xa5);

    reset_reads();read_result=KUI_LOADER_SD_TIMEOUT;fail_read_call=1;
    assert(media.read(NULL,77,1,data)==-1);
    assert(read_calls==1 && single_read_calls==1 && !multi_read_calls);
    e=snapshot();assert(e.last_lba==77 && e.last_count==1 && e.sd_command==17 &&
        e.last_result==KUI_STORAGE_ERROR_TIMEOUT);
    /* Failure is returned once; it never silently retries via CMD18. */
    reset_reads();kui_storage_errors_reset();clear_log();
}
int main(void) {
    uint8_t data[130u*512u]={0};
    kui_sd_set_params(KUI_STORAGE_SCI,true);
    assert(kui_sd_connect());
    assert(strstr(log_text,"SCI filesystem reads: CMD17 for one block; CMD18 for multiple blocks"));
    size_t connected_log_size=log_used;
    assert(kui_sd_connect() && log_used==connected_log_size);
    test_read_policy(data);
    kui_storage_errors_reset();
    clear_log();
    assert(media.read(NULL,10,1,data)==0 && snapshot().total==0);
    assert(media.read(NULL,10,1,NULL)==-1 && snapshot().total==0);
    assert(media.read(NULL,10,0,data)==-1 && snapshot().total==0);
    assert(media.read(NULL,9999,2,data)==-1 && snapshot().total==0);
    assert(!log_text[0]);

    read_result=KUI_LOADER_SD_CRC;read_calls=0;fail_read_call=2;
    assert(media.read(NULL,10,130,data)==-1);
    struct kui_storage_errors e=snapshot();
    assert(e.total==1 && e.read_errors==1 && e.crc_errors==1);
    assert(e.last_operation==KUI_STORAGE_ERROR_READ && e.last_result==KUI_STORAGE_ERROR_CRC);
    assert(e.last_transport==KUI_STORAGE_SCI && e.last_lba==138 && e.last_count==2);
    assert(e.sd_detail_valid && e.sd_command==18 && e.sd_response==0);
    assert(strstr(log_text,"operation=read (1) result=CRC mismatch (3)"));
    assert(strstr(log_text,"protocol=SD CRC16 mismatch (6) LBA=138 blocks=2"));
    assert(strstr(log_text,"ready=1 healthy=1 SD_detail=1 first_flag_fault=0"));
    assert(strstr(log_text,"CMD18 response=00"));

    clear_log();
    write_result=KUI_LOADER_SD_CRC;
    assert(media.write(NULL,55,2,data)==-1);
    e=snapshot();assert(e.total==2 && e.write_errors==1 && e.crc_errors==2);
    assert(e.last_lba==55 && e.last_count==2 && e.sd_command==25 && e.sd_response==0x0b);
    assert(strstr(log_text,"operation=write (2) result=CRC mismatch (3)"));
    assert(strstr(log_text,"LBA=55 blocks=2") && strstr(log_text,"CMD25 response=0b"));
    clear_log();
    sync_result=KUI_LOADER_SD_COMMAND;
    assert(media.sync(NULL)==-1);
    e=snapshot();assert(e.total==3 && e.sync_errors==1 && e.rejected_errors==1);
    assert(e.last_lba==0 && e.last_count==0 && e.sd_command==13 && e.sd_response==0x20);
    assert(strstr(log_text,"operation=sync (3) result=card rejected request (4)"));
    assert(strstr(log_text,"protocol=SD command rejected (4) LBA=0 blocks=0"));
    assert(strstr(log_text,"CMD13 response=20"));

    clear_log();
    healthy=false;first_fault=(struct kui_sci_sd_fault){.valid=1,.wait_flag=0x40,
        .ssr=0x20,.scr=0x30,.smr=0x80,.brr=0,.scmr=0,.sptr=0x60,.pdtr=0x80,.polls=37};
    read_calls=0;fail_read_call=0;read_result=KUI_LOADER_SD_TIMEOUT;
    assert(media.read(NULL,2,1,data)==-1 && snapshot().timeout_errors==1);
    assert(strstr(log_text,"operation=read (1) result=timeout (2)"));
    assert(strstr(log_text,"protocol=card timeout (3) LBA=2 blocks=1"));
    assert(strstr(log_text,"ready=1 healthy=0 SD_detail=1 first_flag_fault=1"));
    assert(strstr(log_text,"flag=40 SSR=20 SCR=30 polls=37"));
    assert(strstr(log_text,"SMR=80 BRR=00 SCMR=00 SPTR=0060 PDTR=0080"));
    clear_log();
    healthy=true;first_fault=(struct kui_sci_sd_fault){0};
    read_result=KUI_LOADER_SD_NOT_READY;
    assert(media.read(NULL,2,1,data)==-1 && snapshot().total==4);
    assert(!log_text[0]);
    read_result=KUI_LOADER_SD_OK;healthy=false;
    assert(media.read(NULL,2,1,data)==-1);
    e=snapshot();assert(e.total==5 && e.io_errors==1 && !e.sd_detail_valid);
    assert(strstr(log_text,"protocol=OK (0) LBA=2 blocks=1"));
    assert(strstr(log_text,"ready=1 healthy=0 SD_detail=0 first_flag_fault=0"));
    assert(strstr(log_text,"SCI failure command: unavailable"));
    assert(!strstr(log_text,"CMD18"));
    healthy=true;

    kui_storage_errors_reset();
    e=snapshot();assert(e.total==0 && e.last_operation==KUI_STORAGE_ERROR_NONE && !e.sd_detail_valid);
    assert(kui_storage_active()==KUI_STORAGE_SCI && media.read(NULL,2,1,data)==0);
    kui_storage_errors_get(NULL);
    kui_sd_disconnect();
    clear_log();
    init_result=KUI_LOADER_SD_TIMEOUT;
    assert(!kui_sd_connect());
    e=snapshot();assert(e.total==1 && e.init_errors==1 && e.timeout_errors==1);
    assert(e.sd_detail_valid && e.sd_command==41 && e.last_lba==0 && e.last_count==0);
    assert(strstr(log_text,"operation=init (4) result=timeout (2)"));
    assert(strstr(log_text,"ready=0 healthy=1 SD_detail=1 first_flag_fault=0"));
    assert(strstr(log_text,"CMD41 response=ff"));

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
