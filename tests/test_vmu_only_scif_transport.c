/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual SCIF GPIO bus, scoped low loan and CMD24/CMD13 write protocol.
 * MMIO is the boundary model; no stage/launcher bus pointer survives. */
#define KUI_VMU_SD_LEASE_HOST_TEST 1
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_native_background.h"
#include "kui/vmu_sd_lease.h"
#include "retail_storage.h"
#include "sci_sd_bus.h"
#include "../src/dreamcast/sci_sd_storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SCR UINT32_C(0xffe80008)
#define FCR UINT32_C(0xffe80018)
#define FDR UINT32_C(0xffe8001c)
#define PTR UINT32_C(0xffe80020)
static struct kui_retail_storage card;
volatile uint32_t kui_retail_hook_active;
struct kui_vmu_observe_boot_control kui_vmu_observe_boot_control;
static unsigned callbacks,checks;
static bool reenter,return_fault,poison_loan,change_controls;
static uint32_t entry_sr;
static struct {
    uint16_t scr,fcr,fdr,ptr;
    unsigned writes,wire,commands,blocks,restores;
    unsigned bits,frame_n,data_n,reply_n,busy;
    uint8_t tx,rx,miso,frame[6],data[514],reply[2];
    bool selected,writing,reject,status_error,busy_forever,restore_fault;
} fake;
static uint8_t payload[512];
static int transaction(void *,struct kui_loader_sd *);
uintptr_t kui_vmu_sd_lease_test_target(kui_vmu_sd_transaction callback) {
    return callback==transaction?UINT32_C(0x8c010120):0u;
}
#include "vmu_native_resident_guards.h"
uint32_t kui_vmu_sd_lease_test_sr(void) {return entry_sr;}
/* Unreachable SCI backend calls are observable, never silently accepted. */
bool kui_sci_sd_healthy(void) {assert(!"SCIF used SCI health");return false;}
bool kui_sci_sd_faulted(void) {assert(!"SCIF used SCI latch");return true;}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {assert(!"SCIF acquired SCI");return KUI_LOADER_SD_UNSUPPORTED;}
void kui_sci_sd_release(void) {assert(!"SCIF released SCI");}
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) {assert(!"SCIF rebound SCI");return NULL;}

static uint16_t reference_crc(const uint8_t *p) {
    uint16_t crc=0;
    for(unsigned i=0;i<512u;i++) for(int bit=7;bit>=0;bit--) {
        bool high=((crc>>15)^((p[i]>>bit)&1u))!=0;
        crc=(uint16_t)(crc<<1);if(high) crc^=UINT16_C(0x1021);
    }
    return crc;
}
static uint8_t peek(void) {
    if(fake.reply_n) return fake.reply[0];
    if((fake.busy_forever && fake.blocks) || fake.busy) return 0u;
    return 0xffu;
}
static uint8_t byte(uint8_t value) {
    ++fake.wire;
    if(fake.reply_n) {
        uint8_t result=fake.reply[0];fake.reply[0]=fake.reply[1];--fake.reply_n;return result;
    }
    if(fake.busy_forever && fake.blocks) return 0u;
    if(fake.busy) {--fake.busy;return 0u;}
    if(fake.data_n) {
        fake.data[fake.data_n-1u]=value;
        if(++fake.data_n==515u) {
            assert(!memcmp(fake.data,payload,512u));
            uint16_t crc=reference_crc(payload);
            assert(fake.data[512]==(uint8_t)(crc>>8) && fake.data[513]==(uint8_t)crc);
            ++fake.blocks;fake.data_n=0u;fake.writing=false;
            fake.reply[0]=fake.reject?0x0bu:5u;fake.reply_n=1u;fake.busy=3u;
        }
        return 0xffu;
    }
    if(fake.writing && value==0xfeu) {fake.data_n=1u;return 0xffu;}
    if(fake.frame_n || (value&0xc0u)==0x40u) {
        fake.frame[fake.frame_n++]=value;
        if(fake.frame_n==6u) {
            ++fake.commands;fake.frame_n=0u;fake.reply[0]=0u;fake.reply_n=1u;
            unsigned cmd=fake.frame[0]&63u;
            if(cmd==24u) {
                assert(fake.frame[1]==0u && fake.frame[2]==0u && fake.frame[3]==0u && fake.frame[4]==17u);
                fake.writing=true;
            } else {
                assert(cmd==13u);fake.reply[1]=fake.status_error?0x20u:0u;fake.reply_n=2u;
            }
        }
    }
    return 0xffu;
}
uint16_t kui_retail_sd_test_read16(uint32_t address) {
    switch(address) {
    case SCR:return fake.scr;
    case FCR:return fake.fcr;
    case FDR:return fake.fdr;
    case PTR:return (fake.ptr&(uint16_t)~1u)|fake.miso;
    default:assert(!"Unexpected MMIO read");return 0u;
    }
}
void kui_retail_sd_test_write16(uint32_t address,uint16_t value) {
    ++fake.writes;
    switch(address) {
    case SCR:
        if(value==3u) {++fake.restores;if(fake.restore_fault) value=2u;}
        fake.scr=value;break;
    case FCR:assert(!(value&6u));fake.fcr=value;break;
    case PTR: {
        bool selected=!(value&0x40u),edge=!(fake.ptr&0x10u) && (value&0x10u);
        if(selected && edge) {
            assert(fake.selected && fake.scr==0u && !(fake.fcr&8u));
            if(!fake.bits) {fake.rx=peek();fake.tx=0u;}
            fake.miso=(fake.rx>>(7u-fake.bits))&1u;
            fake.tx=(uint8_t)((fake.tx<<1)|(value&1u));
            if(++fake.bits==8u) {assert(byte(fake.tx)==fake.rx);fake.bits=0u;}
        }
        if(!selected) assert(!fake.bits);
        fake.selected=selected;fake.ptr=value;break;
    }
    default:assert(!"Unexpected MMIO write");
    }
}
void kui_retail_sd_test_delay(void) {assert(!"Borrowed prepared card used slow init");}

