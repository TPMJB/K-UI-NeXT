/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_handoff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint32_t random_state=0x29536b4du;
static uint32_t random32(void) {
    random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;
    return random_state;
}
static struct kui_cdda_handoff_desc descriptor(void) {
    return (struct kui_cdda_handoff_desc){
        .magic0=KUI_CDDA_HANDOFF_MAGIC0,.magic1=KUI_CDDA_HANDOFF_MAGIC1,
        .version=1,.bytes=160,.service_revision=1,.flags=3,.storage_transport=1,
        .storage_rights=3,.resource_rights=31,.tmu_unit=1,.sound_channels=3,
        .clock_hz=KUI_CDDA_TMU_HZ,.service_gap_ticks=2316184u,
        .engine_code_first=0x8c010000u,.engine_code_end=0x8c018000u,
        .engine_state_first=0x8c018000u,.engine_state_end=0x8c100000u,
        .engine_stack_first=0x8c200000u,.engine_stack_end=0x8c210000u,
        .service_stack_first=0x8c220000u,.service_stack_end=0x8c230000u,
        .client_code_first=0x8c300000u,.client_code_end=0x8c310000u,
        .client_stack_first=0x8c400000u,.client_stack_end=0x8c410000u,
        .sound_first=0x100000u,.sound_end=0x110000u,
        .client_entry=0x8c300002u,.service_entry=0x8c010002u
    };
}
static uint32_t *first(struct kui_cdda_handoff_desc *d,unsigned which) {
    switch(which) {
    case 0:return &d->engine_code_first;case 1:return &d->engine_state_first;
    case 2:return &d->engine_stack_first;case 3:return &d->service_stack_first;
    case 4:return &d->client_code_first;default:return &d->client_stack_first;
    }
}
static uint32_t *end(struct kui_cdda_handoff_desc *d,unsigned which) {
    switch(which) {
    case 0:return &d->engine_code_end;case 1:return &d->engine_state_end;
    case 2:return &d->engine_stack_end;case 3:return &d->service_stack_end;
    case 4:return &d->client_code_end;default:return &d->client_stack_end;
    }
}
static void reject(struct kui_cdda_handoff_desc d,enum kui_cdda_handoff_result result) {
    struct kui_cdda_handoff_desc saved=d;
    CHECK(kui_cdda_handoff_validate(&d)==result);CHECK(!memcmp(&d,&saved,sizeof(d)));
}
static void descriptor_boundaries(void) {
    struct kui_cdda_handoff_desc good=descriptor(),d;
    CHECK(sizeof(good)==160u);CHECK(kui_cdda_handoff_validate(&good)==KUI_CDDA_HANDOFF_OK);
    CHECK(kui_cdda_handoff_validate(NULL)==KUI_CDDA_HANDOFF_INVALID);
    d=good;d.magic0^=1u;reject(d,KUI_CDDA_HANDOFF_INVALID);
    d=good;d.magic1^=0x01000000u;reject(d,KUI_CDDA_HANDOFF_INVALID);
    d=good;d.version=0;reject(d,KUI_CDDA_HANDOFF_REVISION);
    d=good;d.version=2;reject(d,KUI_CDDA_HANDOFF_REVISION);
    d=good;d.bytes=128;reject(d,KUI_CDDA_HANDOFF_REVISION);
    d=good;d.bytes=164;reject(d,KUI_CDDA_HANDOFF_REVISION);
    d=good;d.service_revision=2;reject(d,KUI_CDDA_HANDOFF_REVISION);
    d=good;d.clock_hz++;reject(d,KUI_CDDA_HANDOFF_REVISION);
    for(unsigned bit=0;bit<32u;bit++) {
        d=good;d.flags^=1u<<bit;reject(d,KUI_CDDA_HANDOFF_RIGHTS);
        d=good;d.storage_rights^=1u<<bit;reject(d,KUI_CDDA_HANDOFF_RIGHTS);
        d=good;d.resource_rights^=1u<<bit;reject(d,KUI_CDDA_HANDOFF_RIGHTS);
    }
    for(unsigned i=0;i<11u;i++) {d=good;d.reserved[i]=1;reject(d,KUI_CDDA_HANDOFF_INVALID);}
    d=good;d.storage_transport=0;reject(d,KUI_CDDA_HANDOFF_RIGHTS);
    d=good;d.storage_transport=2;reject(d,KUI_CDDA_HANDOFF_RIGHTS);
    d=good;d.tmu_unit=0;reject(d,KUI_CDDA_HANDOFF_RIGHTS);
    d=good;d.tmu_unit=2;reject(d,KUI_CDDA_HANDOFF_RIGHTS);
    d=good;d.sound_channels=1;reject(d,KUI_CDDA_HANDOFF_RIGHTS);
    d=good;d.sound_channels=7;reject(d,KUI_CDDA_HANDOFF_RIGHTS);
    d=good;d.sound_first--;reject(d,KUI_CDDA_HANDOFF_RANGE);
    d=good;d.sound_end++;reject(d,KUI_CDDA_HANDOFF_RANGE);
    d=good;d.sound_first=UINT32_MAX;d.sound_end=0;reject(d,KUI_CDDA_HANDOFF_RANGE);
    d=good;d.service_gap_ticks=0;reject(d,KUI_CDDA_HANDOFF_RANGE);
    d=good;d.service_gap_ticks=KUI_CDDA_HANDOFF_MAX_GAP_TICKS+1u;reject(d,KUI_CDDA_HANDOFF_RANGE);
    d=good;d.service_gap_ticks=1;CHECK(kui_cdda_handoff_validate(&d)==KUI_CDDA_HANDOFF_OK);
    d=good;d.service_gap_ticks=KUI_CDDA_HANDOFF_MAX_GAP_TICKS;
    CHECK(kui_cdda_handoff_validate(&d)==KUI_CDDA_HANDOFF_OK);
    for(unsigned i=0;i<6u;i++) {
        d=good;*first(&d,i)=*end(&d,i);reject(d,KUI_CDDA_HANDOFF_RANGE);
        d=good;*first(&d,i)=*end(&d,i)+32u;reject(d,KUI_CDDA_HANDOFF_RANGE);
        d=good;*first(&d,i)|=1u;reject(d,KUI_CDDA_HANDOFF_RANGE);
        d=good;*end(&d,i)|=16u;reject(d,KUI_CDDA_HANDOFF_RANGE);
        d=good;*first(&d,i)=0xffffffe0u;*end(&d,i)=32u;reject(d,KUI_CDDA_HANDOFF_RANGE);
        d=good;*first(&d,i)=0x8c000000u;reject(d,KUI_CDDA_HANDOFF_RANGE);
        d=good;*end(&d,i)=0x8d000020u;reject(d,KUI_CDDA_HANDOFF_RANGE);
        for(unsigned alias=0;alias<2u;alias++) {
            d=good;uint32_t area=alias?0xac000000u:0x0c000000u;
            *first(&d,i)=(*first(&d,i)&0xffffffu)|area;
            *end(&d,i)=(*end(&d,i)&0xffffffu)|area;
            reject(d,KUI_CDDA_HANDOFF_RANGE);
        }
        for(unsigned j=i+1u;j<6u;j++) {
            d=good;*first(&d,j)=*first(&d,i);*end(&d,j)=*end(&d,i);
            reject(d,KUI_CDDA_HANDOFF_OVERLAP);
        }
    }
    d=good;d.client_stack_first=0x8cffffe0u;d.client_stack_end=0x8d000000u;
    CHECK(kui_cdda_handoff_validate(&d)==KUI_CDDA_HANDOFF_OK);
    d=good;d.client_entry=d.client_code_first;d.service_entry=d.engine_code_end-2u;
    CHECK(kui_cdda_handoff_validate(&d)==KUI_CDDA_HANDOFF_OK);
    d=good;d.client_entry=d.client_code_end;reject(d,KUI_CDDA_HANDOFF_ENTRY);
    d=good;d.service_entry=d.engine_code_first-2u;reject(d,KUI_CDDA_HANDOFF_ENTRY);
    d=good;d.client_entry++;reject(d,KUI_CDDA_HANDOFF_ENTRY);
    d=good;d.service_entry++;reject(d,KUI_CDDA_HANDOFF_ENTRY);
    d=good;d.service_entry=d.engine_state_first;reject(d,KUI_CDDA_HANDOFF_ENTRY);
    d=good;d.client_entry=d.client_stack_first;reject(d,KUI_CDDA_HANDOFF_ENTRY);
    d=good;d.client_entry&=0x1fffffffu;reject(d,KUI_CDDA_HANDOFF_ENTRY);
}
static void encode(const struct kui_cdda_handoff_desc *d,uint8_t *wire) {
    unsigned at=0;
#define WORD(field) do {uint32_t n=d->field;for(unsigned b=0;b<4;b++) wire[at++]=(uint8_t)(n>>(8u*b));} while(0)
    WORD(magic0);WORD(magic1);WORD(version);WORD(bytes);WORD(service_revision);
    WORD(flags);WORD(storage_transport);WORD(storage_rights);WORD(resource_rights);WORD(tmu_unit);
    WORD(sound_channels);WORD(clock_hz);WORD(service_gap_ticks);
    WORD(engine_code_first);WORD(engine_code_end);WORD(engine_state_first);WORD(engine_state_end);
    WORD(engine_stack_first);WORD(engine_stack_end);WORD(service_stack_first);WORD(service_stack_end);
    WORD(client_code_first);WORD(client_code_end);WORD(client_stack_first);WORD(client_stack_end);
    WORD(sound_first);WORD(sound_end);WORD(client_entry);WORD(service_entry);
#undef WORD
    for(unsigned i=0;i<11u;i++) for(unsigned b=0;b<4u;b++) wire[at++]=(uint8_t)(d->reserved[i]>>(8u*b));
    CHECK(at==160u);
}
static void decoding(void) {
    uint8_t storage[162],*wire=storage+1u;
    struct kui_cdda_handoff_desc good=descriptor(),out,saved;
    memset(&out,0x5a,sizeof(out));saved=out;encode(&good,wire);
    CHECK(!memcmp(wire,"KCDDAH1",8));
    for(unsigned n=0;n<162u;n++) if(n!=160u) {
        CHECK(kui_cdda_handoff_decode(wire,n,&out)==KUI_CDDA_HANDOFF_INVALID);
        CHECK(!memcmp(&out,&saved,sizeof(out)));
    }
    CHECK(kui_cdda_handoff_decode(NULL,160,&out)==KUI_CDDA_HANDOFF_INVALID);
    CHECK(kui_cdda_handoff_decode(wire,160,NULL)==KUI_CDDA_HANDOFF_INVALID);
    CHECK(kui_cdda_handoff_decode(wire,160,&out)==KUI_CDDA_HANDOFF_OK);
    CHECK(!memcmp(&out,&good,sizeof(out)));
    for(unsigned i=0;i<160u;i++) {
        encode(&good,wire);wire[i]^=0x80u;out=saved;
        enum kui_cdda_handoff_result result=kui_cdda_handoff_decode(wire,160,&out);
        if(result!=KUI_CDDA_HANDOFF_OK) CHECK(!memcmp(&out,&saved,sizeof(out)));
        else CHECK(kui_cdda_handoff_validate(&out)==KUI_CDDA_HANDOFF_OK);
    }
}
/* An independent wide-integer interval oracle, including exact v1 rights. */
static bool descriptor_oracle(struct kui_cdda_handoff_desc d) {
    if(d.magic0!=KUI_CDDA_HANDOFF_MAGIC0 || d.magic1!=KUI_CDDA_HANDOFF_MAGIC1 ||
       d.version!=1 || d.bytes!=160 || d.service_revision!=1 || d.flags!=3 ||
       d.storage_transport!=1 || d.storage_rights!=3 || d.resource_rights!=31 ||
       d.tmu_unit!=1 || d.sound_channels!=3 || d.clock_hz!=KUI_CDDA_TMU_HZ ||
       !d.service_gap_ticks || d.service_gap_ticks>KUI_CDDA_HANDOFF_MAX_GAP_TICKS ||
       d.sound_first!=0x100000u || d.sound_end!=0x110000u) return false;
    for(unsigned i=0;i<11u;i++) if(d.reserved[i]) return false;
    for(unsigned i=0;i<6u;i++) {
        uint64_t a=*first(&d,i),b=*end(&d,i);
        if(a<0x8c010000ull || b>0x8d000000ull || b<=a || a%32u || b%32u) return false;
        for(unsigned j=i+1u;j<6u;j++) {
            uint64_t c=*first(&d,j),e=*end(&d,j);
            uint64_t left=a>c?a:c,right=b<e?b:e;
            if(left<right) return false;
        }
    }
    return !(d.client_entry%2u) && !(d.service_entry%2u) &&
        d.client_entry>=d.client_code_first && d.client_entry<d.client_code_end &&
        d.service_entry>=d.engine_code_first && d.service_entry<d.engine_code_end;
}
static void descriptor_properties(void) {
    for(unsigned n=0;n<50000u;n++) {
        struct kui_cdda_handoff_desc d=descriptor();
        for(unsigned i=0;i<6u;i++) {
            *first(&d,i)=0x8c010000u+i*0x200000u+(random32()%1024u)*32u;
            *end(&d,i)=*first(&d,i)+(1u+random32()%2048u)*32u;
        }
        d.client_entry=d.client_code_first+2u*(random32()%((d.client_code_end-d.client_code_first)/2u));
        d.service_entry=d.engine_code_first+2u*(random32()%((d.engine_code_end-d.engine_code_first)/2u));
        CHECK(descriptor_oracle(d));CHECK(kui_cdda_handoff_validate(&d)==KUI_CDDA_HANDOFF_OK);
        unsigned which=random32()%6u;
        switch(n%8u) {
        case 0:*first(&d,which)=random32();break;
        case 1:*end(&d,which)=random32();break;
        case 2:*first(&d,which)=*first(&d,(which+1u)%6u);*end(&d,which)=*end(&d,(which+1u)%6u);break;
        case 3:*end(&d,which)-=32u;break;
        case 4:*first(&d,which)|=0x20000000u;break;
        case 5:d.client_entry=d.client_code_end;break;
        case 6:d.service_entry=d.engine_code_end-2u;break;
        default:d.flags^=1u<<(random32()%32u);break;
        }
        CHECK((kui_cdda_handoff_validate(&d)==KUI_CDDA_HANDOFF_OK)==descriptor_oracle(d));
    }
}

