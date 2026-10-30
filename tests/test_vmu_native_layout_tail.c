/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic tail-thunk code, independently executed from its unique caller.
 * No retail bytes are embedded. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/vmu_native_layout.c"

static unsigned checks,accepted;
#define CHECK(x) do {++checks;assert(x);} while(0)
#define BASE UINT32_C(0x8c010000)
#define IMAGE 0x300u
#define CALLER 0x40u
#define LEAF 0x100u
#define JUMP (LEAF+6u)
#define UPPER (BASE+0x280u)
#define RX UINT32_C(0x0c500004)
#define TX UINT32_C(0x0c510004)
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
static void fixture(uint8_t *p) {
    memset(p,0,IMAGE);
    put32(p,0x80u,BASE+LEAF);
    put16(p,CALLER,load(1u,CALLER,0x80u));
    put16(p,CALLER+2u,0x410bu);put16(p,CALLER+4u,0x0009u);
    put32(p,0x140u,UPPER);put32(p,0x144u,RX);put32(p,0x148u,TX);
    put16(p,LEAF,load(3u,LEAF,0x140u));
    put16(p,LEAF+2u,load(5u,LEAF+2u,0x144u));
    put16(p,LEAF+4u,load(6u,LEAF+4u,0x148u));
    put16(p,JUMP,0x432bu);put16(p,JUMP+2u,0xe400u);
}
/* Run from the caller, with arguments/targets initially unknown. Delayed
 * control transfers sample their target first; only JSR changes PR. */
static bool model(const uint8_t *p,size_t n,uint32_t *rx_slot,uint32_t *rx,
    uint32_t *tx_slot,uint32_t *tx) {
    uint32_t r[16]={0},pr=0x12345678u,pc=BASE+CALLER,pending=0u;
    bool valid[16]={false};unsigned calls=0,jumps=0;
    r[15]=0x0cf00000u;valid[15]=true;
    for(unsigned step=0;step<12u;step++) {
        if(phys(pc)==phys(UPPER)) {
            if(calls!=1u || jumps!=1u || pending || pr!=BASE+CALLER+6u ||
               r[15]!=0x0cf00000u || !valid[4] || r[4] || !valid[5] || !valid[6]) return false;
            *rx=r[5];*tx=r[6];return true;
        }
        if(phys(pc)<phys(BASE)) return false;
        size_t at_=phys(pc)-phys(BASE);
        if(at_>n || n-at_<2u) return false;
        uint16_t op=u16(p+at_);unsigned reg=(op>>8)&15u;
        uint32_t next=pc+2u,after=pending;pending=0u;
        if((op&0xf000u)==0xd000u) {
            uint32_t address=((pc+4u)&~3u)+4u*(op&255u);
            if(phys(address)<phys(BASE)) return false;
            size_t slot=phys(address)-phys(BASE);
            if(slot>n || n-slot<4u) return false;
            r[reg]=u32(p+slot);valid[reg]=true;
            if(reg==5u) *rx_slot=address;
            if(reg==6u) *tx_slot=address;
        } else if((op&0xf000u)==0xe000u) {
            r[reg]=(uint32_t)(int32_t)(int8_t)op;valid[reg]=true;
        } else if((op&0xf0ffu)==0x400bu || (op&0xf0ffu)==0x402bu) {
            if(after || !valid[reg]) return false;
            pending=r[reg];
            if((op&0xf0ffu)==0x400bu) {pr=pc+4u;++calls;}
            else ++jumps;
        } else if(op!=0x0009u) return false;
        pc=after?after:next;
    }
    return false;
}
static bool decode(const uint8_t *p,size_t n) {
    uint32_t rs=0,rx=0,ts=0,tx=0;
    bool found=uniform_tail_root(p,n,BASE,JUMP,UPPER,&rs,&rx,&ts,&tx);
    if(found) {
        uint32_t mrs=0,mrx=0,mts=0,mtx=0;++accepted;
        CHECK(model(p,n,&mrs,&mrx,&mts,&mtx));
        CHECK(phys(rs)==phys(mrs) && rx==mrx && phys(ts)==phys(mts) && tx==mtx);
    }
    return found;
}
int main(void) {
    uint8_t image[IMAGE];fixture(image);CHECK(decode(image,IMAGE));
    /* Every instruction replacement in both the tail and its claimed
     * caller. An admission must still execute the proved target/arguments
     * with unchanged caller PR and stack. */
    const unsigned sites[]={LEAF,LEAF+2u,LEAF+4u,JUMP,JUMP+2u,CALLER,CALLER+2u,CALLER+4u};
    for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++) {
        uint16_t original=u16(image+sites[i]);
        for(unsigned value=0;value<=UINT16_MAX;value++) {
            put16(image,sites[i],(uint16_t)value);(void)decode(image,IMAGE);
        }
        put16(image,sites[i],original);
    }
    /* Coordinated register changes can preserve the sampled target while
     * corrupting state: loading it into R15 replaces SP before the upper
     * wrapper saves anything. Exercise the complete MOV.L/JSR pair for
     * every register, not only one-instruction replacements. */
    for(unsigned reg=0;reg<16u;reg++) {
        fixture(image);put16(image,CALLER,load(reg,CALLER,0x80u));
        put16(image,CALLER+2u,(uint16_t)(0x400bu|(reg<<8)));
        CHECK(decode(image,IMAGE)==(reg!=15u));
    }
    fixture(image);put32(image,0x200u,BASE+LEAF);CHECK(!decode(image,IMAGE));
    fixture(image);put32(image,0x200u,(BASE+LEAF)|0x20000000u);CHECK(!decode(image,IMAGE));
    fixture(image);put16(image,0x60u,load(2u,0x60u,0x80u));CHECK(!decode(image,IMAGE));
    fixture(image);put32(image,0x80u,0u);CHECK(!decode(image,IMAGE));
    fixture(image);put32(image,0x140u,UPPER+2u);CHECK(!decode(image,IMAGE));
    fixture(image);for(unsigned n=0;n<0x14cu;n++) CHECK(!decode(image,n));
    fixture(image);put32(image,0x80u,(BASE+LEAF)&0x1fffffffu);CHECK(decode(image,IMAGE));
    printf("Native uniform tail root: %u checks, %u independently executed admissions; 524288 instruction mutations\n",checks,accepted);
    return 0;
}
