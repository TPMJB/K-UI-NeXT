/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual private witness decoder and complete discovery. No
 * original executable bytes are embedded in this test. */
#include "../src/core/vmu_native_layout.c"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static unsigned rejected_mutations,equivalent_literals;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"Timer registration check failed at %u: %s\n",__LINE__,#x);exit(1);}} while(0)
#define BASE UINT32_C(0x8c010000)
static uint8_t fixture[4096],saved[4096];
static void put16(size_t at_,uint16_t v) {CHECK(at_+2u<=sizeof(fixture));fixture[at_]=(uint8_t)v;fixture[at_+1u]=(uint8_t)(v>>8);}
static void put32(size_t at_,uint32_t v) {put16(at_,(uint16_t)v);put16(at_+2u,(uint16_t)(v>>16));}
static void long_load(size_t pc,unsigned reg,size_t slot,uint32_t value) {
    size_t origin=(pc+4u)&~3u;CHECK(slot>=origin && !((slot-origin)&3u) && (slot-origin)/4u<=255u);
    put16(pc,(uint16_t)(0xd000u|(reg<<8)|((slot-origin)/4u)));put32(slot,value);
}
static void short_load(size_t pc,unsigned reg,size_t slot,uint16_t value) {
    CHECK(slot>=pc+4u && !((slot-pc-4u)&1u) && (slot-pc-4u)/2u<=255u);
    put16(pc,(uint16_t)(0x9000u|(reg<<8)|((slot-pc-4u)/2u)));put16(slot,value);
}
static size_t registration(size_t handler,unsigned timer) {return timer?handler+0x52u:handler-0xaeu;}
static void dispatcher(size_t s,size_t pool,unsigned timer,uint32_t callback) {
    static const uint16_t op[18]={0,0x6030,0,0x2300,0,0xe3df,0x6241,0x2239,0x2421,
        0,0x6141,0x2129,0x2411,0,0,0x6012,0x402b,0x6432};
    for(unsigned i=0;i<18u;i++) put16(s+2u*i,op[i]);
    put16(s+4u,timer?0xc9fbu:0xc9fdu);
    long_load(s,3u,pool,0xffd80004u);long_load(s+8u,4u,pool+4u,timer?0xffd80028u:0xffd8001cu);
    short_load(s+18u,2u,pool+16u,0xfeffu);
    long_load(s+26u,1u,pool+8u,callback);long_load(s+28u,3u,pool+12u,callback+4u);
}
static void setter(size_t handler,size_t pool,unsigned timer,uint32_t callback,uint32_t entry) {
    size_t s=registration(handler,timer);
    short_load(s+2u,0u,pool+18u,timer?0x248u:0x244u);
    long_load(s+4u,timer?3u:2u,pool+20u,entry);
    if(!timer) {
        put16(s,0x0122u);put16(s+8u,0x0126u);put16(s+12u,0x2352u);put16(s+14u,0x2262u);
        long_load(s+6u,3u,pool+24u,callback);long_load(s+10u,2u,pool+28u,callback+4u);
    } else {
        put16(s,0x0222u);put16(s+6u,0x0236u);put16(s+12u,0x2242u);put16(s+14u,0x2352u);
        long_load(s+8u,3u,pool+28u,callback+4u);long_load(s+10u,2u,pool+24u,callback);
    }
}
static void reset(void) {
    memset(fixture,0,sizeof(fixture));
    dispatcher(0x200u,0x300u,0u,0x8c090000u);setter(0x200u,0x300u,0u,0x8c090000u,BASE+0x200u);
    dispatcher(0x600u,0x780u,1u,0x8c090008u);setter(0x600u,0x780u,1u,0x8c090008u,BASE+0x600u);
}
static void synthetic(void) {
    reset();uint32_t one=0,two=0;
    CHECK(timers(fixture,sizeof(fixture),BASE,&one,&two));CHECK(one==0x8c090000u && two==0x8c090008u);
    memcpy(saved,fixture,sizeof(saved));
    for(unsigned timer=0;timer<2u;timer++) {
        memcpy(fixture,saved,sizeof(saved));
        size_t handler=timer?0x600u:0x200u,pool=timer?0x780u:0x300u,s=registration(handler,timer);
        uint32_t callback=timer?0x8c090008u:0x8c090000u;
        CHECK(timer_registration(fixture,sizeof(fixture),BASE,handler,timer,callback,callback+4u));
        for(unsigned byte=0;byte<16u;byte++) for(unsigned bit=0;bit<8u;bit++) {
            memcpy(fixture,saved,sizeof(saved));fixture[s+byte]^=(uint8_t)(1u<<bit);
            if(!timer_registration(fixture,sizeof(fixture),BASE,handler,timer,callback,callback+4u)) {
                ++rejected_mutations;continue;
            }
            /* A displacement may select another literal containing the same
             * value. That is a valid transport, not an opcode mutation to
             * reject. Check its independently decoded operand explicitly. */
            unsigned offset=byte&~1u;CHECK(!(byte&1u));
            CHECK(offset==2u || offset==4u || offset==10u || offset==(timer?8u:6u));
            uint16_t op=u16(fixture+s+offset);size_t address;
            if(offset==2u) {
                address=s+offset+4u+2u*(op&255u);CHECK(u16(fixture+address)==(timer?0x248u:0x244u));
            } else {
                address=((s+offset+4u)&~3u)+4u*(op&255u);
                uint32_t expected=offset==4u?BASE+(uint32_t)handler:
                    offset==(timer?8u:10u)?callback+4u:callback;
                CHECK(u32(fixture+address)==expected);
            }
            ++equivalent_literals;
        }
        for(unsigned slot=0;slot<4u;slot++) {
            size_t at_=pool+(slot==0u?18u:slot==1u?20u:slot==2u?24u:28u);
            unsigned width=slot?4u:2u;
            for(unsigned bit=0;bit<width*8u;bit++) {
                /* A code pointer may retain any admitted RAM alias. Lower
                 * physical bits and unsupported aliases remain constrained. */
                if(slot==1u && (bit==29u || bit==31u)) continue;
                memcpy(fixture,saved,sizeof(saved));fixture[at_+bit/8u]^=(uint8_t)(1u<<(bit&7u));
                CHECK(!timer_registration(fixture,sizeof(fixture),BASE,handler,timer,callback,callback+4u));
            }
        }
        memcpy(fixture,saved,sizeof(saved));put32(pool+20u,0xec010000u+(uint32_t)handler);
        CHECK(!timer_registration(fixture,sizeof(fixture),BASE,handler,timer,callback,callback+4u));
        memcpy(fixture,saved,sizeof(saved));put32(pool+20u,0x0c010000u+(uint32_t)handler);
        CHECK(timer_registration(fixture,sizeof(fixture),BASE,handler,timer,callback,callback+4u));
        put32(pool+20u,0xac010000u+(uint32_t)handler);
        CHECK(timer_registration(fixture,sizeof(fixture),BASE,handler,timer,callback,callback+4u));
        memcpy(fixture,saved,sizeof(saved));fixture[s]^=1u;one=0xdeadbeefu;two=0xdeadbeefu;
        CHECK(!timers(fixture,sizeof(fixture),BASE,&one,&two));CHECK(one==0xdeadbeefu && two==0xdeadbeefu);
    }
    memcpy(fixture,saved,sizeof(saved));
    /* Dispatcher-like data is insufficient. A foreign link-address setter
     * cannot register the current as-loaded copy. */
    dispatcher(0xb00u,0xc00u,0u,0x8c090010u);
    CHECK(timers(fixture,sizeof(fixture),BASE,&one,&two));
    setter(0xb00u,0xc00u,0u,0x8c090010u,0x8c009f4cu);
    CHECK(timers(fixture,sizeof(fixture),BASE,&one,&two));
    setter(0xb00u,0xc00u,0u,0x8c090010u,BASE+0xb00u);
    CHECK(!timers(fixture,sizeof(fixture),BASE,&one,&two));
    memcpy(fixture,saved,sizeof(saved));
    CHECK(!timer_registration(fixture,sizeof(fixture),BASE,0x20u,0u,0,0));
    CHECK(!timer_registration(fixture,0x623u,BASE,0x600u,1u,0,0));
    CHECK(!timer_registration(fixture,sizeof(fixture),BASE,0x200u,2u,0,0));
}
static void original(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);
    CHECK(length>0 && length<=0xc00000);rewind(f);size_t n=(size_t)length;
    uint8_t *image=malloc(n);CHECK(image);CHECK(fread(image,1,n,f)==n);CHECK(!fclose(f));
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;
    CHECK(kui_vmu_native_discover_layout(image,n,BASE,&d,&l));uint32_t one,two;
    CHECK(timers(image,n,BASE,&one,&two));CHECK(one==l.timer1_callback && two==l.timer2_callback);
    printf("Original timer registration verified: %s (%08x, %08x)\n",path,one,two);free(image);
}
int main(int argc,char **argv) {
    synthetic();for(int i=1;i<argc;i++) original(argv[i]);
    printf("Native timer self-registration: %u checks; %u rejected instruction mutations, %u equivalent literals; foreign copies excluded and live duplicates refused\n",checks,rejected_mutations,equivalent_literals);
    return 0;
}
