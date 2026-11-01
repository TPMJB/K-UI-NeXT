/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_vmu_transport.h"
#include "kui/storage.h"
#include "retail_sd.h"
#include "sci_sd_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static struct {
    uint32_t sr;
    unsigned acquire,release,adopt,transactions,wire,wrong_backend;
    enum kui_loader_sd_result acquired,adopted;
    bool held,fault,healthy,release_fault,missing_bus;
    unsigned mutation;
} bus;
static struct kui_disc_vmu_transport transport;
static const uint8_t cid[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

uint32_t kui_disc_vmu_transport_test_sr(void) {return bus.sr;}
static void begin(void *context) {(void)context;CHECK(false);}
static void end(void *context) {(void)context;CHECK(false);}
static void select_card(void *context,bool selected) {
    CHECK(context==NULL && bus.held);(void)selected;
}
static uint8_t transfer(void *context,uint8_t data,bool slow) {
    CHECK(context==NULL && bus.held && !slow);++bus.wire;return data;
}
static uint32_t ticks(void *context) {CHECK(context==NULL);return 200u;}
static const struct kui_loader_sd_bus resident_bus={NULL,begin,end,select_card,transfer,ticks,NULL};
static enum kui_loader_sd_result acquire(void) {
    ++bus.acquire;CHECK(!bus.held);
    if(bus.acquired==KUI_LOADER_SD_OK) bus.held=true;
    return bus.acquired;
}
static void release(void) {
    ++bus.release;CHECK(bus.held);bus.held=false;
    if(bus.release_fault) bus.fault=true;
}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {
#if KUI_RETAIL_TRANSPORT == 1
    return acquire();
#else
    ++bus.wrong_backend;CHECK(false);return KUI_LOADER_SD_UNSUPPORTED;
#endif
}
void kui_sci_sd_release(void) {
#if KUI_RETAIL_TRANSPORT == 1
    release();
#else
    ++bus.wrong_backend;CHECK(false);
#endif
}
bool kui_sci_sd_healthy(void) {
#if KUI_RETAIL_TRANSPORT == 1
    CHECK(bus.held);return bus.healthy;
#else
    ++bus.wrong_backend;CHECK(false);return false;
#endif
}
bool kui_sci_sd_faulted(void) {
#if KUI_RETAIL_TRANSPORT == 1
    return bus.fault;
#else
    ++bus.wrong_backend;CHECK(false);return true;
#endif
}
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) {
#if KUI_RETAIL_TRANSPORT == 1
    return bus.missing_bus?NULL:&resident_bus;
#else
    ++bus.wrong_backend;CHECK(false);return NULL;
#endif
}
enum kui_loader_sd_result kui_retail_sd_acquire(void) {
#if KUI_RETAIL_TRANSPORT == 0
    return acquire();
#else
    ++bus.wrong_backend;CHECK(false);return KUI_LOADER_SD_UNSUPPORTED;
#endif
}
void kui_retail_sd_release(void) {
#if KUI_RETAIL_TRANSPORT == 0
    release();
#else
    ++bus.wrong_backend;CHECK(false);
#endif
}
bool kui_retail_sd_healthy(void) {
#if KUI_RETAIL_TRANSPORT == 0
    CHECK(bus.held);return bus.healthy;
#else
    ++bus.wrong_backend;CHECK(false);return false;
#endif
}
bool kui_retail_sd_faulted(void) {
#if KUI_RETAIL_TRANSPORT == 0
    return bus.fault;
#else
    ++bus.wrong_backend;CHECK(false);return true;
#endif
}
enum kui_loader_sd_result kui_retail_sd_adopt(struct kui_loader_sd *out,
    const struct kui_loader_sd *prepared) {
#if KUI_RETAIL_TRANSPORT == 0
    ++bus.adopt;
    if(bus.adopted!=KUI_LOADER_SD_OK) return bus.adopted;
    *out=*prepared;out->bus=resident_bus;
    return KUI_LOADER_SD_OK;
#else
    (void)out;(void)prepared;++bus.wrong_backend;CHECK(false);return KUI_LOADER_SD_UNSUPPORTED;
#endif
}
static int wrong_transaction(void *context,struct kui_loader_sd *loan) {
    (void)context;(void)loan;CHECK(false);return -1;
}
static int transaction(void *context,struct kui_loader_sd *loan) {
    CHECK(context==&bus && bus.held && loan->ready && !loan->slow);
    CHECK(loan->blocks==12345u && loan->high_capacity);
    CHECK(loan->bus.begin==begin && loan->bus.end==end && loan->bus.ctx==NULL &&
        loan->bus.transfer==transfer && loan->bus.ticks==ticks && !loan->bus.transfer_block);
    ++bus.transactions;
    loan->bus.select(loan->bus.ctx,true);
    CHECK(loan->bus.transfer(loan->bus.ctx,0xa5u,false)==0xa5u);
    loan->bus.select(loan->bus.ctx,false);
    loan->last_command=24u;loan->last_response=0u;
    if(bus.mutation==1u) {
        CHECK(kui_disc_vmu_transport_loan(&transport,transaction,&bus)==KUI_VMU_SD_LEASE_BUSY);
        CHECK(bus.held && !bus.release);
    }
    if(bus.mutation==2u) return -1;
    if(bus.mutation==3u) loan->ready=false;
    if(bus.mutation==4u) bus.healthy=false;
    if(bus.mutation==5u) bus.fault=true;
    if(bus.mutation==6u) loan->bus.transfer=NULL;
    return 0;
}
static struct kui_loader_sd prepared(void) {
    /* No stage callback may survive: all seven bus slots deliberately contain
     * an unusable descriptor. Only protocol values can be adopted. */
    return (struct kui_loader_sd){.bus={0},.blocks=12345u,.high_capacity=true,
        .ready=true,.slow=false,.last_command=59u,.last_response=0u};
}
static void reset(void) {
    memset(&bus,0,sizeof(bus));memset(&transport,0,sizeof(transport));
    bus.sr=0xf0u;bus.healthy=true;
}
static bool bind(struct kui_loader_sd *card) {
    return kui_disc_vmu_transport_bind(&transport,KUI_RETAIL_TRANSPORT,cid,card,transaction,&bus);
}
static void bound(void) {
    struct kui_loader_sd card=prepared();CHECK(bind(&card));
    CHECK(!bus.acquire && !bus.release && !bus.wire && !bus.transactions && !bus.wrong_backend);
    memset(&card,0,sizeof(card));
}
static void run(void) {
    reset();bound();
    CHECK(kui_disc_vmu_transport_loan(&transport,transaction,&bus)==KUI_VMU_SD_LEASE_OK);
    CHECK(bus.acquire==1u && bus.release==1u && bus.transactions==1u && bus.wire==1u && !bus.held);
    CHECK(!transport.storage.fault && transport.storage.completed==1u &&
        transport.storage.last_command==24u && !transport.storage.last_response);
    struct kui_disc_vmu_transport copied=transport;
    CHECK(kui_disc_vmu_transport_loan(&copied,transaction,&bus)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(bus.acquire==1u);
    CHECK(kui_disc_vmu_transport_loan(&transport,wrong_transaction,&bus)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(kui_disc_vmu_transport_loan(&transport,transaction,NULL)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(kui_disc_vmu_transport_loan(NULL,transaction,&bus)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(bus.acquire==1u);
    struct kui_loader_sd card=prepared();
    CHECK(!bind(&card));
    CHECK(kui_disc_vmu_transport_loan(&transport,transaction,&bus)==KUI_VMU_SD_LEASE_OK);

    for(unsigned i=0;i<7u;i++) {
        reset();bound();bus.mutation=i;
        int result=kui_disc_vmu_transport_loan(&transport,transaction,&bus);
        CHECK(result==(i<2u?KUI_VMU_SD_LEASE_OK:KUI_VMU_SD_LEASE_FAULT));
        CHECK(bus.release==1u && !bus.held && bus.transactions==1u);
        if(i>=2u) {
            CHECK(kui_disc_vmu_transport_loan(&transport,transaction,&bus)==KUI_VMU_SD_LEASE_FAULT);
            CHECK(bus.acquire==1u && bus.release==1u && bus.transactions==1u);
        }
    }
    reset();bound();bus.release_fault=true;
    CHECK(kui_disc_vmu_transport_loan(&transport,transaction,&bus)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(bus.release==1u && !bus.held && transport.storage.fault);
    CHECK(kui_disc_vmu_transport_loan(&transport,transaction,&bus)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(bus.acquire==1u);

    for(unsigned i=0;i<3u;i++) {
        reset();bound();bus.acquired=i==0u?KUI_LOADER_SD_NOT_READY:
            i==1u?KUI_LOADER_SD_UNSUPPORTED:KUI_LOADER_SD_TIMEOUT;
        CHECK(kui_disc_vmu_transport_loan(&transport,transaction,&bus)==
            (i<2u?KUI_VMU_SD_LEASE_BUSY:KUI_VMU_SD_LEASE_FAULT));
        CHECK(bus.acquire==1u && !bus.release && !bus.transactions && !bus.wire);
        bus.acquired=KUI_LOADER_SD_OK;
        CHECK(kui_disc_vmu_transport_loan(&transport,transaction,&bus)==
            (i<2u?KUI_VMU_SD_LEASE_OK:KUI_VMU_SD_LEASE_FAULT));
    }
    for(unsigned i=0;i<10u;i++) {
        reset();card=prepared();
        uint8_t blank_cid[16]={0};
        bool result=false;
        switch(i) {
        case 0u:card.ready=false;break;
        case 1u:card.slow=true;break;
        case 2u:card.blocks=0u;break;
        case 3u:bus.sr=0xe0u;break;
        case 4u:bus.fault=true;break;
        case 5u:result=kui_disc_vmu_transport_bind(&transport,KUI_RETAIL_TRANSPORT^1u,cid,&card,transaction,&bus);break;
        case 6u:result=kui_disc_vmu_transport_bind(&transport,KUI_RETAIL_TRANSPORT,blank_cid,&card,transaction,&bus);break;
        case 7u:result=kui_disc_vmu_transport_bind(NULL,KUI_RETAIL_TRANSPORT,cid,&card,transaction,&bus);break;
        case 8u:result=kui_disc_vmu_transport_bind(&transport,KUI_RETAIL_TRANSPORT,cid,&card,NULL,&bus);break;
        case 9u:transport.storage.completed=1u;break;
        }
        if(i<5u || i==9u) result=bind(&card);
        CHECK(!result && !bus.acquire && !bus.release && !bus.wire && !bus.transactions && !bus.wrong_backend);
        CHECK(transport.storage.completed==(i==9u?1u:0u) && !transport.storage.state);
    }
#if KUI_RETAIL_TRANSPORT == 1
    reset();card=prepared();bus.missing_bus=true;CHECK(!bind(&card));
#else
    reset();card=prepared();bus.adopted=KUI_LOADER_SD_NOT_READY;CHECK(!bind(&card));
#endif
    CHECK(!transport.storage.state && !bus.acquire && !bus.release && !bus.wrong_backend);
}
int main(void) {
    run();
    printf("Physical-disc %s adapter: %u checks; independently rebound bus, exact worker/context, safe refusal and restoration faults\n",
        KUI_RETAIL_TRANSPORT==1?"SCI":"SCIF",checks);
    return 0;
}
