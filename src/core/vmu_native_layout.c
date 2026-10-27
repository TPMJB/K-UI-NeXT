/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_layout.h"
#include <string.h>

struct witness {uint16_t at,word;};
static uint16_t u16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p) {return (uint32_t)u16(p)|(uint32_t)u16(p+2)<<16;}
static uint32_t phys(uint32_t a) {return a&0x1fffffffu;}
static bool ram(uint32_t a,uint32_t bytes,uint32_t alignment) {
    uint32_t area=a&0xe0000000u,p=phys(a);
    return (area==0u || area==0x80000000u || area==0xa0000000u) &&
        bytes && p>=0x0c010000u && p<0x0d000000u && bytes<=0x0d000000u-p && !(p&(alignment-1u));
}
static bool overlaps(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    a=phys(a);b=phys(b);return a<b+bn && b<a+an;
}
static bool at(uint32_t a,uint32_t base,size_t n,size_t bytes,size_t *i) {
    if(!ram(a,(uint32_t)bytes,2u) || phys(a)<phys(base) || phys(a)-phys(base)>n || bytes>n-(phys(a)-phys(base))) return false;
    *i=phys(a)-phys(base);return true;
}
static bool words(const uint8_t *p,size_t n,size_t start,const struct witness *w,size_t count) {
    for(size_t i=0;i<count;i++) if(start>n || w[i].at>n-start || n-start-w[i].at<2u || u16(p+start+w[i].at)!=w[i].word) return false;
    return true;
}
#define WORDS(p,n,s,w) words((p),(n),(s),(w),sizeof(w)/sizeof((w)[0]))
/* Decode literal addresses from actual SH PC-relative instructions, including
 * a load at the halfword rather than longword-aligned instruction position. */
static bool literal(const uint8_t *p,size_t n,uint32_t base,size_t instruction,unsigned reg,uint32_t *value) {
    if(instruction>n || n-instruction<2u) return false;
    uint16_t code=u16(p+instruction);
    if((code&0xff00u)!=(0xd000u|reg<<8)) return false;
    uint32_t address=((base+(uint32_t)instruction+4u)&~3u)+4u*(code&255u);size_t slot;
    if(!at(address,base,n,4u,&slot) || (address&3u)) return false;
    *value=u32(p+slot);return true;
}
static bool word_literal(const uint8_t *p,size_t n,uint32_t base,size_t instruction,unsigned reg,uint16_t value) {
    if(instruction>n || n-instruction<2u) return false;
    uint16_t code=u16(p+instruction);size_t slot;
    uint32_t address=base+(uint32_t)instruction+4u+2u*(code&255u);
    return (code&0xff00u)==(0x9000u|reg<<8) && at(address,base,n,2u,&slot) && u16(p+slot)==value;
}
static bool target(const uint8_t *p,size_t n,uint32_t base,size_t instruction,unsigned reg,uint32_t expected) {
    uint32_t got;return literal(p,n,base,instruction,reg,&got) && phys(got)==phys(expected);
}
static bool exact_literal(const uint8_t *p,size_t n,uint32_t base,size_t instruction,unsigned reg,uint32_t expected) {
    uint32_t got;return literal(p,n,base,instruction,reg,&got) && got==expected;
}
static bool bsr_target(const uint8_t *p,size_t n,uint32_t base,size_t instruction,uint32_t expected) {
    if(instruction>n || n-instruction<2u) return false;
    uint16_t code=u16(p+instruction);int32_t displacement=(int32_t)((code&4095u)^2048u)-2048;
    return (code&0xf000u)==0xb000u && phys(base+(uint32_t)instruction+4u+(uint32_t)(2*displacement))==phys(expected);
}
static bool bsr_code(const uint8_t *p,size_t n,uint32_t base,size_t instruction) {
    if(instruction>n || n-instruction<2u) return false;
    uint16_t code=u16(p+instruction);int32_t displacement=(int32_t)((code&4095u)^2048u)-2048;
    uint32_t destination=base+(uint32_t)instruction+4u+(uint32_t)(2*displacement);size_t offset;
    return (code&0xf000u)==0xb000u && at(destination,base,n,2u,&offset);
}
/* Decode every gap in the upper wrapper's argument-save prefix. These are
 * real instructions: leaving even an otherwise unrelated literal load
 * unconstrained would permit a replacement that corrupts R15 or R5/R6 before
 * their saved arguments are reloaded. Both wrappers use the same context. */
static bool upper_context(const uint8_t *p,size_t n,uint32_t base,size_t s,bool variable) {
    uint32_t mode,flag,base_cell,context,other,clear,again;size_t code;
    return literal(p,n,base,s+18u,3u,&mode) && literal(p,n,base,s+22u,2u,&flag) &&
        literal(p,n,base,s+26u,0u,&base_cell) && literal(p,n,base,s+28u,12u,&context) &&
        literal(p,n,base,s+(variable?44u:38u),3u,&other) &&
        literal(p,n,base,s+(variable?46u:44u),2u,&clear) &&
        literal(p,n,base,s+(variable?68u:66u),2u,&again) &&
        word_literal(p,n,base,s+(variable?36u:42u),6u,0x4e0u) &&
        ram(flag,12u,4u) && ram(base_cell,4u,4u) && ram(context,0x544u,4u) &&
        base_cell==flag-4u && context==flag+8u && other==flag+4u && mode==context+0x540u &&
        again==flag && !(clear&1u) && at(clear,base,n,2u,&code);
}
/* Variable-initializer contract: two input banks selected by immutable
 * indices, each 1 KiB; fc8 remains the aligned pointer-array address. */
