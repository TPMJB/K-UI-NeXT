/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint8_t *memory;
static uint32_t refuse_address,unstable_address,unstable_reads;
static uint32_t physical(uint32_t a) {return a&0x1fffffffu;}
static void put(uint32_t a,uint32_t v) {memcpy(memory+physical(a)-0x0c000000u,&v,4u);}
static bool read_memory(void *context,uint32_t a,unsigned width,uint32_t *v) {
    (void)context;uint32_t p=physical(a),area=a&0xe0000000u;
    if((area!=0 && area!=0x80000000u && area!=0xa0000000u) || width!=4u ||
       p<0x0c000000u || p>0x0cfffffcu || (p&3u) || a==refuse_address) return false;
    memcpy(v,memory+p-0x0c000000u,4u);
    if(a==unstable_address && ++unstable_reads>1u) *v^=32u;
    return true;
}
static void synthetic(struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *l,
    struct kui_vmu_native_runtime_config *c) {
    memset(memory,0,0x1000000u);memset(d,0,sizeof(*d));memset(l,0,sizeof(*l));memset(c,0,sizeof(*c));
    refuse_address=unstable_address=unstable_reads=0;
    *d=(struct kui_vmu_native_discovery){.version=1u,.bytes=sizeof(*d),.family=1u,.requirements=7u,
        .load_address=0x8c010000u,.image_bytes=0x200000u,.manager_initial=0x0c300000u,.manager_cell=0x0c3010fcu,.lock_cell=0x0c080000u};
    l->version=1u;l->bytes=sizeof(*l);l->tx_base=0xac400000u;l->rx_layout=KUI_VMU_NATIVE_RX_EXPLICIT48;
    l->input_array=0x0c450000u;l->rx_base=0xac450000u;l->inputs=12u;l->gun_flag=0x0c600000u;
    l->timer1_callback=0x0c600010u;l->timer2_callback=0x0c600020u;
    static const uint32_t ids[12]={0,1,2,6,7,8,12,13,14,18,19,20};memcpy(l->indices,ids,sizeof(ids));
    c->version=1u;c->bytes=sizeof(*c);c->lease_address=0x0c700000u;c->lease_bytes=0x30000u;
    c->lease_header_bytes=32u;c->required_lease_bytes=0x30000u;
    put(d->manager_cell,d->manager_initial);put(d->manager_initial+0xfc4u,l->tx_base);put(d->manager_initial+0xfc8u,l->rx_base);
    for(unsigned i=0;i<12u;i++) {
        uint32_t raw=0x0c500004u+0x1000u*i,expected=0xac500020u+0x1000u*i;
        put(l->input_array+4u*i,raw);put(d->manager_initial+0xfe8u+4u*ids[i],expected);
        put(d->manager_initial+0xfe8u+4u*(ids[i]+24u),expected+0x400u);
    }
}
static void binding_tests(void) {
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;struct kui_vmu_native_runtime_config c,before;
    synthetic(&d,&l,&c);before=c;
    CHECK(kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    CHECK(c.lease_address==before.lease_address && c.lease_bytes==before.lease_bytes && c.lease_header_bytes==32u);
    CHECK(c.required_lease_bytes==before.required_lease_bytes && c.rx_base==l.rx_base && c.rx_layout==1u);
    unsigned nonzero=0;for(unsigned i=0;i<48u;i++) if(c.rx_slots[i]) {++nonzero;CHECK((c.rx_slots[i]&0xe000001fu)==0xa0000000u);}
    CHECK(nonzero==24u);CHECK(c.rx_slots[0]==0xac500020u && c.rx_slots[24]==0xac500420u && !c.rx_slots[3]);
    CHECK(kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    const uint32_t bad[]={0u,0x0bffffffu,0x0d000000u,0x0cffffe0u,0x0c500003u,0xec500004u,
        0x0c6fffe0u,0x0c700000u,0xac700000u,0x0c400004u,0x0c300004u,0x0c080000u,
        0x0c010000u,0x0c600000u,0x0c600010u,0x0c600020u,0x0c450000u};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        synthetic(&d,&l,&c);before=c;put(l.input_array,bad[i]);
        CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));CHECK(!memcmp(&c,&before,sizeof(c)));
    }
    synthetic(&d,&l,&c);put(l.input_array+4u,0x0c500004u);CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);l.indices[11]=l.indices[0];CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);l.indices[0]=24u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    for(unsigned i=0;i<48u;i++) {
        synthetic(&d,&l,&c);uint32_t a=d.manager_initial+0xfe8u+4u*i,v;
        CHECK(read_memory(NULL,a,4u,&v));put(a,v^32u);CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    }
    synthetic(&d,&l,&c);put(d.manager_initial+0xfc4u,l.tx_base+32u);CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);put(d.manager_initial+0xfc8u,l.rx_base+32u);CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);put(d.manager_cell,d.manager_initial+4u);CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);refuse_address=l.input_array+44u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);unstable_address=l.input_array+44u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);l.input_array=0xac450000u;CHECK(kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);l.input_array=c.lease_address-32u;l.rx_base=l.input_array|0xa0000000u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);l.input_array=d.load_address;l.rx_base=l.input_array|0xa0000000u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);d.image_bytes=0xffffffffu;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
}
static void image_tests(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long z=ftell(f);CHECK(z>0 && z<=0xc00000);rewind(f);
    size_t n=(size_t)z;uint8_t *p=malloc(n),*original=malloc(n);CHECK(p && original);CHECK(fread(p,1,n,f)==n);fclose(f);memcpy(original,p,n);
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;
    CHECK(kui_vmu_native_discover(p,n,0x8c010000u,&d)==KUI_VMU_NATIVE_RESOLVED);
    CHECK(kui_vmu_native_layout_discover(p,n,&d,&l));
    CHECK(l.initializer==0x8c2d9748u && l.initializer_wrapper==0x8c2bc4d6u && l.initializer_call==0x8c02d6c8u);
    CHECK(l.tx_base==0xac20ea60u && l.rx_base==0xac315180u && l.inputs==12u);
    CHECK(l.gun_flag==0x0c8c2178u && l.timer1_callback==0x0c8c1be8u && l.timer2_callback==0x0c8c1bf0u);
    CHECK(l.indices[0]==0u && l.indices[3]==6u && l.indices[11]==20u);
    /* Re-discover the owner after each mutation so failures exercise the
     * layout contract rather than merely the outer image-hash binding. */
    const size_t mutations[]={0x2c9748u+34u,0x2c9748u+116u,0x2c9748u+196u,
        0x2ac4d6u+38u,0x2ac4d6u+54u,0x2ac564u,0x2a0fdcu+84u,0x2a1114u,
        0x1d6c8u-10u,0x1d6c8u-4u,0x1d784u,0x1d788u,0x198ebcu,
        0x2ac1e0u+24u,0x2ac1e0u+36u,0x2ac2f4u,0x2abf84u+32u,0x2ac078u,0x2ac07cu,0x2ac084u,0x2ac088u};
    for(unsigned i=0;i<sizeof(mutations)/sizeof(mutations[0]);i++) {
        memcpy(p,original,n);CHECK(mutations[i]<n);p[mutations[i]]^=1u;
        CHECK(kui_vmu_native_discover(p,n,0x8c010000u,&d)==KUI_VMU_NATIVE_RESOLVED);
        CHECK(!kui_vmu_native_layout_discover(p,n,&d,&l));
    }
    /* A second otherwise genuine TMU1 dispatch candidate is ambiguous. */
    /* Reuse a complete dispatch at a four-byte-aligned displacement inside
     * the image while retaining identical PC-relative literal offsets. */
    memcpy(p,original,n);memcpy(p+0x200u,p+0x2abf84u,0x100u);
    CHECK(kui_vmu_native_discover(p,n,0x8c010000u,&d)==KUI_VMU_NATIVE_RESOLVED);
    CHECK(!kui_vmu_native_layout_discover(p,n,&d,&l));
    memcpy(p,original,n);CHECK(!kui_vmu_native_layout_discover(p,n-1u,&d,&l));
    free(original);free(p);
}
int main(int argc,char **argv) {
    memory=calloc(1u,0x1000000u);CHECK(memory);binding_tests();
    if(argc==2) image_tests(argv[1]);else CHECK(argc==1);
    free(memory);printf("Native layout: %u checks passed%s\n",checks,argc==2?" including real startup mutations":"");return 0;
}
