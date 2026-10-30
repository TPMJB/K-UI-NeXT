/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/g1_mode_probe.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* A packet-device model: the host must issue the documented CDB and consume
 * exactly the advertised phase. Faults below deliberately leave outstanding
 * phases; a host which guesses/drains/reset-aborts them fails assertions. */
enum phase { IDLE, PACKET, GET_DATA, SET_DATA, FINISHED, ERROR_END };
enum fault { NONE, PACKET_REASON, DATA_REASON, SHORT, LONG, ODD, EXTRA,
    TIMEOUT_PACKET, TIMEOUT_DATA, TIMEOUT_FINISH, EARLY_COMPLETE };
struct fake {
    enum phase phase;
    enum fault fault;
    unsigned fault_command, error_command, commands, cdb_words, data_words;
    unsigned reads, writes, sets, queries, pauses, safe_lost_at;
    unsigned cancel_at, freeze_restore_at;
    uint64_t now;
    bool clock_stopped, cancel_now, unsafe, busy, slave, all_speeds;
    uint8_t mode[10], payload[10], cdb[12], bytecount[2], features;
};
static unsigned cases;
static unsigned progress_calls;
static unsigned cancel_on_progress;
static struct fake device;
static uint8_t original[10]={0,0,0,0,0,180,0x19,0,0,8};
static bool active_fault(struct fake *d,enum fault f) {
    return d->commands==d->fault_command && d->fault==f;
}
static uint8_t status(struct fake *d) {
    if(d->busy || active_fault(d,TIMEOUT_PACKET) ||
       (active_fault(d,TIMEOUT_DATA) && d->phase!=PACKET) ||
       (active_fault(d,TIMEOUT_FINISH) && d->phase==FINISHED))return 0x80;
    if(d->phase==PACKET || d->phase==GET_DATA || d->phase==SET_DATA)return 0x58;
    if(d->phase==ERROR_END)return 0x51;
    if(d->phase==FINISHED && active_fault(d,EXTRA))return 0x58;
    return 0x50;
}
static uint8_t reason(struct fake *d) {
    if(d->phase==PACKET)return active_fault(d,PACKET_REASON)?2:1;
    if(d->phase==GET_DATA)return active_fault(d,DATA_REASON)?0:2;
    if(d->phase==SET_DATA)return active_fault(d,DATA_REASON)?2:0;
    if(d->phase==FINISHED && active_fault(d,EXTRA))return 2;
    return 3;
}
static uint8_t read8(void *ctx,enum kui_g1_probe_reg r) {
    struct fake *d=ctx;
    switch(r) {
    case KUI_G1_ALTSTATUS:return status(d);
    case KUI_G1_STATUS:return status(d);
    case KUI_G1_DEVICE:return d->slave?0xb0:0xa0;
    case KUI_G1_REASON:return reason(d);
    case KUI_G1_ERROR:return 0x50;
    case KUI_G1_BYTECOUNT_LO:
        return active_fault(d,SHORT)?8:active_fault(d,LONG)?12:active_fault(d,ODD)?9:10;
    case KUI_G1_BYTECOUNT_HI:return 0;
    default:assert(!"unexpected taskfile read");return 0;
    }
}
static void write8(void *ctx,enum kui_g1_probe_reg r,uint8_t value) {
    struct fake *d=ctx;
    switch(r) {
    case KUI_G1_FEATURES:assert(value==0);d->features=value;break;
    case KUI_G1_BYTECOUNT_LO:d->bytecount[0]=value;break;
    case KUI_G1_BYTECOUNT_HI:d->bytecount[1]=value;break;
    case KUI_G1_COMMAND:
        assert(value==0xa0 && !d->features && d->bytecount[0]==10 && !d->bytecount[1]);
        assert(d->phase==IDLE || d->phase==FINISHED || d->phase==ERROR_END);
        ++d->commands;d->cdb_words=d->data_words=0;
        memset(d->cdb,0xa5,sizeof(d->cdb));memset(d->payload,0xa5,sizeof(d->payload));
        d->phase=d->commands==d->error_command?ERROR_END:PACKET;
        break;
    default:assert(!"host changed a forbidden taskfile register");
    }
}
static void write16(void *ctx,uint16_t value) {
    struct fake *d=ctx;++d->writes;
    if(d->phase==PACKET) {
        assert(d->cdb_words<6);unsigned i=d->cdb_words++*2;
        d->cdb[i]=(uint8_t)value;d->cdb[i+1]=(uint8_t)(value>>8);
        if(d->cdb_words==6) {
            assert(d->cdb[0]==0x11 || d->cdb[0]==0x12);
            for(unsigned n=1;n<12;++n)assert(d->cdb[n]==(n==4?10:0));
            if(d->cdb[0]==0x11)++d->queries;else ++d->sets;
            d->phase=active_fault(d,EARLY_COMPLETE)?FINISHED:
                d->cdb[0]==0x11?GET_DATA:SET_DATA;
        }
    } else {
        assert(d->phase==SET_DATA && d->data_words<5);
        unsigned i=d->data_words++*2;d->payload[i]=(uint8_t)value;d->payload[i+1]=(uint8_t)(value>>8);
        if(d->data_words==5) {
            if(d->commands!=d->freeze_restore_at) {
                memcpy(d->mode,d->payload,10);
                if(!d->all_speeds)d->mode[2]=0;
            }
            d->phase=FINISHED;
        }
    }
}
static uint16_t read16(void *ctx) {
    struct fake *d=ctx;assert(d->phase==GET_DATA && d->data_words<5);++d->reads;
    unsigned i=d->data_words++*2;
    uint16_t value=(uint16_t)d->mode[i]|(uint16_t)d->mode[i+1]<<8;
    if(d->data_words==5)d->phase=FINISHED;
    return value;
}
static bool safe(void *ctx) {
    struct fake *d=ctx;return !d->unsafe && (!d->safe_lost_at || d->commands<d->safe_lost_at);
}
static uint64_t clock_us(void *ctx) {return ((struct fake *)ctx)->now;}
static void pause_fake(void *ctx) {
    struct fake *d=ctx;++d->pauses;if(!d->clock_stopped)d->now+=1000;
}
static bool cancelled(void *ctx) {
    struct fake *d=ctx;return d->cancel_now || (d->cancel_at && d->commands>=d->cancel_at);
}
static void log_ignore(const char *format,...) {(void)format;}
static void progress(void *ctx,const struct kui_g1_mode_probe_status *o) {
    assert(ctx==&progress_calls && o->total==8);++progress_calls;
    if(cancel_on_progress && progress_calls==cancel_on_progress)device.cancel_now=true;
}
static void setup(void) {
    memset(&device,0,sizeof(device));memcpy(device.mode,original,10);progress_calls=0;cancel_on_progress=0;
}
static bool run(struct kui_g1_mode_probe_status *o) {
    struct kui_g1_mode_probe_bus bus={&device,read8,write8,read16,write16,safe,
        clock_us,pause_fake,cancelled,log_ignore};
    ++cases;return kui_g1_mode_probe_run(&bus,progress,&progress_calls,o);
}
static void complete_runs(void) {
    for(unsigned support=0;support<2;++support) {
        setup();device.all_speeds=support!=0;struct kui_g1_mode_probe_status o;
        assert(run(&o) && o.complete && o.restored && !o.failed && !o.restart_required);
        assert(o.snapshot_valid && o.done==8 && device.commands==33 && device.sets==16 && device.queries==17);
        assert(device.reads==17*5 && !memcmp(device.mode,original,10));
        for(unsigned code=0;code<8;++code) {
            assert(o.tried[code] && o.observed_valid[code] && o.requested[code][2]==code);
            for(unsigned n=0;n<10;++n)if(n!=2)assert(o.requested[code][n]==original[n]);
            assert(o.verified[code]==(support || !code));
            assert(o.observed[code][2]==(support?code:0));
            assert(o.difference[code]==(support || !code?0:4));
            assert(o.attempt[code]==(support || !code?KUI_G1_PROBE_OK:KUI_G1_PROBE_MISMATCH));
        }
    }
}
static void protocol_faults(void) {
    const enum fault faults[]={PACKET_REASON,DATA_REASON,SHORT,LONG,ODD,EXTRA,
        TIMEOUT_PACKET,TIMEOUT_DATA,TIMEOUT_FINISH};
    for(unsigned f=0;f<sizeof(faults)/sizeof(faults[0]);++f) {
        for(unsigned command=1;command<=3;++command) {
            setup();device.fault=faults[f];device.fault_command=command;
            struct kui_g1_mode_probe_status o;assert(!run(&o));
            assert(o.failed && !o.complete && !o.restored && o.restart_required);
            assert(device.commands==command); /* No guessed drain/reset/restore. */
            if(command>1)assert(o.tried[0]);
            if(faults[f]==TIMEOUT_PACKET || faults[f]==TIMEOUT_DATA || faults[f]==TIMEOUT_FINISH)
                assert(device.pauses==5000);
        }
    }
    setup();device.fault=TIMEOUT_PACKET;device.fault_command=1;device.clock_stopped=true;
    struct kui_g1_mode_probe_status o;assert(!run(&o) && o.restart_required);
    assert(device.commands==1 && device.pauses==1000000); /* Clock failure still terminates. */
}
static void clean_errors_and_stop(void) {
    struct kui_g1_mode_probe_status o;
    setup();device.error_command=1;assert(!run(&o));
    assert(o.failed && o.restored && !o.restart_required && !o.snapshot_valid && device.commands==1);
    setup();device.error_command=6;assert(run(&o));
    assert(o.complete && !o.verified[1] && o.attempt[1]==KUI_G1_PROBE_DEVICE_ERROR && o.observed_valid[1]);
    assert(o.set_result[1]==KUI_G1_PROBE_DEVICE_ERROR && o.set_status[1]==0x51 && o.set_reason[1]==3 && o.set_error[1]==0x50);
    assert(o.query_result[1]==KUI_G1_PROBE_OK);
    assert(!memcmp(device.mode,original,10));
    setup();device.error_command=7;assert(run(&o));
    assert(!o.verified[1] && !o.observed_valid[1] && o.restored);
    setup();device.fault=EARLY_COMPLETE;device.fault_command=1;assert(!run(&o));
    assert(o.result==KUI_G1_PROBE_LENGTH && o.restored && !o.restart_required && device.commands==1);
    setup();device.cancel_now=true;assert(!run(&o));
    assert(o.stopped && o.restored && !o.failed && !o.restart_required && !device.commands);
    setup();device.cancel_at=1;assert(!run(&o));
    assert(o.stopped && o.snapshot_valid && o.restored && device.commands==1 && !device.sets);
    setup();device.all_speeds=true;device.cancel_at=6;assert(!run(&o));
    assert(o.stopped && o.restored && !o.failed && device.commands==9 && o.done==2);
    assert(!memcmp(device.mode,original,10));
    setup();cancel_on_progress=2;assert(!run(&o));
    assert(o.stopped && !o.tried[0] && o.restored && !o.failed && device.commands==1 && !device.sets);
    setup();device.cancel_at=33;assert(!run(&o));
    assert(o.stopped && o.restored && !o.failed && device.commands==33 && o.done==8);
    setup();device.all_speeds=true;device.freeze_restore_at=8;assert(!run(&o));
    assert(o.failed && !o.restored && o.restart_required && device.commands==9 && device.mode[2]==1);
    assert(o.observed[1][2]==1 && o.requested[1][2]==1 && o.verified[1]);
}
static void admission_and_ownership(void) {
    struct kui_g1_mode_probe_status o;
    setup();device.busy=true;assert(!run(&o));assert(!device.commands && o.restart_required);
    setup();device.slave=true;assert(!run(&o));assert(!device.commands && o.restart_required);
    setup();device.unsafe=true;assert(!run(&o));assert(!device.commands && o.restart_required);
    for(unsigned c=1;c<=4;++c) {
        setup();device.safe_lost_at=c;assert(!run(&o));
        assert(device.commands==c && o.restart_required && !o.restored);
    }
    for(unsigned b=0;b<10;++b)if(b==0 || b==1 || b==2 || b==3 || b==6 || b==7 || b==8) {
        setup();device.mode[b]=b==2?8:b==6?0x80:1;assert(!run(&o));
        assert(o.failed && o.restored && !o.restart_required && device.commands==1 && !device.sets);
    }
    ++cases;assert(!kui_g1_mode_probe_run(NULL,NULL,NULL,&o) && o.failed && o.result==KUI_G1_PROBE_INVALID);
    ++cases;assert(!kui_g1_mode_probe_run(NULL,NULL,NULL,NULL));
    assert(!strcmp(kui_g1_probe_result_name(KUI_G1_PROBE_LENGTH),"unexpected transfer length"));
}
int main(void) {
    complete_runs();protocol_faults();clean_errors_and_stop();admission_and_ownership();
    printf("PASS direct G1 mode protocol: %u scenarios; exact packets, phases, lengths, restoration and terminal ownership\n",cases);
    return 0;
}