static bool variable(const uint8_t *p,size_t n,uint32_t base,size_t s,const struct kui_vmu_native_discovery *d) {
    static const struct witness w[]={
        {0,0x2fe6},{2,0x2fd6},{4,0x2fc6},{6,0x2fb6},{8,0x2fa6},{10,0x2f96},{12,0x4f22},{14,0x7ff4},
        {18,0xeb1f},{22,0x6c73},{28,0x1f42},{30,0x2f52},{32,0x2e32},{34,0x0266},{36,0x63e2},{38,0x62f2},
        {42,0x0326},{44,0x70fc},{46,0x63e2},{48,0x023e},{50,0x22b8},{52,0x8d09},{54,0xe9e0},
        {58,0x61e2},{60,0x031e},{62,0x7320},{64,0x0136},{66,0x63e2},{68,0x023e},{70,0x2299},{72,0x0326},
        {76,0x64e2},{78,0x034e},{80,0x23b8},{82,0x8906},{84,0x034e},{86,0x7320},{88,0x0436},
        {90,0x63e2},{92,0x023e},{94,0x2299},{96,0x0326},
        {100,0x63e2},{104,0x023e},{106,0x22ab},{108,0x0326},{110,0x7004},{112,0x63e2},{114,0x023e},{116,0x22ab},{118,0x0326},
        {122,0x430b},{124,0x0009},{126,0xed00},{128,0x64d3},{130,0xe560},
        {134,0x62e2},{136,0x332c},{138,0x334c},{140,0x23d2},{144,0x62e2},{146,0x332c},{148,0x334c},{150,0x7404},{152,0x3452},{154,0x7360},{156,0x8ff2},{158,0x23d2},
        {160,0x53f2},{162,0x4c15},{166,0x1f31},{168,0x8f21},{170,0x65d3},
        {172,0x64f2},{174,0x7404},{176,0x2f42},{178,0x74fc},{180,0x6442},{182,0x6343},{184,0x23b8},{186,0x8901},{188,0x7420},{190,0x2499},
        {192,0x51f1},{194,0x24ab},{198,0x7501},{200,0x7104},{202,0x35c3},{204,0x1f11},{206,0x71fc},{208,0x6112},{210,0x63e2},{212,0x6613},
        {214,0x4608},{216,0x323c},{218,0x326c},{220,0x2242},{222,0x347c},{226,0x63e2},{228,0x323c},{230,0x362c},{232,0x7660},{234,0x8fdf},{236,0x2642},
        {238,0x62e2},{240,0x64d3},{244,0xe518},{246,0x6323},{248,0x013e},{252,0x0216},{254,0x61c3},{256,0x63e2},{258,0x4118},{262,0x4108},
        {264,0x6233},{266,0x022e},{270,0x312c},{272,0x0316}
    };
    uint32_t alias;
    return WORDS(p,n,s,w) && target(p,n,base,s+16u,3u,d->manager_initial) && target(p,n,base,s+20u,14u,d->manager_cell) &&
        word_literal(p,n,base,s+24u,0u,0xfc4u) && word_literal(p,n,base,s+40u,0u,0xfc8u) &&
        word_literal(p,n,base,s+132u,3u,0xfe8u) && word_literal(p,n,base,s+142u,3u,0xfe8u) &&
        word_literal(p,n,base,s+164u,7u,0x400u) && word_literal(p,n,base,s+196u,2u,0xfe8u) &&
        word_literal(p,n,base,s+224u,2u,0xfe8u) && literal(p,n,base,s+102u,10u,&alias) && alias==0xa0000000u;
}
/* Bind a timer dispatcher to the SDK setter which actually installs that
 * as-loaded entry in VBR. Executable files may contain an unrelocated IP.BIN
 * copy with matching dispatcher bytes but a different link address. Such a
 * copy cannot authorize this runtime's callback cells. A second complete
 * self-registered dispatcher remains ambiguous and is still refused. */
static bool timer_registration(const uint8_t *p,size_t n,uint32_t base,size_t handler,
    unsigned timer,uint32_t callback,uint32_t argument) {
    static const struct witness one[]={{0,0x0122},{8,0x0126},{12,0x2352},{14,0x2262}};
    static const struct witness two[]={{0,0x0222},{6,0x0236},{12,0x2242},{14,0x2352}};
    size_t s;uint32_t entry;
    if(timer>1u || handler>n || n-handler<36u) return false;
    if(!timer) {if(handler<0xaeu) return false;s=handler-0xaeu;}
    else {if(n-handler<0x62u) return false;s=handler+0x52u;}
    if(s>n || n-s<16u || !word_literal(p,n,base,s+2u,0u,timer?0x248u:0x244u) ||
       !literal(p,n,base,s+4u,timer?3u:2u,&entry) || !ram(entry,2u,2u) ||
       phys(entry)!=phys(base+(uint32_t)handler)) return false;
    if(!timer) return WORDS(p,n,s,one) &&
        exact_literal(p,n,base,s+6u,3u,callback) && exact_literal(p,n,base,s+10u,2u,argument);
    return WORDS(p,n,s,two) &&
        exact_literal(p,n,base,s+8u,3u,argument) && exact_literal(p,n,base,s+10u,2u,callback);
}
static bool timers(const uint8_t *p,size_t n,uint32_t base,uint32_t *one,uint32_t *two) {
    static const struct witness w[]={{2,0x6030},{6,0x2300},{10,0xe3df},{12,0x6241},{14,0x2239},{16,0x2421},
        {20,0x6141},{22,0x2129},{24,0x2411},{30,0x6012},{32,0x402b},{34,0x6432}};
    unsigned counts[2]={0,0};uint32_t chosen[2]={0,0};
    for(size_t s=0;s<n && n-s>=36u;s+=2u) {
        /* Most halfwords are not a dispatch prologue. Avoid entering the
         * bounded witness decoder on every instruction/data halfword while
         * retaining the complete witness and uniqueness checks below. */
        if((u16(p+s)&0xff00u)!=0xd300u || u16(p+s+2u)!=0x6030u) continue;
        if(!WORDS(p,n,s,w)) continue;
        uint32_t tstr,tcr,callback,arg;
        if(!literal(p,n,base,s,3u,&tstr) || tstr!=0xffd80004u || !literal(p,n,base,s+8u,4u,&tcr) ||
           !word_literal(p,n,base,s+18u,2u,0xfeffu) || !literal(p,n,base,s+26u,1u,&callback) ||
           !literal(p,n,base,s+28u,3u,&arg) || !ram(callback,8u,4u) || arg!=callback+4u) continue;
        unsigned i;
        if(u16(p+s+4u)==0xc9fdu && tcr==0xffd8001cu) i=0u;
        else if(u16(p+s+4u)==0xc9fbu && tcr==0xffd80028u) i=1u;
        else continue;
        if(!timer_registration(p,n,base,s,i,callback,arg)) continue;
        chosen[i]=callback;++counts[i];
    }
    if(counts[0]!=1u || counts[1]!=1u || overlaps(chosen[0],8u,chosen[1],8u)) return false;
    *one=chosen[0];*two=chosen[1];return true;
}
static bool gun(const uint8_t *p,size_t n,uint32_t base,const struct kui_vmu_native_discovery *d,uint32_t *out) {
    static const struct witness w[]={{0,0x2fe6},{2,0x2fd6},{4,0x0002},{6,0x2fc6},{10,0x2fb6},{12,0x4f22},
        {16,0xcbf0},{18,0x400e},{26,0x8b19},
        {30,0x6232},{32,0x7201},{34,0x2322},{38,0x0009},{42,0x0009},
        {80,0xe300},{82,0x2432},{86,0x6242},{88,0x7201},{90,0x2422}};
    unsigned count=0;uint32_t chosen=0;
    for(size_t s=0;s<n && n-s>=92u;s+=2u) {
        if(u16(p+s)!=0x2fe6u || u16(p+s+2u)!=0x2fd6u || u16(p+s+4u)!=0x0002u) continue;
        if(!WORDS(p,n,s,w)) continue;
        /* Both reviewed IRQ wrappers mask SR and test the same flag. The
         * second compiler shape exchanges R2/R3 and relocates its following
         * direct helper across a small group of extra public thunks. */
        bool old=u16(p+s+14u)==0x2039u && u16(p+s+22u)==0x6242u &&
            u16(p+s+24u)==0x2228u && u16(p+s+40u)==0xb01eu;
        bool alternate=u16(p+s+14u)==0x2029u && u16(p+s+22u)==0x6342u &&
            u16(p+s+24u)==0x2338u && u16(p+s+40u)==0xb03au;
        if((!old && !alternate) || !word_literal(p,n,base,s+8u,old?3u:2u,0xff0fu) ||
           d->completion_call<0x4au || !bsr_target(p,n,base,s+36u,d->completion_call-0x4au)) continue;
        uint32_t flag,cleared;
        if(!literal(p,n,base,s+20u,4u,&flag) || !literal(p,n,base,s+84u,4u,&cleared) || !ram(flag,4u,4u) || flag==cleared) continue;
        chosen=flag;++count;
    }
    if(count!=1u) return false;
    *out=chosen;return true;
}

