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
static bool bsr_target(const uint8_t *p,size_t n,uint32_t base,size_t instruction,uint32_t expected) {
    if(instruction>n || n-instruction<2u) return false;
    uint16_t code=u16(p+instruction);int32_t displacement=(int32_t)((code&4095u)^2048u)-2048;
    return (code&0xf000u)==0xb000u && phys(base+(uint32_t)instruction+4u+(uint32_t)(2*displacement))==phys(expected);
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
        chosen[i]=callback;++counts[i];
    }
    if(counts[0]!=1u || counts[1]!=1u || overlaps(chosen[0],8u,chosen[1],8u)) return false;
    *one=chosen[0];*two=chosen[1];return true;
}
static bool gun(const uint8_t *p,size_t n,uint32_t base,const struct kui_vmu_native_discovery *d,uint32_t *out) {
    static const struct witness w[]={{0,0x2fe6},{2,0x2fd6},{4,0x0002},{6,0x2fc6},{10,0x2fb6},{12,0x4f22},
        {14,0x2039},{16,0xcbf0},{18,0x400e},{22,0x6242},{24,0x2228},{26,0x8b19},
        {30,0x6232},{32,0x7201},{34,0x2322},{38,0x0009},{40,0xb01e},{42,0x0009},
        {80,0xe300},{82,0x2432},{86,0x6242},{88,0x7201},{90,0x2422}};
    unsigned count=0;uint32_t chosen=0;
    for(size_t s=0;s<n && n-s>=92u;s+=2u) {
        if(u16(p+s)!=0x2fe6u || u16(p+s+2u)!=0x2fd6u || u16(p+s+4u)!=0x0002u) continue;
        if(!WORDS(p,n,s,w) || !word_literal(p,n,base,s+8u,3u,0xff0fu) ||
           d->completion_call<0x4au || !bsr_target(p,n,base,s+36u,d->completion_call-0x4au)) continue;
        uint32_t flag,cleared;
        if(!literal(p,n,base,s+20u,4u,&flag) || !literal(p,n,base,s+84u,4u,&cleared) || !ram(flag,4u,4u) || flag==cleared) continue;
        chosen=flag;++count;
    }
    if(count!=1u) return false;
    *out=chosen;return true;
}

/* Called only after an exact image-bound driver resolution, either produced
 * immediately by the combined API or replayed by the strict cached API. */
static bool layout_from_bound(const void *image,size_t n,
    const struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *out) {
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
       word_literal(p,n,base,s+6u,3u,0xff0fu) && target(p,n,base,s+46u,3u,r.initializer)) {wrapper=s;++found;}
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
       u16(p+s+2u)==0x2fd6u && u16(p+s+4u)==0x2fc6u && WORDS(p,n,s,upper) &&
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
    SCHEMA(l->version,1u);SCHEMA(l->bytes,sizeof(*l));SCHEMA(d->version,1u);SCHEMA(d->bytes,sizeof(*d));
    SCHEMA(d->family,KUI_VMU_NATIVE_FAMILY_UNIFORM_V1);SCHEMA(d->requirements,KUI_VMU_NATIVE_REQUIREMENTS);
    SCHEMA(out->version,1u);SCHEMA(out->bytes,sizeof(*out));SCHEMA(out->lease_header_bytes,32u);
    SCHEMA(l->inputs,12u);SCHEMA(l->rx_layout,KUI_VMU_NATIVE_RX_EXPLICIT48);
    SCHEMA(l->rx_base,((l->input_array+31u)&~31u)|0xa0000000u);
    SPAN(d->load_address,d->image_bytes,4u);SPAN(d->manager_cell,4u,4u);SPAN(d->lock_cell,5u,4u);
    SPAN(out->lease_address,out->lease_bytes,32u);
    SPAN(out->lease_address-out->lease_header_bytes,out->lease_bytes+out->lease_header_bytes,1u);
    SPAN(l->input_array,l->inputs*4u,4u);SPAN(l->tx_base,0x6000u,32u);SPAN(d->manager_initial,0x1100u,4u);
    SPAN(l->gun_flag,4u,4u);SPAN(l->timer1_callback,4u,4u);SPAN(l->timer2_callback,4u,4u);
#undef SPAN
#undef SCHEMA
    uint32_t guard=out->lease_address-out->lease_header_bytes,guard_bytes=out->lease_bytes+out->lease_header_bytes;
    const uint32_t blocked[][2]={{guard,guard_bytes},{l->tx_base,0x6000u},{d->manager_initial,0x1100u},{d->lock_cell,5u},
        {d->load_address,d->image_bytes},{l->gun_flag,4u},{l->timer1_callback,4u},{l->timer2_callback,4u}};
    /* The reviewed caller passes the original 4-byte-aligned immutable
     * pointer array. The SDK stores an aligned alias in fc8, but its loop
     * still reads the original saved R5, not the fc8 alias. */
    uint32_t array_offset=phys(l->input_array)-phys(d->load_address);
    if(phys(l->input_array)<phys(d->load_address) || array_offset>d->image_bytes ||
       l->inputs*4u>d->image_bytes-array_offset)
        return bind_failure(diag,KUI_VMU_LAYOUT_BIND_ARRAY_LOCATION,l->input_array,array_offset,d->image_bytes);
    for(unsigned j=0;j<sizeof(blocked)/sizeof(blocked[0]);j++)
        if(j!=4u && overlaps(l->input_array,l->inputs*4u,blocked[j][0],blocked[j][1]))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_ARRAY_CONFLICT,l->input_array,blocked[j][0],blocked[j][1]);
    struct kui_vmu_native_runtime_config c=*out;
    c.rx_layout=l->rx_layout;c.rx_base=l->rx_base;c.gun_flag=l->gun_flag;
    c.timer1_callback=l->timer1_callback;c.timer2_callback=l->timer2_callback;memset(c.rx_slots,0,sizeof(c.rx_slots));
    uint32_t raw[24]={0};
    for(unsigned i=0;i<l->inputs;i++) {
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
        for(unsigned j=0;j<sizeof(blocked)/sizeof(blocked[0]);j++)
            if(overlaps(buffer,0x800u,blocked[j][0],blocked[j][1]))
                return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,buffer,blocked[j][0],blocked[j][1]);
        if(overlaps(buffer,0x800u,l->input_array,l->inputs*4u))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,buffer,l->input_array,l->inputs*4u);
        for(unsigned j=0;j<i;j++) if(overlaps(buffer,0x800u,c.rx_slots[l->indices[j]],0x800u))
            return bind_failure(diag,KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT,buffer,c.rx_slots[l->indices[j]],0x800u);
        c.rx_slots[l->indices[i]]=buffer;c.rx_slots[l->indices[i]+24u]=buffer+0x400u;
    }
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
    *out=c;return true;
}
bool kui_vmu_native_layout_bind(const struct kui_vmu_native_layout *l,const struct kui_vmu_native_discovery *d,
    kui_vmu_native_runtime_read read,void *context,struct kui_vmu_native_runtime_config *out) {
    return kui_vmu_native_layout_bind_checked(l,d,read,context,out,NULL);
}
