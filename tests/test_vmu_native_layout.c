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
    l->input_array=0x0c02002cu;l->rx_base=0xac020040u;l->inputs=12u;l->gun_flag=0x0c600000u;
    l->timer1_callback=0x0c600010u;l->timer2_callback=0x0c600020u;
    static const uint32_t ids[12]={0,1,2,6,7,8,12,13,14,18,19,20};memcpy(l->indices,ids,sizeof(ids));
    c->version=1u;c->bytes=sizeof(*c);c->lease_address=0x0c700000u;c->lease_bytes=0x30000u;
    c->lease_header_bytes=32u;c->required_lease_bytes=0x30000u;
    put(d->manager_cell,d->manager_initial);put(d->manager_initial+0xfc4u,l->tx_base);put(d->manager_initial+0xfc8u,l->rx_base);
    for(unsigned i=0;i<12u;i++) {
        uint32_t raw=0x0c500004u+0x1000u*i,expected=0xac500020u+0x1000u*i;
        l->input_values[i]=raw;l->rx_slots[ids[i]]=expected;l->rx_slots[ids[i]+24u]=expected+0x400u;
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
        synthetic(&d,&l,&c);before=c;put(l.input_array,bad[i]);l.input_values[0]=bad[i];
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
    synthetic(&d,&l,&c);l.input_array=0xac02002cu;CHECK(kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);l.input_array=c.lease_address-32u;l.rx_base=l.input_array|0xa0000000u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);l.input_array=d.load_address+d.image_bytes;l.rx_base=l.input_array|0xa0000000u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    synthetic(&d,&l,&c);d.image_bytes=0xffffffffu;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    struct kui_vmu_native_layout_diagnostic diag;
    synthetic(&d,&l,&c);CHECK(kui_vmu_native_layout_bind_checked(&l,&d,read_memory,NULL,&c,&diag));CHECK(!diag.code);
    synthetic(&d,&l,&c);put(l.input_array,l.input_values[0]^32u);before=c;
    CHECK(!kui_vmu_native_layout_bind_checked(&l,&d,read_memory,NULL,&c,&diag));
    CHECK(diag.code==KUI_VMU_LAYOUT_BIND_INPUT_VALUE && diag.address==l.input_array &&
        diag.value==(l.input_values[0]^32u) && diag.expected==l.input_values[0]);CHECK(!memcmp(&c,&before,sizeof(c)));
    synthetic(&d,&l,&c);l.version=3u;CHECK(!kui_vmu_native_layout_bind_checked(&l,&d,read_memory,NULL,&c,&diag));
    CHECK(diag.code==KUI_VMU_LAYOUT_BIND_SCHEMA && diag.value==3u && diag.expected==1u);
    synthetic(&d,&l,&c);put(d.manager_initial+0xfc4u,l.tx_base+32u);
    CHECK(!kui_vmu_native_layout_bind_checked(&l,&d,read_memory,NULL,&c,&diag));
    CHECK(diag.code==KUI_VMU_LAYOUT_BIND_TX_VALUE && diag.address==d.manager_initial+0xfc4u &&
        diag.value==l.tx_base+32u && diag.expected==l.tx_base);
    synthetic(&d,&l,&c);l.tx_base=d.load_address;before=c;
    CHECK(!kui_vmu_native_layout_bind_checked(&l,&d,read_memory,NULL,&c,&diag));
    CHECK(diag.code==KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT && diag.address==l.tx_base);
    CHECK(!memcmp(&c,&before,sizeof(c)));
}
static void uniform_binding_tests(void) {
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;
    struct kui_vmu_native_runtime_config c,before;
    synthetic(&d,&l,&c);
    l.rx_layout=KUI_VMU_NATIVE_RX_UNIFORM48;l.inputs=1u;
    l.input_array=0x0c02002cu;l.index_array=0x0c020040u;
    l.input_values[0]=0x0c500004u;l.indices[0]=0x0c400004u;
    l.rx_base=0xac500020u;l.tx_base=0xac400020u;
    put(l.input_array,l.input_values[0]);put(l.index_array,l.indices[0]);
    put(d.manager_initial+0xfc4u,l.tx_base);put(d.manager_initial+0xfc8u,l.rx_base);
    for(unsigned i=0;i<48u;i++) {l.rx_slots[i]=l.rx_base+1024u*i;put(d.manager_initial+0xfe8u+4u*i,l.rx_slots[i]);}
    before=c;CHECK(kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    CHECK(c.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48 && !memcmp(c.rx_slots,l.rx_slots,sizeof(l.rx_slots)));
    CHECK(c.lease_address==before.lease_address && c.lease_bytes==before.lease_bytes);
    struct kui_vmu_native_layout good=l;
    for(unsigned i=0;i<48u;i++) {
        put(d.manager_initial+0xfe8u+4u*i,l.rx_slots[i]^32u);before=c;
        CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));CHECK(!memcmp(&c,&before,sizeof(c)));
        put(d.manager_initial+0xfe8u+4u*i,l.rx_slots[i]);
    }
    for(unsigned which=0;which<2u;which++) {
        uint32_t slot=which?l.index_array:l.input_array,expected=which?l.indices[0]:l.input_values[0];
        put(slot,expected^32u);CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));put(slot,expected);
        unstable_address=slot;unstable_reads=0;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));unstable_address=0;
        refuse_address=slot;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));refuse_address=0;
    }
    l.index_array=d.load_address+d.image_bytes;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));l=good;
    l.inputs=12u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));l=good;
    l.rx_layout=2u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));l=good;
    l.rx_base+=32u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));l=good;
    l.tx_base+=32u;CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));l=good;
    l.input_values[0]=l.indices[0];l.rx_base=l.tx_base;
    CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    l=good;l.indices[0]=d.load_address;l.tx_base=d.load_address|0xa0000000u;
    put(l.index_array,l.indices[0]);before=c;
    CHECK(!kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));CHECK(!memcmp(&c,&before,sizeof(c)));
}
/* Independent SH integer model for the real argument-transport path. These
 * operations decode actual instruction words rather than repeating layout
 * decoder offsets or assigning the expected RX/TX constants to registers.
 * Unrelated external calls adversarially clobber caller-saved registers. */
