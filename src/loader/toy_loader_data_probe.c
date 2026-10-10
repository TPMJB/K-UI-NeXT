/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_loader_data_probe.h"
#include "kui/toy_pilot_clock.h"
#if KUI_TOY_PILOT_DATA_PROBE
#include "retail_storage.h"
#include "sci_sd_bus.h"
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 0
#include "kui/toy_loader_payload_control.h"
#endif

_Static_assert(sizeof(struct kui_sci_sd_diagnostic)==64u,"DATA probe exact low diagnostic ABI");
_Static_assert(offsetof(struct kui_sci_sd_diagnostic,started)==52u,"DATA probe DMA started ABI");
_Static_assert(offsetof(struct kui_sci_sd_diagnostic,success)==56u,"DATA probe DMA success ABI");
_Static_assert(offsetof(struct kui_sci_sd_diagnostic,fallback)==60u,"DATA probe DMA fallback ABI");

/* Every mutable observation and scratch object is high resident BSS. */
static struct kui_toy_loader_data_probe_report data_probe_report;
struct data_probe_stamp {uint32_t tick,valid,epoch;};
static struct {
    struct kui_retail_gd *service;
    uint32_t active,nested,phase,saved_sr,caller_pr,initialized,epoch,installed;
} data_probe_scope;
static struct {
    struct data_probe_stamp start,last,payload_start,payload_end,read_end;
    uint32_t active,payload_active,suppressed,blocks,lba,sectors,invalid;
    struct {uint32_t started,success,fallback;} dma_before,dma_after;
} data_probe_read_state;

#ifdef KUI_TOY_LOADER_DATA_PROBE_HOST_TEST
static struct kui_retail_storage *data_probe_card;
static const volatile uint32_t *data_probe_sr,*data_probe_caller;
static const volatile struct kui_sci_sd_diagnostic *data_probe_diagnostic;
static kui_toy_loader_data_probe_read_fn data_probe_original_read;
static kui_toy_loader_data_probe_block_fn data_probe_original_block;
#define DATA_PROBE_READ(a,n) kui_toy_loader_data_probe_host_read((a),(n))
#else
#include "toy_pilot_resident_symbols.h"
_Static_assert(sizeof(struct kui_retail_storage)==72u,"DATA probe exact low card ABI");
_Static_assert(offsetof(struct kui_retail_storage,device.sd)==4u,"DATA probe exact low SD offset");
_Static_assert(sizeof(struct kui_loader_sd)==44u,"DATA probe exact low SD ABI");
_Static_assert(offsetof(struct kui_retail_storage,stream)==48u,"DATA probe exact low stream offset");
#define data_probe_card ((struct kui_retail_storage *)(uintptr_t)KUI_TOY_PILOT_LOW_PROBE_CARD)
#define data_probe_sr ((volatile const uint32_t *)(uintptr_t)KUI_TOY_PILOT_LOW_PROBE_SR)
#define data_probe_caller ((volatile const uint32_t *)(uintptr_t)KUI_TOY_PILOT_LOW_PROBE_CALLER)
#define data_probe_diagnostic ((volatile const struct kui_sci_sd_diagnostic *)(uintptr_t)KUI_TOY_PILOT_LOW_PROBE_DIAGNOSTIC)
#define data_probe_original_read ((kui_toy_loader_data_probe_read_fn)(uintptr_t)KUI_TOY_PILOT_LOW_PROBE_READ)
#define data_probe_original_block ((kui_toy_loader_data_probe_block_fn)(uintptr_t)KUI_TOY_PILOT_LOW_PROBE_BLOCK)
static uint32_t data_probe_mmio(uint32_t address,unsigned bytes) {
    if(bytes==1u) return *(volatile const uint8_t *)(uintptr_t)address;
    if(bytes==2u) return *(volatile const uint16_t *)(uintptr_t)address;
    return *(volatile const uint32_t *)(uintptr_t)address;
}
#define DATA_PROBE_READ(a,n) data_probe_mmio((a),(n))
#endif

