/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the private bounded argument decoder against independent SH
 * execution. All bytes here are authored synthetic code, not game data. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/vmu_native_layout.c"

static unsigned checks,accepted;
#define CHECK(x) do {++checks;assert(x);} while(0)
#define BASE UINT32_C(0x8c010000)
#define CALL 0x28u
#define UPPER UINT32_C(0x8c010180)
#define RX UINT32_C(0x8c500004)
#define TX UINT32_C(0x8c510004)
static void put16(uint8_t *p,size_t offset,uint16_t value) {
    p[offset]=(uint8_t)value;p[offset+1u]=(uint8_t)(value>>8);
}
static void put32(uint8_t *p,size_t offset,uint32_t value) {
    put16(p,offset,(uint16_t)value);put16(p,offset+2u,(uint16_t)(value>>16));
}
static uint16_t load(unsigned reg,size_t instruction,size_t slot) {
    size_t origin=(instruction+4u)&~3u;
    CHECK(slot>=origin && !((slot-origin)&3u) && slot-origin<=1020u);
    return (uint16_t)(0xd000u|(reg<<8)|((slot-origin)/4u));
}
/* Execute instruction semantics, with inherited registers deliberately
 * unknown. The indirect target is sampled before its delay instruction.
 * This model does not use the decoder's known-bit mask or result offsets. */
static bool model(const uint8_t *p,size_t n,uint32_t *rx_slot,uint32_t *rx_value,
    uint32_t *tx_slot,uint32_t *tx_value) {
    uint32_t registers[16]={0};bool valid[16]={false};
    for(size_t pc=CALL-8u;pc<=CALL+2u;pc+=2u) {
        uint16_t op=u16(p+pc);unsigned reg=(op>>8)&15u;
        if(pc==CALL) {
            if((op&0xf0ffu)!=0x400bu || !valid[reg] || phys(registers[reg])!=phys(UPPER)) return false;
            continue;
        }
        if(op==0x0009u) continue;
        if((op&0xf000u)==0xe000u) {registers[reg]=(uint32_t)(int32_t)(int8_t)op;valid[reg]=true;}
        else if((op&0xf000u)==0xd000u) {
            size_t slot=((pc+4u)&~3u)+4u*(op&255u);
            if(slot>n || n-slot<4u) return false;
            registers[reg]=u32(p+slot);valid[reg]=true;
            if(reg==5u) *rx_slot=BASE+(uint32_t)slot;
            if(reg==6u) *tx_slot=BASE+(uint32_t)slot;
        } else return false;
    }
    *rx_value=registers[5];*tx_value=registers[6];
    return valid[4] && valid[5] && valid[6] && registers[4]==0u;
}
static bool decode(const uint8_t *p,size_t n) {
    uint32_t rs=0,rv=0,ts=0,tv=0;
    bool found=uniform_root(p,n,BASE,CALL,UPPER,&rs,&rv,&ts,&tv);
    if(found) {
        uint32_t model_rx=0,model_tx=0,model_rv=0,model_tv=0;++accepted;
        bool executed=model(p,n,&model_rx,&model_rv,&model_tx,&model_tv);
        if(!executed) {
            for(size_t pc=CALL-8u;pc<=CALL+2u;pc+=2u) fprintf(stderr,"%04x ",u16(p+pc));
            fprintf(stderr,"n=%zu\n",n);
        }
        CHECK(executed);
        CHECK(rv==model_rv && tv==model_tv && rs==model_rx && ts==model_tx);
    }
    return found;
}
static void fixture(uint8_t *p,unsigned target,const unsigned order[5]) {
    memset(p,0,0x200u);put32(p,0x100u,UPPER);put32(p,0x104u,RX);put32(p,0x108u,TX);
    static const unsigned positions[5]={CALL-8u,CALL-6u,CALL-4u,CALL-2u,CALL+2u};
    for(unsigned i=0;i<5u;i++) {
        size_t at=positions[i];uint16_t op;
        switch(order[i]) {
        case 0:op=load(target,at,0x100u);break;
        case 1:op=load(5u,at,0x104u);break;
        case 2:op=load(6u,at,0x108u);break;
        case 3:op=0xe400u;break;
        default:op=0x0009u;break;
        }
        put16(p,at,op);
    }
    put16(p,CALL,(uint16_t)(0x400bu|(target<<8)));
}
static void permutations(unsigned *order,unsigned used,unsigned depth) {
    if(depth<5u) {
        for(unsigned i=0;i<5u;i++) if(!(used&(1u<<i))) {
            order[depth]=i;permutations(order,used|(1u<<i),depth+1u);
        }
        return;
    }
    uint8_t image[0x200];
    const unsigned targets[]={0u,2u,3u};
    for(unsigned i=0;i<3u;i++) {
        fixture(image,targets[i],order);
        /* Target in the delay instruction is not known at JSR execution. */
        CHECK(decode(image,sizeof(image))==(order[4]!=0u));
    }
}
int main(void) {
    unsigned order[5]={0};permutations(order,0u,0u);
    const unsigned standard[5]={0u,1u,2u,3u,4u};
    uint8_t image[0x200];
    /* Every 16-bit replacement at every root instruction, including JSR
     * and its delay slot. Every accepted mutation must execute with the
     * same independently verified arguments and sampled target. */
    const unsigned target_allocations[]={0u,2u,3u};
    for(unsigned allocation=0;allocation<3u;allocation++) {
        fixture(image,target_allocations[allocation],standard);
        for(unsigned instruction=0;instruction<6u;instruction++) {
            size_t pc=CALL-8u+2u*instruction;uint16_t original=u16(image+pc);
            for(unsigned value=0;value<=UINT16_MAX;value++) {
                put16(image,pc,(uint16_t)value);(void)decode(image,sizeof(image));
            }
            put16(image,pc,original);
        }
    }
    /* All newly admitted target allocations retain rejection of unknown
     * targets, target/argument aliasing and untrusted control flow. */
    for(unsigned target=0;target<16u;target++) {
        fixture(image,target,standard);
        CHECK(decode(image,sizeof(image))==(target==0u || target==2u || target==3u));
    }
    fixture(image,0u,standard);
    for(unsigned n=0;n<CALL+4u;n++) CHECK(!decode(image,n));
    put32(image,0x100u,UPPER+2u);CHECK(!decode(image,sizeof(image)));
    printf("Native uniform root: %u checks, %u semantically verified admissions; 360 permutations and 1179648 instruction mutations\n",checks,accepted);
    return 0;
}
