/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_resolution_cache.h"
#include "kui/hash.h"
#include <string.h>

#define CACHE_HEADER_BYTES 64u
#define CACHE_PAYLOAD_END 748u
#define CACHE_DIGEST_OFFSET 992u
_Static_assert(sizeof(struct kui_vmu_native_discovery)==104u,"Update cache codec/schema for driver changes");
_Static_assert(sizeof(struct kui_vmu_native_layout)==440u,"Update cache codec/schema for layout changes");
_Static_assert(sizeof(struct kui_vmu_native_heap)==72u,"Update cache codec/schema for heap changes");
_Static_assert(sizeof(struct kui_vmu_native_heap_call)==20u,"Update cache codec/schema for call changes");
_Static_assert(sizeof(struct kui_vmu_native_exit)==48u,"Update cache codec/schema for exit changes");
static const uint8_t magic[8]={'K','U','I','N','V','R','0','1'};
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static bool ram_span(uintptr_t address,size_t bytes,uint32_t *p) {
#if UINTPTR_MAX>UINT32_MAX
    if(address>UINT32_MAX) return false;
#endif
    uint32_t a=(uint32_t)address,area=a&UINT32_C(0xe0000000);*p=physical(a);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        *p>=UINT32_C(0x0c000000) && *p<UINT32_C(0x0d000000) && bytes<=UINT32_C(0x0d000000)-*p;
}
static bool object(const void *p,size_t n) {return p && n<=UINTPTR_MAX-(uintptr_t)p;}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    if(!object(a,an) || !object(b,bn)) return true;
    uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    if(aa<bb+bn && bb<aa+an) return true;
    uint32_t ap,bp;
    return ram_span(aa,an,&ap) && ram_span(bb,bn,&bp) && ap<bp+bn && bp<ap+an;
}
static bool image_valid(const void *image,size_t n,uint32_t load) {
    uint32_t p;
    return object(image,n) && n>=0x300u && n<=UINT32_C(0xc00000) && !(load&3u) &&
        ram_span(load,n,&p) && p>=UINT32_C(0x0c010000);
}
static void hash(const void *p,size_t n,uint8_t digest[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,p,n);kui_sha256_digest(&h,digest);
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);p[2]=(uint8_t)(value>>16);p[3]=(uint8_t)(value>>24);
}
struct cursor {uint8_t *out;const uint8_t *in;size_t at;};
static void word(struct cursor *c,uint32_t *value) {
    if(c->out) put32(c->out+c->at,*value);else *value=get32(c->in+c->at);
    c->at+=4u;
}
/* Enumerate every field explicitly. The wire ABI never copies C struct bytes
 * or depends on compiler padding, alignment, host byte order or pointer size. */