static inline __attribute__((always_inline)) void data_probe_add(uint32_t *value,uint32_t amount) {
    if(amount>UINT32_MAX-*value) {*value=UINT32_MAX;data_probe_report.saturated=1u;}
    else *value+=amount;
}
static inline __attribute__((always_inline)) void data_probe_initialize(void) {
    if(data_probe_scope.initialized) return;
    data_probe_scope.initialized=1u;
    data_probe_report.magic=KUI_TOY_LOADER_DATA_PROBE_MAGIC;
    data_probe_report.version=KUI_TOY_LOADER_DATA_PROBE_VERSION;data_probe_report.words=192u;
    data_probe_report.phase_words=88u;data_probe_report.tick_hz=781250u;
    data_probe_report.feature_flags=KUI_TOY_LOADER_DATA_PROBE_DMA_ATTRIBUTION;
    data_probe_report.payload_mode=KUI_TOY_PILOT_DATA_PAYLOAD_MODE;
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 1
    data_probe_report.feature_flags|=KUI_TOY_LOADER_DATA_PROBE_CACHED_PAYLOAD;
#elif KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 2
    data_probe_report.feature_flags|=KUI_TOY_LOADER_DATA_PROBE_PIO_PAYLOAD;
#endif
}
static inline __attribute__((always_inline)) void data_probe_control_snapshot(void) {
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 0
    const struct kui_toy_loader_payload_control_counts *counts=kui_toy_loader_payload_control_counts();
    data_probe_report.payload_attempts=counts->attempts;
    data_probe_report.payload_declines=counts->declines;
    data_probe_report.payload_publications=counts->publications;
    data_probe_report.payload_failed=counts->failed;
    if(counts->attempts==UINT32_MAX || counts->declines==UINT32_MAX ||
       counts->publications==UINT32_MAX || counts->failed==UINT32_MAX)
        data_probe_report.saturated=1u;
#endif
}
static void data_probe_sample(struct data_probe_stamp *out) {
    out->valid=kui_toy_pilot_clock_profile(DATA_PROBE_READ(0xffc00000u,2u),
        DATA_PROBE_READ(0xffd80008u,4u),DATA_PROBE_READ(0xffd80010u,2u),DATA_PROBE_READ(0xffd80004u,1u));
    out->tick=DATA_PROBE_READ(0xffd8000cu,4u);
    if(!out->valid) data_probe_add(&data_probe_scope.epoch,1u);
    out->epoch=data_probe_scope.epoch;
}
static inline __attribute__((always_inline)) int data_probe_interval(const struct data_probe_stamp *a,
    const struct data_probe_stamp *b) {
    return a->valid && b->valid && a->epoch==b->epoch &&
        a->epoch!=UINT32_MAX && a->tick-b->tick<=UINT32_C(0x7fffffff);
}
static void data_probe_metric(struct kui_toy_loader_data_probe_metric *m,
    const struct data_probe_stamp *a,const struct data_probe_stamp *b) {
    uint32_t elapsed=a->tick-b->tick;
    if(data_probe_interval(a,b)) {
        if(!m->samples || elapsed<m->ticks_min) m->ticks_min=elapsed;
        if(elapsed>m->ticks_max) m->ticks_max=elapsed;
        data_probe_add(&m->samples,1u);data_probe_add(&m->ticks_total,elapsed);
    } else data_probe_add(&data_probe_report.phase[data_probe_scope.phase].invalid_intervals,1u);
}
static inline __attribute__((always_inline)) void data_probe_dma_before(void) {
    if(!data_probe_diagnostic) return;
    data_probe_read_state.dma_before.started=data_probe_diagnostic->started;
    data_probe_read_state.dma_before.success=data_probe_diagnostic->success;
    data_probe_read_state.dma_before.fallback=data_probe_diagnostic->fallback;
}
static inline __attribute__((always_inline)) void data_probe_dma_after(void) {
    if(!data_probe_diagnostic) return;
    data_probe_read_state.dma_after.started=data_probe_diagnostic->started;
    data_probe_read_state.dma_after.success=data_probe_diagnostic->success;
    data_probe_read_state.dma_after.fallback=data_probe_diagnostic->fallback;
}
static inline __attribute__((always_inline)) void data_probe_dma_attribute(
    struct kui_toy_loader_data_probe_phase *p,bool result) {
    uint32_t started=data_probe_read_state.dma_after.started-data_probe_read_state.dma_before.started;
    uint32_t success=data_probe_read_state.dma_after.success-data_probe_read_state.dma_before.success;
    uint32_t fallback=data_probe_read_state.dma_after.fallback-data_probe_read_state.dma_before.fallback;
    if(!data_probe_diagnostic || data_probe_read_state.invalid ||
       !data_probe_interval(&data_probe_read_state.payload_start,&data_probe_read_state.payload_end)) {
        data_probe_add(&p->dma_delta_invalid,1u);return;
    }
    if(started==1u && fallback==0u && success==(result?1u:0u)) {
        data_probe_add(&p->dma_started,1u);
        if(result) data_probe_add(&p->dma_payload_ok,1u);
        data_probe_metric(&p->dma_payload_body,&data_probe_read_state.payload_start,&data_probe_read_state.payload_end);
    } else if(!started && !success && fallback==1u) {
        data_probe_add(&p->pio_fallback,1u);
        data_probe_metric(&p->pio_payload_body,&data_probe_read_state.payload_start,&data_probe_read_state.payload_end);
    } else if(!started && !success && !fallback && !result) {
        data_probe_add(&p->prestart_failed,1u);
    } else {data_probe_add(&p->dma_delta_invalid,1u);return;}
    data_probe_add(&p->dma_counter_wraps,
        (data_probe_read_state.dma_after.started<data_probe_read_state.dma_before.started?1u:0u)+
        (data_probe_read_state.dma_after.success<data_probe_read_state.dma_before.success?1u:0u)+
        (data_probe_read_state.dma_after.fallback<data_probe_read_state.dma_before.fallback?1u:0u));
}
static bool data_probe_payload(void *context,const uint8_t *tx,uint8_t *rx,
    size_t count,bool slow,uint16_t *crc) {
    struct kui_toy_loader_data_probe_phase *p=&data_probe_report.phase[data_probe_scope.phase];
    if(!data_probe_read_state.active || data_probe_report.frozen)
        return data_probe_original_block(context,tx,rx,count,slow,crc);
    if(data_probe_read_state.payload_active || data_probe_read_state.suppressed) {
        data_probe_add(&p->reentrant_payload,1u);data_probe_read_state.invalid=1u;
        return data_probe_original_block(context,tx,rx,count,slow,crc);
    }
    data_probe_read_state.payload_active=1u;
    data_probe_sample(&data_probe_read_state.payload_start);
    data_probe_dma_before();
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 0
    bool result=kui_toy_loader_payload_call(data_probe_original_block,context,tx,rx,count,slow,crc);
#else
    bool result=data_probe_original_block(context,tx,rx,count,slow,crc);
#endif
    /* last is only needed after original returns. Reentrant calls bypass it. */
    if(!data_probe_report.frozen) {
        data_probe_dma_after();
        data_probe_sample(&data_probe_read_state.payload_end);
        data_probe_metric(data_probe_read_state.blocks?&p->inter_payload_gap:&p->first_payload_gap,
            data_probe_read_state.blocks?&data_probe_read_state.last:&data_probe_read_state.start,
            &data_probe_read_state.payload_start);
        data_probe_metric(&p->payload_body,&data_probe_read_state.payload_start,&data_probe_read_state.payload_end);
        data_probe_add(&p->payload_calls,1u);data_probe_add(result?&p->payload_ok:&p->payload_failed,1u);
        if(count>UINT32_MAX) {p->payload_bytes=UINT32_MAX;data_probe_report.saturated=1u;}
        else data_probe_add(&p->payload_bytes,(uint32_t)count);
        if(count!=512u || tx || !rx || slow) data_probe_add(&p->nonstandard_payload,1u);
        else data_probe_dma_attribute(p,result);
        data_probe_add(&data_probe_read_state.blocks,1u);
        data_probe_read_state.last.tick=data_probe_read_state.payload_end.tick;
        data_probe_read_state.last.valid=data_probe_read_state.payload_end.valid;
        data_probe_read_state.last.epoch=data_probe_read_state.payload_end.epoch;
    }
    data_probe_read_state.payload_active=0u;
    return result;
}
static int data_probe_read(void *context,uint32_t lba,uint32_t sectors,
    uint32_t bytes,void *output) {
    struct kui_toy_loader_data_probe_phase *p=&data_probe_report.phase[data_probe_scope.phase];
    /* execute() resolves one read callback per STEP. Restore immediately so
     * even a permanent low fatal report can never leave ops.read patched. */
    if(data_probe_scope.service && data_probe_scope.service->ops.read==data_probe_read)
        data_probe_scope.service->ops.read=data_probe_original_read;
    if(bytes!=2048u || !data_probe_scope.active || data_probe_report.frozen)
        return data_probe_original_read(context,lba,sectors,bytes,output);
    if(data_probe_read_state.active) {
        data_probe_add(&p->reentrant_reads,1u);data_probe_read_state.invalid=1u;
        ++data_probe_read_state.suppressed;
        int result=data_probe_original_read(context,lba,sectors,bytes,output);
        --data_probe_read_state.suppressed;
        return result;
    }
    if(data_probe_card->device.sd.bus.transfer_block!=data_probe_original_block) {
        data_probe_add(&p->callback_conflicts,1u);
        return data_probe_original_read(context,lba,sectors,bytes,output);
    }
    data_probe_read_state.active=1u;data_probe_read_state.blocks=0u;
    data_probe_read_state.invalid=0u;data_probe_read_state.lba=lba;
    data_probe_read_state.sectors=sectors;
    data_probe_sample(&data_probe_read_state.start);
    data_probe_add(&p->read_calls,1u);
    data_probe_card->device.sd.bus.transfer_block=data_probe_payload;
    int result=data_probe_original_read(context,lba,sectors,bytes,output);
    if(data_probe_card->device.sd.bus.transfer_block==data_probe_payload)
        data_probe_card->device.sd.bus.transfer_block=data_probe_original_block;
    else if(!data_probe_report.frozen) {
        data_probe_add(&p->callback_conflicts,1u);data_probe_read_state.invalid=1u;
    }
    if(!data_probe_report.frozen) {
        data_probe_sample(&data_probe_read_state.read_end);
        data_probe_add(result?&p->read_failed:&p->read_ok,1u);
        if(data_probe_read_state.invalid) data_probe_add(&p->invalid_intervals,1u);
        else data_probe_metric(&p->read_body,&data_probe_read_state.start,&data_probe_read_state.read_end);
        if(!data_probe_read_state.blocks) data_probe_add(&p->no_payload_reads,1u);
        else data_probe_metric(&p->tail_gap,&data_probe_read_state.last,&data_probe_read_state.read_end);
        if(!data_probe_read_state.invalid && data_probe_interval(&data_probe_read_state.start,&data_probe_read_state.read_end) &&
           (!p->read_body.samples || data_probe_read_state.start.tick-data_probe_read_state.read_end.tick>=p->worst_read[6])) {
            p->worst_read[0]=data_probe_scope.service->token;
            p->worst_read[1]=data_probe_scope.service->lba;
            p->worst_read[2]=data_probe_read_state.lba;p->worst_read[3]=data_probe_read_state.sectors;
            p->worst_read[4]=data_probe_scope.saved_sr;p->worst_read[5]=data_probe_scope.caller_pr;
            p->worst_read[6]=data_probe_read_state.start.tick-data_probe_read_state.read_end.tick;
            p->worst_read[7]=data_probe_read_state.blocks;
        }
    }
    data_probe_read_state.active=0u;
    return result;
}