/* The leaf is reached exactly once: one aligned image word holds its
 * address, one PC-relative load reads it, and the very next instruction
 * calls through that register with a no-op delay slot. */
static bool called_once(const uint8_t *p,size_t n,uint32_t base,uint32_t function) {
    unsigned words=0,loads=0;bool called=false;
    for(size_t i=0;i<n && n-i>=4u;i+=4u) if(phys(u32(p+i))==phys(function)) ++words;
    for(size_t s=0;s<n && n-s>=6u;s+=2u) {
        uint16_t code=u16(p+s);unsigned reg=(code>>8)&15u;uint32_t value;
        if((code&0xf000u)!=0xd000u || !literal(p,n,base,s,reg,&value) || phys(value)!=phys(function)) continue;
        ++loads;called=u16(p+s+2u)==(0x400bu|reg<<8) && u16(p+s+4u)==0x0009u;
    }
    return words==1u && loads==1u && called;
}
/* Second reviewed variable root: a leaf the system initializer calls once,
 * with every interrupt masked. It pushes the immediate count, loads the TX
 * storage (R7), raw RX pointer array (R6) and index array (R5), and calls
 * the upper wrapper through R0 with mode zero in the delay slot; the upper
 * wrapper receives exactly the registers the inline root supplies. */
static bool variable_leaf_root(const uint8_t *p,size_t n,uint32_t base,size_t s,uint32_t upper,
    uint32_t *input,uint32_t *index,uint32_t *tx,uint32_t *count) {
    static const struct witness w[]={{0,0x4f22},{4,0x2f06},{14,0x400b},{16,0xe400},
        {18,0x7f04},{20,0x4f26},{22,0x000b},{24,0x0009}};
    uint32_t a,b,c;
    if(s>n || n-s<26u || u16(p+s)!=0x4f22u || !WORDS(p,n,s,w) || (u16(p+s+2u)&0xff00u)!=0xe000u ||
       !literal(p,n,base,s+6u,7u,&c) || !literal(p,n,base,s+8u,6u,&a) || !literal(p,n,base,s+10u,5u,&b) ||
       !target(p,n,base,s+12u,0u,upper) || !called_once(p,n,base,base+(uint32_t)s)) return false;
    *input=a;*index=b;*tx=c;*count=(uint32_t)(int32_t)(int8_t)(u16(p+s+2u)&255u);return true;
}
/* Called only after an exact image-bound driver resolution, either produced
 * immediately by the combined API or replayed by the strict cached API.
 * `leaf` also admits the leaf root; see layout_from_bound. */
static bool variable_from_bound(const void *image,size_t n,
    const struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *out,bool leaf) {
    uint32_t base=d->load_address;
    const uint8_t *p=image;size_t init;
    if(!at(d->initializer+0x11cu,base,n,0x148u,&init) || !variable(p,n,base,init,d)) return false;
    struct kui_vmu_native_layout r={0};r.version=1u;r.bytes=sizeof(r);r.initializer=base+(uint32_t)init;r.rx_layout=KUI_VMU_NATIVE_RX_EXPLICIT48;
    /* The unique wrapper saves four actual argument registers, masks IRQs,
     * then reloads every argument immediately before its initializer call. */
    static const struct witness wrap[]={{0,0x4f22},{2,0x7fec},{4,0x0002},{8,0x1f43},{10,0x4009},{12,0x1f52},{14,0x4009},
        {16,0x1f61},{18,0xc90f},{20,0x2f72},{22,0x1f04},{24,0x0002},{26,0x2039},{28,0xcbf0},{30,0x400e},
        {36,0x6242},{38,0x223b},{42,0x2422},{44,0x55f2},{48,0x67f2},{50,0x56f1},{52,0x430b},{54,0x54f3}};
    unsigned found=0;size_t wrapper=0;
    for(size_t s=0;s<n && n-s>=56u;s+=2u) if(u16(p+s)==0x4f22u &&
       u16(p+s+2u)==0x7fecu && u16(p+s+4u)==0x0002u && WORDS(p,n,s,wrap) &&
       word_literal(p,n,base,s+6u,3u,0xff0fu) && target(p,n,base,s+32u,4u,d->lock_cell) &&
       exact_literal(p,n,base,s+34u,3u,0xa0000000u) && bsr_code(p,n,base,s+40u) &&
       target(p,n,base,s+46u,3u,r.initializer)) {wrapper=s;++found;}
    if(found!=1u) return false;
    r.initializer_wrapper=base+(uint32_t)wrapper;
    /* Reviewed upper wrapper transports caller R5/R6/R7 and stack argument
     * without synthesizing a receive address or cardinality. */
    static const struct witness upper[]={{0,0x2fe6},{2,0x2fd6},{4,0x2fc6},{6,0x2fb6},{8,0x2fa6},{10,0x2f96},{12,0x2f86},{14,0x4f22},{16,0x7ff4},
        {20,0xee00},{24,0xe101},{30,0x1f52},{32,0x65e3},{34,0x2f62},{38,0x1f71},{40,0x2342},{42,0x22e2},
        {48,0x2012},{50,0x23e2},{52,0x420b},{54,0x64c3},{56,0x53f1},{58,0x2338},{60,0x890b},
        {62,0x63f2},{64,0x2338},{66,0x8908},{70,0xe301},{72,0x2232},{74,0x65f2},{78,0x57fb},{80,0x56f1},{82,0x430b},{84,0x54f2}};
    found=0;size_t upper_site=0;
    for(size_t s=0;s<n && n-s>=86u;s+=2u) if(u16(p+s)==0x2fe6u &&
       u16(p+s+2u)==0x2fd6u && u16(p+s+4u)==0x2fc6u && WORDS(p,n,s,upper) && upper_context(p,n,base,s,true) &&
       target(p,n,base,s+76u,3u,r.initializer_wrapper)) {upper_site=s;++found;}
    if(found!=1u) return false;
    /* Root's reviewed leaf call supplies three immutable pointers and an
     * immediate count on the stack. Its unrelated first argument is forwarded
     * to the upper wrapper's mode cell, never to the SDK initializer. */
    found=0;uint32_t tx=0,input=0,index=0,count=0;
    for(size_t s=12u;s<n && n-s>=4u;s+=2u) {
        if(u16(p+s)!=0x420bu || u16(p+s+2u)!=0x64a3u || u16(p+s-2u)!=0x2f36u ||
           (u16(p+s-10u)&0xff00u)!=0xe300u || !target(p,n,base,s-6u,2u,base+(uint32_t)upper_site)) continue;
        uint32_t a,b,c;if(!literal(p,n,base,s-12u,6u,&a) || !literal(p,n,base,s-8u,5u,&b) ||
           !literal(p,n,base,s-4u,7u,&c)) continue;
        /* Short startup-family witness. The value forwarded R13 -> R10 ->
         * R4 is deliberately not interpreted as a constant by this adapter. */
        if(s<28u || u16(p+s-20u)!=0x6ad3u || u16(p+s-18u)!=0x2029u ||
           u16(p+s-16u)!=0xcbe0u || u16(p+s-14u)!=0x400eu) continue;
        /* Upper wrapper reloads R5 from its saved caller R6 and R6 from
         * saved caller R7. The SDK stores R5 to fc8 (raw RX pointer array)
         * and R6 to fc4 (TX storage). Keep that transport explicit. */
        input=a;index=b;tx=c;count=u16(p+s-10u)&255u;r.initializer_call=base+(uint32_t)s;++found;
    }
    for(size_t s=0;leaf && !found && s<n && n-s>=26u;s+=2u) {
        uint32_t a,b,c,k;
        if(!variable_leaf_root(p,n,base,s,base+(uint32_t)upper_site,&a,&b,&c,&k)) continue;
        input=a;index=b;tx=c;count=k;r.initializer_call=base+(uint32_t)s+14u;++found;
    }
    /* This runtime family uses two 0x3000 TX banks; the variable initializer
     * computes the bank distance as input count * 1024. */
    if(found!=1u || count!=12u || !ram(tx,0x6020u,4u) ||
       !ram(input,count*4u,4u) || !ram(index,count*4u,4u)) return false;
    size_t indices,inputs;
    if(!at(index,base,n,count*4u,&indices) || !at(input,base,n,count*4u,&inputs)) return false;
    r.tx_base=((tx+31u)&~31u)|0xa0000000u;r.rx_base=((input+31u)&~31u)|0xa0000000u;
    r.inputs=count;r.input_array=input;r.index_array=index;
    for(unsigned i=0;i<count;i++) {
        uint32_t entry=u32(p+indices+4u*i);if(entry>=24u) return false;
        for(unsigned j=0;j<i;j++) if(entry==r.indices[j]) return false;
        r.indices[i]=entry;
        uint32_t raw=u32(p+inputs+4u*i);
        if(!ram(raw,0x820u,4u)) return false;
        uint32_t buffer=((raw+31u)&~31u)|0xa0000000u;
        r.input_values[i]=raw;r.rx_slots[entry]=buffer;r.rx_slots[entry+24u]=buffer+0x400u;
    }
    if(!gun(p,n,base,d,&r.gun_flag) || !timers(p,n,base,&r.timer1_callback,&r.timer2_callback)) return false;
    *out=r;return true;
}

