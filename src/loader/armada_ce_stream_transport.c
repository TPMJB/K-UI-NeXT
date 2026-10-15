/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_stream_transport.h"
#include "kui/armada_ce_stream_seed.h"
#include "kui/armada_ce_stream_model.h"
#include "kui/retail_cursor.h"
#include "retail_async.h"
#include <string.h>

#define DATA_QUANTUM 16u
#define AUDIO_QUANTUM 8u
/* A16-sector2352-byte source job can touch one unaligned partial block at
 * each end. Let its urgent grant finish before returning SCI to DATA. */
#define AUDIO_URGENT_QUANTUM ((KUI_ARMADA_CE_STREAM_BATCH_BYTES+1022u)/512u)
#define AUDIO_URGENT_ENTER_BYTES 176400u
#define AUDIO_URGENT_EXIT_BYTES 264600u
#define TOKEN_LIMIT 65536u
#define RETRIES 8u
#define OWNER_NONE 0u
#define OWNER_DATA 1u
#define OWNER_AUDIO 2u
struct held_block { uint32_t used,lba,result; uint8_t bytes[512]; };
static struct {
    struct kui_armada_ce_stream_bus bus;
    const struct kui_retail_manifest *map;
    uintptr_t first,end,destination;
    struct kui_retail_cursor cursor;
    struct kui_armada_ce_stream_transport_snapshot stats;
    struct held_block held[2];
    uint32_t initialized,generation,data_token,data_credit,audio_credit,cancelled,early_polled,urgent;
} t;
#ifdef KUI_ARMADA_CE_STREAM_TRANSPORT_TEST
void kui_armada_ce_stream_transport_test_reset(void) { memset(&t,0,sizeof(t)); }
#endif

void kui_armada_ce_stream_transport_priority(uint32_t fresh_bytes,bool active) {
    if(!active || fresh_bytes>=AUDIO_URGENT_EXIT_BYTES) t.urgent=0u;
    else if(fresh_bytes<=AUDIO_URGENT_ENTER_BYTES) t.urgent=1u;
}
bool kui_armada_ce_stream_transport_urgent(void) { return t.urgent!=0u; }
bool kui_armada_ce_stream_transport_ready_to_pump(void) {
    return t.stats.attached && t.stats.status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING &&
        t.stats.owner==OWNER_AUDIO && !t.cancelled && !t.bus.busy() &&
        t.bus.ready(t.cursor.block);
}