void kui_toy_loader_data_probe_begin(struct kui_retail_gd *s,uint32_t function,uint32_t phase) {
    data_probe_initialize();
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 0
    /* The retained trace flips phase only after the first accepted PLAY
     * mailbox. That call has no cooked payload; the next dispatch enters
     * here before any work and permanently restores the original callbacks.
     * Rejected PLAY and a refused mailbox leave phase zero and keep startup
     * controls enabled. Mode zero retains both observational phases. */
    if(phase>0u) {kui_toy_loader_data_probe_freeze();return;}
#endif
    if(data_probe_report.frozen) return;
    if(data_probe_scope.active) {
        data_probe_add(&data_probe_report.nested_begin,1u);
        data_probe_add(&data_probe_scope.nested,1u);return;
    }
    data_probe_scope.active=1u;data_probe_scope.service=s;data_probe_scope.phase=phase>1u?1u:phase;
    data_probe_scope.installed=0u;
    if(!s || (function!=KUI_GD_EXEC && function!=KUI_GD_CHECK) ||
       (s->command!=KUI_GD_PIOREAD && s->command!=KUI_GD_DMAREAD)) return;
    struct kui_toy_loader_data_probe_phase *p=&data_probe_report.phase[data_probe_scope.phase];
    data_probe_add(&p->data_visits,1u);
    if(!data_probe_card || !data_probe_sr || !data_probe_caller || !data_probe_original_read || !data_probe_original_block) {
        data_probe_add(&p->callback_conflicts,1u);return;
    }
    data_probe_scope.saved_sr=*data_probe_sr;data_probe_scope.caller_pr=data_probe_caller[0];
    data_probe_add(&p->sr_buckets[(data_probe_scope.saved_sr&UINT32_C(0x10000000)?16u:0u)+((data_probe_scope.saved_sr>>4u)&15u)],1u);
    if(s->ops.read!=data_probe_original_read || data_probe_card->transport!=KUI_STORAGE_SCI ||
       data_probe_card->device.sd.bus.transfer_block!=data_probe_original_block) {
        data_probe_add(&p->callback_conflicts,1u);return;
    }
    s->ops.read=data_probe_read;data_probe_scope.installed=1u;
}
void kui_toy_loader_data_probe_end(struct kui_retail_gd *s) {
    if(data_probe_report.frozen) return;
    if(!data_probe_scope.active) {data_probe_initialize();data_probe_add(&data_probe_report.unmatched_end,1u);return;}
    if(data_probe_scope.nested) {--data_probe_scope.nested;return;}
    if(data_probe_scope.service && data_probe_scope.service->ops.read==data_probe_read)
        data_probe_scope.service->ops.read=data_probe_original_read;
    else if(data_probe_scope.installed && data_probe_scope.service &&
            data_probe_scope.service->ops.read!=data_probe_original_read)
        data_probe_add(&data_probe_report.phase[data_probe_scope.phase].callback_conflicts,1u);
    if(s!=data_probe_scope.service) data_probe_add(&data_probe_report.phase[data_probe_scope.phase].callback_conflicts,1u);
    data_probe_scope.active=0u;data_probe_scope.service=NULL;
}
const uint32_t *kui_toy_loader_data_probe_words(void) {
    data_probe_initialize();return (const uint32_t *)(const void *)&data_probe_report;
}
uint32_t kui_toy_loader_data_probe_word_count(void) {return 192u;}
void kui_toy_loader_data_probe_freeze(void) {
    data_probe_initialize();
    if(data_probe_report.frozen) return;
    if(data_probe_scope.service && data_probe_scope.service->ops.read==data_probe_read)
        data_probe_scope.service->ops.read=data_probe_original_read;
    if(data_probe_card && data_probe_card->device.sd.bus.transfer_block==data_probe_payload)
        data_probe_card->device.sd.bus.transfer_block=data_probe_original_block;
    data_probe_scope.active=0u;data_probe_scope.service=NULL;
    data_probe_control_snapshot();data_probe_report.frozen=1u;
}
#ifdef KUI_TOY_LOADER_DATA_PROBE_HOST_TEST
void kui_toy_loader_data_probe_host_bind(struct kui_retail_storage *card,
    const volatile uint32_t *sr,const volatile uint32_t *caller,
    kui_toy_loader_data_probe_read_fn read,kui_toy_loader_data_probe_block_fn block) {
    data_probe_card=card;data_probe_sr=sr;data_probe_caller=caller;
    data_probe_original_read=read;data_probe_original_block=block;
}
void kui_toy_loader_data_probe_host_bind_diagnostic(const volatile struct kui_sci_sd_diagnostic *diagnostic) {
    data_probe_diagnostic=diagnostic;
}
void kui_toy_loader_data_probe_host_reset(void) {
    kui_toy_loader_data_probe_freeze();
    data_probe_report=(struct kui_toy_loader_data_probe_report){0};
    data_probe_scope.service=NULL;data_probe_scope.active=0u;data_probe_scope.nested=0u;
    data_probe_scope.phase=0u;data_probe_scope.saved_sr=0u;data_probe_scope.caller_pr=0u;
    data_probe_scope.initialized=0u;data_probe_scope.epoch=0u;data_probe_scope.installed=0u;
    data_probe_read_state.active=0u;data_probe_read_state.payload_active=0u;
    data_probe_read_state.suppressed=0u;
    data_probe_diagnostic=NULL;
}
#endif
#undef DATA_PROBE_READ
#endif