/* Original SDK initializer: caller R4/R6 become aligned P2 TX/RX bases;
 * its constructor assigns every one of the 48 contiguous 1 KiB RX slots.
 * The optional pointer-array initializer may coexist without being called. */
static bool uniform_initializer(const uint8_t *p,size_t n,uint32_t base,
    const struct kui_vmu_native_discovery *d) {
    static const struct witness init[]={
        {0,0x2fe6},{8,0x6233},{10,0x2fd6},{12,0x2fc6},{14,0x2fb6},{16,0x2fa6},{18,0x4f22},
        {20,0x2e32},{22,0x0246},{24,0x7004},{26,0x63e2},{28,0x0366},{30,0x70fc},
        {32,0x63e2},{34,0xe61f},{36,0x023e},{38,0x2268},{40,0x8d09},{42,0xe5e0},
        {46,0x61e2},{48,0x031e},{50,0x7320},{52,0x0136},{54,0x63e2},{56,0x023e},{58,0x2259},{60,0x0326},
        {64,0x64e2},{66,0x034e},{68,0x2638},{70,0x8906},{72,0x034e},{74,0x7320},{76,0x0436},
        {78,0x63e2},{80,0x023e},{82,0x2259},{84,0x0326},
        {88,0x63e2},{92,0x023e},{94,0x224b},{96,0x0326},{98,0x7004},{100,0x63e2},{102,0x023e},{104,0x224b},{106,0x0326},
        {110,0x430b},{112,0x0009}
    };
    static const struct witness construct[]={
        {0,0x2fe6},{2,0x2fd6},{4,0x2fc6},{6,0x2fb6},{8,0x2fa6},{10,0x2f96},{12,0x2f86},{14,0x4f22},{16,0x7ff8},
        {20,0xed00},{24,0x64d3},{26,0x63e2},{28,0xec60},{32,0x67d3},{34,0x03d6},{36,0x7004},
        {38,0x63e2},{42,0x03d6},{46,0x63e2},{50,0x013e},{52,0x323c},{54,0x317c},{56,0x324c},{58,0x2212},
        {62,0x63e2},{64,0x013e},{66,0x323c},{68,0x324c},{70,0x7404},{72,0x316c},{74,0x34c2},{76,0x7260},{78,0x2212},
        {80,0x365c},{82,0x8feb},{84,0x375c},{86,0x62e2},{88,0xe608},{92,0xe500},{94,0x6323},
        {96,0x013e},{100,0x0216},{102,0x63e2},{106,0x6233},{110,0x022e},{114,0x321c},{116,0x0326},
        {118,0x70fc},{120,0x63e2},{122,0x6233},{124,0x022e},{128,0x0326}
    };
    size_t s,c;uint32_t alias,constructor;
    if(!at(d->initializer,base,n,114u,&s) || !WORDS(p,n,s,init) ||
       !target(p,n,base,s+2u,14u,d->manager_cell) || !target(p,n,base,s+4u,3u,d->manager_initial) ||
       !word_literal(p,n,base,s+6u,0u,0xfc4u) || !word_literal(p,n,base,s+44u,0u,0xfc4u) ||
       !word_literal(p,n,base,s+62u,0u,0xfc8u) || !word_literal(p,n,base,s+86u,0u,0xfc4u) ||
       !literal(p,n,base,s+90u,4u,&alias) || alias!=0xa0000000u ||
       !literal(p,n,base,s+108u,3u,&constructor) || !at(constructor,base,n,130u,&c) || !WORDS(p,n,c,construct)) return false;
    return target(p,n,base,c+18u,14u,d->manager_cell) &&
        word_literal(p,n,base,c+22u,0u,0x10b4u) && word_literal(p,n,base,c+30u,6u,0x6000u) &&
        word_literal(p,n,base,c+40u,5u,0x400u) && word_literal(p,n,base,c+44u,0u,0xfc8u) &&
        word_literal(p,n,base,c+48u,2u,0xfe8u) && word_literal(p,n,base,c+60u,2u,0xfe8u) &&
        word_literal(p,n,base,c+90u,0u,0xfc4u) && word_literal(p,n,base,c+98u,0u,0x10a8u) &&
        word_literal(p,n,base,c+104u,0u,0xfc4u) && word_literal(p,n,base,c+108u,1u,0x3000u) &&
        word_literal(p,n,base,c+112u,0u,0x10acu) && word_literal(p,n,base,c+126u,0u,0xfccu);
}
static bool uniform_root(const uint8_t *p,size_t n,uint32_t base,size_t call,
    uint32_t upper,uint32_t *rx_slot,uint32_t *rx,uint32_t *tx_slot,uint32_t *tx) {
    if(call<8u || call>n || n-call<4u || (u16(p+call)&0xf0ffu)!=0x400bu) return false;
    unsigned target_reg=(u16(p+call)>>8)&15u;
    /* Equivalent reviewed caller allocations use R0, R2 or R3 for the
     * indirect target. RX/TX/mode remain R5/R6/R4; no inherited register or
     * value inferred after the call may authorize its sampled target. */
    if(target_reg!=0u && target_reg!=2u && target_reg!=3u) return false;
    /* Two complete caller continuations supply the sampled target and both
     * buffer arguments in three literal loads, then mode zero in the JSR
     * delay. The first follows an actual prior JSR/R4-stack-reload pair;
     * the second follows the complete R14..R8/PR entry prologue. Their stack
     * value cannot supply a buffer or mode. An unknown preceding branch is
     * deliberately not skipped: it is outside both reviewed continuations.
     * In particular this cannot manufacture a second root after a loop. */
    static const struct witness entry[]={{0,0x2fe6},{2,0x2fd6},{4,0x2fc6},
        {6,0x2fb6},{8,0x2fa6},{10,0x2f96},{12,0x2f86},{14,0x4f22}};
    bool after_call=call>=10u && (u16(p+call-10u)&0xf0ffu)==0x400bu &&
        (u16(p+call-8u)&0xfff0u)==0x54f0u;
    bool after_entry=call>=22u && WORDS(p,n,call-22u,entry);
    if((after_call || after_entry) && u16(p+call+2u)==0xe400u &&
       target(p,n,base,call-6u,target_reg,upper) &&
       literal(p,n,base,call-4u,5u,rx) && literal(p,n,base,call-2u,6u,tx)) {
        *rx_slot=((base+(uint32_t)call)&~3u)+4u*(u16(p+call-4u)&255u);
        *tx_slot=((base+(uint32_t)call+2u)&~3u)+4u*(u16(p+call-2u)&255u);
        return true;
    }
    unsigned known=0;
    for(size_t s=call-8u;s<=call+2u;s+=2u) {
        /* SH samples the indirect target before executing its delay slot.
         * Argument registers may be prepared there, but the target must name
         * the proved upper wrapper when JSR itself executes. */
        if(s==call) {if(!(known&(1u<<target_reg))) return false;continue;}
        uint16_t op=u16(p+s);
        if(op==0x0009u) continue;
        if(op==0xe400u) {if(known&16u) return false;known|=16u;continue;}
        unsigned reg=(op>>8)&15u;uint32_t value;
        if((op&0xf000u)!=0xd000u || (reg!=target_reg && reg!=5u && reg!=6u) ||
           (known&(1u<<reg)) || !literal(p,n,base,s,reg,&value)) return false;
        known|=1u<<reg;
        if(reg==target_reg) {if(phys(value)!=phys(upper)) return false;}
        else {
            uint32_t slot=((base+(uint32_t)s+4u)&~3u)+4u*(op&255u);
            if(reg==5u) {*rx_slot=slot;*rx=value;} else {*tx_slot=slot;*tx=value;}
        }
    }
    return known==(0x70u|(1u<<target_reg));
}
static bool uniform_from_bound(const void *image,size_t n,
    const struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *out) {
    const uint8_t *p=image;uint32_t base=d->load_address;
    if(!uniform_initializer(p,n,base,d)) return false;
    static const struct witness wrap[]={
        {0,0x4f22},{2,0x7ff4},{4,0x0002},{8,0x2f42},{10,0x4009},{12,0x1f51},{14,0x4009},
        {16,0xc90f},{18,0x1f02},{20,0x0002},{22,0x2039},{24,0xcbf0},{26,0x400e},
        {32,0x6242},{34,0x223b},{38,0x2422},{44,0x66f2},{46,0x6573},{48,0x430b},{50,0x54f1}
    };
    unsigned found=0;size_t wrapper=0;
    for(size_t s=0;s<n && n-s>=52u;s+=2u) {
        uint32_t alias,capacity;
        if(u16(p+s)!=0x4f22u || u16(p+s+2u)!=0x7ff4u || !WORDS(p,n,s,wrap) ||
           !word_literal(p,n,base,s+6u,3u,0xff0fu) || !target(p,n,base,s+28u,4u,d->lock_cell) ||
           !literal(p,n,base,s+30u,3u,&alias) || alias!=0xa0000000u ||
           !literal(p,n,base,s+40u,7u,&capacity) || capacity!=0xc000u ||
           !bsr_code(p,n,base,s+36u) ||
           !target(p,n,base,s+42u,3u,d->initializer)) continue;
        wrapper=s;if(++found>1u) return false;
    }
    if(found!=1u) return false;
    static const struct witness upper[]={
        {0,0x2fe6},{2,0x2fd6},{4,0x2fc6},{6,0x2fb6},{8,0x2fa6},{10,0x2f96},{12,0x2f86},{14,0x4f22},{16,0x7ff8},
        {20,0xee00},{24,0xe101},{30,0x1f51},{32,0x65e3},{34,0x2f62},{36,0x2342},{40,0x22e2},
        {46,0x2012},{48,0x23e2},{50,0x420b},{52,0x64c3},{54,0x63f2},{56,0x2338},{58,0x8909},
        {60,0x53f1},{62,0x2338},{64,0x8906},{68,0xe301},{70,0x2232},{74,0x65f2},{76,0x430b},{78,0x54f1}
    };
    found=0;size_t upper_site=0;
    for(size_t s=0;s<n && n-s>=80u;s+=2u) if(u16(p+s)==0x2fe6u && u16(p+s+2u)==0x2fd6u &&
       WORDS(p,n,s,upper) && upper_context(p,n,base,s,false) && target(p,n,base,s+72u,3u,base+(uint32_t)wrapper)) {
        upper_site=s;if(++found>1u) return false;
    }
    if(found!=1u) return false;
    struct kui_vmu_native_layout r={0};uint32_t rx=0,tx=0;found=0;
    for(size_t s=8u;s<n && n-s>=4u;s+=2u) {
        uint32_t rx_at=0,raw_rx=0,tx_at=0,raw_tx=0;
        if((u16(p+s)&0xf0ffu)!=0x400bu ||
           !uniform_root(p,n,base,s,base+(uint32_t)upper_site,&rx_at,&raw_rx,&tx_at,&raw_tx)) continue;
        r.input_array=rx_at;rx=raw_rx;r.index_array=tx_at;tx=raw_tx;
        r.initializer_call=base+(uint32_t)s;if(++found>1u) return false;
    }
    if(found!=1u || !ram(rx,0xc020u,4u) || !ram(tx,0x6020u,4u)) return false;
    r.version=1u;r.bytes=sizeof(r);r.initializer=d->initializer;r.initializer_wrapper=base+(uint32_t)wrapper;
    r.rx_layout=KUI_VMU_NATIVE_RX_UNIFORM48;r.inputs=1u;r.input_values[0]=rx;r.indices[0]=tx;
    r.rx_base=((rx+31u)&~31u)|0xa0000000u;r.tx_base=((tx+31u)&~31u)|0xa0000000u;
    for(unsigned i=0;i<48u;i++) r.rx_slots[i]=r.rx_base+1024u*i;
    if(!gun(p,n,base,d,&r.gun_flag) || !timers(p,n,base,&r.timer1_callback,&r.timer2_callback)) return false;
    *out=r;return true;
}
/* SDK rings are derived from admitted initializer arguments, never inferred
 * from zero runs. A source-file container may include their zero-initialized
 * storage. Require a complete ring inside that container or a disjoint ring;
 * a partial boundary crossing is not this provenance family. */