/* Include the actual backend so a new fixture can reset its private state
 * as a new resident image would. No runtime API clears the fault for a loan. */
#include "../src/loader/retail_sd.c"

#include "../src/loader/vmu_sd_lease.inc"

static int transaction(void *context,struct kui_loader_sd *loan) {
    assert(context==payload && loan!=&card.device.sd && loan->bus.transfer);
    assert(kui_retail_hook_active==1u && kui_retail_sd_healthy());++callbacks;
    if(reenter) {
        unsigned before=fake.writes;
        assert(kui_vmu_observe_sd_lease(transaction,context)==KUI_VMU_SD_LEASE_BUSY);
        assert(fake.writes==before);
    }
    enum kui_loader_sd_result result=kui_sci_sd_write(loan,17u,1u,payload);
    if(result==KUI_LOADER_SD_OK) result=kui_sci_sd_sync(loan);
    if(poison_loan) loan->ready=false;
    if(change_controls) fake.scr=0x10u;
    return result==KUI_LOADER_SD_OK && !return_fault?0:-1;
}
static void fresh(void) {
    fake.restore_fault=false;kui_retail_sd_release();
    memset(&fake,0,sizeof(fake));fake.scr=3u;fake.fcr=0xb8u;fake.ptr=0x4cu;
    memset(&port,0,sizeof(port));
    memset(&card,0,sizeof(card));card.transport=KUI_STORAGE_SCIF;
    const struct kui_loader_sd prepared={.ready=true,.high_capacity=true,.blocks=10000u};
    assert(kui_retail_sd_adopt(&card.device.sd,&prepared)==KUI_LOADER_SD_OK);
    kui_vmu_observe_boot_control=(struct kui_vmu_observe_boot_control){
        .status=KUI_VMU_OBS_BOOT_INSTALLED,.lease=UINT32_C(0x0c010020),
        .worker_end=UINT32_C(0xac010020)+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES,
        .terminal=UINT32_C(0x8c010220),.sd_transaction=UINT32_C(0x8c010120)};
    entry_sr=UINT32_C(0x600000f3);kui_retail_hook_active=0u;callbacks=0u;
    reenter=return_fault=poison_loan=change_controls=false;
    for(unsigned i=0;i<512u;i++) payload[i]=(uint8_t)(i*29u+7u);
}
static int call(void) {++checks;return kui_vmu_observe_sd_lease(transaction,payload);}
static void restored(void) {
    assert(fake.scr==3u && fake.fcr==0xb8u && (fake.ptr&~1u)==0x4cu);
    assert(!fake.selected && !kui_retail_sd_healthy() && !kui_retail_sd_faulted());
    assert(!kui_retail_hook_active);
}
int main(void) {
    fresh();reenter=true;struct kui_loader_sd before=card.device.sd;
    assert(native_resident_destination(&kui_vmu_observe_boot_control,UINT32_C(0x8c100000),512u));
    assert(!native_resident_destination(&kui_vmu_observe_boot_control,UINT32_C(0x8c010020),512u));
    assert(call()==KUI_VMU_SD_LEASE_OK && callbacks==1u && fake.blocks==1u && fake.commands==3u);
    assert(!memcmp(&before,&card.device.sd,sizeof(before)));restored();
    assert(call()==KUI_VMU_SD_LEASE_OK && callbacks==2u && fake.blocks==2u);restored();
    for(unsigned i=0;i<9u;i++) {
        fresh();unsigned writes=fake.writes;
        if(i==0u) entry_sr&=~UINT32_C(0x10);
        if(i==1u) kui_vmu_observe_boot_control.sd_transaction^=2u;
        if(i==2u) card.transport=KUI_STORAGE_IDE;
        if(i==3u) card.stream.active=true;
        if(i==4u) card.device.sd.ready=false;
        if(i==5u) card.device.sd.slow=true;
        if(i==6u) card.device.sd.blocks=0u;
        if(i==7u) kui_vmu_observe_boot_control.status=KUI_VMU_OBS_BOOT_LEASED;
        if(i==8u) card.transport=KUI_STORAGE_SCI;
        assert(call()==KUI_VMU_SD_LEASE_FAULT && !callbacks && fake.writes==writes && !fake.wire);
    }
    for(unsigned i=0;i<3u;i++) {
        fresh();
        if(i==0u) fake.scr|=0x10u;
        if(i==1u) fake.fdr=0x100u;
        if(i==2u) assert(kui_retail_sd_acquire()==KUI_LOADER_SD_OK);
        unsigned writes=fake.writes;
        assert(call()==KUI_VMU_SD_LEASE_BUSY && !callbacks && fake.writes==writes && !fake.wire);
        assert(card.device.sd.ready && !kui_retail_hook_active);
        if(i==2u) kui_retail_sd_release();
    }
    for(unsigned i=0;i<7u;i++) {
        fresh();
        if(i==0u) fake.reject=true;
        if(i==1u) fake.status_error=true;
        if(i==2u) fake.busy_forever=true;
        if(i==3u) return_fault=true;
        if(i==4u) poison_loan=true;
        if(i==5u) change_controls=true;
        if(i==6u) fake.restore_fault=true;
        int result=call();
        if(result!=KUI_VMU_SD_LEASE_FAULT || callbacks!=1u || fake.restores!=1u)
            fprintf(stderr,"fault case %u: result=%d callbacks=%u restores=%u\n",i,result,callbacks,fake.restores);
        assert(result==KUI_VMU_SD_LEASE_FAULT && callbacks==1u && fake.restores==1u);
        assert(!card.device.sd.ready && !kui_retail_hook_active);
        unsigned writes=fake.writes,wire=fake.wire;
        assert(call()==KUI_VMU_SD_LEASE_FAULT && callbacks==1u && fake.writes==writes && fake.wire==wire);
        if(kui_retail_sd_faulted()) {
            assert(kui_retail_sd_acquire()==KUI_LOADER_SD_NOT_READY);
            struct kui_loader_sd adopted={0};
            assert(kui_retail_sd_adopt(&adopted,&card.device.sd)==KUI_LOADER_SD_NOT_READY);
            assert(fake.writes==writes && fake.wire==wire && kui_retail_sd_faulted());
        }
    }
    printf("VMU-only SCIF: %u loan cases; actual GPIO CMD24/CRC/CMD13, exact restore, BUSY/no-I/O and persistent protocol/control faults passed\n",checks);
    return 0;
}