static void guard_basics(void) {
    struct kui_cdda_service_guard g,saved;
    uint32_t epoch=99;
    struct kui_cdda_service_ticket ticket={71,81},out=ticket;
    CHECK(kui_cdda_service_init(NULL)==KUI_CDDA_SERVICE_INVALID);
    CHECK(kui_cdda_service_init(&g)==KUI_CDDA_SERVICE_OK);saved=g;
    CHECK(kui_cdda_service_enter(&g,0,0,&out)==KUI_CDDA_SERVICE_STATE);
    CHECK(!memcmp(&g,&saved,sizeof(g)) && !memcmp(&out,&ticket,sizeof(out)));
    CHECK(kui_cdda_service_start(&g,100,0,&epoch)==KUI_CDDA_SERVICE_INVALID);
    CHECK(kui_cdda_service_start(&g,100,KUI_CDDA_HANDOFF_MAX_GAP_TICKS+1u,&epoch)==KUI_CDDA_SERVICE_INVALID);
    CHECK(kui_cdda_service_start(&g,100,100,NULL)==KUI_CDDA_SERVICE_INVALID);
    CHECK(kui_cdda_service_start(&g,100,100,&g.generation)==KUI_CDDA_SERVICE_INVALID);
    CHECK(!memcmp(&g,&saved,sizeof(g)) && epoch==99);
    CHECK(kui_cdda_service_start(&g,100,100,&epoch)==KUI_CDDA_SERVICE_OK && epoch==1);
    saved=g;CHECK(kui_cdda_service_start(&g,100,100,&epoch)==KUI_CDDA_SERVICE_STATE);
    CHECK(!memcmp(&g,&saved,sizeof(g)));
    CHECK(kui_cdda_service_enter(&g,epoch-1u,UINT32_MAX,&out)==KUI_CDDA_SERVICE_STALE);
    CHECK(!memcmp(&g,&saved,sizeof(g)) && !memcmp(&out,&ticket,sizeof(out)));
    CHECK(kui_cdda_service_enter(&g,epoch,110,&g.pending)==KUI_CDDA_SERVICE_INVALID);
    CHECK(!memcmp(&g,&saved,sizeof(g)));
    CHECK(kui_cdda_service_enter(&g,epoch,110,&ticket)==KUI_CDDA_SERVICE_OK);
    CHECK(ticket.epoch==1 && ticket.call==1 && g.state==KUI_CDDA_SERVICE_BUSY && g.last_tick==100);
    saved=g;out=(struct kui_cdda_service_ticket){71,81};
    CHECK(kui_cdda_service_enter(&g,epoch,UINT32_MAX,&out)==KUI_CDDA_SERVICE_BUSY_RESULT);
    CHECK(kui_cdda_service_enter(&g,epoch+1u,UINT32_MAX,&out)==KUI_CDDA_SERVICE_STALE);
    CHECK(kui_cdda_service_start(&g,100,100,&epoch)==KUI_CDDA_SERVICE_BUSY_RESULT);
    CHECK(kui_cdda_service_stop(&g,epoch)==KUI_CDDA_SERVICE_BUSY_RESULT);
    CHECK(!memcmp(&g,&saved,sizeof(g)) && out.epoch==71 && out.call==81);
    struct kui_cdda_service_ticket forged=ticket;forged.call++;
    CHECK(kui_cdda_service_leave(&g,&forged,UINT32_MAX,false)==KUI_CDDA_SERVICE_STALE);
    forged=ticket;forged.epoch++;
    CHECK(kui_cdda_service_leave(&g,&forged,UINT32_MAX,false)==KUI_CDDA_SERVICE_STALE);
    CHECK(!memcmp(&g,&saved,sizeof(g)));
    CHECK(kui_cdda_service_leave(&g,&ticket,120,true)==KUI_CDDA_SERVICE_OK && g.last_tick==120);
    saved=g;CHECK(kui_cdda_service_leave(&g,&ticket,121,false)==KUI_CDDA_SERVICE_STALE);
    CHECK(!memcmp(&g,&saved,sizeof(g)));
    CHECK(kui_cdda_service_enter(&g,epoch,121,&out)==KUI_CDDA_SERVICE_OK && out.call==2);
    saved=g;CHECK(kui_cdda_service_leave(&g,&ticket,122,false)==KUI_CDDA_SERVICE_STALE);
    CHECK(!memcmp(&g,&saved,sizeof(g)));
    CHECK(kui_cdda_service_leave(&g,&out,122,true)==KUI_CDDA_SERVICE_OK);
    CHECK(kui_cdda_service_stop(&g,epoch)==KUI_CDDA_SERVICE_OK);
    saved=g;CHECK(kui_cdda_service_stop(&g,epoch)==KUI_CDDA_SERVICE_OK);
    CHECK(!memcmp(&g,&saved,sizeof(g)));
    CHECK(kui_cdda_service_start(&g,200,100,&epoch)==KUI_CDDA_SERVICE_OK && epoch==2);
    saved=g;CHECK(kui_cdda_service_enter(&g,ticket.epoch,200,&out)==KUI_CDDA_SERVICE_STALE);
    CHECK(kui_cdda_service_leave(&g,&ticket,200,true)==KUI_CDDA_SERVICE_STALE);
    CHECK(kui_cdda_service_stop(&g,ticket.epoch)==KUI_CDDA_SERVICE_STALE);
    CHECK(!memcmp(&g,&saved,sizeof(g)));

    struct {uint32_t before;struct kui_cdda_service_guard owner;} overlap;
    overlap.before=37u;
    CHECK(kui_cdda_service_init(&overlap.owner)==KUI_CDDA_SERVICE_OK);
    CHECK(kui_cdda_service_start(&overlap.owner,100,100,&overlap.before)==KUI_CDDA_SERVICE_OK);
    saved=overlap.owner;
    CHECK(kui_cdda_service_enter(&overlap.owner,overlap.before,100,
        (struct kui_cdda_service_ticket *)&overlap.before)==KUI_CDDA_SERVICE_INVALID);
    CHECK(!memcmp(&overlap.owner,&saved,sizeof(saved)) && overlap.before==1u);
}
static void guard_deadlines(void) {
    const uint32_t limits[]={1,2,100,2316184u,KUI_CDDA_HANDOFF_MAX_GAP_TICKS};
    for(unsigned i=0;i<sizeof(limits)/sizeof(limits[0]);i++) {
        uint32_t gap=limits[i],epoch;struct kui_cdda_service_guard g,saved;
        struct kui_cdda_service_ticket ticket,out={7,9};
        CHECK(kui_cdda_service_init(&g)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_start(&g,UINT32_MAX-40u,gap,&epoch)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_enter(&g,epoch,g.last_tick+gap-1u,&ticket)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_leave(&g,&ticket,g.last_tick+gap-1u,true)==KUI_CDDA_SERVICE_OK);
        uint32_t previous=g.last_tick;
        CHECK(kui_cdda_service_enter(&g,epoch,previous+gap,&out)==KUI_CDDA_SERVICE_DEADLINE);
        CHECK(g.state==KUI_CDDA_SERVICE_FAULT && !g.pending.call && g.last_tick==previous && out.epoch==7);
        saved=g;CHECK(kui_cdda_service_enter(&g,epoch,previous,&out)==KUI_CDDA_SERVICE_STATE);
        CHECK(!memcmp(&g,&saved,sizeof(g)));
        CHECK(kui_cdda_service_start(&g,previous,gap,&epoch)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_enter(&g,epoch,previous+gap-1u,&ticket)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_leave(&g,&ticket,previous+gap,true)==KUI_CDDA_SERVICE_DEADLINE);
        CHECK(g.state==KUI_CDDA_SERVICE_FAULT && !g.pending.epoch && g.last_tick==previous);
        CHECK(kui_cdda_service_start(&g,100,gap,&epoch)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_enter(&g,epoch,99,&out)==KUI_CDDA_SERVICE_DEADLINE);
        CHECK(kui_cdda_service_start(&g,100,gap,&epoch)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_enter(&g,epoch,100,&ticket)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_leave(&g,&ticket,99,true)==KUI_CDDA_SERVICE_DEADLINE);
        CHECK(kui_cdda_service_start(&g,100,gap,&epoch)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_enter(&g,epoch,100,&ticket)==KUI_CDDA_SERVICE_OK);
        CHECK(kui_cdda_service_leave(&g,&ticket,100,false)==KUI_CDDA_SERVICE_IO);
        CHECK(g.state==KUI_CDDA_SERVICE_FAULT && !g.pending.call);
    }
    struct kui_cdda_service_guard g,saved;uint32_t epoch=19;struct kui_cdda_service_ticket ticket={8,9};
    CHECK(kui_cdda_service_init(&g)==KUI_CDDA_SERVICE_OK);g.generation=UINT32_MAX;saved=g;
    CHECK(kui_cdda_service_start(&g,0,100,&epoch)==KUI_CDDA_SERVICE_OVERFLOW);
    CHECK(!memcmp(&g,&saved,sizeof(g)) && epoch==19);
    CHECK(kui_cdda_service_init(&g)==KUI_CDDA_SERVICE_OK);
    CHECK(kui_cdda_service_start(&g,0,100,&epoch)==KUI_CDDA_SERVICE_OK);
    g.call_generation=UINT32_MAX;saved=g;
    CHECK(kui_cdda_service_enter(&g,epoch,1,&ticket)==KUI_CDDA_SERVICE_OVERFLOW);
    CHECK(!memcmp(&g,&saved,sizeof(g)) && ticket.epoch==8 && ticket.call==9);
}
static void guard_properties(void) {
    struct kui_cdda_service_guard g;uint32_t epoch;
    CHECK(kui_cdda_service_init(&g)==KUI_CDDA_SERVICE_OK);
    uint64_t time=UINT32_MAX-500000u;
    CHECK(kui_cdda_service_start(&g,(uint32_t)time,KUI_CDDA_HANDOFF_MAX_GAP_TICKS,&epoch)==KUI_CDDA_SERVICE_OK);
    struct kui_cdda_service_ticket old={0};
    for(unsigned n=0;n<100000u;n++) {
        uint32_t idle=random32()%(g.gap_ticks/2u),work=random32()%(g.gap_ticks/2u);
        uint32_t expected_call=g.call_generation+1u;
        struct kui_cdda_service_ticket ticket,out={99,199};
        time+=idle;
        CHECK(kui_cdda_service_enter(&g,epoch,(uint32_t)time,&ticket)==KUI_CDDA_SERVICE_OK);
        CHECK(ticket.epoch==epoch && ticket.call==expected_call);
        struct kui_cdda_service_guard saved=g;
        CHECK(kui_cdda_service_enter(&g,epoch,(uint32_t)time+g.gap_ticks,&out)==KUI_CDDA_SERVICE_BUSY_RESULT);
        CHECK(kui_cdda_service_leave(&g,&old,(uint32_t)time,false)==KUI_CDDA_SERVICE_STALE);
        CHECK(!memcmp(&g,&saved,sizeof(g)) && out.epoch==99 && out.call==199);
        time+=work;
        CHECK(kui_cdda_service_leave(&g,&ticket,(uint32_t)time,true)==KUI_CDDA_SERVICE_OK);
        CHECK(g.last_tick==(uint32_t)time && !g.pending.call && g.state==KUI_CDDA_SERVICE_ACTIVE);
        old=ticket;
        if(n%17u==0) {
            CHECK(kui_cdda_service_stop(&g,epoch)==KUI_CDDA_SERVICE_OK);
            uint32_t previous=epoch;
            CHECK(kui_cdda_service_start(&g,(uint32_t)time,g.gap_ticks,&epoch)==KUI_CDDA_SERVICE_OK);
            CHECK(epoch==previous+1u);saved=g;
            CHECK(kui_cdda_service_enter(&g,previous,(uint32_t)time,&out)==KUI_CDDA_SERVICE_STALE);
            CHECK(!memcmp(&g,&saved,sizeof(g)));
        }
    }
    CHECK(time>3ull*UINT32_MAX);
}
int main(void) {
    descriptor_boundaries();decoding();descriptor_properties();guard_basics();guard_deadlines();guard_properties();
    printf("PASS CDDA handoff: %u checks; descriptor ranges/rights, wire decoding, service leases/reentry/stale tickets, clock wraps and faults\n",checks);
    return 0;
}
