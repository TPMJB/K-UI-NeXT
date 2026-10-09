/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact-Toy high-worker SCI adapter; ordinary reader targets do not link it. */
#include "toy_pilot_sci.h"
#include "sci_stream.h"
#include "kui/retail_cursor.h"
#include <stddef.h>
#include <string.h>

#if !KUI_TOY_PILOT_SHARED_SCI
#error The shared SCI adapter requires its isolated Toy build
#endif
#define IPRB UINT32_C(0xffd00008)
#define SCI_FIELD 0x00f0u
#define SCI_LEVEL 0x0010u
#define TOKEN_LIMIT 65536u
#define RETRIES 8u
#define POLLED_AFTER 2u
#define STACK_GUARD UINT32_C(0xa55a5aa5)

/* Offsets are used only by our high vector forwarding assembly. Three
 * nested native interrupt returns fit; exceptions retain their original SPC. */
struct kui_toy_pilot_sci_release_state {
    uint32_t vbr,sci,armed,depth,pending[3][2],rehooks,released[3];
};
struct kui_toy_pilot_sci_release_state kui_toy_pilot_sci_release;
volatile uint32_t kui_toy_pilot_sci_irq_fault;
_Static_assert(offsetof(struct kui_toy_pilot_sci_release_state,pending)==16u &&
    offsetof(struct kui_toy_pilot_sci_release_state,rehooks)==40u &&
    offsetof(struct kui_toy_pilot_sci_release_state,released)==44u,"SCI release ABI");
struct vector_region {
    uint32_t vector100[8];
    uint8_t gap0[0x300u-0x20u];
    uint32_t vector400[8];
    uint8_t gap1[0x500u-0x320u];
    uint32_t vector600[16];
};
struct vector_region kui_toy_pilot_sci_vectors __attribute__((aligned(32)));
_Static_assert(offsetof(struct vector_region,vector400)==0x300u &&
    offsetof(struct vector_region,vector600)==0x500u,"SCI vector offsets");
extern const uint32_t kui_toy_pilot_sci_forward[3];
extern const uint32_t kui_toy_pilot_sci_interrupt[11];
extern void kui_toy_pilot_sci_release_100(void),kui_toy_pilot_sci_release_400(void);
extern void kui_toy_pilot_sci_release_600(void);
#ifndef KUI_TOY_PILOT_SCI_TEST
extern uint8_t __toy_sci_irq_stack_bottom[] __asm__("__toy_sci_irq_stack_bottom");
extern uint8_t __toy_sci_irq_stack_top[] __asm__("__toy_sci_irq_stack_top");
#endif

static struct {
    const struct kui_retail_manifest *manifest;
    const struct kui_loader_sd *card;
    enum kui_loader_sd_result (*acquire)(void);
    void (*release)(void);
    struct kui_retail_gd *service;
    struct kui_retail_cursor cursor;
    uint32_t token,command,lba,count,destination,sector_bytes;
    uint32_t configured,active,opened,failed,retry,audio_waiting,audio_held,in_irq,hooked;
    uint32_t boundary_needed,transport_fault,verified_since;
    struct kui_toy_pilot_sci_stats stats;
    uint8_t area0[KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));
    uint8_t area1[KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));
#if KUI_TOY_PILOT_ASYNC_CDDA
    struct {
        struct kui_retail_cursor cursor;
        uint32_t lba,generation,active,ready,delivered,error,retry,opened;
        void *output;
        uint8_t bytes[KUI_GAME_RAW_BYTES] __attribute__((aligned(32)));
    } audio;
#endif
} e;

#ifdef KUI_TOY_PILOT_SCI_TEST
#define vbr_get kui_toy_pilot_sci_test_vbr
#define vbr_set kui_toy_pilot_sci_test_set_vbr
#define rd16 kui_toy_pilot_sci_test_read16
#define wr16 kui_toy_pilot_sci_test_write16
#define ticks kui_toy_pilot_sci_test_ticks
#else
static uint32_t vbr_get(void) {uint32_t v;__asm__ volatile("stc vbr,%0":"=r"(v));return v;}
static void vbr_set(uint32_t v) {__asm__ volatile("ldc %0,vbr"::"r"(v):"memory");}
static uint16_t rd16(uint32_t a) {return *(volatile const uint16_t *)(uintptr_t)a;}
static void wr16(uint32_t a,uint16_t v) {*(volatile uint16_t *)(uintptr_t)a=v;}
static uint32_t ticks(void) {return ~*(volatile const uint32_t *)(uintptr_t)0xffd8000cu;}
/* Initialization writes vector instructions in the leased BSS. Publish
 * exactly those lines and invalidate instructions through P2, preserving
 * every other CCR policy bit as the admitted startup publisher does. */
