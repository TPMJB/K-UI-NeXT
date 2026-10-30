/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/g1_mode_probe.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Sega SPI v1.31, sections 3.4, 7.1/7.2 and REQ_MODE/SET_MODE. This
 * implementation deliberately supports only their ten-byte mode payload. */
#define BSY 0x80u
#define DF 0x20u
#define DRQ 0x08u
#define CHECK 0x01u
#define TIMEOUT_US UINT64_C(5000000)
#define POLL_LIMIT 1000000u
struct probe {
    const struct kui_g1_mode_probe_bus *bus;
    struct kui_g1_mode_probe_status *out;
    bool uncertain, modified;
};
const char *kui_g1_probe_result_name(enum kui_g1_probe_result r) {
    static const char *const names[]={"verified","invalid request","G1 busy",
        "ownership changed","command timeout","unexpected packet phase",
        "unexpected transfer length","device rejected command","mode readback differs"};
    return (unsigned)r<sizeof(names)/sizeof(names[0])?names[r]:"invalid result";
}
static uint8_t rd(struct probe *p,enum kui_g1_probe_reg r) {
    return p->bus->read8(p->bus->ctx,r);
}
static void wr(struct probe *p,enum kui_g1_probe_reg r,uint8_t v) {
    p->bus->write8(p->bus->ctx,r,v);
}
static void settle(struct probe *p) {
    /* The taskfile's BSY transition is only guaranteed after 400 ns. Four
     * alternate-status reads are the conventional ATA settle interval. */
    for(unsigned i=0;i<4;++i)(void)rd(p,KUI_G1_ALTSTATUS);
}
static enum kui_g1_probe_result uncertain(struct probe *p,enum kui_g1_probe_result r) {
    p->uncertain=true;p->out->restart_required=true;return r;
}
static enum kui_g1_probe_result terminal(struct probe *p,uint8_t status) {
    struct kui_g1_mode_probe_status *o=p->out;
    o->status=status;o->reason=rd(p,KUI_G1_REASON);
    if((o->reason&3u)!=3u)return uncertain(p,KUI_G1_PROBE_PHASE);
    /* Reading Status acknowledges only this completed raw command's IRQ. */
    o->status=rd(p,KUI_G1_STATUS);
    if(o->status&(BSY|DRQ|DF))return uncertain(p,KUI_G1_PROBE_PHASE);
    o->error=(o->status&CHECK)?rd(p,KUI_G1_ERROR):0;
    return (o->status&CHECK)?KUI_G1_PROBE_DEVICE_ERROR:KUI_G1_PROBE_OK;
}
static enum kui_g1_probe_result wait_phase(struct probe *p,bool data,uint8_t reason,
                                         uint64_t started) {
    const struct kui_g1_mode_probe_bus *b=p->bus;
    for(unsigned n=0;n<POLL_LIMIT;++n) {
        if(!b->safe(b->ctx))return uncertain(p,KUI_G1_PROBE_OWNERSHIP);
        uint8_t status=rd(p,KUI_G1_ALTSTATUS);p->out->status=status;
        if(status==0xffu || status==0)return uncertain(p,KUI_G1_PROBE_PHASE);
        if(!(status&BSY)) {
            if(status&DF)return uncertain(p,KUI_G1_PROBE_DEVICE_ERROR);
            if(status&DRQ) {
                p->out->reason=rd(p,KUI_G1_REASON);
                if(!data || (p->out->reason&3u)!=reason)
                    return uncertain(p,KUI_G1_PROBE_PHASE);
                if(status&CHECK)return uncertain(p,KUI_G1_PROBE_DEVICE_ERROR);
                return KUI_G1_PROBE_OK;
            }
            /* An idle phase may precede device processing briefly. A final
             * CoD=IO=1 is authoritative completion, including CHECK errors. */
            if((rd(p,KUI_G1_REASON)&3u)==3u) {
                enum kui_g1_probe_result r=terminal(p,status);
                return data && r==KUI_G1_PROBE_OK?KUI_G1_PROBE_LENGTH:r;
            }
        }
        if(b->now_us(b->ctx)-started>=TIMEOUT_US)break;
        b->pause(b->ctx);
    }
    return uncertain(p,KUI_G1_PROBE_TIMEOUT);
}
static enum kui_g1_probe_result packet(struct probe *p,bool set,uint8_t data[KUI_G1_MODE_BYTES]) {
    const struct kui_g1_mode_probe_bus *b=p->bus;
    p->out->status=0;p->out->reason=0;p->out->error=0;
    if(p->uncertain || !b->safe(b->ctx))return uncertain(p,KUI_G1_PROBE_OWNERSHIP);
    uint8_t status=rd(p,KUI_G1_ALTSTATUS);
    p->out->status=status;
    if(status==0 || status==0xff || status&(BSY|DRQ|DF))
        return uncertain(p,KUI_G1_PROBE_BUSY);
    if(rd(p,KUI_G1_DEVICE)&0x1fu)return uncertain(p,KUI_G1_PROBE_OWNERSHIP);
    const uint8_t cdb[12]={set?0x12u:0x11u,0,0,0,KUI_G1_MODE_BYTES,0,0,0,0,0,0,0};
    uint64_t started=b->now_us(b->ctx);
    wr(p,KUI_G1_FEATURES,0); /* PIO, never DMA. */
    wr(p,KUI_G1_BYTECOUNT_LO,KUI_G1_MODE_BYTES);wr(p,KUI_G1_BYTECOUNT_HI,0);
    wr(p,KUI_G1_COMMAND,0xa0);++p->out->commands;
    if(set)p->modified=true; /* Even a rejected SET may be partial. */
    settle(p);
    enum kui_g1_probe_result r=wait_phase(p,true,1,started);
    if(r!=KUI_G1_PROBE_OK)return r;
    status=rd(p,KUI_G1_STATUS); /* Acknowledge our packet/data request. */
    if((status&(BSY|DRQ|DF|CHECK))!=DRQ)return uncertain(p,KUI_G1_PROBE_PHASE);
    for(unsigned i=0;i<12;i+=2)b->write16(b->ctx,(uint16_t)cdb[i]|(uint16_t)cdb[i+1]<<8);
    settle(p);
    r=wait_phase(p,true,set?0u:2u,started);
    if(r!=KUI_G1_PROBE_OK)return r;
    unsigned bytes=(unsigned)rd(p,KUI_G1_BYTECOUNT_LO)|((unsigned)rd(p,KUI_G1_BYTECOUNT_HI)<<8);
    if(bytes!=KUI_G1_MODE_BYTES)return uncertain(p,KUI_G1_PROBE_LENGTH);
    status=rd(p,KUI_G1_STATUS);
    if((status&(BSY|DRQ|DF|CHECK))!=DRQ)return uncertain(p,KUI_G1_PROBE_PHASE);
    for(unsigned i=0;i<KUI_G1_MODE_BYTES;i+=2) {
        if(set)b->write16(b->ctx,(uint16_t)data[i]|(uint16_t)data[i+1]<<8);
        else {uint16_t v=b->read16(b->ctx);data[i]=(uint8_t)v;data[i+1]=(uint8_t)(v>>8);}
    }
    settle(p);
    return wait_phase(p,false,3,started);
}
static uint16_t difference(const uint8_t *a,const uint8_t *b) {
    uint16_t mask=0;for(unsigned i=0;i<KUI_G1_MODE_BYTES;++i)if(a[i]!=b[i])mask|=(uint16_t)(1u<<i);
    return mask;
}
static bool valid_mode(const uint8_t *m) {
    return !m[0] && !m[1] && m[2]<=7 && !m[3] && !(m[6]&~0x39u) && !m[7] && !m[8];
}
static bool restore(struct probe *p) {
    if(p->uncertain)return false;
    uint8_t original[KUI_G1_MODE_BYTES],observed[KUI_G1_MODE_BYTES]={0};
    memcpy(original,p->out->original,sizeof(original));
    enum kui_g1_probe_result a=packet(p,true,original);
    enum kui_g1_probe_result q=p->uncertain?KUI_G1_PROBE_OWNERSHIP:packet(p,false,observed);
    bool ok=q==KUI_G1_PROBE_OK && !difference(original,observed);
    p->bus->log("G1 mode restore set=%s query=%s exact=%u",kui_g1_probe_result_name(a),
        kui_g1_probe_result_name(q),ok?1u:0u);
    p->out->restored=ok;
    if(!ok) {
        p->out->restart_required=true;
        p->out->result=q!=KUI_G1_PROBE_OK?q:KUI_G1_PROBE_MISMATCH;
    }
    return ok;
}
static void mode_log(struct probe *p,const char *label,const uint8_t *m) {
    p->bus->log("G1 %s raw=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x speed=%u standby=%u flags=%02x retries=%u",
        label,m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[2],
        (unsigned)m[4]*256u+m[5],m[6],m[9]);
}
bool kui_g1_mode_probe_run(const struct kui_g1_mode_probe_bus *b,
    void (*progress)(void *,const struct kui_g1_mode_probe_status *),void *ctx,
    struct kui_g1_mode_probe_status *o) {
    if(!o)return false;
    memset(o,0,sizeof(*o));o->total=KUI_G1_MODE_CHOICES;o->result=KUI_G1_PROBE_INVALID;
    if(!b || !b->read8 || !b->write8 || !b->read16 || !b->write16 || !b->safe ||
       !b->now_us || !b->pause || !b->cancelled || !b->log) {
        o->failed=true;snprintf(o->message,sizeof(o->message),"Invalid direct G1 probe request");return false;
    }
    struct probe p={b,o,false,false};uint64_t started=b->now_us(b->ctx);
    snprintf(o->message,sizeof(o->message),"Reading original mode directly over G1");
    if(progress)progress(ctx,o);
    if(b->cancelled(b->ctx)) {o->stopped=true;o->restored=true;goto done;}
    o->result=packet(&p,false,o->original);
    if(o->result!=KUI_G1_PROBE_OK) {o->failed=true;goto done;}
    o->restored=true;
    mode_log(&p,"original raw reply",o->original);
    if(!valid_mode(o->original)) {o->result=KUI_G1_PROBE_INVALID;o->failed=true;goto done;}
    o->snapshot_valid=true;
    for(unsigned code=0;code<KUI_G1_MODE_CHOICES;++code) {
        if(b->cancelled(b->ctx)) {o->stopped=true;break;}
        o->current=code;o->tried[code]=true;o->restored=false;
        memcpy(o->requested[code],o->original,KUI_G1_MODE_BYTES);o->requested[code][2]=(uint8_t)code;
        snprintf(o->message,sizeof(o->message),"Testing direct G1 speed code %u; no sector reads",code);
        if(progress)progress(ctx,o);
        if(b->cancelled(b->ctx)) {o->tried[code]=false;o->restored=true;o->stopped=true;break;}
        mode_log(&p,"requested",o->requested[code]);
        enum kui_g1_probe_result applied=packet(&p,true,o->requested[code]);
        o->set_result[code]=applied;o->set_status[code]=o->status;
        o->set_reason[code]=o->reason;o->set_error[code]=o->error;
        b->log("G1 SET speed code=%u result=%s status=%02x reason=%02x error=%02x",
            code,kui_g1_probe_result_name(applied),o->set_status[code],o->set_reason[code],o->set_error[code]);
        enum kui_g1_probe_result queried=p.uncertain?KUI_G1_PROBE_OWNERSHIP:packet(&p,false,o->observed[code]);
        o->query_result[code]=queried;
        if(queried==KUI_G1_PROBE_OK) {
            o->observed_valid[code]=true;
            o->difference[code]=difference(o->requested[code],o->observed[code]);
            mode_log(&p,"returned before restore",o->observed[code]);
        }
        o->attempt[code]=applied!=KUI_G1_PROBE_OK?applied:queried!=KUI_G1_PROBE_OK?queried:
            o->difference[code]?KUI_G1_PROBE_MISMATCH:KUI_G1_PROBE_OK;
        o->verified[code]=o->attempt[code]==KUI_G1_PROBE_OK;
        b->log("G1 speed code=%u result=%s observed=%u diff_byte_mask=%03x status=%02x reason=%02x error=%02x",
            code,kui_g1_probe_result_name(o->attempt[code]),o->observed_valid[code]?1u:0u,
            o->difference[code],o->status,o->reason,o->error);
        if(p.uncertain) {o->result=o->attempt[code];o->failed=true;break;}
        if(!restore(&p)) {o->failed=true;break;}
        ++o->done;
        if(progress)progress(ctx,o);
        if(b->cancelled(b->ctx)) {o->stopped=true;break;}
    }
    if(!o->failed && !o->stopped) {o->complete=o->done==KUI_G1_MODE_CHOICES;o->result=KUI_G1_PROBE_OK;}
done:
    if(p.uncertain) {o->restored=false;o->restart_required=true;}
    else if(!p.modified)o->restored=true;
    o->elapsed_us=b->now_us(b->ctx)-started;
    snprintf(o->message,sizeof(o->message),"%s",o->restart_required?
        "Direct G1 mode uncertain; restart console; saved jobs unchanged":o->stopped?
        "Direct G1 probe stopped; original mode restored":o->complete?
        "Direct G1 mode probe finished; original mode restored":
        "Direct G1 mode probe refused or failed; see Diagnostics");
    b->log("G1 probe end complete=%u stopped=%u failed=%u restored=%u restart=%u commands=%u elapsed_us=%" PRIu64,
        o->complete?1u:0u,o->stopped?1u:0u,o->failed?1u:0u,o->restored?1u:0u,
        o->restart_required?1u:0u,o->commands,o->elapsed_us);
    if(progress)progress(ctx,o);
    return o->complete && !o->failed && o->restored && !o->restart_required;
}