static bool ring_image_location(uint32_t load,uint32_t image_bytes,uint32_t address,
    uint32_t bytes,bool *inside) {
    if(!inside || !ram(load,image_bytes,4u) || !ram(address,bytes,32u)) return false;
    *inside=false;
    if(!overlaps(address,bytes,load,image_bytes)) return true;
    uint32_t start=phys(address),begin=phys(load);
    if(start<begin || start-begin>image_bytes || bytes>image_bytes-(start-begin)) return false;
    *inside=true;return true;
}
static bool source_ring(const uint8_t *p,size_t n,uint32_t load,uint32_t address,
    uint32_t bytes,bool *inside) {
    if(n>UINT32_MAX || !ring_image_location(load,(uint32_t)n,address,bytes,inside)) return false;
    if(!*inside) return true;
    size_t at_=phys(address)-phys(load);
    for(uint32_t i=0;i<bytes;i++) if(p[at_+i]) return false;
    return true;
}
static bool source_buffers(const uint8_t *p,size_t n,const struct kui_vmu_native_discovery *d,
    const struct kui_vmu_native_layout *l,bool *infile) {
    struct span {uint32_t address,bytes;} rings[KUI_VMU_NATIVE_LAYOUT_INPUTS+1u];
    unsigned count=1u;rings[0]=(struct span){l->tx_base,0x6000u};*infile=false;
    bool uniform=l->rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48;
    if(l->rx_layout>KUI_VMU_NATIVE_RX_EXPLICIT48 || l->inputs!=(uniform?1u:12u)) return false;
    if(uniform) rings[count++]=(struct span){l->rx_base,0xc000u};
    else for(unsigned i=0;i<l->inputs;i++) {
        if(l->indices[i]>=24u) return false;
        rings[count++]=(struct span){l->rx_slots[l->indices[i]],0x800u};
    }
    const struct span protected[]={
        {d->manager_initial,0x1100u},{d->manager_cell,4u},{d->lock_cell,5u},
        {l->gun_flag,4u},{l->timer1_callback,8u},{l->timer2_callback,8u},
        {l->input_array,l->inputs*4u},{l->index_array,(uniform?1u:l->inputs)*4u},
        {d->submit,12u},{d->initializer,12u},{l->initializer,12u},
        {l->initializer_wrapper,uniform?52u:56u},{l->initializer_call,4u},
        {d->completion_call,4u},{d->consumer,2u},{d->gate,2u},{d->ready,2u}};
    for(unsigned i=0;i<count;i++) {
        bool inside;
        if(!source_ring(p,n,d->load_address,rings[i].address,rings[i].bytes,&inside)) return false;
        *infile|=inside;
        for(unsigned j=0;j<i;j++)
            if(overlaps(rings[i].address,rings[i].bytes,rings[j].address,rings[j].bytes)) return false;
        for(unsigned j=0;j<sizeof(protected)/sizeof(protected[0]);j++)
            if(overlaps(rings[i].address,rings[i].bytes,protected[j].address,protected[j].bytes)) return false;
    }
    return true;
}
static bool layout_from_bound(const void *image,size_t n,
    const struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *out) {
    struct kui_vmu_native_layout variable_layout,uniform_layout;
    bool variable_found=variable_from_bound(image,n,d,&variable_layout,false);
    bool uniform_found=uniform_from_bound(image,n,d,&uniform_layout);
    /* The leaf root is tried only where no reviewed family resolves. Every
     * image with an earlier proof keeps exactly that proof, so cached
     * records stay current without a detector-epoch change. */
    if(!variable_found && !uniform_found)
        variable_found=variable_from_bound(image,n,d,&variable_layout,true);
    if(variable_found==uniform_found) return false;
    struct kui_vmu_native_layout chosen=variable_found?variable_layout:uniform_layout;bool infile;
    if(!source_buffers(image,n,d,&chosen,&infile)) return false;
    chosen.version=infile?KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION:KUI_VMU_NATIVE_LAYOUT_VERSION;
    *out=chosen;return true;
}

