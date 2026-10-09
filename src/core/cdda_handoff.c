/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_handoff.h"

_Static_assert(sizeof(struct kui_cdda_handoff_desc)==KUI_CDDA_HANDOFF_BYTES,
    "CDDA handoff must contain exactly40 wire words");

static bool range(uint32_t first,uint32_t end) {
    return first>=KUI_CDDA_HANDOFF_RAM_FIRST && end<=KUI_CDDA_HANDOFF_RAM_END &&
        first<end && !(first&31u) && !(end&31u);
}
static bool covered(uint32_t entry,uint32_t first,uint32_t end) {
    return !(entry&1u) && entry>=first && entry<end;
}
enum kui_cdda_handoff_result kui_cdda_handoff_validate(const struct kui_cdda_handoff_desc *d) {
    if(!d || d->magic0!=KUI_CDDA_HANDOFF_MAGIC0 || d->magic1!=KUI_CDDA_HANDOFF_MAGIC1)
        return KUI_CDDA_HANDOFF_INVALID;
    if(d->version!=KUI_CDDA_HANDOFF_VERSION || d->bytes!=KUI_CDDA_HANDOFF_BYTES ||
       d->service_revision!=KUI_CDDA_HANDOFF_SERVICE_REVISION || d->clock_hz!=KUI_CDDA_TMU_HZ)
        return KUI_CDDA_HANDOFF_REVISION;
    if(d->flags!=KUI_CDDA_HANDOFF_FLAGS || d->storage_transport!=KUI_CDDA_HANDOFF_SCI ||
       d->storage_rights!=KUI_CDDA_HANDOFF_STORAGE_RIGHTS ||
       d->resource_rights!=KUI_CDDA_HANDOFF_RESOURCE_RIGHTS || d->tmu_unit!=1u || d->sound_channels!=3u)
        return KUI_CDDA_HANDOFF_RIGHTS;
    for(unsigned i=0;i<11u;i++) if(d->reserved[i]) return KUI_CDDA_HANDOFF_INVALID;
    if(d->sound_first!=KUI_CDDA_HANDOFF_SOUND_FIRST || d->sound_end!=KUI_CDDA_HANDOFF_SOUND_END ||
       !d->service_gap_ticks || d->service_gap_ticks>KUI_CDDA_HANDOFF_MAX_GAP_TICKS)
        return KUI_CDDA_HANDOFF_RANGE;
    uint32_t first[]={d->engine_code_first,d->engine_state_first,d->engine_stack_first,
        d->service_stack_first,d->client_code_first,d->client_stack_first};
    uint32_t end[]={d->engine_code_end,d->engine_state_end,d->engine_stack_end,
        d->service_stack_end,d->client_code_end,d->client_stack_end};
    for(unsigned i=0;i<6u;i++) if(!range(first[i],end[i])) return KUI_CDDA_HANDOFF_RANGE;
    for(unsigned i=0;i<6u;i++) for(unsigned j=i+1u;j<6u;j++)
        if(first[i]<end[j] && first[j]<end[i]) return KUI_CDDA_HANDOFF_OVERLAP;
    if(!covered(d->client_entry,d->client_code_first,d->client_code_end) ||
       !covered(d->service_entry,d->engine_code_first,d->engine_code_end)) return KUI_CDDA_HANDOFF_ENTRY;
    return KUI_CDDA_HANDOFF_OK;
}
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
enum kui_cdda_handoff_result kui_cdda_handoff_decode(const uint8_t *wire,size_t bytes,
    struct kui_cdda_handoff_desc *out) {
    if(!wire || !out || bytes!=KUI_CDDA_HANDOFF_BYTES) return KUI_CDDA_HANDOFF_INVALID;
    struct kui_cdda_handoff_desc d;
    unsigned at=0;
#define WORD(field) do {d.field=read32(wire+at);at+=4u;} while(0)
    WORD(magic0);WORD(magic1);WORD(version);WORD(bytes);WORD(service_revision);
    WORD(flags);WORD(storage_transport);WORD(storage_rights);WORD(resource_rights);WORD(tmu_unit);
    WORD(sound_channels);WORD(clock_hz);WORD(service_gap_ticks);
    WORD(engine_code_first);WORD(engine_code_end);WORD(engine_state_first);WORD(engine_state_end);
    WORD(engine_stack_first);WORD(engine_stack_end);WORD(service_stack_first);WORD(service_stack_end);
    WORD(client_code_first);WORD(client_code_end);WORD(client_stack_first);WORD(client_stack_end);
    WORD(sound_first);WORD(sound_end);WORD(client_entry);WORD(service_entry);
#undef WORD
    for(unsigned i=0;i<11u;i++) {d.reserved[i]=read32(wire+at);at+=4u;}
    enum kui_cdda_handoff_result result=kui_cdda_handoff_validate(&d);
    if(result==KUI_CDDA_HANDOFF_OK) *out=d;
    return result;
}

