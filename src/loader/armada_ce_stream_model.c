/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_stream_model.h"
#include <stddef.h>

#define R KUI_ARMADA_CE_STREAM_RING_BYTES
#define S KUI_ARMADA_CE_STREAM_SECTOR_BYTES
#define G KUI_ARMADA_CE_STREAM_GUARD_BYTES
static bool valid(const struct kui_armada_ce_stream_model *m) {return m && m->initialized;}
static void cancel(struct kui_armada_ce_stream_model *m) {
    m->read=(struct kui_armada_ce_stream_read){0};
    m->refill=(struct kui_armada_ce_stream_refill){0};
}
static enum kui_armada_ce_stream_result fault(struct kui_armada_ce_stream_model *m) {
    cancel(m);m->state=KUI_ARMADA_CE_STREAM_FAULT;
    m->after_stop=KUI_ARMADA_CE_STREAM_FAULT;m->stop_needed=m->native_running;
    return KUI_ARMADA_CE_STREAM_IO;
}
static void prime(struct kui_armada_ce_stream_model *m) {
    m->origin=m->fetch=m->audible;m->produced=m->consumed=0;
    m->staged=m->last_play=m->write_cursor=0;cancel(m);
}
static void apply(struct kui_armada_ce_stream_model *m) {
    if(m->pending_play) {
        m->first_lba=m->next_first_lba;m->end_lba=m->next_end_lba;
        m->repeats=m->next_repeats;m->selection=m->next_selection;
        m->infinite=m->repeats==15u;
        m->finite_end=m->infinite?UINT64_MAX:
            (uint64_t)(m->end_lba-m->first_lba)*S*(m->repeats+1u);
        m->audible=0;m->pending_play=false;
    }
    m->state=m->after_stop;m->stop_needed=false;m->native_running=false;
    if(m->state==KUI_ARMADA_CE_STREAM_PREFILL) prime(m);
    else {m->staged=0;cancel(m);}
    if(m->state==KUI_ARMADA_CE_STREAM_STOPPED) {
        m->selection=m->first_lba=m->end_lba=m->repeats=0;
        m->origin=m->audible=m->fetch=m->produced=m->consumed=m->finite_end=0;
    }
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_init(
    struct kui_armada_ce_stream_model *m,const struct kui_armada_ce_stream_track *t,uint32_t n) {
    if(!m || !t || !n || n>99u) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    for(uint32_t i=0;i<n;i++) if(t[i].number!=i+1u ||
       (t[i].control!=0u && t[i].control!=4u) || t[i].first_lba>=t[i].end_lba ||
       t[i].end_lba>719850u || (i && t[i-1u].end_lba>t[i].first_lba))
        return KUI_ARMADA_CE_STREAM_ARGUMENT;
    *m=(struct kui_armada_ce_stream_model){.tracks=t,.track_count=n,
        .state=KUI_ARMADA_CE_STREAM_STOPPED,.initialized=true};
    return KUI_ARMADA_CE_STREAM_OK;
}
static bool audio_range(const struct kui_armada_ce_stream_model *m,uint32_t first,uint32_t end) {
    if(first>=end || end>719850u) return false;
    uint32_t at=first;
    for(uint32_t i=0;i<m->track_count && at<end;i++) {
        const struct kui_armada_ce_stream_track *t=&m->tracks[i];
        if(at>=t->end_lba) continue;
        if(at<t->first_lba || t->control) return false;
        at=end<t->end_lba?end:t->end_lba;
    }
    return at==end;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_request(
    struct kui_armada_ce_stream_model *m,uint32_t cmd,uint32_t first,uint32_t end,uint32_t repeat) {
    if(!valid(m)) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    if(cmd==20u) {
        if(!first || first>end || end>m->track_count || repeat>15u)
            return KUI_ARMADA_CE_STREAM_ARGUMENT;
        first=m->tracks[first-1u].first_lba;end=m->tracks[end-1u].end_lba;
    } else if(cmd==21u) {
        if(first<150u || end<150u || repeat>15u) return KUI_ARMADA_CE_STREAM_ARGUMENT;
        first-=150u;end-=150u;
    } else if(cmd!=22u && cmd!=23u && cmd!=33u) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    if((cmd==20u || cmd==21u) && !audio_range(m,first,end)) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    /* Native resume is idempotent while playing or already preparing the
     * same selection. Repeated resume must not cancel useful prefetch or
     * cause an unnecessary audible stop/re-prime. */
    if(!m->pending_play && !m->stop_needed &&
       ((cmd==23u && (m->state==KUI_ARMADA_CE_STREAM_PLAYING || m->state==KUI_ARMADA_CE_STREAM_PREFILL)) ||
        (cmd==22u && m->state==KUI_ARMADA_CE_STREAM_PAUSED) ||
        (cmd==33u && m->state==KUI_ARMADA_CE_STREAM_STOPPED))) return KUI_ARMADA_CE_STREAM_OK;
    if(m->epoch==UINT32_MAX) return KUI_ARMADA_CE_STREAM_OVERFLOW;
    ++m->epoch;cancel(m);
    if(cmd==20u || cmd==21u) {
        m->next_first_lba=first;m->next_end_lba=end;m->next_repeats=repeat;
        m->next_selection=m->epoch;m->pending_play=true;
        m->after_stop=KUI_ARMADA_CE_STREAM_PREFILL;
    } else if(cmd==33u) {
        /* Revoke selection immediately. Keep the old ring's interval and
         * origin until its native Stop completes so cursor observation can
         * still account for that lease, but RELEASE cannot revive it. */
        m->pending_play=false;m->selection=0;m->after_stop=KUI_ARMADA_CE_STREAM_STOPPED;
    } else if(!m->selection && !m->pending_play) m->after_stop=KUI_ARMADA_CE_STREAM_STOPPED;
    else if(cmd==22u) m->after_stop=KUI_ARMADA_CE_STREAM_PAUSED;
    else m->after_stop=!m->pending_play && !m->infinite && m->audible>=m->finite_end?
        KUI_ARMADA_CE_STREAM_ENDED:KUI_ARMADA_CE_STREAM_PREFILL;
    m->stop_needed=m->native_running;
    if(m->stop_needed) m->state=KUI_ARMADA_CE_STREAM_QUIESCING;
    else apply(m);
    return KUI_ARMADA_CE_STREAM_OK;
}
static bool filling(const struct kui_armada_ce_stream_model *m) {
    return m->state==KUI_ARMADA_CE_STREAM_PREFILL || m->state==KUI_ARMADA_CE_STREAM_PLAYING;
}
static bool source_eof(const struct kui_armada_ce_stream_model *m) {
    return !m->infinite && m->fetch==m->finite_end;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_read_plan(
    struct kui_armada_ce_stream_model *m,uint32_t max,struct kui_armada_ce_stream_read *out) {
    if(!valid(m) || !out || !max || max>KUI_ARMADA_CE_STREAM_BATCH_SECTORS)
        return KUI_ARMADA_CE_STREAM_ARGUMENT;
    if(!filling(m)) return KUI_ARMADA_CE_STREAM_STATE;
    if(m->read.id || m->refill.id) return KUI_ARMADA_CE_STREAM_BUSY;
    if(m->staged>=64u || source_eof(m)) return KUI_ARMADA_CE_STREAM_NOTHING;
    if(m->sequence==UINT32_MAX) return KUI_ARMADA_CE_STREAM_OVERFLOW;
    uint64_t span=(uint64_t)(m->end_lba-m->first_lba)*S;
    uint64_t relative=m->fetch%span;
    uint32_t lba=m->first_lba+(uint32_t)(relative/S),skip=(uint32_t)(relative%S);
    uint32_t track=0,end=m->end_lba;
    for(uint32_t i=0;i<m->track_count;i++) if(lba>=m->tracks[i].first_lba && lba<m->tracks[i].end_lba) {
        track=m->tracks[i].number;if(end>m->tracks[i].end_lba) end=m->tracks[i].end_lba;break;
    }
    uint32_t sectors=end-lba;if(sectors>max) sectors=max;
    uint32_t bytes=sectors*S-skip;
    if(!m->infinite && bytes>m->finite_end-m->fetch) bytes=(uint32_t)(m->finite_end-m->fetch);
    if(!track || !bytes || bytes>KUI_ARMADA_CE_STREAM_STAGE_BYTES-m->staged)
        return KUI_ARMADA_CE_STREAM_ARGUMENT;
    m->read=(struct kui_armada_ce_stream_read){m->epoch,m->selection,++m->sequence,track,lba,sectors,skip,bytes};
    *out=m->read;return KUI_ARMADA_CE_STREAM_OK;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_read_validate(
    const struct kui_armada_ce_stream_model *m,const struct kui_armada_ce_stream_read *r) {
    if(!valid(m) || !r) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    const struct kui_armada_ce_stream_read *a=&m->read;
    if(!filling(m) || !r->id || r->epoch!=m->epoch || r->selection!=m->selection ||
       r->epoch!=a->epoch || r->selection!=a->selection || r->id!=a->id ||
       r->track!=a->track || r->lba!=a->lba || r->sectors!=a->sectors ||
       r->skip!=a->skip || r->bytes!=a->bytes) return KUI_ARMADA_CE_STREAM_STALE;
    return KUI_ARMADA_CE_STREAM_OK;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_read_complete(
    struct kui_armada_ce_stream_model *m,const struct kui_armada_ce_stream_read *r,bool success,uint32_t bytes) {
    enum kui_armada_ce_stream_result s=kui_armada_ce_stream_read_validate(m,r);
    if(s!=KUI_ARMADA_CE_STREAM_OK) return s;
    if(!success) return fault(m);
    if(bytes!=r->bytes) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    if(m->fetch>UINT64_MAX-bytes) return KUI_ARMADA_CE_STREAM_OVERFLOW;
    m->fetch+=bytes;m->staged+=bytes;m->read=(struct kui_armada_ce_stream_read){0};
    return KUI_ARMADA_CE_STREAM_OK;
}
static bool overlap(uint32_t first,uint32_t bytes,uint32_t a,uint32_t b) {
    if(a==b) return false;
    return a<b?(first<b && a<first+bytes):((first<R && a<first+bytes) || first<b);
}
static uint32_t room(const struct kui_armada_ce_stream_model *m) {
    if(m->produced<m->consumed || m->produced-m->consumed>R) return 0;
    uint32_t n=R-(uint32_t)(m->produced-m->consumed);
    if(m->native_running) n=n>G?n-G:0u;
    return n;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_refill_plan(
    struct kui_armada_ce_stream_model *m,uint32_t max,struct kui_armada_ce_stream_refill *out) {
    if(!valid(m) || !out || max<64u) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    if(!filling(m)) return KUI_ARMADA_CE_STREAM_STATE;
    if(m->read.id || m->refill.id) return KUI_ARMADA_CE_STREAM_BUSY;
    uint32_t offset=(uint32_t)(m->produced%R),bytes=room(m),audio;
    if(bytes>R-offset) bytes=R-offset;
    if(bytes>max) bytes=max;
    bytes&=~63u;
    if(!source_eof(m) && bytes>m->staged) bytes=m->staged&~63u;
    if(!bytes) return KUI_ARMADA_CE_STREAM_NOTHING;
    if(m->native_running && overlap(offset,bytes,m->last_play,m->write_cursor))
        return KUI_ARMADA_CE_STREAM_NOTHING;
    if(m->sequence==UINT32_MAX) return KUI_ARMADA_CE_STREAM_OVERFLOW;
    audio=bytes<m->staged?bytes:m->staged;
    m->refill=(struct kui_armada_ce_stream_refill){m->epoch,m->selection,++m->sequence,offset,bytes,audio,bytes-audio};
    *out=m->refill;return KUI_ARMADA_CE_STREAM_OK;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_refill_validate(
    const struct kui_armada_ce_stream_model *m,const struct kui_armada_ce_stream_refill *r) {
    if(!valid(m) || !r) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    const struct kui_armada_ce_stream_refill *a=&m->refill;
    if(!filling(m) || !r->id || r->epoch!=m->epoch || r->selection!=m->selection ||
       r->epoch!=a->epoch || r->selection!=a->selection || r->id!=a->id ||
       r->ring_offset!=a->ring_offset || r->bytes!=a->bytes ||
       r->audio_bytes!=a->audio_bytes || r->pad_bytes!=a->pad_bytes || r->bytes>room(m) ||
       (m->native_running && overlap(r->ring_offset,r->bytes,m->last_play,m->write_cursor)))
        return KUI_ARMADA_CE_STREAM_STALE;
    return KUI_ARMADA_CE_STREAM_OK;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_refill_complete(
    struct kui_armada_ce_stream_model *m,const struct kui_armada_ce_stream_refill *r,bool success) {
    enum kui_armada_ce_stream_result s=kui_armada_ce_stream_refill_validate(m,r);
    if(s!=KUI_ARMADA_CE_STREAM_OK) return s;
    if(!success) return fault(m);
    if(m->produced>UINT64_MAX-r->bytes) return KUI_ARMADA_CE_STREAM_OVERFLOW;
    m->staged-=r->audio_bytes;m->produced+=r->bytes;
    m->refill=(struct kui_armada_ce_stream_refill){0};return KUI_ARMADA_CE_STREAM_OK;
}
bool kui_armada_ce_stream_can_start(const struct kui_armada_ce_stream_model *m) {
    return valid(m) && m->state==KUI_ARMADA_CE_STREAM_PREFILL &&
        !m->native_running && m->produced==R && !m->read.id && !m->refill.id;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_start_complete(
    struct kui_armada_ce_stream_model *m,uint32_t epoch,uint32_t now,bool success) {
    if(!valid(m)) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    if(epoch!=m->epoch) return KUI_ARMADA_CE_STREAM_STALE;
    if(!kui_armada_ce_stream_can_start(m)) return KUI_ARMADA_CE_STREAM_STATE;
    if(!success) return fault(m);
    m->ring_epoch=epoch;m->last_ms=now;m->last_play=m->write_cursor=0;m->elapsed_ms=0;
    m->native_running=true;m->state=KUI_ARMADA_CE_STREAM_PLAYING;return KUI_ARMADA_CE_STREAM_OK;
}
static enum kui_armada_ce_stream_result recover(struct kui_armada_ce_stream_model *m,
    enum kui_armada_ce_stream_result why) {
    if(m->state==KUI_ARMADA_CE_STREAM_QUIESCING) return why;
    if(m->epoch==UINT32_MAX) return KUI_ARMADA_CE_STREAM_OVERFLOW;
    ++m->epoch;++m->recoveries;cancel(m);m->after_stop=KUI_ARMADA_CE_STREAM_PREFILL;
    m->state=KUI_ARMADA_CE_STREAM_QUIESCING;m->stop_needed=true;return why;
}
uint32_t kui_armada_ce_stream_cursor_allowance(const struct kui_armada_ce_stream_model *m,uint32_t now) {
    uint64_t elapsed=m->elapsed_ms+(uint32_t)(now-m->last_ms)+20u;
    if(elapsed>UINT64_MAX/176400u) return 0;
    uint64_t maximum=(elapsed*176400u/1000u)&~UINT64_C(3);
    if(maximum<=m->consumed) return 0;
    maximum-=m->consumed;
    return maximum>UINT32_MAX?UINT32_MAX:(uint32_t)maximum;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_observe(
    struct kui_armada_ce_stream_model *m,uint32_t lease,uint32_t play,uint32_t write,uint32_t now) {
    if(!valid(m)) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    if(!m->native_running || lease!=m->ring_epoch) return KUI_ARMADA_CE_STREAM_STALE;
    if(play>=R || write>=R || (play&3u) || (write&3u)) return recover(m,KUI_ARMADA_CE_STREAM_CURSOR);
    if(now-m->last_ms>KUI_ARMADA_CE_STREAM_MAX_OBSERVE_MS) return recover(m,KUI_ARMADA_CE_STREAM_AGE);
    uint32_t delta=play>=m->last_play?play-m->last_play:R-m->last_play+play;
    if(delta>R/2u) return recover(m,KUI_ARMADA_CE_STREAM_CURSOR);
    /* Native position can remain unchanged across several polls then catch
     * up. Preserve their elapsed time and compare consumption to the whole
     * playback lease, retaining the same20ms phase allowance. Resetting the
     * budget at each unchanged poll falsely rejects that normal catch-up. */
    uint32_t age=now-m->last_ms;
    if(m->elapsed_ms>UINT64_MAX/176400u-age-20u) return KUI_ARMADA_CE_STREAM_OVERFLOW;
    uint32_t maximum=kui_armada_ce_stream_cursor_allowance(m,now);
    if(delta>maximum) return recover(m,KUI_ARMADA_CE_STREAM_CURSOR);
    uint64_t ready=m->produced-m->consumed;
    if(delta>ready) return recover(m,KUI_ARMADA_CE_STREAM_UNDERRUN);
    if(m->consumed>UINT64_MAX-delta || m->origin>UINT64_MAX-(m->consumed+delta))
        return KUI_ARMADA_CE_STREAM_OVERFLOW;
    m->consumed+=delta;m->audible=m->origin+m->consumed;
    if(!m->infinite && m->audible>m->finite_end) m->audible=m->finite_end;
    m->last_play=play;m->write_cursor=write;m->last_ms=now;m->elapsed_ms+=age;
    if(!m->infinite && m->audible==m->finite_end && m->state!=KUI_ARMADA_CE_STREAM_QUIESCING) {
        if(m->epoch==UINT32_MAX) return KUI_ARMADA_CE_STREAM_OVERFLOW;
        ++m->epoch;cancel(m);m->state=KUI_ARMADA_CE_STREAM_QUIESCING;
        m->after_stop=KUI_ARMADA_CE_STREAM_ENDED;m->stop_needed=true;return KUI_ARMADA_CE_STREAM_EOF;
    }
    return KUI_ARMADA_CE_STREAM_OK;
}
enum kui_armada_ce_stream_result kui_armada_ce_stream_stop_complete(
    struct kui_armada_ce_stream_model *m,uint32_t epoch,bool success) {
    if(!valid(m)) return KUI_ARMADA_CE_STREAM_ARGUMENT;
    if(epoch!=m->epoch) return KUI_ARMADA_CE_STREAM_STALE;
    if(!m->stop_needed || !m->native_running) return KUI_ARMADA_CE_STREAM_STATE;
    if(!success) return fault(m);
    apply(m);return KUI_ARMADA_CE_STREAM_OK;
}