static bool main_ram_object(uintptr_t address,size_t bytes,uint32_t *physical) {
#if UINTPTR_MAX > UINT32_MAX
    if(address>UINT32_MAX) return false;
#endif
    uint32_t a=(uint32_t)address,area=a&0xe0000000u,p=phys(a);
    if((area!=0u && area!=0x80000000u && area!=0xa0000000u) ||
       p<0x0c000000u || p>=0x0d000000u || bytes>0x0d000000u-p) return false;
    *physical=p;return true;
}
static bool object_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t ap=(uintptr_t)a,bp=(uintptr_t)b;
    if(ap<bp+bn && bp<ap+an) return true;
    uint32_t ar,br;
    return main_ram_object(ap,an,&ar) && main_ram_object(bp,bn,&br) && ar<br+bn && br<ar+an;
}
static bool layout_arguments(const void *image,size_t n,
    const struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *out) {
    return image && d && out && n>=0x300u && n<=0xc00000u &&
        (uintptr_t)image<=UINTPTR_MAX-n && (uintptr_t)d<=UINTPTR_MAX-sizeof(*d) &&
        (uintptr_t)out<=UINTPTR_MAX-sizeof(*out) &&
        !object_overlap(image,n,out,sizeof(*out)) && !object_overlap(d,sizeof(*d),out,sizeof(*out));
}
bool kui_vmu_native_layout_discover(const void *image,size_t n,
    const struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *out) {
    if(!layout_arguments(image,n,d,out)) return false;
    memset(out,0,sizeof(*out));
    return kui_vmu_native_discovery_cache_valid(d,image,n,d->load_address) && layout_from_bound(image,n,d,out);
}
bool kui_vmu_native_discover_layout(const void *image,size_t n,uint32_t load,
    struct kui_vmu_native_discovery *driver,struct kui_vmu_native_layout *out) {
    if(!layout_arguments(image,n,driver,out) || object_overlap(image,n,driver,sizeof(*driver))) return false;
    memset(driver,0,sizeof(*driver));memset(out,0,sizeof(*out));
    struct kui_vmu_native_discovery fresh;
    struct kui_vmu_native_layout layout;
    if(kui_vmu_native_discover(image,n,load,&fresh)!=KUI_VMU_NATIVE_RESOLVED ||
       !layout_from_bound(image,n,&fresh,&layout)) return false;
    *driver=fresh;*out=layout;return true;
}

