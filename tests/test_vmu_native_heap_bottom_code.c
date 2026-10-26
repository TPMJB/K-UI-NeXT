/* SPDX-License-Identifier: GPL-3.0-only */
/* Full original allocator/release code-family contract. Retail inputs are
 * optional external files, never embedded here. The authored fixture checks
 * every instruction bit, exact decoded owner literal roles, callback read
 * bounds, and preservation of harmless, unreferenced literal-pool padding. */
#include "../src/core/vmu_native_heap.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,mutants,real_inputs;
#define CHECK(x) do {++checks;assert(x);} while(0)
struct source {uint8_t *data;size_t bytes;uint32_t load;unsigned reads,fail_at;};
static bool source_read(void *context,uint32_t address,uint32_t *value) {
    struct source *s=context;unsigned call=++s->reads;
    CHECK(!(address&3u));CHECK(span(address,4u));
    uint32_t at=phys(address),base=phys(s->load);
    CHECK(at>=base && at-base<=s->bytes && 4u<=s->bytes-(at-base));
    if(s->fail_at && call==s->fail_at)return false;
    *value=h32(s->data+(at-base));return true;
}
static void put16(struct source *s,uint32_t address,uint16_t value) {
    size_t at=phys(address)-phys(s->load);CHECK(at+2u<=s->bytes);
    s->data[at]=(uint8_t)value;s->data[at+1u]=(uint8_t)(value>>8);
}
static void put32(struct source *s,uint32_t address,uint32_t value) {
    put16(s,address,(uint16_t)value);put16(s,address+2u,(uint16_t)(value>>16));
}
static uint16_t get16(struct source *s,uint32_t address) {
    return h16(s->data+phys(address)-phys(s->load));
}
static struct kui_vmu_native_heap fixture(struct source *s,uint32_t alias,uint16_t padding) {
    memset(s->data,0,s->bytes);s->load=alias|UINT32_C(0x0c010000);s->reads=s->fail_at=0u;
    struct kui_vmu_native_heap h={
        .version=1u,.bytes=sizeof(h),.family=KUI_VMU_NATIVE_HEAP_TAIL32,
        .layout=KUI_VMU_NATIVE_HEAP_DISPATCH2,.image_bytes=(uint32_t)s->bytes,.load=s->load,
        .initializer=s->load+0x100u,.initializer_slot=s->load+0x80u,.initializer_target=s->load+0x100u,
        .allocate=s->load+0x1e2u,.release=s->load+0x2b0u,
        .dispatch_cell=alias|UINT32_C(0x0c6ffffc),.dispatch_table=s->load+0x700u,
        .sentinel=alias|UINT32_C(0x0c700000),.cursor_cell=alias|UINT32_C(0x0c700020),
        .begin_cell=alias|UINT32_C(0x0c700024),.bytes_cell=alias|UINT32_C(0x0c700028),
        .begin_copy_cell=alias|UINT32_C(0x0c70002c)};
    for(unsigned i=0;i<sizeof(heap_bottom_allocate_words)/sizeof(heap_bottom_allocate_words[0]);i++)
        put16(s,h.allocate+2u*i,heap_bottom_allocate_words[i]);
    for(unsigned i=0;i<sizeof(heap_bottom_release_words)/sizeof(heap_bottom_release_words[0]);i++)
        put16(s,h.release+2u*i,heap_bottom_release_words[i]);
    const uint32_t roles[7]={h.begin_cell,h.begin_copy_cell,h.bytes_cell,h.sentinel,
        h.cursor_cell,h.dispatch_cell,h.dispatch_table};
    put16(s,h.allocate+0x44u,padding);
    for(unsigned i=0;i<7u;i++)put32(s,h.allocate+0x46u+4u*i,roles[i]);
    put16(s,h.release+0xaeu,0xff0fu);put32(s,h.release+0xb0u,h.dispatch_cell);
    put32(s,h.release+0xb4u,h.cursor_cell);
    return h;
}
static void mutation16(struct source *s,const struct kui_vmu_native_heap *h,uint32_t address) {
    uint16_t saved=get16(s,address);
    for(unsigned bit=0;bit<16u;bit++) {
        put16(s,address,(uint16_t)(saved^(1u<<bit)));
        CHECK(!kui_vmu_native_heap_bottom_code_valid(h,source_read,s));++mutants;
    }
    put16(s,address,saved);
}
static void authored(void) {
    uint8_t data[0x1000];struct source s={data,sizeof(data),0u,0u,0u};
    const uint32_t aliases[3]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    const uint16_t paddings[2]={0u,UINT16_MAX};
    for(unsigned a=0;a<3u;a++)for(unsigned pad=0;pad<2u;pad++) {
        struct kui_vmu_native_heap h=fixture(&s,aliases[a],paddings[pad]);
        CHECK(kui_vmu_native_heap_bottom_code_valid(&h,source_read,&s));
        /* Literal role words are mutated too; only the documented unused
         * padding halfword has no source consumer and carries no authority. */
        for(unsigned off=0u;off<0xceu;off+=2u)
            if(off!=0x44u)mutation16(&s,&h,h.allocate+off);
        for(unsigned off=0u;off<0x9au;off+=2u)mutation16(&s,&h,h.release+off);
        mutation16(&s,&h,h.release+0xaeu);
        for(unsigned off=0xb0u;off<0xb8u;off+=2u)mutation16(&s,&h,h.release+off);
        /* Every failed observation refuses, including late pointer words. */
        s.reads=0u;CHECK(kui_vmu_native_heap_bottom_code_valid(&h,source_read,&s));
        unsigned reads=s.reads;
        for(unsigned fail=1u;fail<=reads;fail++) {
            s.reads=0u;s.fail_at=fail;
            CHECK(!kui_vmu_native_heap_bottom_code_valid(&h,source_read,&s));
        }
        s.fail_at=0u;
        /* Generic metadata and bounded image extent cannot be replaced by
         * matching bytes at an unowned address or by another alias role. */
        struct kui_vmu_native_heap bad=h;bad.image_bytes=phys(h.release)-phys(h.load)+0xb7u;
        s.reads=0u;CHECK(!kui_vmu_native_heap_bottom_code_valid(&bad,source_read,&s));CHECK(!s.reads);
        bad=h;bad.family=0u;s.reads=0u;CHECK(!kui_vmu_native_heap_bottom_code_valid(&bad,source_read,&s));CHECK(!s.reads);
        bad=h;bad.initializer+=2u;bad.allocate+=2u;bad.release+=2u;bad.initializer_target+=2u;
        s.reads=0u;CHECK(!kui_vmu_native_heap_bottom_code_valid(&bad,source_read,&s));CHECK(!s.reads);
        CHECK(!kui_vmu_native_heap_bottom_code_valid(NULL,source_read,&s));
        CHECK(!kui_vmu_native_heap_bottom_code_valid(&h,NULL,&s));
    }
}
static uint8_t *file(const char *path,size_t *bytes) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));
    long n=ftell(f);CHECK(n>0 && n<=0xc00000);rewind(f);
    uint8_t *p=malloc((size_t)n);CHECK(p);CHECK(fread(p,1u,(size_t)n,f)==(size_t)n);CHECK(!fclose(f));
    *bytes=(size_t)n;return p;
}
static void real(const char *program,const char *record) {
    size_t n,wn;uint8_t *data=file(program,&n),*wire=file(record,&wn);CHECK(wn==1024u);
    struct kui_vmu_native_heap h;uint32_t *fields=(uint32_t *)(void *)&h;
    CHECK(sizeof(h)==72u);
    for(unsigned i=0;i<18u;i++)fields[i]=h32(wire+608u+4u*i);
    CHECK(h.image_bytes<=n);
    struct source s={data,h.image_bytes,h.load,0u,0u};
    CHECK(kui_vmu_native_heap_bottom_code_valid(&h,source_read,&s));
    ++real_inputs;free(wire);free(data);
}
int main(int argc,char **argv) {
    CHECK(!(argc%2==0));authored();
    for(int i=1;i+1<argc;i+=2)real(argv[i],argv[i+1]);
    printf("Native bottom allocator code: %u checks, %u bit mutants refused, %u external originals accepted\n",checks,mutants,real_inputs);
    return 0;
}
