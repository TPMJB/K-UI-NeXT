/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_vmu_storage.h"
#include "kui/storage.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
struct fixture {
    struct kui_disc_vmu_storage owner;
    struct kui_disc_vmu_storage_config config;
    struct kui_disc_vmu_storage_ops ops;
    uint32_t sr;
    unsigned sr_calls,streams,acquires,releases,health_calls,fault_calls,transactions,wire;
    enum kui_loader_sd_result acquire_result;
    bool stream,port_fault,healthy,held,release_fault,acquire_fault;
    int callback_result;
    unsigned mutation;
};
static void begin(void *context) {(void)context;}
static void end(void *context) {(void)context;}
static void select_card(void *context,bool selected) {(void)context;(void)selected;}
static uint8_t transfer(void *context,uint8_t data,bool slow) {
    struct fixture *f=context;CHECK(!slow && f->held);++f->wire;return data;
}
static uint32_t ticks(void *context) {(void)context;return 123u;}
static bool transfer_block(void *context,const uint8_t *tx,uint8_t *rx,size_t bytes,
    bool slow,uint16_t *crc) {
    (void)context;(void)tx;(void)rx;(void)bytes;(void)slow;(void)crc;return true;
}
static uint32_t current_sr(void *context) {
    struct fixture *f=context;++f->sr_calls;return f->sr;
}
static bool stream_active(void *context) {
    struct fixture *f=context;++f->streams;return f->stream;
}
static enum kui_loader_sd_result acquire(void *context) {
    struct fixture *f=context;++f->acquires;CHECK(!f->held);
    if(f->acquire_fault) f->port_fault=true;
    if(f->acquire_result==KUI_LOADER_SD_OK) f->held=true;
    return f->acquire_result;
}
static void release(void *context) {
    struct fixture *f=context;++f->releases;CHECK(f->held);f->held=false;
    if(f->release_fault) f->port_fault=true;
}
static void wrong_release(void *context) {(void)context;CHECK(false);}
static bool healthy(void *context) {
    struct fixture *f=context;++f->health_calls;CHECK(f->held);return f->healthy;
}
static bool faulted(void *context) {
    struct fixture *f=context;++f->fault_calls;return f->port_fault;
}
static int wrong_transaction(void *context,struct kui_loader_sd *loan) {
    (void)context;(void)loan;CHECK(false);return -1;
}
static int transaction(void *context,struct kui_loader_sd *loan) {
    struct fixture *f=context;++f->transactions;
    CHECK(f->held && f->owner.busy && loan!=&f->owner.config.card);
    CHECK(loan->bus.transfer(loan->bus.ctx,0x5au,false)==0x5au);
    loan->last_command=24u;loan->last_response=0u;
    switch(f->mutation) {
    case 0u:break;
    case 1u:
        CHECK(kui_disc_vmu_storage_loan(&f->owner,transaction,f)==KUI_VMU_SD_LEASE_BUSY);
        CHECK(f->owner.busy && f->held && f->acquires==1u && !f->releases);break;
    case 2u:loan->ready=false;break;
    case 3u:++loan->blocks;break;
    case 4u:loan->high_capacity=!loan->high_capacity;break;
    case 5u:loan->slow=true;break;
    case 6u:loan->bus.ctx=NULL;break;
    case 7u:loan->bus.begin=NULL;break;
    case 8u:loan->bus.end=NULL;break;
    case 9u:loan->bus.select=NULL;break;
    case 10u:loan->bus.transfer=NULL;break;
    case 11u:loan->bus.ticks=NULL;break;
    case 12u:loan->bus.transfer_block=transfer_block;break;
    case 13u:f->port_fault=true;break;
    case 14u:f->healthy=false;break;
    case 15u:f->owner.config.cid[3]^=1u;break;
    case 16u:f->owner.config.transport^=1u;break;
    case 17u:f->owner.ops.release=wrong_release;break;
    case 18u:f->owner.platform_context=NULL;break;
    case 19u:f->owner.busy=false;break;
    case 20u:f->owner.state^=1u;break;
    case 21u:f->owner.self=0u;break;
    case 22u:f->owner.config.transaction=wrong_transaction;break;
    case 23u:f->owner.config.transaction_context=NULL;break;
    case 24u:++f->owner.config.card.blocks;break;
    case 25u:f->sr=0u;break;
    default:CHECK(false);break;
    }
    return f->callback_result;
}
static void setup(struct fixture *f,uint32_t transport) {
    memset(f,0,sizeof(*f));f->sr=0xf0u;f->healthy=true;
    f->ops=(struct kui_disc_vmu_storage_ops){current_sr,stream_active,acquire,release,healthy,faulted};
    f->config.transport=transport;
    for(unsigned i=0u;i<16u;i++) f->config.cid[i]=(uint8_t)(i+3u);
    f->config.card=(struct kui_loader_sd){
        .bus={f,begin,end,select_card,transfer,ticks,NULL},.blocks=10000u,
        .high_capacity=true,.ready=true,.slow=false,.last_command=0u,.last_response=0xffu};
    f->config.transaction=transaction;f->config.transaction_context=f;
}
static void bind(struct fixture *f) {
    CHECK(kui_disc_vmu_storage_bind(&f->owner,&f->config,&f->ops,f));
    CHECK(!f->streams && !f->acquires && !f->releases && !f->wire && !f->transactions);
}
static unsigned calls(const struct fixture *f) {
    return f->sr_calls+f->streams+f->acquires+f->releases+f->health_calls+f->fault_calls;
}
static int loan(struct fixture *f) {return kui_disc_vmu_storage_loan(&f->owner,transaction,f);}
static void refuses_forever(struct fixture *f) {
    unsigned before=calls(f),writes=f->wire,transactions=f->transactions;
    CHECK(!f->owner.busy && f->owner.fault && f->owner.faults==1u);
    CHECK(loan(f)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(calls(f)==before && f->wire==writes && f->transactions==transactions);
    CHECK(!kui_disc_vmu_storage_bind(&f->owner,&f->config,&f->ops,f));
}
static void invalid_bind(void) {
    for(unsigned which=0u;which<23u;which++) {
        struct fixture f;setup(&f,KUI_STORAGE_SCI);
        switch(which) {
        case 0u:f.config.transport=KUI_STORAGE_AUTO;break;
        case 1u:f.config.transport=KUI_STORAGE_IDE;break;
        case 2u:memset(f.config.cid,0,16u);break;
        case 3u:memset(f.config.cid,0xff,16u);break;
        case 4u:f.config.card.ready=false;break;
        case 5u:f.config.card.slow=true;break;
        case 6u:f.config.card.blocks=0u;break;
        case 7u:f.config.card.blocks=UINT64_C(0x100000001);break;
        case 8u:f.config.card.bus.begin=NULL;break;
        case 9u:f.config.card.bus.end=NULL;break;
        case 10u:f.config.card.bus.select=NULL;break;
        case 11u:f.config.card.bus.transfer=NULL;break;
        case 12u:f.config.card.bus.ticks=NULL;break;
        case 13u:f.config.transaction=NULL;break;
        case 14u:f.ops.current_sr=NULL;break;
        case 15u:f.ops.stream_active=NULL;break;
        case 16u:f.ops.acquire=NULL;break;
        case 17u:f.ops.release=NULL;break;
        case 18u:f.ops.healthy=NULL;break;
        case 19u:f.ops.faulted=NULL;break;
        case 20u:f.sr=0xe0u;break;
        case 21u:f.owner.completed=1u;break;
        case 22u:f.config.card.high_capacity=false;f.config.card.blocks=UINT64_C(0x800001);break;
        }
        struct kui_disc_vmu_storage before=f.owner;
        CHECK(!kui_disc_vmu_storage_bind(&f.owner,&f.config,&f.ops,&f));
        CHECK(!memcmp(&f.owner,&before,sizeof(before)) && !f.acquires && !f.releases && !f.wire);
    }
    struct fixture f;setup(&f,KUI_STORAGE_SCI);
    CHECK(!kui_disc_vmu_storage_bind(NULL,&f.config,&f.ops,&f));
    CHECK(!kui_disc_vmu_storage_bind(&f.owner,NULL,&f.ops,&f));
    CHECK(!kui_disc_vmu_storage_bind(&f.owner,&f.config,NULL,&f));
    CHECK(!kui_disc_vmu_storage_bind(&f.owner,(const void *)&f.owner,&f.ops,&f));
    CHECK(!kui_disc_vmu_storage_bind(&f.owner,&f.config,(const void *)&f.owner,&f));
}
static void ordinary_and_refusals(uint32_t transport) {
    struct fixture f;setup(&f,transport);bind(&f);
    struct kui_disc_vmu_storage_config before=f.owner.config;
    CHECK(loan(&f)==KUI_VMU_SD_LEASE_OK);
    CHECK(f.transactions==1u && f.acquires==1u && f.releases==1u && !f.held &&
        f.owner.acquired==1u && f.owner.completed==1u && !f.owner.busy && !f.owner.fault &&
        f.owner.last_command==24u && !f.owner.last_response);
    CHECK(!memcmp(&before,&f.owner.config,sizeof(before)));
    CHECK(!kui_disc_vmu_storage_bind(&f.owner,&f.config,&f.ops,&f));
    unsigned c=calls(&f);
    CHECK(kui_disc_vmu_storage_loan(&f.owner,wrong_transaction,&f)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(kui_disc_vmu_storage_loan(&f.owner,transaction,NULL)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(kui_disc_vmu_storage_loan(&f.owner,NULL,&f)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(calls(&f)==c && !f.owner.fault);
    struct kui_disc_vmu_storage copied=f.owner;
    CHECK(kui_disc_vmu_storage_loan(&copied,transaction,&f)==KUI_VMU_SD_LEASE_FAULT && calls(&f)==c);
    CHECK(kui_disc_vmu_storage_loan(NULL,transaction,&f)==KUI_VMU_SD_LEASE_FAULT);
    /* Binding copies its inputs. Later edits of the temporary stage config
     * and ops must not redirect a live callback, card or release entry. */
    f.config.card.bus.transfer=NULL;f.config.transaction=wrong_transaction;f.ops.release=wrong_release;
    CHECK(loan(&f)==KUI_VMU_SD_LEASE_OK && f.transactions==2u && f.releases==2u);
    for(unsigned mask=0u;mask<15u;mask++) {
        f.sr=mask<<4;c=f.acquires;
        CHECK(loan(&f)==KUI_VMU_SD_LEASE_FAULT && f.acquires==c && !f.owner.fault);
    }
    f.sr=0xf0u;f.stream=true;c=f.acquires;
    CHECK(loan(&f)==KUI_VMU_SD_LEASE_BUSY && f.acquires==c && !f.owner.busy && !f.owner.fault);
    f.stream=false;
    CHECK(loan(&f)==KUI_VMU_SD_LEASE_OK);
    setup(&f,transport);bind(&f);f.mutation=1u;
    CHECK(loan(&f)==KUI_VMU_SD_LEASE_OK && f.owner.busy_refusals==1u && f.releases==1u);
    setup(&f,transport);f.config.card.blocks=UINT64_C(0x100000000);bind(&f);
    CHECK(loan(&f)==KUI_VMU_SD_LEASE_OK);
    setup(&f,transport);f.config.card.high_capacity=false;f.config.card.blocks=UINT64_C(0x800000);bind(&f);
    CHECK(loan(&f)==KUI_VMU_SD_LEASE_OK);
}
static void acquisition_paths(uint32_t transport) {
    for(unsigned result=1u;result<=11u;result++) {
        struct fixture f;setup(&f,transport);bind(&f);f.acquire_result=(enum kui_loader_sd_result)result;
        bool retry=result==KUI_LOADER_SD_NOT_READY || result==KUI_LOADER_SD_UNSUPPORTED;
        CHECK(loan(&f)==(retry?KUI_VMU_SD_LEASE_BUSY:KUI_VMU_SD_LEASE_FAULT));
        CHECK(f.acquires==1u && !f.releases && !f.transactions && !f.wire && !f.held);
        if(retry) {
            CHECK(!f.owner.fault && !f.owner.busy && f.owner.busy_refusals==1u);
            f.acquire_result=KUI_LOADER_SD_OK;CHECK(loan(&f)==KUI_VMU_SD_LEASE_OK);
        } else refuses_forever(&f);
    }
    for(unsigned i=0u;i<4u;i++) {
        struct fixture f;setup(&f,transport);bind(&f);
        if(i==0u)f.port_fault=true;
        if(i==1u) {f.acquire_result=KUI_LOADER_SD_NOT_READY;f.acquire_fault=true;}
        if(i==2u)f.healthy=false;
        if(i==3u)f.acquire_fault=true;
        CHECK(loan(&f)==KUI_VMU_SD_LEASE_FAULT);
        CHECK(!f.transactions && !f.wire && f.releases==(i>=2u?1u:0u) && !f.held);
        refuses_forever(&f);
    }
}
static void callback_and_restore_failures(uint32_t transport) {
    for(unsigned mutation=2u;mutation<=25u;mutation++) {
        struct fixture f;setup(&f,transport);bind(&f);f.mutation=mutation;
        CHECK(loan(&f)==KUI_VMU_SD_LEASE_FAULT);
        CHECK(f.transactions==1u && f.acquires==1u && f.releases==1u && !f.held &&
            f.owner.completed==0u && f.owner.acquired==1u);
        refuses_forever(&f);
    }
    for(unsigned i=0u;i<3u;i++) {
        struct fixture f;setup(&f,transport);bind(&f);
        if(i==0u)f.callback_result=-1;
        if(i==1u)f.callback_result=1;
        if(i==2u)f.release_fault=true;
        CHECK(loan(&f)==KUI_VMU_SD_LEASE_FAULT && f.transactions==1u && f.releases==1u && !f.held);
        refuses_forever(&f);
    }
    struct fixture f;setup(&f,transport);bind(&f);
    f.owner.acquired=UINT32_MAX;f.owner.completed=UINT32_MAX;f.owner.busy_refusals=UINT32_MAX;
    CHECK(loan(&f)==KUI_VMU_SD_LEASE_OK && f.owner.acquired==UINT32_MAX && f.owner.completed==UINT32_MAX);
    f.stream=true;CHECK(loan(&f)==KUI_VMU_SD_LEASE_BUSY && f.owner.busy_refusals==UINT32_MAX);
}
int main(void) {
    invalid_bind();
    for(unsigned transport=KUI_STORAGE_SCIF;transport<=KUI_STORAGE_SCI;transport++) {
        ordinary_and_refusals(transport);acquisition_paths(transport);callback_and_restore_failures(transport);
    }
    printf("Disc VMU storage owner: %u checks passed; SCI/SCIF exact callback, scoped card, acquisition/refusal/reentry and restoration faults\n",checks);
    return 0;
}