static bool span(uintptr_t address,uintptr_t first,uintptr_t end,size_t bytes) {
    return address>=first && address<end && bytes<=end-address;
}
#ifndef KUI_ARMADA_CE_STREAM_TRANSPORT_TEST
static bool low_code(uintptr_t address) {
    return !(address&1u) && address>=0x8c008300u && address<0x8c00d800u;
}
#endif
static bool __attribute__((noinline)) bus_valid(const struct kui_armada_ce_stream_bus *b) {
#ifndef KUI_ARMADA_CE_STREAM_TRANSPORT_TEST
    if(!b || ((uintptr_t)b&3u) ||
       !span((uintptr_t)b,0x8c008300u,0x8c00d800u,sizeof(*b))) return false;
#endif
    if(!b || b->version!=KUI_ARMADA_CE_STREAM_BUS_VERSION || b->bytes!=sizeof(*b) ||
       !b->region || !b->open || !b->hook || !b->unhook || !b->wake || !b->busy || !b->ready ||
       !b->poll || !b->wait || !b->fetch || !b->take || !b->discard || !b->token_budget) return false;
#ifndef KUI_ARMADA_CE_STREAM_TRANSPORT_TEST
    if(((uintptr_t)b->region&3u) ||
       !span((uintptr_t)b->region,0x8c008300u,0x8c00d800u,sizeof(*b->region))) return false;
    if(!low_code((uintptr_t)b->open) || !low_code((uintptr_t)b->hook) ||
       !low_code((uintptr_t)b->unhook) || !low_code((uintptr_t)b->wake) || !low_code((uintptr_t)b->busy) ||
       !low_code((uintptr_t)b->ready) || !low_code((uintptr_t)b->poll) ||
       !low_code((uintptr_t)b->wait) || !low_code((uintptr_t)b->fetch) ||
       !low_code((uintptr_t)b->take) || !low_code((uintptr_t)b->discard) ||
       !low_code((uintptr_t)b->token_budget)) return false;
#endif
    return true;
}
static struct kui_retail_async *data(void) { return &t.bus.region->engine; }
static bool data_active(void) {
    struct kui_retail_async *e=data();
    return e->active && !e->failed && t.bus.region->shared.service.pending &&
        e->token==t.bus.region->shared.service.token;
}
static void discard_holds(void) {
    t.held[0].used=t.held[1].used=0;
    t.data_token=data()->token;
}
static void refresh_holds(void) {
    if(t.data_token!=data()->token) discard_holds();
}
static void audio_failure(uint32_t result) {
    if(result==KUI_SCI_STREAM_OVERRUN) t.early_polled=1u;
    ++t.stats.retries;
    if(t.stats.retries>RETRIES || result==KUI_SCI_STREAM_RESET || result==KUI_SCI_STREAM_BUSY)
        t.stats.status=KUI_ARMADA_CE_STREAM_TRANSPORT_IO;
}
static void write_audio(void *unused,uint32_t offset,const uint8_t *bytes,uint32_t count) {
    (void)unused;
    if(t.cancelled || offset!=t.stats.bytes || count>t.stats.total-t.stats.bytes) {
        t.stats.status=KUI_ARMADA_CE_STREAM_TRANSPORT_IO;return;
    }
    memcpy((void *)(t.destination+offset),bytes,count);t.stats.bytes+=count;
}
bool kui_armada_ce_stream_transport_init(const struct kui_retail_manifest *map,
    uintptr_t first,uintptr_t end) {
    if(t.initialized || !map || first>=end || (first&3u) ||
       kui_retail_manifest_validate(map)!=KUI_GAME_OK) return false;
#ifndef KUI_ARMADA_CE_STREAM_TRANSPORT_TEST
    if(!((first==KUI_ARMADA_CE_STREAM_SOURCE_BEGIN &&
          end==KUI_ARMADA_CE_STREAM_SOURCE_BEGIN+KUI_ARMADA_CE_STREAM_SOURCE_BYTES) ||
         (first==KUI_ARMADA_CE_STREAM_WORK_BEGIN && end==KUI_ARMADA_CE_STREAM_WORK_END))) return false;
#endif
    t.map=map;t.first=first;t.end=end;t.initialized=1u;return true;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_submit(
    uint32_t epoch,uint32_t lba,uint32_t sectors,uintptr_t destination,uint32_t capacity) {
    if(!t.initialized || !t.stats.attached || !epoch || !sectors || sectors>64u ||
       sectors>UINT32_MAX/2352u || (destination&3u) || capacity<sectors*2352u ||
       !span(destination,t.first,t.end,sectors*2352u)) return KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT;
    if(epoch<=t.generation) return KUI_ARMADA_CE_STREAM_TRANSPORT_STALE;
    if(t.stats.status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING || t.stats.owner==OWNER_AUDIO)
        return KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY;
    if(kui_retail_image_check_validated(t.map,lba,sectors,KUI_GAME_SECTOR_RAW)!=KUI_GAME_OK)
        return KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT;
    /* RAW accepts data too; this transport is restricted to backed audio. */
    for(uint32_t n=0;n<sectors;n++) {
        bool found=false;
        for(uint32_t i=0;i<t.map->track_count;i++) {
            const struct kui_retail_track *track=&t.map->slots[i].track;
            if(lba+n>=track->start_lba && lba+n<track->end_lba) {
                if(kui_retail_track_control(track) || !track->extent_count)
                    return KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT;
                found=true;break;
            }
        }
        if(!found) return KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT;
    }
    if(kui_retail_cursor_begin(&t.cursor,t.map,lba,sectors,KUI_GAME_SECTOR_RAW,write_audio,NULL)!=KUI_GAME_OK)
        return KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT;
    t.destination=destination;t.generation=epoch;t.cancelled=t.early_polled=0;
    t.stats.epoch=epoch;t.stats.bytes=0;t.stats.total=sectors*2352u;
    t.stats.retries=0;t.stats.status=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;
    return KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;
}
/* End at most one reception. The synchronous DATA fence may wait for that
 * block only. The worker and IRQ never call wait. */
static uint32_t finish(bool wait) {
    if(!t.bus.busy()) return KUI_SCI_STREAM_OK;
    uint32_t result=t.bus.poll(true);
    if(t.bus.busy() && result==KUI_SCI_STREAM_PENDING && wait) result=t.bus.wait();
    return result;
}
static void park_data(uint32_t poll_result) {
    refresh_holds();
    if(!data_active()) return;
    uint32_t lba=data()->cursor.block;
    for(unsigned n=0;n<2u;n++) {
        if(n && data()->cursor.run<2u) break;
        bool exists=false;
        for(unsigned h=0;h<2u;h++) if(t.held[h].used && t.held[h].lba==lba+n) exists=true;
        if(exists) continue;
        enum kui_sci_stream_result result=KUI_SCI_STREAM_PENDING;
        const uint8_t *block=t.bus.take(lba+n,&result);
        if(!n && poll_result>KUI_SCI_STREAM_BUSY && !block) result=(enum kui_sci_stream_result)poll_result;
        if(!block && result==KUI_SCI_STREAM_OK) result=KUI_SCI_STREAM_RESET;
        if(!block && result==KUI_SCI_STREAM_PENDING) continue;
        for(unsigned h=0;h<2u;h++) if(!t.held[h].used) {
            t.held[h].lba=lba+n;t.held[h].result=result;t.held[h].used=1u;
            if(block) memcpy(t.held[h].bytes,block,512u);
            ++t.stats.holds;break;
        }
    }
}
static bool audio_grant(void) {
    if(t.stats.owner==OWNER_AUDIO) return true;
    if(data_active() && data()->spill_bytes<=KUI_RETAIL_ASYNC_SPILL_BYTES-512u &&
       t.data_credit<DATA_QUANTUM) return false;
    enum kui_sci_stream_result result=(enum kui_sci_stream_result)finish(false);
    if(t.bus.busy()) return false;
    park_data(result);t.bus.discard();
    t.stats.owner=OWNER_AUDIO;
    t.audio_credit=t.urgent?AUDIO_URGENT_QUANTUM:AUDIO_QUANTUM;t.data_credit=0;
    return true;
}
static void release_audio(void) {
    t.bus.discard();t.stats.owner=OWNER_NONE;t.audio_credit=0;
    if(!t.bus.busy()) t.bus.unhook();
    if(data_active()) t.bus.wake();
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_pump(void) {
    if(!t.stats.attached) return KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT;
    t.bus.token_budget(true);
    if(t.stats.owner==OWNER_AUDIO && (t.cancelled || t.stats.status!=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING)) {
        (void)finish(false);
        if(t.bus.busy()) return KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY;
        release_audio();return (enum kui_armada_ce_stream_transport_status)t.stats.status;
    }
    if(t.stats.status!=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING)
        return (enum kui_armada_ce_stream_transport_status)t.stats.status;
    if(!data()->opened && !t.bus.open()) {t.stats.status=KUI_ARMADA_CE_STREAM_TRANSPORT_IO;return KUI_ARMADA_CE_STREAM_TRANSPORT_IO;}
    if(!audio_grant()) return KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;
    enum kui_sci_stream_result result=(enum kui_sci_stream_result)finish(false);
    if(t.bus.busy()) return KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;
    if(result>KUI_SCI_STREAM_BUSY) audio_failure(result);
    if(t.stats.status!=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING) {release_audio();return KUI_ARMADA_CE_STREAM_TRANSPORT_IO;}
    uint32_t lba=t.cursor.block;
    const uint8_t *block=t.bus.take(lba,&result);
    if(block) {
        t.stats.retries=0;t.early_polled=0;
        if(kui_retail_cursor_feed(&t.cursor,block)!=KUI_GAME_OK) t.stats.status=KUI_ARMADA_CE_STREAM_TRANSPORT_IO;
        ++t.stats.blocks;++t.stats.audio_blocks;
        if(t.audio_credit) --t.audio_credit;
        if(t.cursor.done==t.cursor.count && t.stats.status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING)
            t.stats.status=KUI_ARMADA_CE_STREAM_TRANSPORT_DONE;
        if(t.stats.status!=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING ||
           (!t.audio_credit && data_active())) {
            release_audio();return (enum kui_armada_ce_stream_transport_status)t.stats.status;
        }
        lba=t.cursor.block;
    } else if(result==KUI_SCI_STREAM_CRC) audio_failure(result);
    if(t.stats.status!=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING) {release_audio();return KUI_ARMADA_CE_STREAM_TRANSPORT_IO;}
    result=t.bus.fetch(lba,TOKEN_LIMIT,t.early_polled || t.stats.retries>=2u);
    if(result>KUI_SCI_STREAM_BUSY) audio_failure(result);
    if(t.bus.busy()) t.bus.hook();
    else t.bus.unhook();
    return (enum kui_armada_ce_stream_transport_status)t.stats.status;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_snapshot(
    uint32_t epoch,struct kui_armada_ce_stream_transport_snapshot *out) {
    if(!out) return KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT;
    if(!epoch || epoch!=t.stats.epoch) return KUI_ARMADA_CE_STREAM_TRANSPORT_STALE;
    *out=t.stats;return (enum kui_armada_ce_stream_transport_status)t.stats.status;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_cancel(uint32_t epoch) {
    if(!epoch || epoch!=t.stats.epoch) return KUI_ARMADA_CE_STREAM_TRANSPORT_STALE;
    t.cancelled=1u;t.stats.status=KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
    return KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
}
bool kui_armada_ce_stream_transport_attached(void) { return t.stats.attached==1u; }
uint32_t kui_armada_ce_stream_transport_event(uint32_t event,uintptr_t argument) {
    if(event==KUI_ARMADA_CE_STREAM_BUS_ATTACH) {
        const struct kui_armada_ce_stream_bus *b=(const struct kui_armada_ce_stream_bus *)argument;
        if(!bus_valid(b)) return 0;
        if(t.stats.attached) return !memcmp(&t.bus,b,sizeof(*b));
        t.bus=*b;t.stats.attached=1u;t.data_token=data()->token;return 1u;
    }
    if(!t.stats.attached)
        return event==KUI_ARMADA_CE_STREAM_BUS_DATA_ENTER ||
               event==KUI_ARMADA_CE_STREAM_BUS_DATA_PREFETCH;
    refresh_holds();
    if(event==KUI_ARMADA_CE_STREAM_BUS_DATA_TAKE) {
        struct kui_armada_ce_stream_data_take *request=(struct kui_armada_ce_stream_data_take *)argument;
#ifndef KUI_ARMADA_CE_STREAM_TRANSPORT_TEST
        if(!span(argument,0x8c008300u,0x8c00e000u,sizeof(*request))) return 0;
#endif
        if(!request) return 0;
        request->copied=0;request->block=NULL;
        for(unsigned h=0;h<2u;h++) if(t.held[h].used && t.held[h].lba==request->lba) {
            request->result=t.held[h].result;request->copied=1;
            if(request->result==KUI_SCI_STREAM_OK) request->block=t.held[h].bytes;
            t.held[h].used=0;return 1u;
        }
        return 0;
    }
    if(event==KUI_ARMADA_CE_STREAM_BUS_DATA_ENTER) {
        if(t.stats.owner==OWNER_AUDIO) {
            enum kui_sci_stream_result result=(enum kui_sci_stream_result)finish(argument!=0);
            if(t.bus.busy()) return 0;
            /* Preserve completed audio before DATA reuses the areas. */
            if(!t.cancelled && t.stats.status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING) {
                const uint8_t *block=t.bus.take(t.cursor.block,&result);
                if(block) {
                    t.stats.retries=0;t.early_polled=0;
                    if(kui_retail_cursor_feed(&t.cursor,block)!=KUI_GAME_OK)
                        t.stats.status=KUI_ARMADA_CE_STREAM_TRANSPORT_IO;
                    ++t.stats.blocks;++t.stats.audio_blocks;
                    if(t.cursor.done==t.cursor.count && t.stats.status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING)
                        t.stats.status=KUI_ARMADA_CE_STREAM_TRANSPORT_DONE;
                } else if(result>KUI_SCI_STREAM_BUSY) audio_failure(result);
            }
            release_audio();++t.stats.preemptions;
        }
        t.stats.owner=OWNER_DATA;return 1u;
    }
    if(event==KUI_ARMADA_CE_STREAM_BUS_DATA_BLOCK) {
        ++t.stats.data_blocks;if(t.data_credit<DATA_QUANTUM) ++t.data_credit;
        /* An IRQ may yield after this checked DATA block. Foreground data
         * keeps its existing top-up quantum; it never waits for an audio chunk. */
        if(data()->in_irq && t.stats.status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING &&
           t.data_credit>=DATA_QUANTUM) {
            (void)kui_armada_ce_stream_transport_pump();
            return t.stats.owner==OWNER_AUDIO;
        }
        return 0;
    }
    if(event==KUI_ARMADA_CE_STREAM_BUS_DATA_PREFETCH)
        return t.stats.status!=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING || t.data_credit<DATA_QUANTUM;
    if(event==KUI_ARMADA_CE_STREAM_BUS_IRQ) {
        if(t.stats.owner!=OWNER_AUDIO) return 0;
        t.bus.token_budget(true);(void)kui_armada_ce_stream_transport_pump();
        if(!t.bus.busy()) t.bus.unhook();
        return 1u;
    }
    if(event==KUI_ARMADA_CE_STREAM_BUS_IDLE) {
        if(t.stats.status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING &&
           (!data_active() || data()->spill_bytes>KUI_RETAIL_ASYNC_SPILL_BYTES-512u))
            (void)kui_armada_ce_stream_transport_pump();
        return t.stats.owner==OWNER_AUDIO;
    }
    return 0;
}
