/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared real-byte SPI/store fixture; no provider or runtime replacement. */
#ifndef KUI_TEST_ARMADA_CE_VMU_SD_FIXTURE_H
#define KUI_TEST_ARMADA_CE_VMU_SD_FIXTURE_H
#include "kui/armada_ce_vmu_sd.h"
#include "kui/retail_image.h"
#include "vmu_sd_model.h"
#include <stdio.h>
#include <stddef.h>

static struct kui_armada_ce_vmu_sd endpoint;
static struct kui_armada_ce_vmu_sd *active_endpoint=&endpoint;
static struct kui_vmu_durable_handoff handoff;
static unsigned checks,mask,loan_owned,begin_calls,end_calls,port_claims,port_returns,csds;
static unsigned begin_result,end_failure,port_refused,port_release_failure,guard_refused;
static unsigned port_release_unverified;
static unsigned csd_bad,csd_crc_bad,cid_after_write,worker=1u,fence,csd_frame;
static unsigned cid_after_read;
static uint8_t csd_command[6];
#define CHECK(x) do {++checks;assert(x);} while(0)

/* Add read-only CSD to the independent card oracle, without replacing its
 * actual block/CRC/status/busy implementation. Our fixture is1024 SDHC sectors. */
static uint8_t provider_transfer(void *context,uint8_t value,bool slow) {
    CHECK(!mask && worker && !fence && loan_owned);
    if(csd_frame || (sd.selected && !sd.frame_n && sd.reply_at==sd.reply_n &&
       !sd.busy && !sd.write_phase && value==0x49u)) {
        CHECK(context==&sd && sd.locked && sd.owned && !slow);
        ++sd.bytes;++sd.ticks;csd_command[csd_frame++]=value;
        if(csd_frame==6u) {
            CHECK(csd_command[5]==crc7(csd_command));
            for(unsigned i=1;i<5u;i++) CHECK(!csd_command[i]);
            uint8_t csd[16]={0x40u};csd[9]=(uint8_t)csd_bad;
            queue_payload(csd,16u,csd_crc_bad!=0u);csd_frame=0u;++csds;
        }
        return 0xffu;
    }
    uint8_t result=transfer(context,value,slow);
    if(cid_after_write && sd.writes && !sd.busy && !sd.pending_valid && !sd.write_phase) {
        sd.cid[0]^=1u;cid_after_write=0u;
    }
    if(cid_after_read && sd.reads==2u && sd.reply_at==sd.reply_n) {
        sd.cid[0]^=1u;cid_after_read=0u;
    }
    return result;
}
/* The real PIO MMIO implementation has its own exhaustive register tests.
 * These stubs bind its typed API to the real SPI card oracle for this test. */
