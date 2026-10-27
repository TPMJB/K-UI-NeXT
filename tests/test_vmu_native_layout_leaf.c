/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the leaf variable-root decoder and its single-caller rule against
 * independent SH execution. All bytes here are authored synthetic code. */
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
#define UPPER UINT32_C(0x8c010280)
#define INPUT UINT32_C(0x8c500000)
#define INDEX UINT32_C(0x8c500100)
#define TX UINT32_C(0x8c510000)
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
/* The leaf as the reviewed system initializer calls it, and that caller. */
static void fixture(uint8_t *p) {
    memset(p,0,IMAGE);
    put32(p,0x80u,BASE+LEAF);
    put16(p,CALLER,load(0u,CALLER,0x80u));put16(p,CALLER+2u,0x400bu);put16(p,CALLER+4u,0x0009u);
    put32(p,0x140u,TX);put32(p,0x144u,INPUT);put32(p,0x148u,INDEX);put32(p,0x14cu,UPPER);
    static const uint16_t code[]={0x4f22,0xe00c,0x2f06,0,0,0,0,0x400b,0xe400,0x7f04,0x4f26,0x000b,0x0009};
    for(unsigned i=0;i<13u;i++) put16(p,LEAF+2u*i,code[i]);
    put16(p,LEAF+6u,load(7u,LEAF+6u,0x140u));put16(p,LEAF+8u,load(6u,LEAF+8u,0x144u));
    put16(p,LEAF+10u,load(5u,LEAF+10u,0x148u));put16(p,LEAF+12u,load(0u,LEAF+12u,0x14cu));
}
/* Execute the leaf from its entry with every inherited register unknown:
 * the indirect target is sampled before the delay instruction; after the
 * call the epilogue must pop exactly the count and the saved PR, then
 * return with a no-op delay. Returns the upper wrapper's argument view. */
static bool model(const uint8_t *p,size_t n,uint32_t *input,uint32_t *index,uint32_t *tx,uint32_t *count) {
    uint32_t r[16]={0},stack[8];bool v[16]={false};unsigned sp=0;
    enum {RUN,DELAY,CALLED,DONE} phase=RUN;
    for(size_t pc=LEAF;pc<LEAF+26u;pc+=2u) {
        if(pc>n || n-pc<2u) return false;
        uint16_t op=u16(p+pc);unsigned rn=(op>>8)&15u,rm=(op>>4)&15u;
        if(phase==DONE) return false;
        if(op==0x0009u) {if(phase==DELAY) phase=CALLED;continue;}
        if(phase==CALLED && op==0x7f04u) {if(sp<2u) return false;--sp;continue;}
        if(phase==CALLED && op==0x4f26u) {if(sp!=1u || stack[0]!=0x5052u) return false;sp=0;continue;}
        if(phase==CALLED && op==0x000bu) {
            if(pc+2u>n || n-pc-2u<2u || u16(p+pc+2u)!=0x0009u) return false;
            phase=DONE;break;
        }
        if(phase==CALLED) return false;
        if(op==0x4f22u) {if(sp) return false;stack[sp++]=0x5052u;}               /* PR */
        else if((op&0xf000u)==0xe000u) {r[rn]=(uint32_t)(int32_t)(int8_t)op;v[rn]=true;}
        else if((op&0xff0fu)==0x2f06u) {if(!v[rm] || sp>=8u) return false;stack[sp++]=r[rm];}
        else if((op&0xf000u)==0xd000u) {
            size_t slot=((pc+4u)&~3u)+4u*(op&255u);
            if(slot>n || n-slot<4u) return false;
            r[rn]=u32(p+slot);v[rn]=true;
        } else if((op&0xf0ffu)==0x400bu && phase==RUN) {
            if(!v[rn] || phys(r[rn])!=phys(UPPER)) return false;
            phase=DELAY;continue;
        } else return false;
        if(phase==DELAY) {
            /* The delay instruction completes the call's arguments. */
            if(sp!=2u || !v[4] || r[4] || !v[5] || !v[6] || !v[7]) return false;
            *input=r[6];*index=r[5];*tx=r[7];*count=stack[1];phase=CALLED;
        }
    }
    return phase==DONE;
}
static bool decode(const uint8_t *p,size_t n) {
    uint32_t a=0,b=0,c=0,k=0;
    bool found=variable_leaf_root(p,n,BASE,LEAF,UPPER,&a,&b,&c,&k);
    if(found) {
        uint32_t ma=0,mb=0,mc=0,mk=0;++accepted;
        CHECK(model(p,n,&ma,&mb,&mc,&mk));
        CHECK(a==ma && b==mb && c==mc && k==mk);
    }
    return found;
}
int main(void) {
    uint8_t image[IMAGE];
    fixture(image);
    uint32_t a,b,c,k;
    CHECK(variable_leaf_root(image,IMAGE,BASE,LEAF,UPPER,&a,&b,&c,&k));
    CHECK(a==INPUT && b==INDEX && c==TX && k==12u);
    /* Every 16-bit replacement at every leaf instruction. Each accepted
     * mutation must execute with the same independently derived arguments. */
    for(unsigned instruction=0;instruction<13u;instruction++) {
        size_t pc=LEAF+2u*instruction;uint16_t original=u16(image+pc);
        for(unsigned value=0;value<=UINT16_MAX;value++) {
            put16(image,pc,(uint16_t)value);(void)decode(image,IMAGE);
        }
        put16(image,pc,original);
    }
    CHECK(decode(image,IMAGE));
    /* The single caller: a load through one register, a call through the
     * same register, a no-op delay; no other image word names the leaf. */
    fixture(image);put16(image,CALLER+2u,0x410bu);CHECK(!decode(image,IMAGE));
    fixture(image);put16(image,CALLER+4u,0xe400u);CHECK(!decode(image,IMAGE));
    fixture(image);put16(image,CALLER+2u,0x0009u);CHECK(!decode(image,IMAGE));
    fixture(image);put32(image,0x80u,0u);CHECK(!decode(image,IMAGE));          /* Never called. */
    fixture(image);put32(image,0x200u,BASE+LEAF);CHECK(!decode(image,IMAGE));  /* A second reference. */
    fixture(image);put32(image,0x200u,(BASE+LEAF)|0x20000000u);CHECK(!decode(image,IMAGE));
    fixture(image);put16(image,0x60u,load(3u,0x60u,0x80u));CHECK(!decode(image,IMAGE)); /* A second load. */
    fixture(image);put32(image,0x80u,(BASE+LEAF)&0x1fffffffu);CHECK(decode(image,IMAGE)); /* Same address, U0. */
    /* Upper target, count and truncation. */
    fixture(image);put32(image,0x14cu,UPPER+2u);CHECK(!decode(image,IMAGE));
    fixture(image);put16(image,LEAF+2u,0xe0f4u);CHECK(decode(image,IMAGE));    /* Signed, as executed. */
    fixture(image);for(unsigned n=0;n<0x150u;n++) CHECK(!decode(image,n));
    printf("Native leaf variable root: %u checks, %u semantically verified admissions; 851968 instruction mutations\n",
        checks,accepted);
    return 0;
}