static void publish_vectors(void);
__asm__(".section .text.toy_sci_publish,\"ax\"\n"
    ".align 2\n_toy_sci_publish:\nmov.l 1f,r0\njmp @r0\nnop\n"
    "2:\nmov #-32,r0\nand r0,r4\n"
    "3:\ncmp/hs r5,r4\nbt 4f\nocbp @r4\nadd #32,r4\nbra 3b\nnop\n"
    "4:\nmov.l 5f,r0\nmov.l @r0,r1\nmov.l 6f,r2\nor r2,r1\nmov.l r1,@r0\n"
    "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nrts\nnop\n"
    ".align 2\n1: .long 2b+0x20000000\n5: .long 0xff00001c\n6: .long 0x00000800\n.previous\n");
extern void toy_sci_publish(uint32_t,uint32_t);
static void publish_vectors(void) {
    toy_sci_publish((uint32_t)(uintptr_t)&kui_toy_pilot_sci_vectors,
        (uint32_t)(uintptr_t)&kui_toy_pilot_sci_vectors+sizeof(kui_toy_pilot_sci_vectors));
}
#endif
static uint32_t our_vbr(void) {return (uint32_t)(uintptr_t)&kui_toy_pilot_sci_vectors-0x100u;}
static void maximum(uint32_t *out,uint32_t value) {if(value>*out)*out=value;}
static void hook(void) {
    uint32_t vbr=vbr_get();
    uint16_t level=rd16(IPRB);
    if(vbr!=our_vbr()) {
        if(!e.hooked || (level&SCI_FIELD)!=SCI_LEVEL)
            kui_toy_pilot_sci_release.sci=level&SCI_FIELD;
        kui_toy_pilot_sci_release.vbr=vbr;
        vbr_set(our_vbr());
    }
    wr16(IPRB,(uint16_t)((level&~SCI_FIELD)|SCI_LEVEL));
    kui_toy_pilot_sci_release.armed=1u;e.hooked=1u;
}
static void unhook(void) {
    kui_toy_pilot_sci_release.armed=0u;
    if(!e.hooked) return;
    if(vbr_get()==our_vbr())vbr_set(kui_toy_pilot_sci_release.vbr);
    uint16_t level=rd16(IPRB);
    if((level&SCI_FIELD)==SCI_LEVEL)
        wr16(IPRB,(uint16_t)((level&~SCI_FIELD)|kui_toy_pilot_sci_release.sci));
    e.hooked=0u;
}
static bool data_command(uint32_t c) {return c==KUI_GD_PIOREAD || c==KUI_GD_DMAREAD;}
static bool live(void) {
    const struct kui_retail_gd *s=e.service;
    return e.active && s && s->pending && s->token==e.token && s->command==e.command &&
        s->lba==e.lba && s->count==e.count && s->destination==e.destination &&
        s->sector_bytes==e.sector_bytes;
}
static void report(void) {
    if(live())kui_retail_gd_progress(e.service,e.cursor.done,e.failed);
}
/* Only after there is no DMA, unless cancellation intentionally fences a
 * stale transfer first. stop() restores DMA channel registers itself. */
