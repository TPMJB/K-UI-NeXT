/* SPDX-License-Identifier: GPL-3.0-only */
/* Production durable protocol, journal store and SD command implementation
 * across three distinct fresh workers on one unchanged synthetic SD card.
 * This verifies the storage side of SF -> menu -> PSII -> menu -> SF. It
 * does not execute a game, the menu adapter or worker-retirement hooks. */
#include "kui/vmu_durable.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include "../src/dreamcast/sci_sd_storage.h"
#include "vmu_sd_model.h"
#include <stdio.h>

static struct kui_vmu_durable workers[8];
static struct kui_vmu_durable *active;
static unsigned next_worker,leases,releases_without_publication,checkpoints;
static uint8_t response[524];
static size_t response_bytes;

static int transaction(void *context,struct kui_loader_sd *loan) {
    assert(context==active);
    return kui_vmu_durable_transaction(context,loan);
}
static int gateway(kui_vmu_sd_transaction callback,void *context) {
    assert(callback==transaction && context==active);++leases;
    if(sd.deny_enter || sd.deny_acquire)return KUI_VMU_SD_LEASE_BUSY;
    assert(enter(&sd) && acquire(&sd)==KUI_LOADER_SD_OK);
    uint32_t old_crc=active->handoff.block_crc[active->block],old_commits=active->commits;
    uint8_t old_committed=active->protocol.committed_valid;
    struct kui_loader_sd loan=card;
    int result=callback(context,&loan);
    /* Even with all SPI writes complete, nothing may be published until
     * this shared reader loan has been returned successfully. */
    assert(active->handoff.block_crc[active->block]==old_crc && active->commits==old_commits);
    assert(active->protocol.committed_valid==old_committed && !active->store.io.context);
    ++releases_without_publication;
    release(&sd);leave(&sd);
    if(result || !loan.ready || !sd.health) {
        card.ready=false;return KUI_VMU_SD_LEASE_FAULT;
    }
    return KUI_VMU_SD_LEASE_OK;
}
static int stage_read(void *context,uint64_t lba,void *out) {
    assert(context==&card && lba<=UINT32_MAX);
    return kui_loader_sd_read(&card,(uint32_t)lba,1u,out)==KUI_LOADER_SD_OK?0:-1;
}
static int stage_write(void *context,uint64_t lba,const void *data) {
    assert(context==&card && lba<=UINT32_MAX);
    return kui_sci_sd_write(&card,(uint32_t)lba,1u,data)==KUI_LOADER_SD_OK?0:-1;
}
static int stage_sync(void *context) {
    assert(context==&card);
    return kui_sci_sd_sync(&card)==KUI_LOADER_SD_OK?0:-1;
}
static void fresh_stage_handoff(struct kui_vmu_durable_handoff *h) {
    assert(!sd.owned && !sd.locked && !sd.pending_valid);
    assert(enter(&sd) && acquire(&sd)==KUI_LOADER_SD_OK);
    /* Actual redo recovery and explicit checkpoint, using the same production
     * SD reader/writer. Metadata ownership/VMU-filesystem validation is outside
     * this deliberately synthetic fixed-map test. */
    struct kui_vmu_store store;
    const struct kui_vmu_store_io io={&card,stage_read,stage_write,stage_sync};
    assert(!kui_vmu_store_open(&store,&io,&store_config));
    assert(!kui_vmu_store_checkpoint(&store));++checkpoints;
    release(&sd);leave(&sd);
    memset(h,0,sizeof(*h));
    h->card_sectors=1024u;h->high_capacity=1u;
    memcpy(h->cid,sd.cid,16u);memcpy(h->image_id,store_config.image_id,16u);
    h->base_count=h->journal_count=2u;
    for(unsigned i=0;i<2u;i++) {
        h->base[i]=(struct kui_vmu_durable_extent){(uint32_t)image[i].first_lba,image[i].sectors};
        h->journal[i]=(struct kui_vmu_durable_extent){(uint32_t)journal[i].first_lba,journal[i].sectors};
    }
    struct kui_sha256 hash;kui_sha256_init(&hash);
    for(unsigned block=0;block<256u;block++) {
        h->block_crc[block]=kui_retail_crc32(0,sd.media[image_lba(block)],512u);
        kui_sha256_update(&hash,sd.media[image_lba(block)],512u);
    }
    kui_sha256_digest(&hash,h->base_sha256);
    assert(!kui_vmu_durable_handoff_seal(h));
    assert(!memcmp(sd.media[950],(uint8_t[512]){0},512u));
}
static void start_worker(const struct kui_vmu_durable_handoff *h) {
    assert(next_worker<sizeof(workers)/sizeof(workers[0]));
    active=&workers[next_worker++];
    assert(!active->started);
    assert(!kui_vmu_durable_init_address(active,1u,h,gateway,transaction));
    assert(active->self==active && active->ready && !active->bound);
}
static void idle(void) {
    assert(!sd.locked && !sd.owned && !sd.selected && !sd.pending_valid);
    assert(!active->handling && !active->inflight && !active->store.io.context);
    assert(sd.enters==sd.leaves && sd.acquires==sd.releases);
}
static void frame(uint8_t out[140],unsigned command,unsigned block,unsigned phase) {
    memset(out,0,140u);out[0]=(uint8_t)command;out[1]=1u;
    if(command==1u)return;
    out[3]=(uint8_t)(command==12u?34u:2u);out[7]=2u;
    out[9]=(uint8_t)phase;out[11]=(uint8_t)block;
}
static void dispatch(const uint8_t *request,size_t bytes) {
    response_bytes=SIZE_MAX;memset(response,0xa5,sizeof(response));
    assert(kui_vmu_durable_handle(active,request,bytes,response,sizeof(response),&response_bytes)==1);
    assert(response_bytes==4u+4u*response[3] && response[2]==1u);idle();
}
static void compare(unsigned block,const uint8_t expected[512]) {
    uint8_t request[140];frame(request,11u,block,0u);dispatch(request,12u);
    assert(response[0]==8u && response_bytes==524u && !memcmp(response+12u,expected,512u));
}
static void phases(unsigned block,const uint8_t data[512],unsigned first,unsigned end) {
    for(unsigned phase=first;phase<end;phase++) {
        uint8_t request[140];frame(request,12u,block,phase);
        memcpy(request+12u,data+phase*128u,128u);dispatch(request,140u);
        assert(response[0]==7u && response_bytes==4u && active->protocol.active);
    }
}
static void finish(unsigned block,unsigned reply) {
    uint8_t request[140];frame(request,13u,block,4u);dispatch(request,12u);
    assert(response[0]==reply);
}
static void save(unsigned block,const uint8_t expected[512]) {
    unsigned writes=sd.writes,bytes=sd.bytes;
    phases(block,expected,0u,4u);
    assert(sd.writes==writes && sd.bytes==bytes && active->protocol.phases==15u);
    finish(block,7u);
    assert(!active->protocol.active && !active->protocol.phases && !active->protocol.commit_attempted);
    assert(!memcmp(sd.media[image_lba(block)],expected,512u));
    assert(sd.writes==writes+4u && active->handoff.block_crc[block]==kui_retail_crc32(0,expected,512u));
}
static void guards(void) {
    for(unsigned lba=0;lba<1024u;lba++)if(!allowed(lba))
        for(unsigned byte=0;byte<512u;byte++)assert(sd.media[lba][byte]==0x59u);
}
static void successive_games_keep_one_card(void) {
    uint8_t sf[512],psii[512],settings[512],old[512];
    for(unsigned i=0;i<512u;i++) {
        sf[i]=(uint8_t)(i*13u+0x31u);psii[i]=(uint8_t)(i*7u+0x61u);
        settings[i]=(uint8_t)(i*19u+0x91u);old[i]=0x59u;
    }
    struct kui_vmu_durable_handoff h;
    model_reset();fresh_stage_handoff(&h);start_worker(&h); /* Shining Force */
    compare(42u,old);save(42u,sf);save(200u,settings);
    struct kui_vmu_durable *sf_first=active;
    uint32_t first_requests=sf_first->requests;
    assert(!sf_first->protocol.active);idle();
    fresh_stage_handoff(&h);start_worker(&h); /* Phantasy Star II */
    assert(active!=sf_first && sf_first->requests==first_requests);
    compare(42u,sf);compare(200u,settings);compare(43u,old);save(43u,psii);
    struct kui_vmu_durable *psii_worker=active;
    uint32_t psii_requests=psii_worker->requests;
    fresh_stage_handoff(&h);start_worker(&h); /* Shining Force again */
    assert(active!=sf_first && active!=psii_worker);
    compare(42u,sf);compare(43u,psii);compare(200u,settings);
    assert(sf_first->requests==first_requests && psii_worker->requests==psii_requests);
    /* No live instance is copied, reset or reinitialized to achieve rebind. */
    unsigned bytes=sd.bytes;
    assert(kui_vmu_durable_init_address(active,1u,&h,gateway,transaction)==KUI_VMU_DURABLE_INTERNAL);
    assert(sd.bytes==bytes);guards();
}
static void staged_write_stays_pending_through_busy_retry(void) {
    struct kui_vmu_durable_handoff h;uint8_t next[512],old[512];
    memset(next,0xc3,sizeof(next));memset(old,0x59,sizeof(old));
    model_reset();fresh_stage_handoff(&h);start_worker(&h);
    unsigned writes=sd.writes,bytes=sd.bytes;
    phases(44u,next,0u,1u);
    assert(active->protocol.active && active->protocol.phases==1u && !active->commits);
    assert(sd.writes==writes && sd.bytes==bytes && !memcmp(sd.media[image_lba(44u)],old,512u));
    phases(44u,next,1u,4u);sd.deny_enter=true;finish(44u,0xfcu);
    assert(active->protocol.active && active->protocol.phases==15u && active->protocol.commit_attempted);
    assert(!active->commits && sd.writes==writes && !memcmp(active->protocol.staged,next,512u));
    sd.deny_enter=false;finish(44u,7u);
    assert(!active->protocol.active && active->commits==1u);compare(44u,next);guards();
}
int main(void) {
    successive_games_keep_one_card();staged_write_stays_pending_through_busy_retry();
    assert(next_worker==4u && checkpoints==4u && releases_without_publication==leases-1u);
    puts("Successive shared A1 workers: SF -> PSII -> SF saves/settings survive fresh stage checkpoints; pending write retry stays active (menu routing/retirement not executed)");
    return 0;
}