static bool guard_valid(const struct kui_cdda_service_guard *g) {
    return g && g->initialized && g->state>=KUI_CDDA_SERVICE_IDLE && g->state<=KUI_CDDA_SERVICE_FAULT;
}
static bool aliases(const void *out,size_t bytes,const struct kui_cdda_service_guard *g) {
    uintptr_t address=(uintptr_t)out,owner=(uintptr_t)g;
    if(bytes>UINTPTR_MAX-address) return true;
    return address<=owner?owner-address<bytes:address-owner<sizeof(*g);
}
static void service_fault(struct kui_cdda_service_guard *g) {
    g->state=KUI_CDDA_SERVICE_FAULT;g->pending=(struct kui_cdda_service_ticket){0};
}
enum kui_cdda_service_result kui_cdda_service_init(struct kui_cdda_service_guard *g) {
    if(!g) return KUI_CDDA_SERVICE_INVALID;
    *g=(struct kui_cdda_service_guard){.initialized=true};return KUI_CDDA_SERVICE_OK;
}
enum kui_cdda_service_result kui_cdda_service_start(struct kui_cdda_service_guard *g,
    uint32_t now,uint32_t gap,uint32_t *epoch) {
    if(!guard_valid(g) || !epoch || !gap || gap>KUI_CDDA_HANDOFF_MAX_GAP_TICKS || aliases(epoch,sizeof(*epoch),g))
        return KUI_CDDA_SERVICE_INVALID;
    if(g->state==KUI_CDDA_SERVICE_BUSY) return KUI_CDDA_SERVICE_BUSY_RESULT;
    if(g->state==KUI_CDDA_SERVICE_ACTIVE) return KUI_CDDA_SERVICE_STATE;
    if(g->generation==UINT32_MAX) return KUI_CDDA_SERVICE_OVERFLOW;
    g->generation++;g->state=KUI_CDDA_SERVICE_ACTIVE;g->last_tick=now;g->gap_ticks=gap;
    g->pending=(struct kui_cdda_service_ticket){0};*epoch=g->generation;
    return KUI_CDDA_SERVICE_OK;
}
enum kui_cdda_service_result kui_cdda_service_enter(struct kui_cdda_service_guard *g,
    uint32_t epoch,uint32_t now,struct kui_cdda_service_ticket *out) {
    if(!guard_valid(g) || !out || aliases(out,sizeof(*out),g)) return KUI_CDDA_SERVICE_INVALID;
    if(epoch!=g->generation) return KUI_CDDA_SERVICE_STALE;
    if(g->state==KUI_CDDA_SERVICE_BUSY) return KUI_CDDA_SERVICE_BUSY_RESULT;
    if(g->state!=KUI_CDDA_SERVICE_ACTIVE) return KUI_CDDA_SERVICE_STATE;
    if(now-g->last_tick>=g->gap_ticks) {service_fault(g);return KUI_CDDA_SERVICE_DEADLINE;}
    if(g->call_generation==UINT32_MAX) return KUI_CDDA_SERVICE_OVERFLOW;
    g->call_generation++;g->state=KUI_CDDA_SERVICE_BUSY;
    g->pending=(struct kui_cdda_service_ticket){epoch,g->call_generation};*out=g->pending;
    return KUI_CDDA_SERVICE_OK;
}
enum kui_cdda_service_result kui_cdda_service_leave(struct kui_cdda_service_guard *g,
    const struct kui_cdda_service_ticket *ticket,uint32_t now,bool success) {
    if(!guard_valid(g) || !ticket) return KUI_CDDA_SERVICE_INVALID;
    if(g->state!=KUI_CDDA_SERVICE_BUSY || ticket->epoch!=g->generation ||
       ticket->epoch!=g->pending.epoch || ticket->call!=g->pending.call) return KUI_CDDA_SERVICE_STALE;
    if(now-g->last_tick>=g->gap_ticks) {service_fault(g);return KUI_CDDA_SERVICE_DEADLINE;}
    if(!success) {service_fault(g);return KUI_CDDA_SERVICE_IO;}
    g->last_tick=now;g->state=KUI_CDDA_SERVICE_ACTIVE;g->pending=(struct kui_cdda_service_ticket){0};
    return KUI_CDDA_SERVICE_OK;
}
enum kui_cdda_service_result kui_cdda_service_stop(struct kui_cdda_service_guard *g,uint32_t epoch) {
    if(!guard_valid(g)) return KUI_CDDA_SERVICE_INVALID;
    if(epoch!=g->generation) return KUI_CDDA_SERVICE_STALE;
    if(g->state==KUI_CDDA_SERVICE_BUSY) return KUI_CDDA_SERVICE_BUSY_RESULT;
    if(g->state==KUI_CDDA_SERVICE_IDLE) return KUI_CDDA_SERVICE_STATE;
    g->state=KUI_CDDA_SERVICE_STOPPED;g->pending=(struct kui_cdda_service_ticket){0};
    return KUI_CDDA_SERVICE_OK;
}
