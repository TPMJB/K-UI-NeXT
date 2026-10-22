/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored scalar fixtures; optional owner files remain outside the repo. */
#include "../src/core/vmu_native_heap.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint8_t image[0x3001];
static void put16(size_t at,uint16_t v) {image[at]=(uint8_t)v;image[at+1]=(uint8_t)(v>>8);}
static void put32(size_t at,uint32_t v) {put16(at,(uint16_t)v);put16(at+2u,(uint16_t)(v>>16));}
static void load_literal(size_t op,unsigned reg,size_t at,uint32_t value) {
    size_t pc=(op+4u)&~3u;CHECK(at>=pc && !(at&3u) && at-pc<=1020u);
    put16(op,(uint16_t)(0xd000u|(reg<<8)|((at-pc)/4u)));put32(at,value);
}
static void body(size_t at,uint32_t alias) {
    for(unsigned i=0;i<sizeof(heap_words)/sizeof(heap_words[0]);i++)
        put16(at+heap_words[i].offset,heap_words[i].value);
    size_t literal_at=(at+0x220u)&~3u;
    const unsigned op[]={6,8,0xe,0x1a,0x1c,0x2e,0x46,0x4e,0xfa};
    const unsigned reg[]={2,3,1,4,2,2,5,2,14};
    const uint32_t value[]={0x0c70002c,0x0c700024,0x0c700028,0x0c700000,
        0x0c700020,0x0c700024,0x0c6ffffc,0x0c012100,0x0c700020};
    for(unsigned i=0;i<9u;i++) load_literal(at+op[i],reg[i],literal_at+4u*i,alias|value[i]);
    put32(0x2100u,alias|(0x0c010000u+(uint32_t)at+0xe2u));
    put32(0x2104u,alias|(0x0c010000u+(uint32_t)at+0x1b0u));
}
static struct kui_vmu_native_heap fixture(size_t at,uint32_t alias,bool reference) {
    memset(image,0,sizeof(image));body(at,alias);
    if(reference) put32(0x100u,alias|(0x0c010000u+(uint32_t)at));
    struct kui_vmu_native_heap h;
    CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&h)==KUI_VMU_NATIVE_HEAP_FOUND);
    return h;
}
struct cell {uint32_t at,value;};
static struct cell cells[64];static unsigned used,reads;
static void set(uint32_t at,uint32_t value) {
    at=phys(at);
    for(unsigned i=0;i<used;i++) if(cells[i].at==at) {cells[i].value=value;return;}
    CHECK(used<64u);cells[used++]=(struct cell){at,value};
}
static bool read_word(void *ctx,uint32_t at,uint32_t *value) {
    (void)ctx;++reads;CHECK(!(at&3u) && span(at,4u));
    for(unsigned i=0;i<used;i++) if(cells[i].at==phys(at)) {*value=cells[i].value;return true;}
    return false;
}
static void live(const struct kui_vmu_native_heap *h,uint32_t begin) {
    used=reads=0;
    set(h->begin_cell,begin);set(h->begin_copy_cell,begin);set(h->bytes_cell,0x80000u);
    set(h->dispatch_cell,h->dispatch_table);set(h->dispatch_table,h->allocate);
    set(h->dispatch_table+4u,h->release);set(h->sentinel,begin);set(h->sentinel+4u,0u);
    set(h->cursor_cell,h->sentinel);set(begin,h->sentinel);set(begin+4u,0x4000u);
}
static void allocated(const struct kui_vmu_native_heap_lease *l) {
    set(l->begin+4u,l->remaining);set(l->header+4u,l->units);
}
static void owner(const char *path,bool reference) {
    FILE *f=fopen(path,"rb");CHECK(f!=NULL);CHECK(!fseek(f,0,SEEK_END));
    long length=ftell(f);CHECK(length>0 && length<=0xc00000);rewind(f);
    uint8_t *data=malloc((size_t)length);CHECK(data!=NULL);
    CHECK(fread(data,1u,(size_t)length,f)==(size_t)length);CHECK(!fclose(f));
    struct kui_vmu_native_heap h;
    CHECK(kui_vmu_native_heap_discover(data,(size_t)length,0x8c010000u,&h)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK((h.initializer_slot!=0u)==reference);
    CHECK(phys(h.allocate)==phys(h.initializer)+0xe2u &&
          phys(h.release)==phys(h.initializer)+0x1b0u);
    free(data);
}
static struct kui_vmu_native_heap call_fixture(void) {
    struct kui_vmu_native_heap h=fixture(0x800u,0x80000000u,true);
    put32(0x100u,0u);put32(0x700u,h.initializer_target);
    CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&h)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK(h.initializer_slot==0x8c010700u);return h;
}
static void call_expression_tests(void) {
    struct kui_vmu_native_heap h=call_fixture();struct kui_vmu_native_heap_call call;
    /* An independently authored BSS-tail expression with immutable pointer
     * dereferences and the final alignment adjustment in the delay slot. */
    load_literal(0x400u,1u,0x600u,0x8c010720u);put32(0x720u,0x8c500004u);
    put16(0x402u,0x6012u);put16(0x404u,0xe2e0u);put16(0x406u,0x2029u);
    load_literal(0x408u,5u,0x604u,0x8d000000u);put16(0x40au,0x3508u);
    put16(0x40cu,0x75e0u);put16(0x40eu,0x6403u);put16(0x410u,0x1f03u);
    load_literal(0x412u,3u,0x700u,h.initializer_target);
    put16(0x414u,0x430bu);put16(0x416u,0x7420u);
    CHECK(kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    CHECK(call.load==0x8c010412u && call.call==0x8c010414u && call.return_pr==0x8c010418u);
    CHECK(call.entry_begin==0x8c500020u && call.entry_bytes==0xafffe0u);
    const size_t mutations[]={0x400u,0x402u,0x404u,0x406u,0x408u,0x40au,0x40cu,0x40eu,0x410u,0x412u,0x414u,0x416u};
    for(unsigned i=0;i<sizeof(mutations)/sizeof(mutations[0]);i++) {
        uint16_t saved=h16(image+mutations[i]);put16(mutations[i],0x0000u);
        CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));put16(mutations[i],saved);
    }
    /* Neither an out-of-image load nor caller-dependent stack reads can
     * establish a startup argument. A later live BSS mismatch is separately
     * checked by the installer against the returned exact values. */
    put32(0x600u,0x8c700000u);CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));put32(0x600u,0x8c010720u);
    put16(0x402u,0x50f1u);CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));put16(0x402u,0x6012u);
    put16(0x410u,0x2f06u);CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));put16(0x410u,0x1f03u);
    put16(0x410u,0xef00u);CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));put16(0x410u,0x1f03u);
    put32(0x604u,0x8c400000u);CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));put32(0x604u,0x8d000000u);
    /* A second fully known call is ambiguous even when both arenas agree. */
    load_literal(0x500u,4u,0x608u,0x8c500020u);load_literal(0x502u,5u,0x60cu,0xafffe0u);
    load_literal(0x504u,2u,0x700u,h.initializer_target);put16(0x506u,0x420bu);put16(0x508u,0x0009u);
    CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    h=call_fixture();
    /* Different allocation-register scheduling and a fixed four-MiB arena
     * use the same bounded decoder, with no title or address allowlist. */
    load_literal(0x400u,2u,0x600u,0x8c010720u);put32(0x720u,0x8c500004u);
    load_literal(0x402u,3u,0x604u,0xe0000000u);put16(0x404u,0x6422u);
    load_literal(0x406u,1u,0x608u,0x0d000000u);put16(0x408u,0x2439u);
    put16(0x40au,0x241bu);load_literal(0x40cu,2u,0x60cu,0xffc00000u);
    load_literal(0x40eu,5u,0x610u,0x400000u);load_literal(0x410u,3u,0x700u,h.initializer_target);
    put16(0x412u,0x430bu);put16(0x414u,0x342cu);
    CHECK(kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    CHECK(call.entry_begin==0x8cc00000u && call.entry_bytes==0x400000u);
    put16(0x414u,0x341cu);CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    /* Even a pointer transform that happens to preserve the original target
     * is refused: the installer must preserve its replacement hook value. */
    put16(0x412u,0x2339u);put16(0x414u,0x430bu);put16(0x416u,0x342cu);
    CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
}
int main(int argc,char **argv) {
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    for(unsigned a=0;a<3u;a++) for(unsigned half=0;half<2u;half++) {
        size_t at=0x800u+2u*half;struct kui_vmu_native_heap h=fixture(at,aliases[a],true);
        CHECK(h.initializer==0x8c010000u+at && h.initializer_slot==0x8c010100u);
        CHECK(h.initializer_target==(aliases[a]|(0x0c010000u+(uint32_t)at)));
        struct kui_vmu_native_heap_lease l;uint32_t begin=aliases[a]|0x0c500000u;
        live(&h,begin);
        CHECK(!kui_vmu_native_heap_fresh(&h,read_word,NULL,begin,0x80000u,0u,0x30000u,&l));
        CHECK(!kui_vmu_native_heap_fresh(&h,read_word,NULL,begin,0x80000u,2u,0x30000u,&l));
        CHECK(kui_vmu_native_heap_fresh(&h,read_word,NULL,begin,0x80000u,1u,0x30000u,&l));
        CHECK(l.payload==begin+0x50000u && l.header==begin+0x4ffe0u);
        CHECK(l.units==0x1801u && l.remaining==0x27ffu);
        CHECK(kui_vmu_native_heap_preallocate(&l,read_word,NULL,1u));
        CHECK(!kui_vmu_native_heap_preallocate(&l,read_word,NULL,2u));
        struct kui_vmu_native_heap_lease bad=l;
        CHECK(!kui_vmu_native_heap_postallocate(&bad,read_word,NULL,1u,l.payload));
        allocated(&l);CHECK(!kui_vmu_native_heap_preallocate(&l,read_word,NULL,1u));
        CHECK(kui_vmu_native_heap_postallocate(&l,read_word,NULL,1u,l.payload));
        CHECK(kui_vmu_native_heap_lease_valid(&l,read_word,NULL,1u));
        CHECK(!kui_vmu_native_heap_lease_valid(&l,read_word,NULL,2u));
        CHECK(!kui_vmu_native_heap_postallocate(&l,read_word,NULL,1u,l.payload));
        CHECK(l.phase==0u);
        live(&h,begin);CHECK(kui_vmu_native_heap_fresh(&h,read_word,NULL,begin,0x80000u,1u,0x30000u,&l));
        allocated(&l);CHECK(kui_vmu_native_heap_postallocate(&l,read_word,NULL,1u,l.payload));
        /* A real later allocation can split the remaining free arena. */
        set(begin+4u,0x100u);set(begin,begin+0x4000u);
        set(begin+0x4000u,h.sentinel);set(begin+0x4004u,0x100u);set(h.cursor_cell,begin);
        CHECK(kui_vmu_native_heap_lease_valid(&l,read_word,NULL,1u));
        set(begin+0x4000u,l.header);set(l.header,h.sentinel);
        CHECK(!kui_vmu_native_heap_lease_valid(&l,read_word,NULL,1u));
        live(&h,begin);CHECK(!kui_vmu_native_heap_lease_valid(&l,read_word,NULL,1u));
    }
    struct kui_vmu_native_heap h=fixture(0x800u,0u,false);
    CHECK(!h.initializer_slot && !h.initializer_target);
    for(unsigned w=0;w<sizeof(heap_words)/sizeof(heap_words[0]);w++) for(unsigned bit=0;bit<16u;bit++) {
        h=fixture(0x800u,0u,false);size_t at=0x800u+heap_words[w].offset;
        put16(at,(uint16_t)(h16(image+at)^(1u<<bit)));
        CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&h)==KUI_VMU_NATIVE_HEAP_NONE);
    }
    h=fixture(0x800u,0u,true);put32(0x104u,0x0c010800u);
    CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&h)==KUI_VMU_NATIVE_HEAP_AMBIGUOUS);
    h=fixture(0x800u,0u,false);body(0x1400u,0u);
    put32(0x2100u,0x0c0108e2u);put32(0x2104u,0x0c0109b0u);
    put32(0x1400u+0x220u+7u*4u,0x0c012200u);
    put32(0x2200u,0x0c0114e2u);put32(0x2204u,0x0c0115b0u);
    CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&h)==KUI_VMU_NATIVE_HEAP_AMBIGUOUS);
    h=fixture(0x800u,0u,true);live(&h,0x0c500000u);
    struct kui_vmu_native_heap_lease l;
    CHECK(!kui_vmu_native_heap_fresh(&h,NULL,NULL,0x0c500000u,0x80000u,1u,0x30000u,&l));
    CHECK(!kui_vmu_native_heap_fresh(&h,read_word,NULL,0x0c500001u,0x80000u,1u,0x30000u,&l));
    CHECK(!kui_vmu_native_heap_fresh(&h,read_word,NULL,0x0c500000u,0x80001u,1u,0x30000u,&l));
    CHECK(!kui_vmu_native_heap_fresh(&h,read_word,NULL,0x0c500000u,0x80000u,1u,0x80000u,&l));
    unsigned live_cells=used;
    for(unsigned i=0;i<live_cells;i++) {
        live(&h,0x0c500000u);cells[i].value^=32u;
        CHECK(!kui_vmu_native_heap_fresh(&h,read_word,NULL,0x0c500000u,0x80000u,1u,0x30000u,&l));
    }
    live(&h,0x0c500000u);--used;
    CHECK(!kui_vmu_native_heap_fresh(&h,read_word,NULL,0x0c500000u,0x80000u,1u,0x30000u,&l));
    live(&h,0x0c500000u);
    CHECK(kui_vmu_native_heap_fresh(&h,read_word,NULL,0x0c500000u,0x80000u,1u,0x30000u,&l));
    allocated(&l);CHECK(!kui_vmu_native_heap_postallocate(&l,read_word,NULL,1u,l.payload|0x80000000u));
    h=fixture(0x800u,0u,true);CHECK(kui_vmu_native_heap_discover(image,sizeof(image),
        0x8c010000u,(void *)image)==KUI_VMU_NATIVE_HEAP_INVALID);
    for(unsigned a=0;a<3u;a++) for(unsigned b=0;b<3u;b++)
        CHECK(kui_vmu_native_heap_discover((void *)(uintptr_t)(aliases[a]|0x0c010000u),
            sizeof(image),0x8c010000u,(void *)(uintptr_t)(aliases[b]|0x0c010000u))==KUI_VMU_NATIVE_HEAP_INVALID);
    CHECK(kui_vmu_native_heap_discover(NULL,0u,0x8c010000u,&h)==KUI_VMU_NATIVE_HEAP_INVALID);
    call_expression_tests();
    if(argc==3) {owner(argv[1],true);owner(argv[2],false);} else CHECK(argc==1);
    printf("Native heap family / lease checks: %u passed; no allocator calls or lifetime authority\n",checks);
    return 0;
}