static bool close_bus(void) {
    unhook();
    if(!e.opened) return true;
    enum kui_sci_stream_result result=kui_sci_stream_stop();
    kui_sci_stream_discard();e.release();e.opened=0u;
#if KUI_TOY_PILOT_ASYNC_CDDA
    if(e.audio.opened) {e.audio.opened=0u;++e.stats.audio_releases;}
#endif
    e.stats.card_owned=e.audio_held?2u:0u;
    if(result!=KUI_SCI_STREAM_OK)e.transport_fault=1u;
    return result==KUI_SCI_STREAM_OK;
}
static void fail(uint32_t error) {
    e.failed=error;++e.stats.errors;
    report();e.active=0u;e.stats.active_token=0u;
#if KUI_TOY_PILOT_ASYNC_CDDA
    /* A logical data failure cannot revoke an independent audio mailbox. */
    if(!e.audio.opened)(void)close_bus();
#else
    (void)close_bus();
#endif
}
static void retry(enum kui_sci_stream_result result) {
    ++e.stats.retries;
    if(result==KUI_SCI_STREAM_RESET || result==KUI_SCI_STREAM_BUSY || ++e.retry>RETRIES)
        fail(KUI_GD_ERROR_IO);
}
static bool open_bus(void) {
    if(e.opened) return true;
    if(e.acquire()!=KUI_LOADER_SD_OK) {fail(KUI_GD_ERROR_IO);return false;}
    e.opened=1u;e.stats.card_owned=1u;
    if(kui_sci_stream_open(e.card,e.area0,e.area1,false)!=KUI_SCI_STREAM_OK) {
        fail(KUI_GD_ERROR_IO);return false;
    }
    ++e.stats.data_resumes;return true;
}
static void write_out(void *unused,uint32_t offset,const uint8_t *bytes,uint32_t count) {
    (void)unused;
    if(!live()) {e.failed=KUI_GD_ERROR_CANCELLED;return;}
    uint32_t destination=(e.destination&0x00ffffffu)|0x8c000000u;
    if(offset>e.service->request_bytes || count>e.service->request_bytes-offset) {
        e.failed=KUI_GD_ERROR_MEMORY;return;
    }
    uint8_t *out=e.service->ops.map(e.service->ops.context,destination+offset,count,1);
    if(!out)e.failed=KUI_GD_ERROR_MEMORY;
    else memcpy(out,bytes,count);
}
#if KUI_TOY_PILOT_ASYNC_CDDA
static void audio_invalidate(void) {
    /* Revocation precedes any fence. Only staging, never output, is a DMA
     * consumer; generation zero makes its old contents unpublishable. */
    e.audio.active=e.audio.ready=e.audio.delivered=e.audio.generation=0u;
    e.audio.error=e.audio.retry=0u;e.audio.output=NULL;
    e.audio_waiting=0u;
}
static void audio_write(void *unused,uint32_t offset,const uint8_t *bytes,uint32_t count) {
    (void)unused;
    if(!e.audio.active || !e.audio.generation || offset>KUI_GAME_RAW_BYTES ||
       count>KUI_GAME_RAW_BYTES-offset) {e.audio.error=1u;return;}
    memcpy(e.audio.bytes+offset,bytes,count);
}
static int audio_fail(void) {
    e.audio.active=e.audio.ready=0u;e.audio.error=1u;e.audio_waiting=0u;
    ++e.stats.errors;
    if(e.audio.opened)(void)close_bus();
    return KUI_TOY_SCI_FAULT;
}
static bool audio_retry(enum kui_sci_stream_result result) {
    ++e.stats.retries;
    if(result==KUI_SCI_STREAM_RESET || result==KUI_SCI_STREAM_BUSY || ++e.audio.retry>RETRIES) {
        (void)audio_fail();return false;
    }
    return true;
}
static bool audio_open(void) {
    if(e.audio.opened)return true;
    if(e.opened || e.audio_held || !e.configured || e.transport_fault ||
       e.acquire()!=KUI_LOADER_SD_OK) { (void)audio_fail();return false; }
    e.opened=e.audio.opened=1u;e.stats.card_owned=2u;++e.stats.audio_claims;
    if(kui_sci_stream_open(e.card,e.area0,e.area1,false)!=KUI_SCI_STREAM_OK) {
        (void)audio_fail();return false;
    }
    return true;
}
static int audio_work(bool foreground) {
    if(!e.audio.active)return e.audio.error?KUI_TOY_SCI_FAULT:KUI_TOY_SCI_OK;
    if(kui_toy_pilot_sci_irq_fault || e.transport_fault)return audio_fail();
    if(!audio_open())return KUI_TOY_SCI_FAULT;
    enum kui_sci_stream_result result=KUI_SCI_STREAM_OK;
    if(kui_sci_stream_busy()) {
        result=kui_sci_stream_poll(!foreground);
        if(result==KUI_SCI_STREAM_PENDING) {
            if(kui_sci_stream_busy())hook();else unhook();
            return KUI_TOY_SCI_PENDING;
        }
        if(result!=KUI_SCI_STREAM_OK) {
            return audio_retry(result)?KUI_TOY_SCI_PENDING:KUI_TOY_SCI_FAULT;
        }
    }
    const uint8_t *block=kui_sci_stream_take(e.audio.cursor.block,&result);
    if(block) {
        e.audio.retry=0u;
        if(kui_retail_cursor_feed(&e.audio.cursor,block)!=KUI_GAME_OK || e.audio.error)
            return audio_fail();
        if(e.in_irq)++e.stats.irq_blocks;else ++e.stats.call_blocks;
        if(e.verified_since<KUI_TOY_SCI_SERVICE_BLOCKS)++e.verified_since;
        if(e.audio.cursor.done==1u) {
            /* Completion is private until a matching worker poll publishes
             * it. Yield now so delayed worker visits do not hold the card. */
            if(!close_bus())return audio_fail();
            e.audio.active=0u;e.audio.ready=1u;return KUI_TOY_SCI_OK;
        }
    } else if(result==KUI_SCI_STREAM_CRC) {
        if(!audio_retry(result))return KUI_TOY_SCI_FAULT;
    } else if(result!=KUI_SCI_STREAM_PENDING) {
        return audio_retry(result)?KUI_TOY_SCI_PENDING:KUI_TOY_SCI_FAULT;
    }
    uint32_t polled=kui_sci_stream_stats()->polled;
    uint32_t yields=kui_sci_stream_stats()->token_yields;
    result=kui_sci_stream_fetch(e.audio.cursor.block,TOKEN_LIMIT,e.audio.retry>=POLLED_AFTER);
    e.stats.polled_blocks+=kui_sci_stream_stats()->polled-polled;
    e.stats.token_yields+=kui_sci_stream_stats()->token_yields-yields;
    if(result>KUI_SCI_STREAM_PENDING && !audio_retry(result))return KUI_TOY_SCI_FAULT;
    if(kui_sci_stream_busy())hook();else unhook();
    return KUI_TOY_SCI_PENDING;
}
#endif
void kui_toy_pilot_sci_init(const struct kui_retail_manifest *manifest,
    const struct kui_loader_sd *card,enum kui_loader_sd_result (*acquire)(void),
    void (*release)(void)) {
    memset(&e,0,sizeof(e));memset(&kui_toy_pilot_sci_release,0,sizeof(kui_toy_pilot_sci_release));
    kui_toy_pilot_sci_irq_fault=0u;
    e.manifest=manifest;e.card=card;e.acquire=acquire;e.release=release;
    e.configured=manifest && card && acquire && release && card->ready && !card->slow;
    memcpy(kui_toy_pilot_sci_vectors.vector100,kui_toy_pilot_sci_forward,3u*sizeof(uint32_t));
    kui_toy_pilot_sci_vectors.vector100[2]=(uint32_t)(uintptr_t)kui_toy_pilot_sci_release_100;
    memcpy(kui_toy_pilot_sci_vectors.vector400,kui_toy_pilot_sci_forward,3u*sizeof(uint32_t));
    kui_toy_pilot_sci_vectors.vector400[2]=(uint32_t)(uintptr_t)kui_toy_pilot_sci_release_400;
    memcpy(kui_toy_pilot_sci_vectors.vector600,kui_toy_pilot_sci_interrupt,11u*sizeof(uint32_t));
#ifndef KUI_TOY_PILOT_SCI_TEST
    for(unsigned i=0;i<64u/sizeof(uint32_t);i++)
        ((uint32_t *)(void *)__toy_sci_irq_stack_bottom)[i]=STACK_GUARD;
    publish_vectors();
#endif
}
void kui_toy_pilot_sci_cancel(struct kui_retail_gd *s) {
    /* Cursor ownership is revoked before touching a possibly arriving DMA.
     * The DMA writes only our two private areas, never a guest destination. */
    e.active=0u;e.token=0u;e.stats.active_token=0u;e.verified_since=0u;
#if KUI_TOY_PILOT_ASYNC_CDDA
    if(!s)audio_invalidate();
    /* DATA ABORT must not stop a receiver owned by an audio mailbox. */
    if(!s || !e.audio.opened)(void)close_bus();
    e.service=NULL;e.retry=e.failed=0u;
    if(!s) {e.audio_waiting=e.audio_held=0u;e.stats.card_owned=0u;}
#else
    (void)close_bus();e.service=NULL;e.retry=e.failed=e.audio_waiting=0u;
    if(!s) {
        e.audio_waiting=e.audio_held=0u;e.stats.card_owned=0u;
    }
#endif
}
static bool begin(struct kui_retail_gd *s) {
    e.service=s;e.token=s->token;e.command=s->command;
    e.lba=s->lba;e.count=s->count;e.destination=s->destination;e.sector_bytes=s->sector_bytes;
    e.failed=e.retry=e.boundary_needed=e.verified_since=0u;
    e.cursor.done=0u;e.active=1u;e.stats.active_token=e.token;
    if(!e.configured || e.transport_fault || kui_retail_cursor_begin(&e.cursor,e.manifest,e.lba,e.count,
        e.sector_bytes==KUI_GAME_RAW_BYTES?KUI_GAME_SECTOR_RAW:KUI_GAME_SECTOR_MODE1,
        write_out,NULL)!=KUI_GAME_OK) {fail(KUI_GD_ERROR_IO);return false;}
    return true;
}
static int data_work(bool foreground) {
    if(!e.active) return KUI_TOY_SCI_OK;
    if(kui_toy_pilot_sci_irq_fault || e.transport_fault) {fail(KUI_GD_ERROR_IO);return KUI_TOY_SCI_FAULT;}
    if(!live()) {kui_toy_pilot_sci_cancel(e.service);return KUI_TOY_SCI_OK;}
    if(e.audio_held)return KUI_TOY_SCI_PENDING;
    if(!open_bus())return KUI_TOY_SCI_FAULT;
    enum kui_sci_stream_result result=KUI_SCI_STREAM_OK;
    if(kui_sci_stream_busy()) {
        /* Only a foreground GD service can resume a repair immediately.
         * Actual IRQs and one-step audio/request pumps leave that repair
         * for a later entry, as the admitted stream contract requires. */
        result=kui_sci_stream_poll(!foreground);
        if(result==KUI_SCI_STREAM_PENDING) {
            /* An IRQ defers mid-block overrun repair. Start that same block
             * again next time, retaining its exact data cursor. */
            if(kui_sci_stream_busy())hook();else unhook();
            return KUI_TOY_SCI_PENDING;
        }
        if(result!=KUI_SCI_STREAM_OK) {
            retry(result);return e.active?KUI_TOY_SCI_PENDING:KUI_TOY_SCI_FAULT;
        }
    }
    const uint8_t *block=kui_sci_stream_take(e.cursor.block,&result);
    if(block) {
        e.retry=0u;
        if(kui_retail_cursor_feed(&e.cursor,block)!=KUI_GAME_OK || e.failed) {
            fail(e.failed?e.failed:KUI_GD_ERROR_IO);return KUI_TOY_SCI_FAULT;
        }
        if(e.in_irq)++e.stats.irq_blocks;else ++e.stats.call_blocks;
        if(e.verified_since<KUI_TOY_SCI_SERVICE_BLOCKS)++e.verified_since;
        e.boundary_needed=0u;
        if(e.cursor.done==e.cursor.count) {
            if(!close_bus()) {fail(KUI_GD_ERROR_IO);return KUI_TOY_SCI_FAULT;}
            report();e.active=0u;e.stats.active_token=0u;
            return KUI_TOY_SCI_OK;
        }
        report();
    } else if(result==KUI_SCI_STREAM_CRC) {
        retry(result);if(!e.active)return KUI_TOY_SCI_FAULT;
        /* This stream step receives no second payload. A foreground entry
         * can continue only within its shared token/step/time allowances. */
    } else if(result!=KUI_SCI_STREAM_PENDING) {
        retry(result);return e.active?KUI_TOY_SCI_PENDING:KUI_TOY_SCI_FAULT;
    }
    if(e.audio_waiting && !e.boundary_needed) {
        if(!close_bus()) {fail(KUI_GD_ERROR_IO);return KUI_TOY_SCI_FAULT;}
        return KUI_TOY_SCI_PENDING;
    }
    uint32_t polled=kui_sci_stream_stats()->polled;
    uint32_t yields=kui_sci_stream_stats()->token_yields;
    result=kui_sci_stream_fetch(e.cursor.block,TOKEN_LIMIT,e.retry>=POLLED_AFTER);
    if(result==KUI_SCI_STREAM_OK)e.boundary_needed=1u;
    e.stats.polled_blocks+=kui_sci_stream_stats()->polled-polled;
    e.stats.token_yields+=kui_sci_stream_stats()->token_yields-yields;
    if(result>KUI_SCI_STREAM_PENDING)retry(result);
    if(kui_sci_stream_busy())hook();else unhook();
    return e.active?KUI_TOY_SCI_PENDING:KUI_TOY_SCI_FAULT;
}
static int pump_work(bool foreground) {
#if KUI_TOY_PILOT_ASYNC_CDDA
    if(e.audio_held)return KUI_TOY_SCI_PENDING;
    if(e.audio.opened || (e.audio.active && !e.opened)) {
        e.audio_waiting=0u;
        int result=audio_work(foreground);
        /* One verified RAW block ended this lease. Starting DATA here does
         * not check another block: open clears the old private ready areas.
         * It supplies an IRQ even if the next worker visit is far away. */
        if(!e.audio.opened && !e.audio.active && e.active && !e.transport_fault)
            (void)data_work(foreground);
        return result;
    }
    int result=data_work(foreground);
    /* The checked DATA boundary can immediately arm RAW without waiting
     * for another worker visit; RAW's fresh open cannot deliver a payload. */
    if(e.audio.active && !e.opened && !e.transport_fault) {
        e.audio_waiting=0u;return audio_work(foreground);
    }
    return result;
#else
    return data_work(foreground);
#endif
}
static void prepare(struct kui_retail_gd *s) {
#if KUI_TOY_PILOT_ASYNC_CDDA
    /* Audio-only CHECK/EXEC may have no data handle to pass. It can service
     * RAW without interpreting NULL as a request to revoke live DATA. */
    if(e.active && ((s && e.service!=s) || !live()))kui_toy_pilot_sci_cancel(e.service);
#else
    if(e.active && (e.service!=s || !live()))kui_toy_pilot_sci_cancel(e.service);
#endif
    if(s && s->pending && data_command(s->command) && !e.active)
        (void)begin(s);
}
int kui_toy_pilot_sci_pump(struct kui_retail_gd *s) {
    uint32_t before=ticks();++e.stats.calls;
    prepare(s);kui_sci_stream_token_budget(true);
    int result=pump_work(false);
    maximum(&e.stats.work_ticks_max,ticks()-before);return result;
}
int kui_toy_pilot_sci_service(struct kui_retail_gd *s) {
    uint32_t before=ticks();++e.stats.calls;
    prepare(s);kui_sci_stream_token_budget(true);
    int result=e.active
#if KUI_TOY_PILOT_ASYNC_CDDA
        || e.audio.active
#endif
        ?KUI_TOY_SCI_PENDING:KUI_TOY_SCI_OK;
    /* A waiting audio request still needs its arriving data boundary
     * serviced if the game masks SCI while looping in CHECK/EXEC. Preserve
     * the one-step nonblocking pump here, regardless of the data quota;
     * once checked, DATA yields without arming another DATA block. Async
     * RAW may then arm its first block in the newly acquired lease. An
     * actually held synchronous raw lease permits no transport access. */
    unsigned stepped=0u;
    if(e.active && e.opened && e.audio_waiting && !e.audio_held) {
        result=pump_work(false);stepped=1u;
    }
    /* IRQ and one-step work since the preceding CHECK/EXEC already counts
     * toward this quota. This provides a bounded foreground progress
     * opportunity when the game's interrupt mask holds off SCI level 1.
     * Time admission is checked between finite stream steps, so one step
     * may cross it; the step cap also bounds a stopped timer. */
    for(unsigned step=stepped;(e.active
#if KUI_TOY_PILOT_ASYNC_CDDA
        || e.audio.active
#endif
        ) && e.verified_since<KUI_TOY_SCI_SERVICE_BLOCKS &&
        !e.audio_waiting && !e.audio_held && step<KUI_TOY_SCI_SERVICE_STEPS;++step) {
        if(step && ticks()-before>=KUI_TOY_SCI_SERVICE_TICKS)break;
        result=pump_work(true);
        if(result!=KUI_TOY_SCI_PENDING || kui_sci_stream_token_pending())break;
    }
    /* Each CHECK/EXEC starts a new accounting interval, even if its finite
     * admission allowance prevented reaching all four blocks. A newly
     * admitted request and cancellation reset it separately above. */
    e.verified_since=0u;
    maximum(&e.stats.work_ticks_max,ticks()-before);return result;
}
int kui_toy_pilot_sci_audio_acquire(void) {
#if KUI_TOY_PILOT_ASYNC_CDDA
    /* This candidate admits RAW only through the asynchronous tuple API. */
    return KUI_TOY_SCI_FAULT;
#else
    if(e.audio_held || e.transport_fault)return KUI_TOY_SCI_FAULT;
    e.audio_waiting=1u;
    /* A foreground boundary pump may already have yielded the open stream.
     * With no data lease there is no outstanding DMA to service or reopen. */
    if(e.active && e.opened) {
        kui_sci_stream_token_budget(true);
        int result=pump_work(false);
        if(result==KUI_TOY_SCI_FAULT)return result;
    }
    if(e.opened) {
        ++e.stats.audio_pending;return KUI_TOY_SCI_PENDING;
    }
    e.audio_waiting=0u;e.audio_held=1u;e.stats.card_owned=2u;
    ++e.stats.audio_claims;return KUI_TOY_SCI_OK;
#endif
}
void kui_toy_pilot_sci_audio_release(void) {
    if(!e.audio_held)return;
    e.audio_held=0u;e.stats.card_owned=e.opened?1u:0u;++e.stats.audio_releases;
}
void kui_toy_pilot_sci_audio_cancel(void) {
#if KUI_TOY_PILOT_ASYNC_CDDA
    audio_invalidate();
    if(e.audio.opened)(void)close_bus();
#else
    e.audio_waiting=0u;
#endif
}
#if KUI_TOY_PILOT_ASYNC_CDDA
int kui_toy_pilot_sci_audio_read(uint32_t lba,uint32_t generation,void *output) {
    uint32_t before=ticks();++e.stats.calls;
    if(!generation || !output)return KUI_TOY_SCI_FAULT;
    if(e.audio.generation!=generation || e.audio.lba!=lba || e.audio.output!=output) {
        kui_toy_pilot_sci_audio_cancel();
        e.audio.lba=lba;e.audio.generation=generation;e.audio.output=output;
        if(!e.configured || e.transport_fault ||
           kui_retail_image_check_validated(e.manifest,lba,1u,KUI_GAME_SECTOR_RAW)!=KUI_GAME_OK ||
           kui_retail_cursor_begin(&e.audio.cursor,e.manifest,lba,1u,KUI_GAME_SECTOR_RAW,
               audio_write,NULL)!=KUI_GAME_OK) {
            e.audio.error=1u;++e.stats.errors;
        } else {e.audio.active=1u;e.audio_waiting=1u;}
    }
    if(e.audio.active) {
        /* A worker poll makes one bounded stream step, then gives execution
         * back. Continued IRQs or frequent GD CHECK/EXEC entries must fill
         * the sector between worker visits; worker cadence alone is too slow. */
        kui_sci_stream_token_budget(true);(void)pump_work(false);
    }
    int result=KUI_TOY_SCI_PENDING;
    if(e.audio.error || e.transport_fault)result=KUI_TOY_SCI_FAULT;
    else if(e.audio.ready || e.audio.delivered) {
        if(!e.audio.delivered) {
            memcpy(output,e.audio.bytes,KUI_GAME_RAW_BYTES);
            e.audio.delivered=1u;e.audio.ready=0u;
        }
        result=KUI_TOY_SCI_OK;
    }
    maximum(&e.stats.work_ticks_max,ticks()-before);return result;
}
#endif
const struct kui_toy_pilot_sci_stats *kui_toy_pilot_sci_snapshot(void) {return &e.stats;}
uint32_t kui_toy_pilot_sci_irq(void) {
    if(!e.opened || !kui_sci_stream_busy())return 1u;
    uint32_t before=ticks();++e.stats.irq_calls;e.in_irq=1u;
    kui_sci_stream_token_budget(true);(void)pump_work(false);e.in_irq=0u;
    maximum(&e.stats.irq_ticks_max,ticks()-before);return 0u;
}
