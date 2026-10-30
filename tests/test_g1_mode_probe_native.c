/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the actual native wrapper against KOS/MMIO doubles. The portable packet
 * engine has its own protocol tests; this file exercises its exclusive lease. */
#include "kui/g1_mode_probe.h"
#include <dc/asic.h>
#include <kos/irq.h>
#include <kos/sem.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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

enum core_scenario {CORE_OK,CORE_CLEAN_FAILURE,CORE_TIMEOUT,CORE_MASK_CHANGED,
    CORE_MASK_CHANGED_D,CORE_MASK_CHANGED_9,CORE_OTHER_MASK_CHANGED,
    CORE_DMA_CHANGED,CORE_PENDING,CORE_HANDLER_CHANGED,CORE_NOT_IDLE};
static struct {
    uint32_t masks[3],initial_masks[3],ack[3],dma_enable,dma_status,protection;
    uint8_t status,selection,error,reason,bytecount_lo,bytecount_hi;
    uint8_t features,command;
    uint16_t data_written;
    unsigned irq_state,initial_irq,disables,restores,sem_waits,sem_signals;
    unsigned handler_calls,core_calls,progress_calls,reads8,reads16,writes8,writes16,writes32;
    bool semaphore_busy,stop;
    enum core_scenario scenario;
    asic_evt_handler_entry_t handler;
    const struct kui_g1_mode_probe_bus *core_bus;
    char log[8192];size_t log_used;
} fake;
semaphore_t _g1_ata_sem;
static int owner_token;
static void foreign_handler(uint32_t event,void *data) {(void)event;(void)data;assert(false);}
static int mask_index(uint32_t address) {
    if(address==ASIC_IRQD_B)return 0;
    if(address==ASIC_IRQB_B)return 1;
    if(address==ASIC_IRQ9_B)return 2;
    return -1;
}
uint8_t kui_g1_probe_test_in8(uint32_t address) {
    ++fake.reads8;
    switch(address) {
    case ALTSTATUS:case STATUS_COMMAND:return fake.status;
    case DEVICE:return fake.selection;
    case ERROR_FEATURES:return fake.error;
    case REASON:return fake.reason;
    case BYTECOUNT_LO:return fake.bytecount_lo;
    case BYTECOUNT_HI:return fake.bytecount_hi;
    default:assert(false);return 0;
    }
}
uint32_t kui_g1_probe_test_in32(uint32_t address) {
    int i=mask_index(address);if(i>=0)return fake.masks[i];
    switch(address) {
    case ASIC_ACK_A:return fake.ack[0];
    case ASIC_ACK_B:return fake.ack[1];
    case ASIC_ACK_C:return fake.ack[2];
    case DMA_ENABLE:return fake.dma_enable;
    case DMA_STATUS:return fake.dma_status;
    case PROTECTION:return fake.protection;
    default:assert(false);return 0;
    }
}
void kui_g1_probe_test_out32(uint32_t address,uint32_t value) {
    int i=mask_index(address);assert(i>=0);
    /* No DMA, ACK, protection or unrelated Holly register may be written. */
    assert(((fake.masks[i]^value)&~UINT32_C(1))==0);
    fake.masks[i]=value;++fake.writes32;
}
void kui_g1_probe_test_out8(uint32_t address,uint8_t value) {
    ++fake.writes8;
    switch(address) {
    case ERROR_FEATURES:fake.features=value;break;
    case BYTECOUNT_LO:fake.bytecount_lo=value;break;
    case BYTECOUNT_HI:fake.bytecount_hi=value;break;
    case STATUS_COMMAND:fake.command=value;break;
    default:assert(false);
    }
}
uint16_t kui_g1_probe_test_in16(uint32_t address) {assert(address==DATA);++fake.reads16;return 0xbeef;}
void kui_g1_probe_test_out16(uint32_t address,uint16_t value) {
    assert(address==DATA);++fake.writes16;fake.data_written=value;
}
irq_mask_t irq_disable(void) {unsigned previous=fake.irq_state;fake.irq_state=0xf0;++fake.disables;return previous;}
void irq_restore(irq_mask_t previous) {assert(fake.irq_state==0xf0);fake.irq_state=previous;++fake.restores;}
int sem_wait_timed(semaphore_t *semaphore,unsigned timeout) {
    assert(semaphore==&_g1_ata_sem && timeout==100);++fake.sem_waits;
    if(fake.semaphore_busy)return -1;
    assert(semaphore->count==1);semaphore->count=0;return 0;
}
void sem_signal(semaphore_t *semaphore) {
    assert(semaphore==&_g1_ata_sem && !semaphore->count);
    semaphore->count=1;++fake.sem_signals;
}
asic_evt_handler_entry_t asic_evt_set_handler(uint16_t event,asic_evt_handler handler,void *data) {
    assert(event==ASIC_EVT_GD_COMMAND && fake.irq_state==0xf0);
    asic_evt_handler_entry_t previous=fake.handler;
    fake.handler=(asic_evt_handler_entry_t){handler,data};++fake.handler_calls;return previous;
}
uint64_t timer_us_gettime64(void) {return 123456;}
void thd_pass(void) {}
bool kui_cancelled(void) {return fake.stop;}
void kui_log(const char *format,...) {
    va_list args;va_start(args,format);
    int n=vsnprintf(fake.log+fake.log_used,sizeof(fake.log)-fake.log_used,format,args);va_end(args);
    assert(n>=0 && (size_t)n+1<sizeof(fake.log)-fake.log_used);
    fake.log_used+=(size_t)n;fake.log[fake.log_used++]='\n';fake.log[fake.log_used]=0;
}
static void progress(void *ctx,const struct kui_g1_mode_probe_status *status) {
    assert(ctx==&owner_token && status);++fake.progress_calls;
}
bool kui_g1_mode_probe_run(const struct kui_g1_mode_probe_bus *bus,
    void (*report)(void *,const struct kui_g1_mode_probe_status *),void *ctx,
    struct kui_g1_mode_probe_status *out) {
    ++fake.core_calls;fake.core_bus=bus;
    assert(_g1_ata_sem.count==0 && fake.irq_state==fake.initial_irq);
    assert(!fake.handler.hdl && !fake.handler.data);
    for(unsigned i=0;i<3;++i)assert(fake.masks[i]==(fake.initial_masks[i]&~UINT32_C(1)));
    assert(bus && bus->safe(bus->ctx));
    assert(bus->now_us(bus->ctx)==123456 && !bus->cancelled(bus->ctx));
    bus->pause(bus->ctx);
    /* These calls prove the native bus callbacks reach the expected MMIO
     * addresses; the core's packet-state tests separately check their order. */
    assert(bus->read8(bus->ctx,KUI_G1_ALTSTATUS)==fake.status);
    assert(bus->read8(bus->ctx,KUI_G1_STATUS)==fake.status);
    assert(bus->read8(bus->ctx,KUI_G1_DEVICE)==fake.selection);
    assert(bus->read8(bus->ctx,KUI_G1_ERROR)==fake.error);
    assert(bus->read8(bus->ctx,KUI_G1_REASON)==fake.reason);
    bus->write8(bus->ctx,KUI_G1_FEATURES,0x55);
    bus->write8(bus->ctx,KUI_G1_BYTECOUNT_LO,10);
    bus->write8(bus->ctx,KUI_G1_BYTECOUNT_HI,0);
    bus->write8(bus->ctx,KUI_G1_COMMAND,0xa0);
    assert(fake.features==0x55 && fake.bytecount_lo==10 && !fake.bytecount_hi && fake.command==0xa0);
    assert(bus->read16(bus->ctx)==0xbeef);
    bus->write16(bus->ctx,0x1234);assert(fake.data_written==0x1234);
    memset(out,0,sizeof(*out));out->total=KUI_G1_MODE_CHOICES;
    out->complete=out->restored=true;out->result=KUI_G1_PROBE_OK;
    if(fake.scenario==CORE_CLEAN_FAILURE) {out->complete=false;out->failed=true;out->result=KUI_G1_PROBE_INVALID;}
    else if(fake.scenario==CORE_TIMEOUT) {
        out->complete=out->restored=false;out->failed=out->restart_required=true;
        out->result=KUI_G1_PROBE_TIMEOUT;fake.status=0x80;
    } else if(fake.scenario==CORE_MASK_CHANGED) {fake.masks[1]|=1;assert(!bus->safe(bus->ctx));}
    else if(fake.scenario==CORE_MASK_CHANGED_D) {fake.masks[0]|=1;assert(!bus->safe(bus->ctx));}
    else if(fake.scenario==CORE_MASK_CHANGED_9) {fake.masks[2]|=1;assert(!bus->safe(bus->ctx));}
    else if(fake.scenario==CORE_OTHER_MASK_CHANGED) {
        fake.masks[0]^=0x80;fake.masks[1]|=0x200;fake.masks[2]&=~UINT32_C(8);
        assert(bus->safe(bus->ctx));
    }
    else if(fake.scenario==CORE_DMA_CHANGED) {fake.dma_status=1;assert(!bus->safe(bus->ctx));}
    else if(fake.scenario==CORE_PENDING)fake.ack[1]|=1;
    else if(fake.scenario==CORE_HANDLER_CHANGED)fake.handler=(asic_evt_handler_entry_t){foreign_handler,&owner_token};
    else if(fake.scenario==CORE_NOT_IDLE)fake.status=0x58;
    if(report)report(ctx,out);
    return out->complete && !out->failed;
}
static void reset(void) {
    memset(&fake,0,sizeof(fake));_g1_ata_sem.count=1;
    fake.status=0x50;fake.selection=0xa0;fake.protection=3;fake.dma_enable=1;
    fake.masks[0]=0x15;fake.masks[1]=0x26;fake.masks[2]=0x39;
    memcpy(fake.initial_masks,fake.masks,sizeof(fake.masks));
    /* Unrelated pending ASIC events do not belong to this diagnostic. */
    fake.ack[0]=0x200;fake.ack[1]=0x20;fake.ack[2]=0x800;
    fake.irq_state=fake.initial_irq=0x42;
}
static void original_masks(void) {
    uint32_t expected[3];memcpy(expected,fake.initial_masks,sizeof(expected));
    if(fake.scenario==CORE_OTHER_MASK_CHANGED) {expected[0]^=0x80;expected[1]|=0x200;expected[2]&=~UINT32_C(8);}
    assert(!memcmp(fake.masks,expected,sizeof(fake.masks)));
    assert(fake.irq_state==fake.initial_irq && fake.disables==fake.restores);
    assert(fake.dma_enable==1 && fake.protection==3);
    assert(fake.ack[0]==0x200 && fake.ack[1]==0x20 && fake.ack[2]==0x800);
}
static int run_case(const char *name) {
    reset();struct kui_g1_mode_probe_status out;
    bool core=false,retained=false,restored=false,uncertain=true;
    if(!strcmp(name,"clean"))core=restored=true;
    else if(!strcmp(name,"unrelated-masks-changed")) {core=restored=true;fake.scenario=CORE_OTHER_MASK_CHANGED;}
    else if(!strcmp(name,"clean-failure")) {core=restored=true;fake.scenario=CORE_CLEAN_FAILURE;}
    else if(!strcmp(name,"frozen-irq")) {core=restored=true;fake.irq_state=fake.initial_irq=0xf0;}
    else if(!strcmp(name,"busy-sem"))fake.semaphore_busy=true;
    else if(!strcmp(name,"handler")) {fake.handler=(asic_evt_handler_entry_t){foreign_handler,&owner_token};uncertain=false;}
    else if(!strcmp(name,"handler-data")) {fake.handler.data=&owner_token;uncertain=false;}
    else if(!strcmp(name,"pending-command"))fake.ack[1]|=1;
    else if(!strcmp(name,"pending-dma"))fake.ack[0]|=UINT32_C(1)<<14;
    else if(!strcmp(name,"pending-dma-error"))fake.ack[2]|=UINT32_C(1)<<12;
    else if(!strcmp(name,"pending-dma-error2"))fake.ack[2]|=UINT32_C(1)<<13;
    else if(!strcmp(name,"dma-active"))fake.dma_status=1;
    else if(!strcmp(name,"slave"))fake.selection=0xb0;
    else if(!strcmp(name,"device-head"))fake.selection=0xa1;
    else if(!strcmp(name,"protection"))fake.protection=0;
    else if(!strcmp(name,"busy"))fake.status=0xd0;
    else if(!strcmp(name,"drq"))fake.status=0x58;
    else if(!strcmp(name,"device-fault"))fake.status=0x70;
    else if(!strcmp(name,"no-device-zero"))fake.status=0;
    else if(!strcmp(name,"no-device-ff"))fake.status=0xff;
    else {
        core=retained=true;
        if(!strcmp(name,"timeout-retained"))fake.scenario=CORE_TIMEOUT;
        else if(!strcmp(name,"mask-changed"))fake.scenario=CORE_MASK_CHANGED;
        else if(!strcmp(name,"mask-changed-d"))fake.scenario=CORE_MASK_CHANGED_D;
        else if(!strcmp(name,"mask-changed-9"))fake.scenario=CORE_MASK_CHANGED_9;
        else if(!strcmp(name,"dma-changed"))fake.scenario=CORE_DMA_CHANGED;
        else if(!strcmp(name,"pending-after"))fake.scenario=CORE_PENDING;
        else if(!strcmp(name,"handler-changed"))fake.scenario=CORE_HANDLER_CHANGED;
        else {assert(!strcmp(name,"not-idle-after"));fake.scenario=CORE_NOT_IDLE;}
    }
    const asic_evt_handler_entry_t handler=fake.handler;
    uint32_t initial_ack[3];memcpy(initial_ack,fake.ack,sizeof(initial_ack));
    uint32_t protection=fake.protection,dma_status=fake.dma_status;
    bool ok=kui_g1_mode_probe_native_run(progress,&owner_token,&out);
    assert(ok==(!strcmp(name,"clean") || !strcmp(name,"frozen-irq") || !strcmp(name,"unrelated-masks-changed")));
    assert(fake.core_calls==(core?1u:0u) && fake.sem_waits==1);
    assert(out.lease_retained==retained);
    assert(fake.irq_state==fake.initial_irq && fake.disables==fake.restores);
    if(retained) {
        assert(out.failed && out.restart_required && !out.restored);
        assert(!_g1_ata_sem.count && !fake.sem_signals);
        for(unsigned i=0;i<3;++i)assert(fake.masks[i]==(fake.initial_masks[i]&~UINT32_C(1)));
        assert(fake.progress_calls==2);
        unsigned waits=fake.sem_waits,core_calls=fake.core_calls;
        struct kui_g1_mode_probe_status again;
        assert(!kui_g1_mode_probe_native_run(progress,&owner_token,&again));
        assert(again.lease_retained && again.restart_required && !again.restored);
        assert(fake.sem_waits==waits && fake.core_calls==core_calls && !fake.sem_signals);
        /* KOS dispatches other pending events even when their own mask is
         * disabled. An unexpected handler must stay detached while this
         * uncertain lease remains held; it must not be invoked or freed. */
        assert(!fake.handler.hdl && !fake.handler.data);
    } else if(core) {
        assert(out.restored && !out.restart_required);
        assert(_g1_ata_sem.count==1 && fake.sem_signals==1);
        assert(!fake.handler.hdl && !fake.handler.data && fake.handler_calls==3);
        assert(fake.progress_calls==1 && fake.reads16==1 && fake.writes16==1 && fake.writes8==4);
        original_masks();
    } else {
        assert(out.failed && !out.complete && out.restart_required==uncertain);
        assert(out.restored==!uncertain);
        assert(_g1_ata_sem.count==1 && fake.sem_signals==(!fake.semaphore_busy?1u:0u));
        assert(!fake.writes8 && !fake.writes16 && !fake.writes32 && !fake.reads16 && !fake.progress_calls);
        assert(!memcmp(fake.masks,fake.initial_masks,sizeof(fake.masks)));
        assert(!memcmp(fake.ack,initial_ack,sizeof(fake.ack)));
        assert(fake.protection==protection && fake.dma_status==dma_status && fake.dma_enable==1);
        assert(fake.handler.hdl==handler.hdl && fake.handler.data==handler.data);
        assert(fake.handler_calls==(handler.hdl || handler.data?2u:0u));
    }
    assert(restored==(!retained && core));
    printf("PASS native direct G1 lease: %s\n",name);return 0;
}
int main(int argc,char **argv) {
    if(argc==2)return run_case(argv[1]);
    assert(argc==1);
    const char *cases[]={"clean","unrelated-masks-changed","clean-failure","frozen-irq","busy-sem","handler","handler-data",
        "pending-command","pending-dma","pending-dma-error","pending-dma-error2","dma-active",
        "slave","device-head","protection","busy","drq","device-fault","no-device-zero","no-device-ff",
        "timeout-retained","mask-changed","mask-changed-d","mask-changed-9","dma-changed","pending-after","handler-changed","not-idle-after"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        pid_t child=fork();assert(child>=0);
        if(!child) {int result=run_case(cases[i]);assert(fflush(stdout)==0);_exit(result);}
        int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
    puts("PASS native direct G1 lease: 28 ownership and IRQ scenarios");return 0;
}
