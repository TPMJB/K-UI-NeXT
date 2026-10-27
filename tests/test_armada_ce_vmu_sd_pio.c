/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_sd_pio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SMR 0xffe00000u
#define BRR 0xffe00004u
#define SCR 0xffe00008u
#define TDR 0xffe0000cu
#define SSR 0xffe00010u
#define RDR 0xffe00014u
#define PTR 0xffe00018u
#define STB 0xffc00004u
#define PCTR 0xff80002cu
#define PDTR 0xff800030u
#define MD1 0x40000000u
#define MASKED (MD1|0xf0u)
#define STAGE(n) KUI_ARMADA_CE_VMU_SD_PIO_STAGE_##n
static uint32_t detail(unsigned stage,unsigned scr,unsigned ssr) {
    return stage<<16|scr<<8|ssr;
}
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
static struct {
    uint32_t sr,pctr,ignored;
    uint16_t pdtr;
    uint8_t smr,brr,scr,ssr,rdr,ptr,stb,tx,rx,missing;
    unsigned reads,writes,polls,tdr_writes,delays,masks,restores,remaining;
    unsigned delay_polls;
    unsigned standby_reads,sleep_on_standby_read;
    bool active,timeout,error;
} h;
static uint8_t reverse_reference(uint8_t b) {
    uint8_t r=0;
    for(unsigned i=0;i<8u;++i) {r=(uint8_t)((r<<1)|(b&1u));b>>=1;}
    return r;
}
static void reset(void) {
    memset(&h,0,sizeof(h));h.sr=MASKED;h.smr=0x23;h.brr=9;h.scr=3;
    h.ssr=0x84;h.ptr=7;h.stb=0xa5;h.pctr=0x1234c567;h.pdtr=0xab43;h.delay_polls=3;
}
static void step(void) {
    if(h.active && !h.timeout && h.remaining && !--h.remaining) {
        h.active=false;h.rdr=reverse_reference(h.rx);h.ssr|=0xc4;
        if(h.error) h.ssr|=0x20;
    }
}
uint32_t kui_armada_ce_vmu_sd_pio_test_read(uint32_t a,unsigned w) {
    ++h.reads;
    if(a>=SMR && a<=PTR) CHECK(!(h.stb&1u));
    if(a==PCTR) {CHECK(w==4);return h.pctr;}
    if(a==PDTR) {CHECK(w==2);return h.pdtr;}
    CHECK(w==1);
    switch(a) {
    case SMR:return h.smr;case BRR:return h.brr;case SCR:return h.scr;
    case SSR:++h.polls;step();return h.ssr&~h.missing;
    case RDR:return h.rdr;case PTR:return h.ptr;
    case STB:
        if(++h.standby_reads==h.sleep_on_standby_read) h.stb|=1u;
        return h.stb;
    default:CHECK(false);return 0;
    }
}
void kui_armada_ce_vmu_sd_pio_test_write(uint32_t a,uint32_t v,unsigned w) {
    ++h.writes;
    if(a>=SMR && a<=PTR) CHECK(!(h.stb&1u));
    /* Any DMA/TMU/cache-controller access is rejected by this whitelist. */
    if(a==PCTR) {CHECK(w==4);if(a!=h.ignored)h.pctr=v;return;}
    if(a==PDTR) {CHECK(w==2);CHECK((h.sr&0xf0u)==0xf0u);if(a!=h.ignored)h.pdtr=(uint16_t)v;return;}
    CHECK(w==1);
    if(a==h.ignored) return;
    switch(a) {
    case SMR:h.smr=(uint8_t)v;break;case BRR:h.brr=(uint8_t)v;break;
    case SCR:h.scr=(uint8_t)v;if(!v)h.active=false;break;
    case PTR:h.ptr=(uint8_t)v;break;case STB:h.stb=(uint8_t)v;break;
    case SSR:h.ssr&=(uint8_t)v;break;
    case TDR:
        CHECK((h.scr&0xf0u)==0x30u && !h.active && h.smr==0x80u);
        h.tx=(uint8_t)v;h.active=true;h.remaining=h.delay_polls;h.ssr&=~0xc4u;++h.tdr_writes;break;
    default:CHECK(false);
    }
}
uint32_t kui_armada_ce_vmu_sd_pio_test_sr(void) {return h.sr;}
void kui_armada_ce_vmu_sd_pio_test_set_sr(uint32_t sr) {
    if((sr&0xf0u)==0xf0u) ++h.masks;else ++h.restores;
    h.sr=sr;
}
void kui_armada_ce_vmu_sd_pio_test_delay(uint32_t n) {CHECK(n==1024u);++h.delays;}
static void initialized(struct kui_armada_ce_vmu_sd_pio *s,struct kui_loader_sd_bus *b) {
    memset(s,0,sizeof(*s));CHECK(!kui_armada_ce_vmu_sd_pio_init(s));
    CHECK(!kui_armada_ce_vmu_sd_pio_bus(s,b));
    CHECK(b->ctx==s && b->select && b->transfer && b->ticks);
    CHECK(!b->begin && !b->end && !b->transfer_block);
}
static void restored(void) {
    CHECK(h.smr==0x23 && h.brr==9 && h.scr==3 && h.ptr==7 && h.stb==0xa5);
    CHECK((h.pctr&0xc000u)==0xc000u && !(h.pdtr&0x80u));
}
static void bytes_and_restore(void) {
    struct kui_armada_ce_vmu_sd_pio s;struct kui_loader_sd_bus b;
    reset();initialized(&s,&b);CHECK(!kui_armada_ce_vmu_sd_pio_acquire(&s));
    unsigned writes=h.writes;
    CHECK(kui_armada_ce_vmu_sd_pio_init(&s)==KUI_ARMADA_CE_VMU_SD_PIO_STATE);
    CHECK(h.writes==writes && kui_armada_ce_vmu_sd_pio_healthy(&s));
    h.sr=MD1|1u;
    for(unsigned i=0;i<256u;++i) {
        h.rx=(uint8_t)(255u-i);bool slow=(i&1u)!=0;
        b.select(b.ctx,true);CHECK(!(h.pdtr&0x80u) && h.sr==(MD1|1u));
        CHECK(b.transfer(b.ctx,(uint8_t)i,slow)==(uint8_t)(255u-i));
        CHECK(h.tx==reverse_reference((uint8_t)i));
        CHECK(h.sr==(MD1|1u) && !h.active && !(h.scr&0xc0u));
        CHECK(kui_armada_ce_vmu_sd_pio_healthy(&s));
        b.select(b.ctx,false);CHECK((h.pdtr&0x80u) && h.sr==(MD1|1u));
    }
    CHECK(b.ticks(b.ctx)==128u*(8u+256u));CHECK(h.tdr_writes==256u);
    /* Other GPIO pins change during the lease; release preserves them. */
    h.pctr^=0x80000101u;h.pdtr^=0x4202u;
    uint32_t other_mode=h.pctr&~0xc000u;uint16_t other_data=h.pdtr&~0x80u;
    h.sr=MASKED;CHECK(!kui_armada_ce_vmu_sd_pio_release(&s));restored();
    CHECK((h.pctr&~0xc000u)==other_mode && (h.pdtr&~0x80u)==other_data);
    CHECK(!s.acquired && !kui_armada_ce_vmu_sd_pio_healthy(&s));
    CHECK(!s.fault && !s.detail);
    CHECK(!kui_armada_ce_vmu_sd_pio_acquire(&s));CHECK(!kui_armada_ce_vmu_sd_pio_release(&s));
}
static void refused(void) {
    struct kui_armada_ce_vmu_sd_pio s;struct kui_loader_sd_bus b;
    const uint8_t bad_scr[]={0x80,0x40,0x20,0x10,0x04};
    for(unsigned i=0;i<sizeof(bad_scr);++i) {
        reset();initialized(&s,&b);h.scr=bad_scr[i];h.stb&=~1u;
        CHECK(kui_armada_ce_vmu_sd_pio_acquire(&s)==KUI_ARMADA_CE_VMU_SD_PIO_IDLE);
        CHECK(!h.writes && !s.acquired && s.fault);
        CHECK(s.detail==detail(STAGE(IDLE),bad_scr[i],0x84u));
    }
    for(unsigned bit=2;bit<8u;++bit) {
        if(bit==7u) continue;
        reset();initialized(&s,&b);h.ssr=(uint8_t)(0x84u|(1u<<bit));h.stb&=~1u;
        if(bit==2u)h.ssr&=~0x84u;
        CHECK(kui_armada_ce_vmu_sd_pio_acquire(&s)==KUI_ARMADA_CE_VMU_SD_PIO_IDLE);
        CHECK(!h.writes && !s.acquired);
    }
    /* Waking an idle-owned module precedes all SCI observations. A refusal
     * after that wake restores only standby, preserving every SCI register. */
    reset();initialized(&s,&b);h.scr=0x20u;
    CHECK(kui_armada_ce_vmu_sd_pio_acquire(&s)==KUI_ARMADA_CE_VMU_SD_PIO_IDLE);
    CHECK(s.acquired && !s.captured && !(h.stb&1u));
    uint32_t first=s.detail;CHECK(first==detail(STAGE(IDLE),0x20u,0x84u));
    CHECK(kui_armada_ce_vmu_sd_pio_release(&s)==KUI_ARMADA_CE_VMU_SD_PIO_IDLE);
    CHECK(h.stb==0xa5u && h.scr==0x20u && h.smr==0x23u && !s.acquired);
    CHECK(s.detail==first); /* No cleanup sample from the now sleeping SCI. */
    const uint32_t bad_sr[]={0xf0u,MD1|0x10u,MD1|0x100000f0u,MD1};
    for(unsigned i=0;i<sizeof(bad_sr)/sizeof(*bad_sr);++i) {
        reset();initialized(&s,&b);h.sr=bad_sr[i];
        CHECK(kui_armada_ce_vmu_sd_pio_acquire(&s)==KUI_ARMADA_CE_VMU_SD_PIO_CONTEXT);
        CHECK(!h.reads && !h.writes);
        CHECK(s.detail==detail(STAGE(ACQUIRE_CONTEXT),0,0));
    }
    reset();initialized(&s,&b);struct kui_armada_ce_vmu_sd_pio copy=s;
    CHECK(kui_armada_ce_vmu_sd_pio_acquire(&copy)==KUI_ARMADA_CE_VMU_SD_PIO_STATE);
    CHECK(!kui_armada_ce_vmu_sd_pio_healthy(&copy) && !h.writes && !h.reads && !copy.detail);
    CHECK(kui_armada_ce_vmu_sd_pio_bus(&s,(struct kui_loader_sd_bus *)(void *)&s)==KUI_ARMADA_CE_VMU_SD_PIO_ARGUMENT);
}
static void ignored_writes(void) {
    const uint32_t regs[]={SMR,BRR,SCR,PTR,STB,PCTR,PDTR};
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i) {
        struct kui_armada_ce_vmu_sd_pio s;struct kui_loader_sd_bus b;
        reset();initialized(&s,&b);h.ignored=regs[i];
        /* SCR initially3 must become0; every ignored claim mutation differs. */
        CHECK(kui_armada_ce_vmu_sd_pio_acquire(&s)==KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
        CHECK(s.acquired && s.fault && !kui_armada_ce_vmu_sd_pio_healthy(&s));
        uint32_t first=s.detail;
        CHECK(first>>16==(regs[i]==STB?STAGE(WAKE):STAGE(SETUP)));
        if(regs[i]==STB) CHECK(first==detail(STAGE(WAKE),0,0));
        h.ignored=0;CHECK(kui_armada_ce_vmu_sd_pio_release(&s)==KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);restored();
        CHECK(s.detail==first);
        unsigned writes=h.writes;
        CHECK(kui_armada_ce_vmu_sd_pio_acquire(&s)==KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
        CHECK(h.writes==writes && kui_armada_ce_vmu_sd_pio_init(&s)==KUI_ARMADA_CE_VMU_SD_PIO_STATE);
        uint32_t saved_fault=s.fault;CHECK(!kui_armada_ce_vmu_sd_pio_bus(&s,&b));CHECK(s.fault==saved_fault);
        reset();initialized(&s,&b);CHECK(!kui_armada_ce_vmu_sd_pio_acquire(&s));
        h.sr=MD1;h.rx=0x57;CHECK(b.transfer(b.ctx,0xaa,false)==0x57);
        h.sr=MASKED;h.ignored=regs[i];
        CHECK(kui_armada_ce_vmu_sd_pio_release(&s)==KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
        CHECK(!s.acquired && s.fault && !kui_armada_ce_vmu_sd_pio_healthy(&s));
        CHECK(s.detail>>16==(regs[i]==STB?STAGE(STANDBY):STAGE(RESTORE)));
        if(regs[i]==STB) CHECK(s.detail==detail(STAGE(STANDBY),0,0));
    }
}
static void byte_faults(void) {
    const uint32_t expected[]={
        (STAGE(TDRE)<<16)|0x3004u,(STAGE(RDRF)<<16)|0x3000u,
        (STAGE(RDRF)<<16)|0x30e4u,(STAGE(SPEED)<<16)|0x0084u,
        (STAGE(SCR)<<16)|0x0084u,(STAGE(RDRF)<<16)|0x3004u,
        (STAGE(CLEAR)<<16)|0x30c4u
    };
    for(unsigned kind=0;kind<7u;++kind) {
        struct kui_armada_ce_vmu_sd_pio s;struct kui_loader_sd_bus b;
        reset();initialized(&s,&b);CHECK(!kui_armada_ce_vmu_sd_pio_acquire(&s));h.sr=MD1;
        if(kind==0u)h.missing=0x80u;
        if(kind==1u)h.timeout=true;
        if(kind==2u)h.error=true;
        if(kind==3u)h.ignored=BRR;
        if(kind==4u)h.ignored=SCR;
        if(kind==5u)h.ignored=TDR;
        if(kind==6u)h.ignored=SSR;
        unsigned polls=h.polls;
        CHECK(b.transfer(b.ctx,0x12,false)==0xffu);
        CHECK(s.fault && !kui_armada_ce_vmu_sd_pio_healthy(&s));
        CHECK(s.detail==expected[kind]);uint32_t first=s.detail;
        CHECK(h.polls-polls<=10010u);
        unsigned writes=h.writes;uint32_t work=b.ticks(b.ctx);
        CHECK(b.transfer(b.ctx,0x33,false)==0xffu);CHECK(h.writes==writes);
        CHECK(s.detail==first);
        CHECK(b.ticks(b.ctx)-work==8u); /* faulted protocol deadlines still advance */
        h.ignored=0;h.missing=0;h.sr=MASKED;
        CHECK(kui_armada_ce_vmu_sd_pio_release(&s)!=0);restored();CHECK(!s.acquired);
        CHECK(s.detail==first);
    }
    struct kui_armada_ce_vmu_sd_pio s;struct kui_loader_sd_bus b;
    reset();initialized(&s,&b);CHECK(!kui_armada_ce_vmu_sd_pio_acquire(&s));
    h.sr=MD1|0x20u;unsigned writes=h.writes;
    CHECK(b.transfer(b.ctx,0x55,false)==0xffu && s.fault==KUI_ARMADA_CE_VMU_SD_PIO_CONTEXT);
    CHECK(s.detail==detail(STAGE(TRANSFER_CONTEXT),0,0));uint32_t first=s.detail;
    CHECK(h.writes==writes);CHECK(kui_armada_ce_vmu_sd_pio_release(&s)==KUI_ARMADA_CE_VMU_SD_PIO_CONTEXT);
    CHECK(s.acquired);h.sr=MASKED;CHECK(kui_armada_ce_vmu_sd_pio_release(&s)!=0);restored();
    CHECK(s.detail==first);
    reset();initialized(&s,&b);CHECK(!kui_armada_ce_vmu_sd_pio_acquire(&s));h.missing=4u;
    unsigned polls=h.polls;CHECK(kui_armada_ce_vmu_sd_pio_release(&s)==KUI_ARMADA_CE_VMU_SD_PIO_TIMEOUT);
    CHECK(!s.acquired && h.polls-polls<=10002u);restored();
    CHECK(s.detail==detail(STAGE(RELEASE_TEND),0,0x80u));
    reset();initialized(&s,&b);CHECK(!kui_armada_ce_vmu_sd_pio_acquire(&s));h.sr=MD1;h.ignored=PDTR;
    b.select(b.ctx,true);CHECK(s.fault==KUI_ARMADA_CE_VMU_SD_PIO_RESTORE && h.sr==MD1);
    CHECK(s.detail==detail(STAGE(SELECT),0,0x84u));first=s.detail;
    h.ignored=0;h.sr=MASKED;CHECK(kui_armada_ce_vmu_sd_pio_release(&s)!=0);restored();
    CHECK(s.detail==first);
    reset();initialized(&s,&b);unsigned before=h.writes;
    CHECK(b.transfer(b.ctx,0x55,false)==0xffu && s.fault==KUI_ARMADA_CE_VMU_SD_PIO_STATE);
    CHECK(h.writes==before && !s.acquired);
    CHECK(s.detail==detail(STAGE(OWNER),0,0));
    s.work=UINT32_MAX-3u;CHECK(b.transfer(b.ctx,0x55,false)==0xffu);
    CHECK(b.ticks(b.ctx)==4u && h.writes==before);
    /* Isolate the extra per-byte final TEND guard from speed selection and
     * prove the pre-cleanup SCR/status survive restoration to standby. */
    reset();initialized(&s,&b);CHECK(!kui_armada_ce_vmu_sd_pio_acquire(&s));
    h.sr=MD1;h.missing=4u;
    CHECK(b.transfer(b.ctx,0x55,true)==0xffu && s.fault==KUI_ARMADA_CE_VMU_SD_PIO_TIMEOUT);
    CHECK(s.detail==detail(STAGE(TEND),0x30u,0x80u));first=s.detail;
    h.missing=0;h.sr=MASKED;CHECK(kui_armada_ce_vmu_sd_pio_release(&s)!=0);restored();
    CHECK(s.detail==first);
}
static void module_off_cleanup(void) {
    for(unsigned during_setup=0;during_setup<2u;during_setup++) {
        struct kui_armada_ce_vmu_sd_pio s;struct kui_loader_sd_bus b;
        reset();initialized(&s,&b);
        if(during_setup) {
            /* The fourth STB observation follows capture and setup writes. */
            h.sleep_on_standby_read=4u;
            CHECK(kui_armada_ce_vmu_sd_pio_acquire(&s)==KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
            CHECK(s.detail==detail(STAGE(SETUP),0,0));
        } else {
            CHECK(!kui_armada_ce_vmu_sd_pio_acquire(&s));h.stb|=1u;
        }
        CHECK(s.acquired && s.captured && (h.stb&1u));
        unsigned reads=h.reads,writes=h.writes;
        uint32_t first=during_setup?s.detail:detail(STAGE(STANDBY),0,0);
        CHECK(kui_armada_ce_vmu_sd_pio_release(&s)==KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
        CHECK(s.acquired && !kui_armada_ce_vmu_sd_pio_healthy(&s));
        CHECK(h.reads==reads+1u && h.writes==writes && s.detail==first);
        CHECK(kui_armada_ce_vmu_sd_pio_release(&s)==KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
        CHECK(s.acquired && h.writes==writes && s.detail==first);
        /* Only an explicit verified wake makes SCI cleanup accessible again.
         * Even then restoration never makes the faulted instance healthy. */
        h.stb&=~1u;
        CHECK(kui_armada_ce_vmu_sd_pio_release(&s)==KUI_ARMADA_CE_VMU_SD_PIO_RESTORE);
        CHECK(!s.acquired && !kui_armada_ce_vmu_sd_pio_healthy(&s) && s.detail==first);restored();
    }
}
int main(void) {
    /* Stateful tests always exercise this adapter's actual C callbacks. */
    bytes_and_restore();refused();ignored_writes();byte_faults();module_off_cleanup();
    printf("armada CE VMU SD PIO: %u checks passed\n",checks);return 0;
}