struct sh_model {uint32_t r[16],pc,pr,sr;bool t;unsigned calls;};
static uint16_t sh_word(uint32_t a) {uint16_t v;CHECK(physical(a)>=0x0c000000u && physical(a)<=0x0cfffffeu);memcpy(&v,memory+physical(a)-0x0c000000u,2u);return v;}
static uint32_t sh_long(uint32_t a) {uint32_t v;CHECK(read_memory(NULL,a,4u,&v));return v;}
static void sh_simple(struct sh_model *m,uint16_t op,uint32_t pc) {
    unsigned n=(op>>8)&15u,s=(op>>4)&15u;
    if(op==0x0009u) return;
    if((op&0xf000u)==0xe000u) m->r[n]=(uint32_t)(int32_t)(int8_t)op;
    else if((op&0xf000u)==0x7000u) m->r[n]+=(uint32_t)(int32_t)(int8_t)op;
    else if((op&0xf000u)==0xd000u) m->r[n]=sh_long(((pc+4u)&~3u)+4u*(op&255u));
    else if((op&0xf000u)==0x9000u) m->r[n]=(uint32_t)(int32_t)(int16_t)sh_word(pc+4u+2u*(op&255u));
    else if((op&0xf000u)==0x1000u) put(m->r[n]+4u*(op&15u),m->r[s]);
    else if((op&0xf000u)==0x5000u) m->r[n]=sh_long(m->r[s]+4u*(op&15u));
    else if((op&0xf00fu)==0x6003u) m->r[n]=m->r[s];
    else if((op&0xf00fu)==0x6002u) m->r[n]=sh_long(m->r[s]);
    else if((op&0xf00fu)==0x2006u) {m->r[n]-=4u;put(m->r[n],m->r[s]);}
    else if((op&0xf00fu)==0x2002u) put(m->r[n],m->r[s]);
    else if((op&0xf00fu)==0x0006u) put(m->r[0]+m->r[n],m->r[s]);
    else if((op&0xf00fu)==0x000eu) m->r[n]=sh_long(m->r[0]+m->r[s]);
    else if((op&0xf00fu)==0x300cu) m->r[n]+=m->r[s];
    else if((op&0xf00fu)==0x3002u) m->t=m->r[n]>=m->r[s];
    else if((op&0xf00fu)==0x3003u) m->t=(int32_t)m->r[n]>=(int32_t)m->r[s];
    else if((op&0xf00fu)==0x2008u) m->t=!(m->r[n]&m->r[s]);
    else if((op&0xf00fu)==0x2009u) m->r[n]&=m->r[s];
    else if((op&0xf00fu)==0x200bu) m->r[n]|=m->r[s];
    else if((op&0xf0ffu)==0x4008u) m->r[n]<<=2u;
    else if((op&0xf0ffu)==0x4009u) m->r[n]>>=2u;
    else if((op&0xf0ffu)==0x4015u) m->t=(int32_t)m->r[n]>0;
    else if((op&0xf0ffu)==0x0002u) m->r[n]=m->sr;
    else if((op&0xf0ffu)==0x400eu) m->sr=m->r[n];
    else if(op==0x4f22u) {m->r[15]-=4u;put(m->r[15],m->pr);}
    else if((op&0xff00u)==0xc900u) m->r[0]&=op&255u;
    else if((op&0xff00u)==0xcb00u) m->r[0]|=op&255u;
    else {fprintf(stderr,"Unsupported transport-model op %04x at %08x\n",op,pc);CHECK(false);}
}
static void transport_model(const uint8_t *image,size_t bytes,const struct kui_vmu_native_discovery *d,
    const struct kui_vmu_native_layout *l) {
    memset(memory,0,0x1000000u);memcpy(memory+physical(d->load_address)-0x0c000000u,image,bytes);
    refuse_address=unstable_address=unstable_reads=0;
    bool uniform=l->rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48;
    uint32_t constructor=0u;
    if(uniform) {uint32_t pc=l->initializer+108u;constructor=sh_long(((pc+4u)&~3u)+4u*(sh_word(pc)&255u));}
    struct sh_model m={0};m.pc=l->initializer_call-(uniform?8u:12u);m.sr=0x600000e0u;m.r[15]=0x0c900000u;m.r[10]=0x12345678u;
    for(unsigned step=0;step<10000u;step++) {
        if(physical(m.pc)==(uniform?physical(constructor)+86u:physical(l->initializer)+238u)) {
            CHECK(m.calls==(uniform?4u:3u));
            CHECK(sh_long(d->manager_initial+0xfc4u)==l->tx_base);
            CHECK(sh_long(d->manager_initial+0xfc8u)==l->rx_base);
            for(unsigned i=0;i<48u;i++) CHECK(sh_long(d->manager_initial+0xfe8u+4u*i)==l->rx_slots[i]);
            return;
        }
        uint16_t op=sh_word(m.pc);uint32_t next=m.pc+2u;
        if((op&0xf0ffu)==0x400bu || (op&0xf000u)==0xb000u) {
            uint32_t target;
            if((op&0xf000u)==0xb000u) {int32_t delta=(int32_t)((op&4095u)^2048u)-2048;target=m.pc+4u+(uint32_t)(2*delta);}
            else target=m.r[(op>>8)&15u];
            m.pr=m.pc+4u;sh_simple(&m,sh_word(m.pc+2u),m.pc+2u);next=m.pc+4u;
            bool enter=m.calls==0u || physical(target)==physical(l->initializer_wrapper) || physical(target)==physical(l->initializer) ||
                (uniform && physical(target)==physical(constructor));
            if(enter) {++m.calls;next=target;} else {
                for(unsigned i=0;i<8u;i++) m.r[i]=0xc0ffee00u+i;
                m.r[0]=0u;
            }
        } else if((op&0xff00u)==0x8900u || (op&0xff00u)==0x8b00u ||
                  (op&0xff00u)==0x8d00u || (op&0xff00u)==0x8f00u) {
            bool wanted=(op&0x0200u)?!m.t:m.t;bool delayed=(op&0x0400u)!=0;
            if(delayed) {sh_simple(&m,sh_word(m.pc+2u),m.pc+2u);next+=2u;}
            if(wanted) next=m.pc+4u+(uint32_t)(2*(int32_t)(int8_t)op);
        } else sh_simple(&m,op,m.pc);
        m.pc=next;
    }
    CHECK(false);
}
static void image_tests(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long z=ftell(f);CHECK(z>0 && z<=0xc00000);rewind(f);
    size_t n=(size_t)z;uint8_t *p=malloc(n),*original=malloc(n);CHECK(p && original);CHECK(fread(p,1,n,f)==n);fclose(f);memcpy(original,p,n);
    struct kui_vmu_native_discovery d,coupled,zero_driver={0};
    struct kui_vmu_native_layout l,coupled_layout,zero_layout={0};
    CHECK(kui_vmu_native_discover(p,n,0x8c010000u,&d)==KUI_VMU_NATIVE_RESOLVED);
    CHECK(kui_vmu_native_layout_discover(p,n,&d,&l));
    /* Fresh driver and layout resolution is one transaction. Preexisting
     * output bytes cannot be treated as a trusted cached resolution. */
    memset(&coupled,0xa5,sizeof(coupled));memset(&coupled_layout,0xa5,sizeof(coupled_layout));
    CHECK(kui_vmu_native_discover_layout(p,n,0x8c010000u,&coupled,&coupled_layout));
    CHECK(!memcmp(&d,&coupled,sizeof(d)) && !memcmp(&l,&coupled_layout,sizeof(l)));
    CHECK(l.initializer==0x8c2d9748u && l.initializer_wrapper==0x8c2bc4d6u && l.initializer_call==0x8c02d6c8u);
    CHECK(l.tx_base==0xac315180u && l.rx_base==0xac20ea60u && l.input_array==0x0c20ea4cu && l.inputs==12u);
    CHECK(l.input_values[0]==0x0c30f000u && l.input_values[11]==0x0c314960u);
    CHECK(l.gun_flag==0x0c8c2178u && l.timer1_callback==0x0c8c1be8u && l.timer2_callback==0x0c8c1bf0u);
    CHECK(l.indices[0]==0u && l.indices[3]==6u && l.indices[11]==20u);
    transport_model(p,n,&d,&l);
    struct kui_vmu_native_runtime_config real_config={.version=1u,.bytes=sizeof(real_config),
        .lease_address=0x0c38a880u,.lease_bytes=0x30000u,.lease_header_bytes=32u,.required_lease_bytes=0x10000u};
    struct kui_vmu_native_layout_diagnostic real_diag;
    CHECK(kui_vmu_native_layout_bind_checked(&l,&d,read_memory,NULL,&real_config,&real_diag));
    CHECK(!real_diag.code && real_config.rx_base==l.rx_base && !memcmp(real_config.rx_slots,l.rx_slots,sizeof(l.rx_slots)));
    struct kui_vmu_native_layout wrong=l;wrong.tx_base=0xac20ea60u;wrong.input_array=0x0c315180u;wrong.rx_base=0xac315180u;
    CHECK(!kui_vmu_native_layout_bind_checked(&wrong,&d,read_memory,NULL,&real_config,&real_diag));
    CHECK(real_diag.code==KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT);
    struct kui_vmu_native_discovery forged=d;forged.submit+=2u;
    CHECK(!kui_vmu_native_layout_discover(p,n,&forged,&l));
    CHECK(!memcmp(&l,&zero_layout,sizeof(l)));
    /* Failed argument/alias admission must not write into the executable or
     * a second output object, including physically aliased RAM on target. */
    CHECK(!kui_vmu_native_discover_layout(p,n,0x8c010000u,(void *)p,&l));
    CHECK(!memcmp(p,original,n));
    CHECK(!kui_vmu_native_discover_layout(p,n,0x8c010000u,&d,(void *)&d));
    CHECK(!kui_vmu_native_discover_layout((const void *)(uintptr_t)0x8c100000u,
        0x1000u,0x8c100000u,(void *)(uintptr_t)0xac100100u,&l));
    /* Re-discover the owner after each mutation so failures exercise the
     * layout contract rather than merely the outer image-hash binding. */
    const size_t mutations[]={0x2c9748u+34u,0x2c9748u+116u,0x2c9748u+196u,
        0x2ac4d6u+38u,0x2ac4d6u+54u,0x2ac564u,0x2a0fdcu+84u,0x2a1114u,
        0x1d6c8u-10u,0x1d6c8u-3u,0x1d784u,0x1d788u,0x198ebcu,
        0x2ac1e0u+24u,0x2ac1e0u+36u,0x2ac2f4u,0x2abf84u+32u,0x2ac078u,0x2ac07cu,0x2ac084u,0x2ac088u};
    for(unsigned i=0;i<sizeof(mutations)/sizeof(mutations[0]);i++) {
        memcpy(p,original,n);CHECK(mutations[i]<n);p[mutations[i]]^=1u;
        CHECK(kui_vmu_native_discover(p,n,0x8c010000u,&d)==KUI_VMU_NATIVE_RESOLVED);
        CHECK(!kui_vmu_native_layout_discover(p,n,&d,&l));
        CHECK(!kui_vmu_native_discover_layout(p,n,0x8c010000u,&coupled,&coupled_layout));
        CHECK(!memcmp(&coupled,&zero_driver,sizeof(coupled)) &&
            !memcmp(&coupled_layout,&zero_layout,sizeof(coupled_layout)));
    }
    const size_t argument_gaps[]={0x2a0fdcu+18u,0x2a0fdcu+22u,0x2a0fdcu+26u,0x2a0fdcu+28u,
        0x2a0fdcu+36u,0x2a0fdcu+44u,0x2a0fdcu+46u,0x2a0fdcu+68u,
        0x2ac4d6u+32u,0x2ac4d6u+34u,0x2ac4d6u+40u};
    for(unsigned i=0;i<sizeof(argument_gaps)/sizeof(argument_gaps[0]);i++) {
        memcpy(p,original,n);p[argument_gaps[i]]=0x00u;p[argument_gaps[i]+1u]=0xefu;
        CHECK(kui_vmu_native_discover(p,n,0x8c010000u,&d)==KUI_VMU_NATIVE_RESOLVED);
        CHECK(!kui_vmu_native_layout_discover(p,n,&d,&l));
        CHECK(!kui_vmu_native_discover_layout(p,n,0x8c010000u,&coupled,&coupled_layout));
    }
    /* An unregistered copied dispatcher is inert data. A second candidate
     * becomes ambiguous only when its VBR registration names that copy. */
    memcpy(p,original,n);memcpy(p+0x200u,p+0x2abf84u,0x100u);
    CHECK(kui_vmu_native_discover(p,n,0x8c010000u,&d)==KUI_VMU_NATIVE_RESOLVED);
    CHECK(kui_vmu_native_layout_discover(p,n,&d,&l));
    CHECK(l.timer1_callback==0x0c8c1be8u && l.timer2_callback==0x0c8c1bf0u);
    CHECK(kui_vmu_native_discover_layout(p,n,0x8c010000u,&coupled,&coupled_layout));
    memcpy(p,original,n);memcpy(p+0x100u,p+0x2abe84u,0x200u);
    size_t pc=0x156u;uint16_t op=(uint16_t)(p[pc]|(uint16_t)p[pc+1u]<<8);
    CHECK((op&0xff00u)==0xd200u);
    size_t handler_slot=((pc+4u)&~3u)+4u*(op&255u);CHECK(handler_slot>=0x100u && handler_slot+4u<=0x300u);
    uint32_t copied_handler=0x8c010200u;
    for(unsigned i=0;i<4u;i++) p[handler_slot+i]=(uint8_t)(copied_handler>>(8u*i));
    CHECK(kui_vmu_native_discover(p,n,0x8c010000u,&d)==KUI_VMU_NATIVE_RESOLVED);
    CHECK(!kui_vmu_native_layout_discover(p,n,&d,&l));
    CHECK(!kui_vmu_native_discover_layout(p,n,0x8c010000u,&coupled,&coupled_layout));
    memcpy(p,original,n);CHECK(!kui_vmu_native_layout_discover(p,n-1u,&d,&l));
    free(original);free(p);
}
static void uniform_image_tests(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long z=ftell(f);CHECK(z>0 && z<=0xc00000);rewind(f);
    size_t n=(size_t)z;uint8_t *p=malloc(n),*original=malloc(n);CHECK(p && original);CHECK(fread(p,1,n,f)==n);fclose(f);memcpy(original,p,n);
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;
    CHECK(kui_vmu_native_discover_layout(p,n,0x8c010000u,&d,&l));
    CHECK(l.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48 && l.inputs==1u && l.initializer==d.initializer);
    CHECK(l.input_array>=d.load_address && l.index_array>=d.load_address);
    transport_model(p,n,&d,&l);
    struct kui_vmu_native_runtime_config config={.version=1u,.bytes=sizeof(config),
        .lease_address=0x8cf00000u,.lease_bytes=0x30000u,.lease_header_bytes=32u,.required_lease_bytes=0x10000u};
    uint32_t image_begin=physical(d.load_address),image_end=image_begin+d.image_bytes;
    bool image_conflict=(physical(l.rx_base)<image_end && image_begin<physical(l.rx_base)+0xc000u) ||
        (physical(l.tx_base)<image_end && image_begin<physical(l.tx_base)+0x6000u);
    struct kui_vmu_native_layout_diagnostic diag;
    bool bound=kui_vmu_native_layout_bind_checked(&l,&d,read_memory,NULL,&config,&diag);
    CHECK(l.version==(image_conflict?KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION:KUI_VMU_NATIVE_LAYOUT_VERSION));
    CHECK(bound);
    CHECK(config.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48 && !memcmp(config.rx_slots,l.rx_slots,sizeof(l.rx_slots)));
    /* Resolve anew after each mutation: the witnesses, rather than a stale
     * outer hash, must reject changed initializer/constructor/root transport. */
    uint32_t pc=d.initializer+108u;uint16_t code=sh_word(pc);
    uint32_t constructor=sh_long(((pc+4u)&~3u)+4u*(code&255u));
    const uint32_t locations[]={d.initializer+22u,d.initializer+38u,d.initializer+104u,
        constructor+34u,constructor+48u,constructor+80u,constructor+82u,
        l.initializer_wrapper+8u,l.initializer_wrapper+44u,l.initializer_wrapper+50u,
        l.initializer_call,l.input_array,l.index_array};
    for(unsigned i=0;i<sizeof(locations)/sizeof(locations[0]);i++) {
        size_t offset=physical(locations[i])-physical(d.load_address);CHECK(offset<n);
        memcpy(p,original,n);p[offset]^=1u;
        struct kui_vmu_native_discovery changed;struct kui_vmu_native_layout refused;
        bool found=kui_vmu_native_discover_layout(p,n,0x8c010000u,&changed,&refused);
        if(i+2u<sizeof(locations)/sizeof(locations[0])) CHECK(!found);
        else if(found) {
            /* A changed scalar can still be a valid new immutable layout;
             * live binding must refuse the unchanged owner state. */
            CHECK(!kui_vmu_native_layout_bind(&refused,&changed,read_memory,NULL,&config));
        }
    }
    /* Every formerly sparse upper-prefix load and the wrapper's direct lock
     * call must remain a decoded instruction, rather than an arbitrary write
     * to R15 or to an argument before it is saved. */
    uint32_t upper_pc=l.initializer_call-2u;
    unsigned target_reg=(sh_word(l.initializer_call)>>8)&15u;
    for(size_t s=physical(l.initializer_call)-physical(d.load_address)-8u;s<physical(l.initializer_call)-physical(d.load_address);s+=2u) {
        uint16_t op=(uint16_t)(original[s]|(uint16_t)original[s+1u]<<8);
        if((op&0xff00u)==(0xd000u|(target_reg<<8))) {
            size_t slot=((s+4u)&~3u)+4u*(op&255u);
            upper_pc=(uint32_t)original[slot]|(uint32_t)original[slot+1u]<<8|
                (uint32_t)original[slot+2u]<<16|(uint32_t)original[slot+3u]<<24;
        }
    }
    const unsigned gaps[]={18u,22u,26u,28u,38u,42u,44u,66u};
    for(unsigned i=0;i<sizeof(gaps)/sizeof(gaps[0])+1u;i++) {
        uint32_t address=i<sizeof(gaps)/sizeof(gaps[0])?upper_pc+gaps[i]:l.initializer_wrapper+36u;
        size_t offset=physical(address)-physical(d.load_address);CHECK(offset+2u<=n);
        memcpy(p,original,n);p[offset]=0x00u;p[offset+1u]=0xefu;
        struct kui_vmu_native_discovery bad_driver;struct kui_vmu_native_layout bad_layout;
        CHECK(!kui_vmu_native_discover_layout(p,n,0x8c010000u,&bad_driver,&bad_layout));
    }
    /* An indirect call samples its target before the delay slot. Moving the only
     * wrapper-target load into that slot cannot prove the actual target,
     * even though all four argument/target registers are known afterward. */
    memcpy(p,original,n);
    size_t root=physical(l.initializer_call)-physical(d.load_address),target_load=0;
    bool mode_before=false;
    for(size_t s=root-8u;s<root;s+=2u) {
        uint16_t op=(uint16_t)(p[s]|(uint16_t)p[s+1u]<<8);
        if((op&0xff00u)==(0xd000u|(target_reg<<8))) target_load=s;
        if(op==0xe400u) mode_before=true;
    }
    CHECK(target_load);
    uint16_t target_op=(uint16_t)(p[target_load]|(uint16_t)p[target_load+1u]<<8);
    size_t slot=((target_load+4u)&~3u)+4u*(target_op&255u),new_pc=(root+6u)&~3u;
    CHECK(slot>=new_pc && slot-new_pc<=1020u);
    uint16_t replacement=(uint16_t)(mode_before?0x0009u:0xe400u);
    p[target_load]=(uint8_t)replacement;p[target_load+1u]=(uint8_t)(replacement>>8);
    uint16_t delayed=(uint16_t)(0xd000u|(target_reg<<8)|((slot-new_pc)/4u));
    p[root+2u]=(uint8_t)delayed;p[root+3u]=(uint8_t)(delayed>>8);
    struct kui_vmu_native_discovery refused_driver;struct kui_vmu_native_layout refused_layout;
    CHECK(!kui_vmu_native_discover_layout(p,n,0x8c010000u,&refused_driver,&refused_layout));
    free(original);free(p);
}
int main(int argc,char **argv) {
    memory=calloc(1u,0x1000000u);CHECK(memory);binding_tests();uniform_binding_tests();
    if(argc>=2) image_tests(argv[1]);
    for(int i=2;i<argc;i++) uniform_image_tests(argv[i]);
    free(memory);printf("Native layout: %u checks passed%s\n",checks,argc==2?" including real startup mutations":"");return 0;
}
