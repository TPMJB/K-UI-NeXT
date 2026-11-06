/* SPDX-License-Identifier: GPL-3.0-only */
/* Author-written control-flow doubles exercise every refusal gate. The same
 * binary also calls the real immutable core for malformed scalar inputs and
 * optional owner-provided executable files; no game bytes are embedded. */
#include "kui/disc_vmu_probe.h"
#include "kui/vmu_native_program.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Disc VMU probe failed at %u\n",__LINE__);exit(1);}} while(0)
static const uint32_t base=UINT32_C(0x8c010000);
static bool doubles;
static const void *expected_image;
static size_t expected_file,expected_program;
static uint32_t expected_base;
static char sequence[16];
static size_t sequence_bytes;
static struct {
    bool extent,layout,slot,call;
    enum kui_vmu_native_discovery_result driver;
    enum kui_vmu_native_heap_result heap;
    int preclear;
} plan;

bool __real_kui_vmu_native_program_extent(const void *,size_t,uint32_t,size_t *);
bool __real_kui_vmu_native_discover_layout(const void *,size_t,uint32_t,
    struct kui_vmu_native_discovery *,struct kui_vmu_native_layout *);
enum kui_vmu_native_discovery_result __real_kui_vmu_native_discover(
    const void *,size_t,uint32_t,struct kui_vmu_native_discovery *);
enum kui_vmu_native_heap_result __real_kui_vmu_native_heap_discover(
    const void *,size_t,uint32_t,struct kui_vmu_native_heap *);
bool __real_kui_vmu_native_heap_call_discover(const void *,size_t,
    const struct kui_vmu_native_heap *,struct kui_vmu_native_heap_call *);
int __real_kui_vmu_native_heap_preclear_plan(const void *,size_t,const uint8_t[32],
    const struct kui_vmu_native_heap *,const struct kui_vmu_native_heap_call *,
    struct kui_vmu_native_heap_preclear *);

