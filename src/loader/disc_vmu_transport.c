/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_vmu_transport.h"
#include "kui/storage.h"
#include "retail_sd.h"
#include "sci_sd_bus.h"
#include <string.h>

#if !KUI_NATIVE_VMU_ONLY || !defined(KUI_RETAIL_TRANSPORT) || \
    (KUI_RETAIL_TRANSPORT != 0 && KUI_RETAIL_TRANSPORT != 1)
#error "Physical-disc saves require an independently linked SCI or SCIF VMU bus"
#endif
#if !KUI_VMU_NATIVE_PERSISTENT_PROBE
#error "Physical-disc saves require persistent serial restoration-fault checks"
#endif

static uint32_t current_sr(void *context) {
    (void)context;
#ifdef KUI_DISC_VMU_TRANSPORT_TEST
    extern uint32_t kui_disc_vmu_transport_test_sr(void);
    return kui_disc_vmu_transport_test_sr();
#else
    uint32_t value;
    __asm__ volatile("stc sr,%0":"=r"(value)::"memory");
    return value;
#endif
}
static bool stream_active(void *context) {
    (void)context;
    /* This owner never starts an image stream. Its single exact callback
     * performs bounded save operations entirely inside the ownership loan. */
    return false;
}
static enum kui_loader_sd_result acquire(void *context) {
    (void)context;
#if KUI_RETAIL_TRANSPORT == 1
    return kui_sci_sd_acquire();
#else
    return kui_retail_sd_acquire();
#endif
}
static void release(void *context) {
    (void)context;
#if KUI_RETAIL_TRANSPORT == 1
    kui_sci_sd_release();
#else
    kui_retail_sd_release();
#endif
}
static bool healthy(void *context) {
    (void)context;
#if KUI_RETAIL_TRANSPORT == 1
    return kui_sci_sd_healthy();
#else
    return kui_retail_sd_healthy();
#endif
}
static bool faulted(void *context) {
    (void)context;
#if KUI_RETAIL_TRANSPORT == 1
    return kui_sci_sd_faulted();
#else
    return kui_retail_sd_faulted();
#endif
}

bool kui_disc_vmu_transport_bind(struct kui_disc_vmu_transport *transport,
    uint32_t storage_transport,const uint8_t cid[16],
    const struct kui_loader_sd *prepared,kui_vmu_sd_transaction transaction,
    void *transaction_context) {
    if(!transport || !cid || !prepared || !transaction ||
       storage_transport!=KUI_RETAIL_TRANSPORT ||
       (current_sr(NULL)&UINT32_C(0xf0))!=UINT32_C(0xf0)) return false;
    const uint8_t *bytes=(const uint8_t *)transport;
    for(size_t i=0u;i<sizeof(*transport);i++) if(bytes[i]) return false;
    struct kui_disc_vmu_storage_config config={0};
    config.transport=storage_transport;
    memcpy(config.cid,cid,sizeof(config.cid));
    config.transaction=transaction;config.transaction_context=transaction_context;
    if(faulted(NULL)) return false;
#if KUI_RETAIL_TRANSPORT == 1
    const struct kui_loader_sd_bus *bus=kui_sci_sd_bus();
    if(!bus) return false;
    config.card=*prepared;
    config.card.bus=*bus;
#else
    if(kui_retail_sd_adopt(&config.card,prepared)!=KUI_LOADER_SD_OK) return false;
#endif
    const struct kui_disc_vmu_storage_ops ops={
        current_sr,stream_active,acquire,release,healthy,faulted
    };
    return kui_disc_vmu_storage_bind(&transport->storage,&config,&ops,NULL);
}

int kui_disc_vmu_transport_loan(struct kui_disc_vmu_transport *transport,
    kui_vmu_sd_transaction transaction,void *transaction_context) {
    return kui_disc_vmu_storage_loan(transport?&transport->storage:NULL,
        transaction,transaction_context);
}
