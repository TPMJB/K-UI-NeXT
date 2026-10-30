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
    struct kui_vmu_native_heap_call expected=call;
    /* The two full-word zero-displacement stack-store encodings are
     * equivalent only with a known source and unchanged SP. Sweep every
     * source register; neither form may invent inherited register knowledge. */
    for(unsigned reg=0u;reg<16u;reg++) {
        bool known=reg==0u || reg==1u || reg==2u || reg==4u || reg==5u;
        put16(0x410u,(uint16_t)(0x2f02u|(reg<<4)));
        CHECK(kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call)==known);
        if(known)CHECK(!memcmp(&call,&expected,sizeof(call)));
        put16(0x410u,(uint16_t)(0x1f00u|(reg<<4)));
        CHECK(kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call)==known);
        if(known)CHECK(!memcmp(&call,&expected,sizeof(call)));
    }
    const uint16_t invalid_sp[]={0x2f00u,0x2f01u,0x2f06u,0x6ff2u,0xef00u,0x4f22u};
    for(unsigned i=0u;i<sizeof(invalid_sp)/sizeof(invalid_sp[0]);i++) {
        put16(0x410u,invalid_sp[i]);CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    }
    put16(0x410u,0x1f03u);
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
    load_literal(0x400u,4u,0x600u,0x8c500020u);load_literal(0x402u,5u,0x604u,0xafffe0u);
    load_literal(0x404u,3u,0x700u,h.initializer_target);put16(0x406u,0x2f32u);
    put16(0x408u,0x430bu);put16(0x40au,0x0009u);
    CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    put16(0x406u,0x1f30u);CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
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
static void known_call(size_t at,size_t slot,uint32_t target) {
    load_literal(at,4u,at+0x40u,0x8c500020u);
    load_literal(at+2u,5u,at+0x44u,0xafffe0u);
    load_literal(at+4u,3u,slot,target);
    put16(at+6u,0x430bu);put16(at+8u,0x0009u);
}
static void caller_target_registers(void) {
    /* Change the target load and JSR together: single-word mutation tests
     * do not exercise coordinated register allocations or the R15 hazard. */
    for(unsigned reg=0u;reg<16u;reg++) {
        struct kui_vmu_native_heap h=call_fixture();
        load_literal(0x400u,4u,0x600u,0x8c500020u);
        load_literal(0x402u,5u,0x604u,0xafffe0u);
        load_literal(0x404u,reg,0x700u,h.initializer_target);
        put16(0x406u,(uint16_t)(0x400bu|(reg<<8)));put16(0x408u,0x0009u);
        struct kui_vmu_native_heap_call got,before;
        memset(&got,0xa5,sizeof(got));before=got;
        bool found=kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&got);
        CHECK(found==(reg<4u));
        if(found) {
            CHECK(got.load==0x8c010404u && got.call==0x8c010406u && got.return_pr==0x8c01040au);
            CHECK(got.entry_begin==0x8c500020u && got.entry_bytes==0xafffe0u);
        } else CHECK(!memcmp(&got,&before,sizeof(got)));
    }
}
static void exported_initializer_tests(void) {
    struct kui_vmu_native_heap h=call_fixture(),found;
    struct kui_vmu_native_heap_call call;
    const uint32_t target=h.initializer_target;
    /* A second raw address in an export table is not a startup-slot proof.
     * Exactly one complete known call selects the actual literal. */
    put32(0x704u,target);known_call(0x400u,0x700u,target);
    CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&found)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK(found.initializer_slot==0x8c010700u && found.initializer_target==target);
    CHECK(kui_vmu_native_heap_call_discover(image,sizeof(image),&found,&call));
    CHECK(call.load==0x8c010404u && call.call==0x8c010406u && call.return_pr==0x8c01040au);
    CHECK(call.entry_begin==0x8c500020u && call.entry_bytes==0xafffe0u);
    for(unsigned i=0;i<5u;i++) {
        size_t at=0x400u+2u*i;uint16_t saved=h16(image+at);put16(at,0u);
        CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&found)==KUI_VMU_NATIVE_HEAP_AMBIGUOUS);
        put16(at,saved);
    }
    /* Changing which literal has the sole proved call changes the selected
     * slot. Neither pointer order nor a title/address allowlist selects it. */
    known_call(0x400u,0x704u,target);
    CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&found)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK(found.initializer_slot==0x8c010704u);
    known_call(0x500u,0x700u,target);
    CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&found)==KUI_VMU_NATIVE_HEAP_AMBIGUOUS);
    /* An ambiguous slot cannot be ignored in favor of another unique call.
     * Three fully known sites, two sharing the same literal, still refuse. */
    known_call(0x580u,0x700u,target);
    CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&found)==KUI_VMU_NATIVE_HEAP_AMBIGUOUS);
    memset(image+0x400u,0,0x200u);known_call(0x400u,0x700u,target);
    for(unsigned i=2u;i<9u;i++) put32(0x700u+4u*i,target);
    CHECK(kui_vmu_native_heap_discover(image,sizeof(image),0x8c010000u,&found)==KUI_VMU_NATIVE_HEAP_AMBIGUOUS);
}
static void guarded_r0_call_tests(void) {
    struct kui_vmu_native_heap h=call_fixture();struct kui_vmu_native_heap_call call;
    load_literal(0x400u,4u,0x600u,0x8c500020u);
    load_literal(0x402u,5u,0x604u,0x8c800000u);put16(0x404u,0x3548u);
    load_literal(0x406u,2u,0x608u,0x80000000u);put16(0x408u,0x6053u);
    put16(0x40au,0x2029u);put16(0x40cu,0x2008u);put16(0x40eu,0x8901u);
    put16(0x410u,0xaffeu);put16(0x412u,0x0009u);
    load_literal(0x414u,0u,0x700u,h.initializer_target);put16(0x416u,0x400bu);put16(0x418u,0x0009u);
    CHECK(kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    CHECK(call.load==0x8c010414u && call.call==0x8c010416u && call.return_pr==0x8c01041au);
    CHECK(call.entry_begin==0x8c500020u && call.entry_bytes==0x2fffe0u);
    /* The chosen initializer load is actually executed and its replacement
     * value reaches the call unchanged. Known raw target values on another
     * path cannot authorize skipping the patched literal instruction. */
    const struct {size_t at;uint16_t op;} mutations[]={
        {0x40cu,0x0009u}, /* inherited T flag */
        {0x40cu,0x2068u}, /* unknown TST operand */
        {0x40eu,0x89fdu}, /* backward branch */
        {0x40eu,0x8902u}, /* skip chosen target load */
        {0x40eu,0x8b01u}, /* known untaken guard reaches failure spin */
        {0x416u,0x0009u}, /* no call */
        {0x418u,0x0000u}, /* unknown delay instruction */
        {0x418u,0x8901u}  /* branch after target load / in call delay slot */
    };
    for(unsigned i=0;i<sizeof(mutations)/sizeof(mutations[0]);i++) {
        uint16_t saved=h16(image+mutations[i].at);put16(mutations[i].at,mutations[i].op);
        CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));put16(mutations[i].at,saved);
    }
    put32(0x604u,0x8c400000u);
    CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));put32(0x604u,0x8c800000u);
    /* The inverse conditional has the same proof when both operands and
     * the taken outcome are known. No instruction follows an inferred edge. */
    put16(0x40cu,0x2448u);put16(0x40eu,0x8b01u);
    CHECK(kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    /* Starting the guard after a second known-target load does not let an
     * early branch bypass the literal selected by call discovery. */
    put16(0x40eu,0x8b02u);
    CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
}
static void patched_pool_guard(bool indirect,bool early_args,bool alternate,uint32_t alias) {
    struct kui_vmu_native_heap h=call_fixture();struct kui_vmu_native_heap_call call;
    size_t at=0x400u,pool=alternate?0x704u:0x700u;
    if(alternate) put32(pool,h.initializer_target);
    if(early_args) {
        load_literal(at,4u,0x600u,0x8c500020u);at+=2u;
        load_literal(at,5u,0x604u,0xafffe0u);at+=2u;
    }
    if(indirect) {
        load_literal(at,1u,0x60cu,alias|phys(0x8c010000u+(uint32_t)pool));at+=2u;
        put16(at,0x6112u);at+=2u;
    } else {load_literal(at,1u,pool,h.initializer_target);at+=2u;}
    load_literal(at,2u,0x608u,0x10000u);at+=2u;put16(at,0x2218u);at+=2u;
    put16(at,0x8b01u);put16(at+2u,0xaffeu);put16(at+4u,0x0009u);at+=6u;
    if(!early_args) {
        load_literal(at,4u,0x600u,0x8c500020u);at+=2u;
        load_literal(at,5u,0x604u,0xafffe0u);at+=2u;
    }
    load_literal(at,0u,0x700u,h.initializer_target);
    put16(at+2u,0x400bu);put16(at+4u,0x0009u);
    /* The original target has bit 16 set; a low hook does not. Patching a
     * shared word would invert this guard and enter its failure spin. Late
     * argument loads intentionally permit a local decoder to start after
     * the guard: global encoded/address-of sharing checks must still refuse.
     * An alternate unpatched export word leaves this conditional unchanged. */
    CHECK(kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call)==alternate);
}
static void patch_dependency_tests(void) {
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    for(unsigned indirect=0u;indirect<2u;indirect++) for(unsigned early=0u;early<2u;early++)
        for(unsigned alternate=0u;alternate<2u;alternate++) for(unsigned alias=0u;alias<3u;alias++)
            patched_pool_guard(indirect,early,alternate,aliases[alias]);
    for(unsigned alias=0u;alias<3u;alias++) {
        struct kui_vmu_native_heap h=call_fixture();struct kui_vmu_native_heap_call call;
        known_call(0x400u,0x700u,h.initializer_target);
        put32(0x780u,phys(h.initializer_slot)|aliases[alias]);
        CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    }
    struct kui_vmu_native_heap h=call_fixture();struct kui_vmu_native_heap_call call;
    known_call(0x400u,0x700u,h.initializer_target);
    load_literal(0x380u,1u,0x700u,h.initializer_target);
    CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    /* Once the chosen target register contains the replacement-dependent
     * value, no data use may clone it into args, guards, pointers or spills.
     * JSR alone consumes it as a code target. Check pre-call and delay slots. */
    const uint16_t uses[]={0x6403u,0x6603u,0x6102u,0x340cu,0x2409u,0x2208u,0x1f01u};
    for(unsigned delay=0u;delay<2u;delay++) for(unsigned i=0u;i<sizeof(uses)/sizeof(uses[0]);i++) {
        h=call_fixture();
        load_literal(0x400u,4u,0x600u,0x8c500020u);
        load_literal(0x402u,5u,0x604u,0x10000u);
        load_literal(0x404u,0u,0x700u,h.initializer_target);
        if(delay) {put16(0x406u,0x400bu);put16(0x408u,uses[i]);}
        else {put16(0x406u,uses[i]);put16(0x408u,0x400bu);put16(0x40au,0x0009u);}
        CHECK(!kui_vmu_native_heap_call_discover(image,sizeof(image),&h,&call));
    }
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
    call_expression_tests();caller_target_registers();exported_initializer_tests();guarded_r0_call_tests();patch_dependency_tests();
    if(argc==3) {owner(argv[1],true);owner(argv[2],false);} else CHECK(argc==1);
    printf("Native heap family / lease checks: %u passed; no allocator calls or lifetime authority\n",checks);
    return 0;
}