static void seen(char gate,const void *p,size_t n,uint32_t load) {
    CHECK(p==expected_image && load==expected_base);
    CHECK(n==(gate=='E'?expected_file:expected_program));
    CHECK(sequence_bytes+1u<sizeof(sequence));sequence[sequence_bytes++]=gate;
    sequence[sequence_bytes]=0;
}
static void driver_candidate(struct kui_vmu_native_discovery *d) {
    memset(d,0,sizeof(*d));d->version=KUI_VMU_NATIVE_DISCOVERY_VERSION;
    d->bytes=sizeof(*d);d->submit=expected_base+0x100u;
    d->image_bytes=(uint32_t)expected_program;d->load_address=expected_base;
}
bool __wrap_kui_vmu_native_program_extent(const void *p,size_t n,uint32_t load,size_t *out) {
    if(!doubles) return __real_kui_vmu_native_program_extent(p,n,load,out);
    seen('E',p,n,load);if(plan.extent) *out=expected_program;return plan.extent;
}
bool __wrap_kui_vmu_native_discover_layout(const void *p,size_t n,uint32_t load,
    struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *l) {
    if(!doubles) return __real_kui_vmu_native_discover_layout(p,n,load,d,l);
    seen('D',p,n,load);memset(d,0,sizeof(*d));memset(l,0,sizeof(*l));
    if(plan.layout) {driver_candidate(d);l->version=KUI_VMU_NATIVE_LAYOUT_VERSION;
        l->bytes=sizeof(*l);l->tx_base=expected_base+0x10000u;}
    return plan.layout;
}
enum kui_vmu_native_discovery_result __wrap_kui_vmu_native_discover(
    const void *p,size_t n,uint32_t load,struct kui_vmu_native_discovery *d) {
    if(!doubles) return __real_kui_vmu_native_discover(p,n,load,d);
    seen('R',p,n,load);memset(d,0,sizeof(*d));
    if(plan.driver==KUI_VMU_NATIVE_RESOLVED) driver_candidate(d);
    return plan.driver;
}
enum kui_vmu_native_heap_result __wrap_kui_vmu_native_heap_discover(
    const void *p,size_t n,uint32_t load,struct kui_vmu_native_heap *h) {
    if(!doubles) return __real_kui_vmu_native_heap_discover(p,n,load,h);
    seen('H',p,n,load);memset(h,0,sizeof(*h));
    if(plan.heap==KUI_VMU_NATIVE_HEAP_FOUND) {
        h->version=KUI_VMU_NATIVE_HEAP_VERSION;h->bytes=sizeof(*h);
        h->initializer=expected_base+0x200u;
        h->initializer_slot=plan.slot?expected_base+0x80u:0u;
    }
    return plan.heap;
}
bool __wrap_kui_vmu_native_heap_call_discover(const void *p,size_t n,
    const struct kui_vmu_native_heap *h,struct kui_vmu_native_heap_call *c) {
    if(!doubles) return __real_kui_vmu_native_heap_call_discover(p,n,h,c);
    seen('C',p,n,expected_base);CHECK(h->initializer==expected_base+0x200u);
    memset(c,0,sizeof(*c));if(plan.call) c->call=expected_base+0x40u;
    return plan.call;
}
int __wrap_kui_vmu_native_heap_preclear_plan(const void *p,size_t n,const uint8_t sha[32],
    const struct kui_vmu_native_heap *h,const struct kui_vmu_native_heap_call *c,
    struct kui_vmu_native_heap_preclear *out) {
    if(!doubles) return __real_kui_vmu_native_heap_preclear_plan(p,n,sha,h,c,out);
    seen('P',p,n,expected_base);CHECK(sha && out && h->initializer==expected_base+0x200u);
    CHECK(c->call==expected_base+0x40u);return plan.preclear;
}
static void reset(const void *p,size_t n,uint32_t load) {
    doubles=true;expected_image=p;expected_file=n;expected_program=n-32u;
    expected_base=load;sequence_bytes=0u;sequence[0]=0;
    plan.extent=true;plan.layout=true;plan.slot=true;plan.call=true;
    plan.driver=KUI_VMU_NATIVE_RESOLVED;plan.heap=KUI_VMU_NATIVE_HEAP_FOUND;
    plan.preclear=0;
}
static bool empty(const void *p,size_t n) {
    const unsigned char *s=p;for(size_t i=0;i<n;i++) if(s[i]) return false;return true;
}
static void header(const struct kui_disc_vmu_probe *r,uint32_t stage) {
    CHECK(r->version==KUI_DISC_VMU_PROBE_VERSION && r->bytes==sizeof(*r));
    CHECK(r->stage==stage);CHECK(*kui_disc_vmu_probe_stage_text(stage));
}
static void ordinary_arguments(void) {
    uint32_t p[0x200]={0};struct kui_disc_vmu_probe r;
    const struct {const void *p;size_t n;uint32_t load;} bad[]={
        {NULL,sizeof(p),base},{p,0u,base},{p,0x2ffu,base},
        {p,0xc00001u,base},{p,sizeof(p),base+2u},
        {p,sizeof(p),0x8c000000u},{p,sizeof(p),0xcc010000u},
        {p,sizeof(p),0x8cfff804u},
        {(const void *)(UINTPTR_MAX-15u),1024u,base}
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        reset(p,sizeof(p),base);memset(&r,0xa5,sizeof(r));
        CHECK(!kui_disc_vmu_probe_run(bad[i].p,bad[i].n,bad[i].load,&r));
        /* A wrapping image span cannot be proved disjoint, so it preserves
         * all output storage; ordinary argument failures clear stale data. */
        if(i+1u==sizeof(bad)/sizeof(bad[0])) {
            unsigned char want[sizeof(r)];memset(want,0xa5,sizeof(want));
            CHECK(!memcmp(&r,want,sizeof(r)));
        } else {
            header(&r,KUI_DISC_VMU_PROBE_ARGUMENT);
            CHECK(!r.file_bytes && !r.program_bytes && !r.file_crc32);
            CHECK(r.driver_result==KUI_VMU_NATIVE_INVALID);
            CHECK(r.heap_result==KUI_VMU_NATIVE_HEAP_INVALID);
            CHECK(empty(&r.driver,sizeof(r.driver)) && empty(&r.layout,sizeof(r.layout)));
            CHECK(empty(&r.heap,sizeof(r.heap)) && empty(&r.call,sizeof(r.call)));
        }
        CHECK(!*sequence);
    }
    unsigned char original[sizeof(p)];memcpy(original,p,sizeof(p));
    CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,NULL));
    CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,(void *)((unsigned char *)p+1u)));
    CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,(void *)p));
    CHECK(!memcmp(p,original,sizeof(p)));
    CHECK(!kui_disc_vmu_probe_run((void *)(uintptr_t)0x8c100000u,1024u,base,
        (struct kui_disc_vmu_probe *)(uintptr_t)0xac100040u));
    CHECK(!kui_disc_vmu_probe_run((void *)(uintptr_t)0x0c100000u,1024u,base,
        (struct kui_disc_vmu_probe *)(uintptr_t)0x8c100040u));
    CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,
        (struct kui_disc_vmu_probe *)(UINTPTR_MAX-3u)));
}
static void dispatch_tests(void) {
    uint8_t p[0x800],original[sizeof(p)];struct kui_disc_vmu_probe r;
    for(size_t i=0;i<sizeof(p);i++) p[i]=(uint8_t)i;
    memcpy(original,p,sizeof(p));
    reset(p,sizeof(p),base);plan.extent=false;
    CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,&r));header(&r,KUI_DISC_VMU_PROBE_EXTENT);
    CHECK(!strcmp(sequence,"E") && !r.program_bytes);
    CHECK(r.file_bytes==sizeof(p) && r.file_crc32==kui_crc32(0u,p,sizeof(p)));
    const enum kui_vmu_native_discovery_result driver_refusals[]={
        KUI_VMU_NATIVE_NOT_FOUND,KUI_VMU_NATIVE_INVALID,KUI_VMU_NATIVE_AMBIGUOUS,
        KUI_VMU_NATIVE_INCOMPLETE,KUI_VMU_NATIVE_RESOLVED};
    for(unsigned i=0;i<sizeof(driver_refusals)/sizeof(driver_refusals[0]);i++) {
        reset(p,sizeof(p),base);plan.layout=false;plan.driver=driver_refusals[i];
        memset(&r,0xa5,sizeof(r));CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,&r));
        header(&r,plan.driver==KUI_VMU_NATIVE_RESOLVED?
            KUI_DISC_VMU_PROBE_MAPLE_LAYOUT:KUI_DISC_VMU_PROBE_MAPLE_DRIVER);
        CHECK(!strcmp(sequence,"EDR") && r.driver_result==plan.driver);
        CHECK(r.program_bytes==expected_program && r.heap_result==KUI_VMU_NATIVE_HEAP_INVALID);
        CHECK(empty(&r.layout,sizeof(r.layout)) && empty(&r.heap,sizeof(r.heap)));
        CHECK(empty(&r.call,sizeof(r.call)));
        CHECK((r.driver.submit!=0u)==(plan.driver==KUI_VMU_NATIVE_RESOLVED));
    }
    const enum kui_vmu_native_heap_result heap_refusals[]={
        KUI_VMU_NATIVE_HEAP_NONE,KUI_VMU_NATIVE_HEAP_INVALID,KUI_VMU_NATIVE_HEAP_AMBIGUOUS};
    for(unsigned i=0;i<sizeof(heap_refusals)/sizeof(heap_refusals[0]);i++) {
        reset(p,sizeof(p),base);plan.heap=heap_refusals[i];
        CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,&r));header(&r,KUI_DISC_VMU_PROBE_HEAP_DISCOVERY);
        CHECK(!strcmp(sequence,"EDH") && r.heap_result==plan.heap);
        CHECK(r.driver_result==KUI_VMU_NATIVE_RESOLVED && r.driver.submit && r.layout.tx_base);
        CHECK(empty(&r.heap,sizeof(r.heap)) && empty(&r.call,sizeof(r.call)));
    }
    reset(p,sizeof(p),base);plan.slot=false;
    CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,&r));header(&r,KUI_DISC_VMU_PROBE_HEAP_SLOT);
    CHECK(!strcmp(sequence,"EDH") && r.heap.initializer && !r.heap.initializer_slot);
    CHECK(r.heap_result==KUI_VMU_NATIVE_HEAP_FOUND && empty(&r.call,sizeof(r.call)));
    reset(p,sizeof(p),base);plan.call=false;
    CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,&r));header(&r,KUI_DISC_VMU_PROBE_HEAP_CALL);
    CHECK(!strcmp(sequence,"EDHC") && r.heap.initializer_slot && empty(&r.call,sizeof(r.call)));
    for(int hazard=-1;hazard<=1;hazard+=2) {
        reset(p,sizeof(p),base);plan.preclear=hazard;
        CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,&r));
        header(&r,KUI_DISC_VMU_PROBE_STARTUP_CLEAR);
        CHECK(!strcmp(sequence,"EDHCP") && r.call.call==base+0x40u);
        CHECK(r.driver_result==KUI_VMU_NATIVE_RESOLVED && r.heap_result==KUI_VMU_NATIVE_HEAP_FOUND);
    }
    const uint32_t aliases[]={0x8c010000u,0xac010000u,0x0c010000u};
    for(unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++) {
        reset(p,sizeof(p),aliases[i]);CHECK(kui_disc_vmu_probe_run(p,sizeof(p),aliases[i],&r));
        header(&r,KUI_DISC_VMU_PROBE_READY);CHECK(!strcmp(sequence,"EDHCP"));
        CHECK(r.driver_result==KUI_VMU_NATIVE_RESOLVED && r.heap_result==KUI_VMU_NATIVE_HEAP_FOUND);
        CHECK(r.driver.submit==aliases[i]+0x100u && r.layout.tx_base==aliases[i]+0x10000u);
        CHECK(r.heap.initializer==aliases[i]+0x200u && r.call.call==aliases[i]+0x40u);
        CHECK(r.file_bytes==sizeof(p) && r.program_bytes==sizeof(p)-32u);
        CHECK(r.file_crc32==kui_crc32(0u,p,sizeof(p)));
    }
    CHECK(!memcmp(p,original,sizeof(p)));
    CHECK(*kui_disc_vmu_probe_stage_text(UINT32_MAX));
}
static void real_scalars(void) {
    uint8_t p[0x11000]={0},original[sizeof(p)];struct kui_disc_vmu_probe r;
    doubles=false;memcpy(original,p,sizeof(p));
    CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,&r));header(&r,KUI_DISC_VMU_PROBE_MAPLE_DRIVER);
    CHECK(r.driver_result==KUI_VMU_NATIVE_NOT_FOUND && r.program_bytes==sizeof(p));
    CHECK(empty(&r.driver,sizeof(r.driver)) && empty(&r.layout,sizeof(r.layout)));
    CHECK(r.heap_result==KUI_VMU_NATIVE_HEAP_INVALID && !memcmp(p,original,sizeof(p)));
    memcpy(p+sizeof(p)-0x8000u,"SEGA SEGAKATANA SEGA ENTERPRISES",32u);
    memcpy(original,p,sizeof(p));CHECK(!kui_disc_vmu_probe_run(p,sizeof(p),base,&r));
    header(&r,KUI_DISC_VMU_PROBE_EXTENT);CHECK(!r.program_bytes);
    CHECK(r.file_bytes==sizeof(p) && r.file_crc32==kui_crc32(0u,p,sizeof(p)));
    CHECK(r.driver_result==KUI_VMU_NATIVE_INVALID && !memcmp(p,original,sizeof(p)));
}
static void actual(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f && !fseek(f,0,SEEK_END));long length=ftell(f);
    CHECK(length>=0x300 && length<=0xc00000);rewind(f);size_t n=(size_t)length;
    uint8_t *p=malloc(n),*original=malloc(n);CHECK(p && original);
    CHECK(fread(p,1,n,f)==n && !fclose(f));memcpy(original,p,n);doubles=false;
    struct kui_disc_vmu_probe r;bool passed=kui_disc_vmu_probe_run(p,n,base,&r);
    CHECK(r.file_bytes==n && r.file_crc32==kui_crc32(0u,p,n));
    size_t extent=0;CHECK(kui_vmu_native_program_extent(p,n,base,&extent));
    CHECK(r.program_bytes==extent);
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;
    bool maple=kui_vmu_native_discover_layout(p,extent,base,&d,&l);
    if(!maple) {
        enum kui_vmu_native_discovery_result result=kui_vmu_native_discover(p,extent,base,&d);
        CHECK(!passed && r.driver_result==result && !memcmp(&r.driver,&d,sizeof(d)));
        CHECK(r.stage==(uint32_t)(result==KUI_VMU_NATIVE_RESOLVED?
            KUI_DISC_VMU_PROBE_MAPLE_LAYOUT:KUI_DISC_VMU_PROBE_MAPLE_DRIVER));
    } else {
        CHECK(!memcmp(&r.driver,&d,sizeof(d)) && !memcmp(&r.layout,&l,sizeof(l)));
        struct kui_vmu_native_heap h;struct kui_vmu_native_heap_call c;
        enum kui_vmu_native_heap_result result=kui_vmu_native_heap_discover(p,extent,base,&h);
        CHECK(r.heap_result==result && !memcmp(&r.heap,&h,sizeof(h)));
        if(result!=KUI_VMU_NATIVE_HEAP_FOUND) CHECK(!passed && r.stage==KUI_DISC_VMU_PROBE_HEAP_DISCOVERY);
        else if(!h.initializer_slot) CHECK(!passed && r.stage==KUI_DISC_VMU_PROBE_HEAP_SLOT);
        else if(!kui_vmu_native_heap_call_discover(p,extent,&h,&c))
            CHECK(!passed && r.stage==KUI_DISC_VMU_PROBE_HEAP_CALL);
        else {
            struct kui_vmu_native_heap_preclear clear;
            int hazard=kui_vmu_native_heap_preclear_plan(p,extent,d.image_sha256,&h,&c,&clear);
            CHECK(passed==(hazard==0) && !memcmp(&r.call,&c,sizeof(c)));
            CHECK(r.stage==(uint32_t)(hazard==0?KUI_DISC_VMU_PROBE_READY:KUI_DISC_VMU_PROBE_STARTUP_CLEAR));
        }
    }
    CHECK(!memcmp(p,original,n));
    printf("  Immutable probe: %s stage=%u driver=%d file=%zu program=%u crc=%08x\n",
        path,r.stage,r.driver_result,n,r.program_bytes,r.file_crc32);
    free(original);free(p);
}
int main(int argc,char **argv) {
    ordinary_arguments();dispatch_tests();real_scalars();
    for(int i=1;i<argc;i++) actual(argv[i]);
    printf("Disc VMU immutable probe: %u checks passed\n",checks);return 0;
}