static bool bind_failure(struct kui_vmu_native_layout_diagnostic *diag,uint32_t code,
    uint32_t address,uint32_t value,uint32_t expected) {
    if(diag) *diag=(struct kui_vmu_native_layout_diagnostic){code,address,value,expected};
    return false;
}
bool kui_vmu_native_layout_bind_checked(const struct kui_vmu_native_layout *l,const struct kui_vmu_native_discovery *d,
    kui_vmu_native_runtime_read read,void *context,struct kui_vmu_native_runtime_config *out,
    struct kui_vmu_native_layout_diagnostic *diag) {
    if(diag) memset(diag,0,sizeof(*diag));
    if(!l || !d || !read || !out) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_ARGUMENT,0u,0u,1u);
#define SCHEMA(field,want) do {uint32_t expect=(want);if((field)!=expect) \
    return bind_failure(diag,KUI_VMU_LAYOUT_BIND_SCHEMA,(uint32_t)(uintptr_t)&(field),(field),expect);} while(0)
#define SPAN(address,bytes,align) do {if(!ram((address),(bytes),(align))) \
    return bind_failure(diag,KUI_VMU_LAYOUT_BIND_SPAN,(address),(bytes),(align));} while(0)
    bool infile=l->version==KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION;
    if(l->version!=KUI_VMU_NATIVE_LAYOUT_VERSION && !infile)
        return bind_failure(diag,KUI_VMU_LAYOUT_BIND_SCHEMA,(uint32_t)(uintptr_t)&l->version,l->version,KUI_VMU_NATIVE_LAYOUT_VERSION);
    SCHEMA(l->bytes,sizeof(*l));SCHEMA(d->version,1u);SCHEMA(d->bytes,sizeof(*d));
    SCHEMA(d->family,KUI_VMU_NATIVE_FAMILY_UNIFORM_V1);SCHEMA(d->requirements,KUI_VMU_NATIVE_REQUIREMENTS);
    SCHEMA(out->version,1u);SCHEMA(out->bytes,sizeof(*out));SCHEMA(out->lease_header_bytes,32u);
    bool uniform=l->rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48;
    if(l->rx_layout>KUI_VMU_NATIVE_RX_EXPLICIT48)
        return bind_failure(diag,KUI_VMU_LAYOUT_BIND_SCHEMA,0u,l->rx_layout,KUI_VMU_NATIVE_RX_EXPLICIT48);
    SCHEMA(l->inputs,uniform?1u:12u);
    SCHEMA(l->rx_base,(((uniform?l->input_values[0]:l->input_array)+31u)&~31u)|0xa0000000u);
    if(uniform) {SCHEMA(l->tx_base,((l->indices[0]+31u)&~31u)|0xa0000000u);}
    SPAN(d->load_address,d->image_bytes,4u);SPAN(d->manager_cell,4u,4u);SPAN(d->lock_cell,5u,4u);
    SPAN(out->lease_address,out->lease_bytes,32u);
    SPAN(out->lease_address-out->lease_header_bytes,out->lease_bytes+out->lease_header_bytes,1u);
    SPAN(l->input_array,l->inputs*4u,4u);SPAN(l->tx_base,0x6000u,32u);SPAN(d->manager_initial,0x1100u,4u);
    if(uniform) {SPAN(l->index_array,4u,4u);SPAN(l->input_values[0],0xc020u,4u);SPAN(l->indices[0],0x6020u,4u);}
    SPAN(l->gun_flag,4u,4u);SPAN(l->timer1_callback,8u,4u);SPAN(l->timer2_callback,8u,4u);
    if(infile && !uniform) {SPAN(l->index_array,l->inputs*4u,4u);}