static size_t codec(struct cursor *c,struct kui_vmu_native_resolution *r) {
#define WORD(x) word(c,&(x))
#define DRIVER(x) WORD(r->driver.x)
    DRIVER(version);DRIVER(bytes);DRIVER(family);DRIVER(image_bytes);DRIVER(load_address);DRIVER(requirements);
    DRIVER(submit);DRIVER(resume);DRIVER(initializer);DRIVER(manager_cell);DRIVER(manager_initial);DRIVER(lock_cell);
    DRIVER(completion_slot);DRIVER(completion_call);DRIVER(consumer);DRIVER(gate);DRIVER(ready);DRIVER(gate_register);
    if(c->out) memcpy(c->out+c->at,r->driver.image_sha256,32u);
    else memcpy(r->driver.image_sha256,c->in+c->at,32u);
    c->at+=32u;
#undef DRIVER
#define LAYOUT(x) WORD(r->layout.x)
    LAYOUT(version);LAYOUT(bytes);LAYOUT(initializer);LAYOUT(initializer_wrapper);LAYOUT(initializer_call);
    LAYOUT(tx_base);LAYOUT(rx_layout);LAYOUT(rx_base);
    for(unsigned i=0;i<KUI_VMU_NATIVE_RX_SLOTS;i++) LAYOUT(rx_slots[i]);
    LAYOUT(gun_flag);LAYOUT(timer1_callback);LAYOUT(timer2_callback);
    LAYOUT(inputs);LAYOUT(input_array);LAYOUT(index_array);
    for(unsigned i=0;i<KUI_VMU_NATIVE_LAYOUT_INPUTS;i++) LAYOUT(indices[i]);
    for(unsigned i=0;i<KUI_VMU_NATIVE_LAYOUT_INPUTS;i++) LAYOUT(input_values[i]);
#undef LAYOUT
#define HEAP(x) WORD(r->heap.x)
    HEAP(version);HEAP(bytes);HEAP(family);HEAP(layout);HEAP(image_bytes);HEAP(load);
    HEAP(initializer);HEAP(initializer_slot);HEAP(initializer_target);HEAP(allocate);HEAP(release);
    HEAP(dispatch_cell);HEAP(dispatch_table);HEAP(sentinel);HEAP(cursor_cell);HEAP(begin_cell);HEAP(bytes_cell);HEAP(begin_copy_cell);
#undef HEAP
    WORD(r->call.load);WORD(r->call.call);WORD(r->call.return_pr);WORD(r->call.entry_begin);WORD(r->call.entry_bytes);
#define EXIT(x) WORD(r->exits.x)
    EXIT(version);EXIT(bytes);EXIT(family);EXIT(load);EXIT(image_bytes);EXIT(count);
    for(unsigned i=0;i<KUI_VMU_NATIVE_EXIT_ENTRIES;i++) EXIT(entries[i]);
    EXIT(bios_vector_literal);EXIT(exec_thunk);EXIT(cleanup);
#undef EXIT
#undef WORD
    return c->at;
}
static bool schema(const struct kui_vmu_native_resolution *r) {
    const struct kui_vmu_native_discovery *d=&r->driver;
    return d->version==KUI_VMU_NATIVE_DISCOVERY_VERSION && d->bytes==sizeof(*d) &&
        d->family==KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 && d->requirements==KUI_VMU_NATIVE_REQUIREMENTS &&
        d->image_bytes>=0x300u && d->image_bytes<=UINT32_C(0xc00000) &&
        (d->gate_register==12u || d->gate_register==13u) &&
        (r->layout.version==KUI_VMU_NATIVE_LAYOUT_VERSION ||
         r->layout.version==KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION) && r->layout.bytes==sizeof(r->layout) &&
        ((r->layout.rx_layout==KUI_VMU_NATIVE_RX_EXPLICIT48 && r->layout.inputs==12u) ||
         (r->layout.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48 && r->layout.inputs==1u)) &&
        r->heap.version==KUI_VMU_NATIVE_HEAP_VERSION && r->heap.bytes==sizeof(r->heap) &&
        r->heap.family==KUI_VMU_NATIVE_HEAP_TAIL32 && r->heap.layout==KUI_VMU_NATIVE_HEAP_DISPATCH2 &&
        r->heap.load==d->load_address && r->heap.image_bytes==d->image_bytes &&
        r->exits.version==KUI_VMU_NATIVE_EXIT_VERSION && r->exits.bytes==sizeof(r->exits) &&
        r->exits.load==d->load_address && r->exits.image_bytes==d->image_bytes &&
        (((r->exits.family==KUI_VMU_NATIVE_EXIT_FAMILY || r->exits.family==KUI_VMU_NATIVE_EXIT_PVR_CLEAR ||
           r->exits.family==KUI_VMU_NATIVE_EXIT_PVR_COMPACT) && r->exits.count==KUI_VMU_NATIVE_EXIT_ENTRIES) ||
         (r->exits.family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY && r->exits.count==2u &&
          !r->exits.entries[2] && !r->exits.exec_thunk) ||
         (r->exits.family==KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH && r->exits.count==3u &&
          r->exits.entries[2] && r->exits.terminal_dispatch.table));
}
static bool decode(const void *wire,size_t n,struct kui_vmu_native_resolution *r,uint8_t digest[32]) {
    if(n!=KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES || !object(wire,n)) return false;
    const uint8_t *p=wire;
    if(memcmp(p,magic,sizeof(magic)) || get32(p+8u)!=KUI_VMU_NATIVE_RESOLUTION_CACHE_VERSION ||
       get32(p+12u)!=KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES ||
       get32(p+16u)!=KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH || get32(p+20u)!=1u) return false;
    for(size_t i=CACHE_PAYLOAD_END;i<CACHE_DIGEST_OFFSET;i++) if(p[i]) return false;
    hash(p,CACHE_DIGEST_OFFSET,digest);
    if(memcmp(digest,p+CACHE_DIGEST_OFFSET,32u)) return false;
    memset(r,0,sizeof(*r));struct cursor c={.in=p,.at=CACHE_HEADER_BYTES};
    if(codec(&c,r)!=CACHE_PAYLOAD_END || !schema(r) || get32(p+24u)!=r->driver.image_bytes ||
       get32(p+28u)!=r->driver.load_address || memcmp(p+32u,r->driver.image_sha256,32u)) return false;
    return true;
}
bool kui_vmu_native_resolution_cache_encode(const struct kui_vmu_native_resolution *r,void *wire,size_t n) {
    if(!object(r,sizeof(*r)) || n!=KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES || !object(wire,n) ||
       overlap(r,sizeof(*r),wire,n) || !schema(r)) return false;
    struct kui_vmu_native_resolution copy=*r;
    uint8_t result[KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES]={0};memcpy(result,magic,sizeof(magic));
    put32(result+8u,KUI_VMU_NATIVE_RESOLUTION_CACHE_VERSION);put32(result+12u,sizeof(result));
    put32(result+16u,KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH);put32(result+20u,1u);
    put32(result+24u,r->driver.image_bytes);put32(result+28u,r->driver.load_address);
    memcpy(result+32u,r->driver.image_sha256,32u);
    struct cursor c={.out=result,.at=CACHE_HEADER_BYTES};
    if(codec(&c,&copy)!=CACHE_PAYLOAD_END) return false;
    hash(result,CACHE_DIGEST_OFFSET,result+CACHE_DIGEST_OFFSET);memcpy(wire,result,sizeof(result));return true;
}
bool kui_vmu_native_resolution_cache_record_digest(const void *wire,size_t n,uint8_t digest[32]) {
    if(!object(digest,32u) || overlap(wire,n,digest,32u)) return false;
    struct kui_vmu_native_resolution decoded;uint8_t result[32];
    if(!decode(wire,n,&decoded,result)) return false;
    memcpy(digest,result,32u);return true;
}
static bool release_window(const void *image,size_t n,uint32_t load,uint32_t release) {
    uint32_t address=physical(release),base=physical(load);
    if(address<base || address-base>n || n-(address-base)<20u) return false;
    const uint8_t *p=(const uint8_t *)image+address-base;
    uint16_t op[10];for(unsigned i=0;i<10u;i++) op[i]=(uint16_t)(p[2u*i]|(uint16_t)p[2u*i+1u]<<8);
    if(op[0]!=0x0002u || (op[1]&0xff00u)!=0x9300u || op[2]!=0x4009u || op[3]!=0x4009u ||
       op[4]!=0xc90fu || op[5]!=0x6703u || op[6]!=0x0002u || op[7]!=0x2039u ||
       op[8]!=0xcbf0u || op[9]!=0x400eu) return false;
    uint32_t literal=address+6u+2u*(op[1]&255u);
    if(literal<base || literal-base>n-2u) return false;
    p=(const uint8_t *)image+literal-base;return p[0]==0x0fu && p[1]==0xffu;
}
static bool discover(const void *image,size_t n,uint32_t load,struct kui_vmu_native_resolution *r) {
    memset(r,0,sizeof(*r));
    if(!kui_vmu_native_discover_layout(image,n,load,&r->driver,&r->layout) ||
       kui_vmu_native_heap_discover(image,n,load,&r->heap)!=KUI_VMU_NATIVE_HEAP_FOUND ||
       !r->heap.initializer_slot || !kui_vmu_native_heap_call_discover(image,n,&r->heap,&r->call) ||
       ((r->heap.initializer|r->heap.release)&3u) || !release_window(image,n,load,r->heap.release)) return false;
    const struct kui_vmu_native_patch_window windows[]={
        {r->driver.submit,12u},{r->heap.initializer,12u},{r->heap.release,20u}};
    return kui_vmu_native_exit_discover_guarded(image,n,load,windows,3u,&r->exits) && schema(r);
}
bool kui_vmu_native_resolution_cache_build(const void *image,size_t n,uint32_t load,
    void *wire,size_t wire_bytes,struct kui_vmu_native_resolution *out) {
    if(!image_valid(image,n,load) || wire_bytes!=KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES ||
       !object(wire,wire_bytes) || overlap(image,n,wire,wire_bytes) ||
       (out && (!object(out,sizeof(*out)) || overlap(out,sizeof(*out),image,n) ||
        overlap(out,sizeof(*out),wire,wire_bytes)))) return false;
    struct kui_vmu_native_resolution fresh;
    if(!discover(image,n,load,&fresh) || !kui_vmu_native_resolution_cache_encode(&fresh,wire,wire_bytes)) return false;
    if(out) *out=fresh;
    return true;
}
static bool replay_arguments(const void *image,size_t n,uint32_t load,const void *wire,size_t wire_bytes,
    struct kui_vmu_native_resolution *out) {
    return image_valid(image,n,load) && wire_bytes==KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES &&
        object(wire,wire_bytes) && object(out,sizeof(*out)) &&
        !overlap(image,n,out,sizeof(*out)) && !overlap(wire,wire_bytes,out,sizeof(*out));
}
bool kui_vmu_native_resolution_cache_replay(const void *image,size_t n,uint32_t load,
    const void *wire,size_t wire_bytes,const uint8_t expected[32],struct kui_vmu_native_resolution *out) {
    if(!replay_arguments(image,n,load,wire,wire_bytes,out) || !object(expected,32u) ||
       overlap(expected,32u,wire,wire_bytes) || overlap(expected,32u,image,n) ||
       overlap(expected,32u,out,sizeof(*out))) return false;
    struct kui_vmu_native_resolution decoded;uint8_t digest[32];
    if(!decode(wire,wire_bytes,&decoded,digest) || memcmp(digest,expected,32u) ||
       decoded.driver.image_bytes!=n || decoded.driver.load_address!=load) return false;
    hash(image,n,digest);
    if(memcmp(digest,decoded.driver.image_sha256,32u)) return false;
    *out=decoded;return true;
}
bool kui_vmu_native_resolution_cache_validate_untrusted(const void *image,size_t n,uint32_t load,
    const void *wire,size_t wire_bytes,struct kui_vmu_native_resolution *out) {
    if(!replay_arguments(image,n,load,wire,wire_bytes,out)) return false;
    struct kui_vmu_native_resolution decoded,fresh;uint8_t digest[32];
    if(!decode(wire,wire_bytes,&decoded,digest) || decoded.driver.image_bytes!=n ||
       decoded.driver.load_address!=load || !discover(image,n,load,&fresh)) return false;
    uint8_t canonical[KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES];
    if(!kui_vmu_native_resolution_cache_encode(&fresh,canonical,sizeof(canonical)) ||
       memcmp(canonical,wire,sizeof(canonical))) return false;
    *out=fresh;return true;
}
