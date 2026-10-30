/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/g1_mode_probe.h"
#include <dc/asic.h>
#include <dc/g1ata.h>
#include <kos/irq.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Pinned KOS g1ata.c supplies this shared G1 exclusion semaphore. K-UI's
 * direct BIOS adapter additionally gates its own commands around this call. */
extern semaphore_t _g1_ata_sem;
#define ALTSTATUS UINT32_C(0xa05f7018)
#define DATA UINT32_C(0xa05f7080)
#define ERROR_FEATURES UINT32_C(0xa05f7084)
#define REASON UINT32_C(0xa05f7088)
#define BYTECOUNT_LO UINT32_C(0xa05f7090)
#define BYTECOUNT_HI UINT32_C(0xa05f7094)
#define DEVICE UINT32_C(0xa05f7098)
#define STATUS_COMMAND UINT32_C(0xa05f709c)
#define DMA_ENABLE UINT32_C(0xa05f7414)
#define DMA_STATUS UINT32_C(0xa05f7418)
#define PROTECTION UINT32_C(0xa05f74ec)
#define GD_DMA_EVENT (UINT32_C(1)<<14)
#define GD_DMA_ERRORS ((UINT32_C(1)<<12)|(UINT32_C(1)<<13))

static const uint32_t masks[3]={ASIC_IRQD_B,ASIC_IRQB_B,ASIC_IRQ9_B};
static struct {
    bool held, handler_removed;
    uint32_t irq_bits[3];
    asic_evt_handler_entry_t handler, unexpected_handler;
} lease;
#ifdef KUI_G1_MODE_PROBE_NATIVE_TEST
extern uint8_t kui_g1_probe_test_in8(uint32_t);
extern uint32_t kui_g1_probe_test_in32(uint32_t);
extern void kui_g1_probe_test_out32(uint32_t,uint32_t);
extern void kui_g1_probe_test_out8(uint32_t,uint8_t);
extern uint16_t kui_g1_probe_test_in16(uint32_t);
extern void kui_g1_probe_test_out16(uint32_t,uint16_t);
#define in8 kui_g1_probe_test_in8
#define in32 kui_g1_probe_test_in32
#define out32 kui_g1_probe_test_out32
#define out8 kui_g1_probe_test_out8
#define in16 kui_g1_probe_test_in16
#define out16 kui_g1_probe_test_out16
#else
static uint8_t in8(uint32_t a) {return *(volatile uint8_t *)(uintptr_t)a;}
static uint32_t in32(uint32_t a) {return *(volatile uint32_t *)(uintptr_t)a;}
static void out32(uint32_t a,uint32_t v) {*(volatile uint32_t *)(uintptr_t)a=v;}
static void out8(uint32_t a,uint8_t v) {*(volatile uint8_t *)(uintptr_t)a=v;}
static uint16_t in16(uint32_t a) {return *(volatile uint16_t *)(uintptr_t)a;}
static void out16(uint32_t a,uint16_t v) {*(volatile uint16_t *)(uintptr_t)a=v;}
#endif
static void irq_settle(void) {
    /* Sega System Architecture's Holly interrupt-write erratum: two dummy
     * Holly reads after the last mask write, before unmasking CPU interrupts. */
    (void)in32(masks[2]);(void)in32(masks[2]);
}
static void retain_masks(void) {
    for(unsigned i=0;i<3;++i)out32(masks[i],in32(masks[i])&~1u);
    irq_settle();
}
static void retain_lease(void) {
    /* Legitimate G1 work is excluded by the outer gate/semaphore. We cannot
     * cancel a foreign threaded callback already queued in violation of that
     * contract; prevent any later raw IRQ from dispatching another one. */
    asic_evt_handler_entry_t entry=asic_evt_set_handler(ASIC_EVT_GD_COMMAND,NULL,NULL);
    if(entry.hdl || entry.data)lease.unexpected_handler=entry;
    retain_masks();
}
static uint32_t address(enum kui_g1_probe_reg r) {
    static const uint32_t a[]={ALTSTATUS,STATUS_COMMAND,ERROR_FEATURES,REASON,
        BYTECOUNT_LO,BYTECOUNT_HI,DEVICE,ERROR_FEATURES,STATUS_COMMAND};
    return a[r];
}
static uint8_t read8(void *ctx,enum kui_g1_probe_reg r) {(void)ctx;return in8(address(r));}
static void write8(void *ctx,enum kui_g1_probe_reg r,uint8_t v) {
    (void)ctx;out8(address(r),v);
}
static uint16_t read16(void *ctx) {(void)ctx;return in16(DATA);}
static void write16(void *ctx,uint16_t v) {(void)ctx;out16(DATA,v);}
static bool foreign_dma(void) {
    return in32(DMA_STATUS)!=0 || (in32(ASIC_ACK_A)&GD_DMA_EVENT) ||
        (in32(ASIC_ACK_C)&GD_DMA_ERRORS);
}
static bool safe(void *ctx) {
    (void)ctx;
    if(!lease.held || foreign_dma())return false;
    for(unsigned i=0;i<3;++i)if(in32(masks[i])&1u)return false;
    return true;
}
static uint64_t clock_us(void *ctx) {(void)ctx;return timer_us_gettime64();}
static void pause_worker(void *ctx) {(void)ctx;thd_pass();}
static bool cancelled(void *ctx) {(void)ctx;return kui_cancelled();}
static bool refuse(struct kui_g1_mode_probe_status *o,const char *message,bool uncertain) {
    memset(o,0,sizeof(*o));o->failed=true;o->total=KUI_G1_MODE_CHOICES;
    o->result=KUI_G1_PROBE_OWNERSHIP;o->restored=!uncertain;o->restart_required=uncertain;
    snprintf(o->message,sizeof(o->message),"%s",message);kui_log("G1 direct probe refused: %s",message);
    return false;
}
bool kui_g1_mode_probe_native_run(
    void (*progress)(void *,const struct kui_g1_mode_probe_status *),void *ctx,
    struct kui_g1_mode_probe_status *o) {
    if(!o)return false;
    if(lease.held) {
        refuse(o,"G1 lease remains blocked; restart console",true);o->lease_retained=true;return false;
    }
    if(sem_wait_timed(&_g1_ata_sem,100))
        return refuse(o,"G1 bus is owned by another operation; restart console",true);
    irq_mask_t irq=irq_disable();
    uint8_t status=in8(ALTSTATUS),selection=in8(DEVICE);
    if(foreign_dma() || (in32(ASIC_ACK_B)&1u) || status==0 || status==0xff ||
       status&(0x80u|0x08u|0x20u) || selection&0x1fu || in32(PROTECTION)!=3u) {
        uint32_t dma=in32(DMA_STATUS),pending=in32(ASIC_ACK_B),protection=in32(PROTECTION);
        uint32_t dma_pending=in32(ASIC_ACK_A)&GD_DMA_EVENT,errors=in32(ASIC_ACK_C)&GD_DMA_ERRORS;
        irq_restore(irq);sem_signal(&_g1_ata_sem);
        kui_log("G1 admission status=%02x selection=%02x dma=%08" PRIx32 " ext=%08" PRIx32 " protection=%08" PRIx32,
            status,selection,dma,pending,protection);
        kui_log("G1 admission DMA pending=%08" PRIx32 " errors=%08" PRIx32,dma_pending,errors);
        return refuse(o,"G1 is not idle and exclusively available; restart console",true);
    }
    /* IRQ masks alone are insufficient: KOS's common ASIC dispatcher examines
     * every pending event on another IRQ too. Refuse an existing GD handler,
     * since an unknown threaded handler could already have queued work. Never
     * remove/free a threaded handler or assume its work has drained. */
    lease.handler=asic_evt_set_handler(ASIC_EVT_GD_COMMAND,NULL,NULL);
    if(lease.handler.hdl || lease.handler.data) {
        (void)asic_evt_set_handler(ASIC_EVT_GD_COMMAND,lease.handler.hdl,lease.handler.data);
        irq_restore(irq);sem_signal(&_g1_ata_sem);
        return refuse(o,"Existing G1 command handler prevents direct probe",false);
    }
    lease.handler_removed=true;
    for(unsigned i=0;i<3;++i) {
        uint32_t m=in32(masks[i]);lease.irq_bits[i]=m&1u;out32(masks[i],m&~1u);
    }
    lease.held=true;irq_settle();irq_restore(irq);
    kui_log("G1 direct mode probe: exclusive PIO; DMA enable=%" PRIu32 " (unchanged); no data-sector reads",in32(DMA_ENABLE));
    const struct kui_g1_mode_probe_bus bus={NULL,read8,write8,read16,write16,safe,
        clock_us,pause_worker,cancelled,kui_log};
    bool ok=kui_g1_mode_probe_run(&bus,progress,ctx,o);
    irq=irq_disable();
    /* Never hand an in-progress raw packet to a BIOS or another G1 owner.
     * An uncertain command retains only this GD event's mask and the shared
     * semaphore until restart; unrelated interrupt masks are not changed. */
    status=in8(ALTSTATUS);
    bool idle=status!=0 && status!=0xff && !(status&(0x80u|0x08u|0x20u));
    if(o->restart_required || !safe(NULL) || !idle || (in32(ASIC_ACK_B)&1u)) {
        o->restart_required=true;o->restored=false;o->failed=true;o->lease_retained=true;ok=false;
        snprintf(o->message,sizeof(o->message),"Raw G1 ownership uncertain; restart console; saved jobs unchanged");
        retain_lease();
        irq_restore(irq);
        kui_log("G1 raw lease retained until restart: status=%02x; no reset, abort or further G1 command",status);
        if(progress)progress(ctx,o);
        return false;
    }
    /* The saved handler was NULL. Detect an unexpected replacement without
     * overwriting it; no legitimate peer may modify this lease mid-probe. */
    asic_evt_handler_entry_t current=asic_evt_set_handler(ASIC_EVT_GD_COMMAND,NULL,NULL);
    if(current.hdl || current.data) {
        /* Keep the unexpected entry alive without allowing a later raw IRQ
         * to enter it. This is terminal ownership loss, not normal cleanup. */
        lease.unexpected_handler=current;
        o->restart_required=true;o->restored=false;o->failed=true;o->lease_retained=true;
        snprintf(o->message,sizeof(o->message),"G1 interrupt ownership changed; restart console");
        retain_masks();
        irq_restore(irq);kui_log("G1 IRQ owner changed; shared G1 lease retained until restart");
        if(progress)progress(ctx,o);
        return false;
    }
    (void)asic_evt_set_handler(ASIC_EVT_GD_COMMAND,lease.handler.hdl,lease.handler.data);
    lease.handler_removed=false;
    for(unsigned i=0;i<3;++i)out32(masks[i],(in32(masks[i])&~1u)|lease.irq_bits[i]);
    lease.held=false;irq_settle();irq_restore(irq);sem_signal(&_g1_ata_sem);
    kui_log("G1 direct probe lease released; GD IRQ masks restored; no recovery jobs opened");
    return ok;
}