#undef SPAN
#undef SCHEMA
    uint32_t guard=out->lease_address-out->lease_header_bytes,guard_bytes=out->lease_bytes+out->lease_header_bytes;
    bool tx_inside,any_inside;
    if(!ring_image_location(d->load_address,d->image_bytes,l->tx_base,0x6000u,&tx_inside) ||
       (tx_inside && !infile))
        return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,l->tx_base,d->load_address,d->image_bytes);
    any_inside=tx_inside;
    enum {BLOCK_TX=1,BLOCK_IMAGE=4};
    const uint32_t blocked[][2]={{guard,guard_bytes},{l->tx_base,0x6000u},{d->manager_initial,0x1100u},{d->lock_cell,5u},
        {d->load_address,d->image_bytes},{l->gun_flag,4u},{l->timer1_callback,8u},{l->timer2_callback,8u},
        {d->manager_cell,4u}};
    for(unsigned j=0;j<sizeof(blocked)/sizeof(blocked[0]);j++)
        if(j!=BLOCK_TX && !(j==BLOCK_IMAGE && infile && tx_inside) &&
           overlaps(l->tx_base,0x6000u,blocked[j][0],blocked[j][1]))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,l->tx_base,blocked[j][0],blocked[j][1]);
    /* The reviewed caller passes the original 4-byte-aligned immutable
     * pointer array. The SDK stores an aligned alias in fc8, but its loop
     * still reads the original saved R5, not the fc8 alias. */
    uint32_t array_offset=phys(l->input_array)-phys(d->load_address);
    if(phys(l->input_array)<phys(d->load_address) || array_offset>d->image_bytes ||
       l->inputs*4u>d->image_bytes-array_offset)
        return bind_failure(diag,KUI_VMU_LAYOUT_BIND_ARRAY_LOCATION,l->input_array,array_offset,d->image_bytes);
    for(unsigned j=0;j<sizeof(blocked)/sizeof(blocked[0]);j++)
        if(j!=BLOCK_IMAGE && overlaps(l->input_array,l->inputs*4u,blocked[j][0],blocked[j][1]))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_ARRAY_CONFLICT,l->input_array,blocked[j][0],blocked[j][1]);
    if(infile && !uniform) {
        uint32_t offset=phys(l->index_array)-phys(d->load_address);
        if(phys(l->index_array)<phys(d->load_address) || offset>d->image_bytes ||
           l->inputs*4u>d->image_bytes-offset)
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_ARRAY_LOCATION,l->index_array,offset,d->image_bytes);
        for(unsigned j=0;j<sizeof(blocked)/sizeof(blocked[0]);j++)
            if(j!=BLOCK_IMAGE && overlaps(l->index_array,l->inputs*4u,blocked[j][0],blocked[j][1]))
                return bind_failure(diag,KUI_VMU_LAYOUT_BIND_ARRAY_CONFLICT,l->index_array,blocked[j][0],blocked[j][1]);
    }
    struct kui_vmu_native_runtime_config c=*out;
    c.rx_layout=l->rx_layout;c.rx_base=l->rx_base;c.gun_flag=l->gun_flag;
    c.timer1_callback=l->timer1_callback;c.timer2_callback=l->timer2_callback;memset(c.rx_slots,0,sizeof(c.rx_slots));
    uint32_t raw[24]={0};
    if(uniform) {
        bool rx_inside;
        if(!ring_image_location(d->load_address,d->image_bytes,l->rx_base,0xc000u,&rx_inside) ||
           (rx_inside && !infile))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,l->rx_base,d->load_address,d->image_bytes);
        any_inside|=rx_inside;
        if(overlaps(l->rx_base,0xc000u,l->input_array,4u) || overlaps(l->rx_base,0xc000u,l->index_array,4u))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,l->rx_base,l->input_array,4u);
        uint32_t offset=phys(l->index_array)-phys(d->load_address);
        if(phys(l->index_array)<phys(d->load_address) || offset>d->image_bytes || d->image_bytes-offset<4u)
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_ARRAY_LOCATION,l->index_array,offset,d->image_bytes);
        for(unsigned j=0;j<sizeof(blocked)/sizeof(blocked[0]);j++) {
            if(j!=BLOCK_IMAGE && overlaps(l->index_array,4u,blocked[j][0],blocked[j][1]))
                return bind_failure(diag,KUI_VMU_LAYOUT_BIND_ARRAY_CONFLICT,l->index_array,blocked[j][0],blocked[j][1]);
            if(!(j==BLOCK_IMAGE && infile && rx_inside) && overlaps(l->rx_base,0xc000u,blocked[j][0],blocked[j][1]))
                return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,l->rx_base,blocked[j][0],blocked[j][1]);
        }
        if(!read(context,l->input_array,4u,&raw[0]))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INPUT_READ,l->input_array,0u,l->input_values[0]);
        if(raw[0]!=l->input_values[0])
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INPUT_VALUE,l->input_array,raw[0],l->input_values[0]);
        if(!read(context,l->index_array,4u,&raw[1]))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INPUT_READ,l->index_array,0u,l->indices[0]);
        if(raw[1]!=l->indices[0])
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INPUT_VALUE,l->index_array,raw[1],l->indices[0]);
        for(unsigned i=0;i<48u;i++) c.rx_slots[i]=l->rx_base+1024u*i;
    } else for(unsigned i=0;i<l->inputs;i++) {
        if(l->indices[i]>=24u) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INDEX,l->index_array+4u*i,l->indices[i],24u);
        for(unsigned j=0;j<i;j++) if(l->indices[i]==l->indices[j])
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INDEX,l->index_array+4u*i,l->indices[i],l->indices[j]);
        if(!read(context,l->input_array+4u*i,4u,&raw[i]))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INPUT_READ,l->input_array+4u*i,0u,l->input_values[i]);
        if(raw[i]!=l->input_values[i])
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INPUT_VALUE,l->input_array+4u*i,raw[i],l->input_values[i]);
        if(!ram(raw[i],0x820u,4u)) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_SPAN,raw[i],0x820u,4u);
        uint32_t buffer=((raw[i]+31u)&~31u)|0xa0000000u;
        if(!ram(buffer,0x800u,32u)) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_SPAN,buffer,0x800u,32u);
        bool rx_inside;
        if(!ring_image_location(d->load_address,d->image_bytes,buffer,0x800u,&rx_inside) || (rx_inside && !infile))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,buffer,d->load_address,d->image_bytes);
        any_inside|=rx_inside;
        for(unsigned j=0;j<sizeof(blocked)/sizeof(blocked[0]);j++)
            if(!(j==BLOCK_IMAGE && infile && rx_inside) && overlaps(buffer,0x800u,blocked[j][0],blocked[j][1]))
                return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,buffer,blocked[j][0],blocked[j][1]);
        if(overlaps(buffer,0x800u,l->input_array,l->inputs*4u))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,buffer,l->input_array,l->inputs*4u);
        if(infile && overlaps(buffer,0x800u,l->index_array,l->inputs*4u))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,buffer,l->index_array,l->inputs*4u);
        for(unsigned j=0;j<i;j++) if(overlaps(buffer,0x800u,c.rx_slots[l->indices[j]],0x800u))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,buffer,c.rx_slots[l->indices[j]],0x800u);
        c.rx_slots[l->indices[i]]=buffer;c.rx_slots[l->indices[i]+24u]=buffer+0x400u;
    }
    if(infile && !any_inside)
        return bind_failure(diag,KUI_VMU_LAYOUT_BIND_SCHEMA,(uint32_t)(uintptr_t)&l->version,l->version,KUI_VMU_NATIVE_LAYOUT_VERSION);
    for(unsigned i=0;i<48u;i++) if(c.rx_slots[i]!=l->rx_slots[i])
        return bind_failure(diag,KUI_VMU_LAYOUT_BIND_SLOT_PROVENANCE,d->manager_initial+0xfe8u+4u*i,c.rx_slots[i],l->rx_slots[i]);
    uint32_t value;
    if(!read(context,d->manager_cell,4u,&value)) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_MANAGER_READ,d->manager_cell,0u,d->manager_initial);
    if(!ram(value,0x1100u,4u) || phys(value)!=phys(d->manager_initial))
        return bind_failure(diag,KUI_VMU_LAYOUT_BIND_MANAGER_VALUE,d->manager_cell,value,d->manager_initial);
    uint32_t manager=value;
    if(!read(context,manager+0xfc4u,4u,&value)) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_TX_READ,manager+0xfc4u,0u,l->tx_base);
    if(value!=l->tx_base) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_TX_VALUE,manager+0xfc4u,value,l->tx_base);
    if(!read(context,manager+0xfc8u,4u,&value)) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_RX_READ,manager+0xfc8u,0u,l->rx_base);
    if(value!=l->rx_base) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_RX_VALUE,manager+0xfc8u,value,l->rx_base);
    for(unsigned i=0;i<48u;i++) {
        uint32_t address=manager+0xfe8u+4u*i;
        if(!read(context,address,4u,&value)) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_SLOT_READ,address,0u,c.rx_slots[i]);
        if(value!=c.rx_slots[i]) return bind_failure(diag,KUI_VMU_LAYOUT_BIND_SLOT_VALUE,address,value,c.rx_slots[i]);
    }
    for(unsigned i=0;i<l->inputs;i++) {
        value=0u;
        if(!read(context,l->input_array+4u*i,4u,&value) || value!=raw[i])
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INPUT_CHANGED,l->input_array+4u*i,value,raw[i]);
    }
    if(uniform && (!read(context,l->index_array,4u,&value) || value!=raw[1]))
        return bind_failure(diag,KUI_VMU_LAYOUT_BIND_INPUT_CHANGED,l->index_array,value,raw[1]);
    *out=c;return true;
}
bool kui_vmu_native_layout_bind(const struct kui_vmu_native_layout *l,const struct kui_vmu_native_discovery *d,
    kui_vmu_native_runtime_read read,void *context,struct kui_vmu_native_runtime_config *out) {
    return kui_vmu_native_layout_bind_checked(l,d,read,context,out,NULL);
}