int kui_armada_ce_vmu_sd_pio_init(struct kui_armada_ce_vmu_sd_pio *p) {
    CHECK(p && !p->self);
    active_endpoint=(struct kui_armada_ce_vmu_sd *)((uint8_t *)p-
        offsetof(struct kui_armada_ce_vmu_sd,pio));
    p->self=(uintptr_t)p;p->magic=1u;return 0;
}
int kui_armada_ce_vmu_sd_pio_bus(struct kui_armada_ce_vmu_sd_pio *p,struct kui_loader_sd_bus *out) {
    CHECK(p->self==(uintptr_t)p && out);
    *out=(struct kui_loader_sd_bus){.ctx=&sd,.select=select_card,.transfer=provider_transfer,.ticks=ticks};return 0;
}
int kui_armada_ce_vmu_sd_pio_acquire(struct kui_armada_ce_vmu_sd_pio *p) {
    CHECK(mask==0xf0u && loan_owned && !p->acquired);++port_claims;
    if(port_refused) {p->fault=1u;return 1;}
    CHECK(acquire(&sd)==KUI_LOADER_SD_OK);p->acquired=1u;return 0;
}
bool kui_armada_ce_vmu_sd_pio_healthy(const struct kui_armada_ce_vmu_sd_pio *p) {
    return p->self==(uintptr_t)p && p->acquired && !p->fault && sd.health;
}
int kui_armada_ce_vmu_sd_pio_release(struct kui_armada_ce_vmu_sd_pio *p) {
    CHECK(mask==0xf0u && loan_owned && p->acquired);++port_returns;
    if(port_release_unverified) {
        /* Actual MMIO tests prove the stopped-module guard. Preserve the
         * same ownership/result contract at this provider test boundary. */
        p->fault=KUI_ARMADA_CE_VMU_SD_PIO_RESTORE;
        p->detail=KUI_ARMADA_CE_VMU_SD_PIO_STAGE_STANDBY<<16;return (int)p->fault;
    }
    release(&sd);p->acquired=0u;
    if(port_release_failure || !sd.health) p->fault=1u;
    return p->fault?1:0;
}
static bool guard_enter(void *context,uint32_t *saved) {
    CHECK(context==&sd && !mask);if(guard_refused) return false;
    *saved=mask;mask=0xf0u;return true;
}
static void guard_leave(void *context,uint32_t saved) {
    CHECK(context==&sd && mask==0xf0u && saved==0u);mask=saved;
}
static bool worker_ready(void *context) {CHECK(context==&sd);return worker && !mask && !fence;}
static int loan_begin(struct kui_loader_sd *copy) {
    CHECK(!mask && !loan_owned && copy);++begin_calls;
    if(begin_result) return (int)begin_result;
    CHECK(enter(&sd));loan_owned=1u;*copy=card;return 0;
}
static int loan_end(bool ok) {
    CHECK(!mask && loan_owned && !active_endpoint->store.io.context);++end_calls;
    /* Runtime CRCs/counters cannot acknowledge an unfinished hardware lease. */
    CHECK(active_endpoint->commits==0u || active_endpoint->handoff.seal==0u);
    if(port_release_unverified) {
        CHECK(!ok && sd.owned && active_endpoint->pio.acquired);
        sd.health=false;loan_owned=0u;return 2; /* Permanent reader quarantine. */
    }
    CHECK(!sd.owned);
    if(!ok || end_failure) sd.health=false;
    loan_owned=0u;leave(&sd);return end_failure?2:0;
}
static inline struct kui_armada_ce_vmu_sd_ops ops(void) {
    return (struct kui_armada_ce_vmu_sd_ops){loan_begin,loan_end,{&sd,guard_enter,guard_leave,worker_ready}};
}
static void prepare_model(void) {
    model_reset();active_endpoint=&endpoint;memset(&endpoint,0,sizeof(endpoint));memset(&handoff,0,sizeof(handoff));
    mask=loan_owned=begin_calls=end_calls=port_claims=port_returns=csds=0u;
    begin_result=end_failure=port_refused=port_release_failure=guard_refused=0u;
    port_release_unverified=0u;
    csd_bad=csd_crc_bad=cid_after_write=cid_after_read=fence=csd_frame=0u;worker=1u;
    handoff.card_sectors=1024u;handoff.high_capacity=1u;handoff.base_count=handoff.journal_count=2u;
    memcpy(handoff.cid,sd.cid,16u);memcpy(handoff.image_id,store_config.image_id,16u);
    handoff.base_sha256[0]=1u; /* Synthetic already-prepared checkpoint provenance. */
    for(unsigned i=0;i<2u;i++) {
        handoff.base[i]=(struct kui_vmu_durable_extent){(uint32_t)image[i].first_lba,image[i].sectors};
        handoff.journal[i]=(struct kui_vmu_durable_extent){(uint32_t)journal[i].first_lba,journal[i].sectors};
    }
    for(unsigned b=0;b<256u;b++) handoff.block_crc[b]=kui_retail_crc32(0,sd.media[image_lba(b)],512u);
    CHECK(!kui_vmu_durable_handoff_seal(&handoff));
}
static inline void setup(void) {
    prepare_model();
    struct kui_armada_ce_vmu_sd_ops platform=ops();
    CHECK(!kui_armada_ce_vmu_sd_init(&endpoint,&handoff,&platform));
    CHECK(!sd.bytes && !endpoint.bound && !endpoint.faulted);
}
static inline void idle(void) {
    CHECK(!mask && !loan_owned && !sd.owned && !sd.locked && !sd.selected);
    CHECK(!active_endpoint->busy && !active_endpoint->store.io.context && !active_endpoint->pio.acquired);
    CHECK(port_claims==port_returns || port_refused);
}
#endif
