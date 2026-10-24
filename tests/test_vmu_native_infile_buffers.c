/* SPDX-License-Identifier: GPL-3.0-only */
/* Original executables are optional read-only inputs, never test fixtures in
 * the repository. Exercise the same private source proof and public binder. */
#include "../src/core/vmu_native_layout.c"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"In-file buffer check failed at %u: %s\n",__LINE__,#x);exit(1);}} while(0)
#define BASE UINT32_C(0x8c010000)
#define FIXTURE_BYTES UINT32_C(0x80000)
static uint8_t source[FIXTURE_BYTES];
static uint8_t *memory;

static void put(uint32_t a,uint32_t value) {
    CHECK(phys(a)>=0x0c000000u && phys(a)<=0x0cfffffcu);
    memcpy(memory+phys(a)-0x0c000000u,&value,4u);
}
static bool read_memory(void *context,uint32_t a,unsigned width,uint32_t *value) {
    (void)context;
    if(width!=4u || !ram(a,4u,4u)) return false;
    memcpy(value,memory+phys(a)-0x0c000000u,4u);return true;
}
static struct kui_vmu_native_runtime_config config(void) {
    struct kui_vmu_native_runtime_config c={.version=1u,.bytes=sizeof(c),
        .lease_address=0x0c0b0000u,.lease_bytes=0x30000u,.lease_header_bytes=32u,
        .required_lease_bytes=0x30000u};
    return c;
}
static void synth(struct kui_vmu_native_discovery *d,struct kui_vmu_native_layout *l) {
    memset(source,0,sizeof(source));memset(d,0,sizeof(*d));memset(l,0,sizeof(*l));
    *d=(struct kui_vmu_native_discovery){.version=1u,.bytes=sizeof(*d),.family=1u,
        .requirements=7u,.load_address=BASE,.image_bytes=sizeof(source),
        .manager_initial=0x8c080000u,.manager_cell=0x8c081100u,.lock_cell=0x8c0a0100u,
        .submit=0x8c011000u,.initializer=0x8c011100u,.completion_call=0x8c011200u,
        .consumer=0x8c011300u,.gate=0x8c011400u,.ready=0x8c011500u};
    *l=(struct kui_vmu_native_layout){.version=KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION,
        .bytes=sizeof(*l),.initializer=d->initializer,.initializer_wrapper=0x8c011600u,
        .initializer_call=0x8c011700u,.tx_base=0xac020020u,.rx_layout=KUI_VMU_NATIVE_RX_UNIFORM48,
        .rx_base=0xac030020u,.gun_flag=0x8c0a0000u,.timer1_callback=0x8c0a0010u,
        .timer2_callback=0x8c0a0020u,.inputs=1u,.input_array=0x8c012000u,.index_array=0x8c012004u};
    l->input_values[0]=0x0c030004u;l->indices[0]=0x0c020004u;
    for(unsigned i=0;i<48u;i++) l->rx_slots[i]=l->rx_base+1024u*i;
    memcpy(source+phys(l->input_array)-phys(BASE),&l->input_values[0],4u);
    memcpy(source+phys(l->index_array)-phys(BASE),&l->indices[0],4u);
}
static void live(const uint8_t *p,size_t n,const struct kui_vmu_native_discovery *d,
    const struct kui_vmu_native_layout *l) {
    memset(memory,0,0x1000000u);memcpy(memory+phys(d->load_address)-0x0c000000u,p,n);
    /* The SDK can already have used these buffers. Source-zero certification
     * belongs to pre-init discovery, not a destructive live-memory recheck. */
    memset(memory+phys(l->tx_base)-0x0c000000u,0xa5,0x6000u);
    if(l->rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48)
        memset(memory+phys(l->rx_base)-0x0c000000u,0x5a,0xc000u);
    put(d->manager_cell,d->manager_initial);put(d->manager_initial+0xfc4u,l->tx_base);
    put(d->manager_initial+0xfc8u,l->rx_base);
    for(unsigned i=0;i<48u;i++) put(d->manager_initial+0xfe8u+4u*i,l->rx_slots[i]);
}
static void move_uniform(struct kui_vmu_native_layout *l,bool tx,uint32_t raw) {
    uint32_t value=((raw+31u)&~31u)|0xa0000000u;
    if(tx) l->indices[0]=raw,l->tx_base=value;
    else {
        l->input_values[0]=raw;l->rx_base=value;
        for(unsigned i=0;i<48u;i++) l->rx_slots[i]=value+1024u*i;
    }
}
static void refused(const struct kui_vmu_native_layout *l,const struct kui_vmu_native_discovery *d,
    struct kui_vmu_native_runtime_config c) {
    struct kui_vmu_native_runtime_config before=c;
    struct kui_vmu_native_layout_diagnostic diag={0};
    CHECK(!kui_vmu_native_layout_bind_checked(l,d,read_memory,NULL,&c,&diag));
    CHECK(diag.code!=0u);CHECK(!memcmp(&c,&before,sizeof(c)));
}
static void geometry_tests(void) {
    bool inside;
    const uint32_t aliases[]={0x0c030020u,0x8c030020u,0xac030020u};
    for(unsigned i=0;i<3u;i++) {
        CHECK(ring_image_location(BASE,FIXTURE_BYTES,aliases[i],0xc000u,&inside));CHECK(inside);
    }
    CHECK(ring_image_location(BASE,FIXTURE_BYTES,0xac100000u,0xc000u,&inside));CHECK(!inside);
    CHECK(ring_image_location(BASE+0x10000u,FIXTURE_BYTES,0xac010000u,0x10000u,&inside));CHECK(!inside);
    CHECK(ring_image_location(BASE,FIXTURE_BYTES,0xac090000u,0xc000u,&inside));CHECK(!inside);
    CHECK(!ring_image_location(BASE,FIXTURE_BYTES,0xac008000u,0xc000u,&inside));
    CHECK(!ring_image_location(BASE+0x10000u,FIXTURE_BYTES,0xac018000u,0xc000u,&inside));
    CHECK(!ring_image_location(BASE,FIXTURE_BYTES,0xac088000u,0xc000u,&inside));
    CHECK(!ring_image_location(BASE,FIXTURE_BYTES,0xec030020u,0xc000u,&inside));
    CHECK(!ring_image_location(BASE,FIXTURE_BYTES,0xac030024u,0xc000u,&inside));
    CHECK(!ring_image_location(BASE,FIXTURE_BYTES,0xacfff000u,0xc000u,&inside));
}
static void source_tests(void) {
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;bool infile;
    synth(&d,&l);CHECK(source_buffers(source,sizeof(source),&d,&l,&infile));CHECK(infile);
    for(unsigned which=0;which<2u;which++) {
        uint32_t bytes=which?0xc000u:0x6000u,address=which?l.rx_base:l.tx_base;
        size_t offset=phys(address)-phys(BASE);
        const size_t positions[]={0u,1u,31u,1023u,bytes/2u,bytes-1u};
        for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);i++) {
            source[offset+positions[i]]=1u;
            CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));
            source[offset+positions[i]]=0u;
        }
    }
    struct kui_vmu_native_discovery good_d=d;struct kui_vmu_native_layout good_l=l;
    uint32_t *protected_fields[]={&d.manager_initial,&d.manager_cell,&d.lock_cell,
        &l.gun_flag,&l.timer1_callback,&l.timer2_callback,&l.input_array,&l.index_array,
        &d.submit,&d.initializer,&l.initializer,&l.initializer_wrapper,&l.initializer_call,
        &d.completion_call,&d.consumer,&d.gate,&d.ready};
    for(unsigned ring=0;ring<2u;ring++) for(unsigned i=0;i<sizeof(protected_fields)/sizeof(protected_fields[0]);i++) {
        *protected_fields[i]=ring?good_l.rx_base:good_l.tx_base;
        CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));d=good_d;l=good_l;
    }
    l.timer1_callback=l.tx_base-4u;CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));l=good_l;
    l.rx_base=l.tx_base;CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));l=good_l;
    move_uniform(&l,true,0x0c08c004u);CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));l=good_l;
    move_uniform(&l,false,0x0c088004u);CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));l=good_l;
    move_uniform(&l,true,0x0c100004u);move_uniform(&l,false,0x0c110004u);
    CHECK(source_buffers(source,sizeof(source),&d,&l,&infile));CHECK(!infile);
    move_uniform(&l,true,good_l.indices[0]);CHECK(source_buffers(source,sizeof(source),&d,&l,&infile));CHECK(infile);
}
static void binding_tests(void) {
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;bool infile;
    synth(&d,&l);CHECK(source_buffers(source,sizeof(source),&d,&l,&infile));CHECK(infile);
    live(source,sizeof(source),&d,&l);
    struct kui_vmu_native_runtime_config c=config(),before=c;struct kui_vmu_native_layout_diagnostic diag;
    CHECK(kui_vmu_native_layout_bind_checked(&l,&d,read_memory,NULL,&c,&diag));CHECK(!diag.code);
    CHECK(c.lease_address==before.lease_address && c.lease_bytes==before.lease_bytes);
    CHECK(!memcmp(c.rx_slots,l.rx_slots,sizeof(c.rx_slots)));
    struct kui_vmu_native_layout good_l=l;struct kui_vmu_native_discovery good_d=d;
    l.version=KUI_VMU_NATIVE_LAYOUT_VERSION;refused(&l,&d,before);l=good_l;
    for(unsigned ring=0;ring<2u;ring++) {
        uint32_t base=ring?l.rx_base:l.tx_base;
        uint32_t *fields[]={&d.manager_initial,&d.manager_cell,&d.lock_cell,&l.gun_flag,&l.timer1_callback,&l.timer2_callback};
        for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
            *fields[i]=base;refused(&l,&d,before);d=good_d;l=good_l;
        }
        c=before;c.lease_address=base;refused(&l,&d,c);
    }
    l.timer1_callback=l.tx_base-4u;refused(&l,&d,before);l=good_l;
    l.input_array=l.rx_base;refused(&l,&d,before);l=good_l;
    l.index_array=l.rx_base;refused(&l,&d,before);l=good_l;
    l.input_array=l.tx_base;refused(&l,&d,before);l=good_l;
    l.index_array=l.tx_base;refused(&l,&d,before);l=good_l;
    move_uniform(&l,false,l.indices[0]);refused(&l,&d,before);l=good_l;
    move_uniform(&l,true,0x0c08c004u);refused(&l,&d,before);l=good_l;
    move_uniform(&l,false,0x0c088004u);refused(&l,&d,before);l=good_l;
    /* Version 2 is not a bypass switch for a wholly external layout. */
    move_uniform(&l,true,0x0c100004u);move_uniform(&l,false,0x0c110004u);
    live(source,sizeof(source),&d,&l);
    put(l.input_array,l.input_values[0]);put(l.index_array,l.indices[0]);
    refused(&l,&d,before);l.version=KUI_VMU_NATIVE_LAYOUT_VERSION;
    c=before;CHECK(kui_vmu_native_layout_bind_checked(&l,&d,read_memory,NULL,&c,&diag));
}
static void explicit_tests(void) {
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout l;bool infile;
    synth(&d,&l);l.rx_layout=KUI_VMU_NATIVE_RX_EXPLICIT48;l.inputs=12u;
    l.input_array=0x8c012000u;l.index_array=0x8c013000u;l.rx_base=0xac012000u;
    memset(l.rx_slots,0,sizeof(l.rx_slots));memset(l.indices,0,sizeof(l.indices));
    memset(l.input_values,0,sizeof(l.input_values));
    static const uint32_t indices[12]={0,1,2,6,7,8,12,13,14,18,19,20};
    for(unsigned i=0;i<12u;i++) {
        l.indices[i]=indices[i];l.input_values[i]=0x0c030004u+0x1000u*i;
        uint32_t buffer=0xac030020u+0x1000u*i;
        l.rx_slots[indices[i]]=buffer;l.rx_slots[indices[i]+24u]=buffer+0x400u;
        memcpy(source+phys(l.input_array)-phys(BASE)+4u*i,&l.input_values[i],4u);
        memcpy(source+phys(l.index_array)-phys(BASE)+4u*i,&l.indices[i],4u);
    }
    CHECK(source_buffers(source,sizeof(source),&d,&l,&infile));CHECK(infile);
    live(source,sizeof(source),&d,&l);
    struct kui_vmu_native_runtime_config c=config();
    CHECK(kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    CHECK(c.rx_layout==KUI_VMU_NATIVE_RX_EXPLICIT48 && !memcmp(c.rx_slots,l.rx_slots,sizeof(c.rx_slots)));
    struct kui_vmu_native_layout good=l;
    for(unsigned i=0;i<12u;i++) {
        size_t offset=phys(l.rx_slots[l.indices[i]])-phys(BASE)+0x7ffu;
        source[offset]=1u;CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));source[offset]=0u;
    }
    l.input_values[0]=0x0c08fc04u;l.rx_slots[0]=0xac08fc20u;l.rx_slots[24]=0xac090020u;
    CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));
    put(l.input_array,l.input_values[0]);refused(&l,&d,config());l=good;
    l.rx_slots[0]=l.tx_base;CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));l=good;
    l.index_array=l.rx_slots[0];CHECK(!source_buffers(source,sizeof(source),&d,&l,&infile));
    refused(&l,&d,config());l=good;
    l.index_array=l.tx_base;refused(&l,&d,config());l=good;
    l.index_array=config().lease_address;refused(&l,&d,config());l=good;
    l.index_array=d.load_address+d.image_bytes-4u;refused(&l,&d,config());l=good;
    l.index_array=d.load_address+d.image_bytes;refused(&l,&d,config());
}
static void image_tests(const char *path,unsigned expected_version) {
    FILE *f=fopen(path,"rb");CHECK(f!=NULL);CHECK(!fseek(f,0,SEEK_END));long z=ftell(f);CHECK(z>0 && z<=0xc00000);rewind(f);
    size_t n=(size_t)z;uint8_t *p=malloc(n);CHECK(p!=NULL);CHECK(fread(p,1u,n,f)==n);CHECK(!fclose(f));
    struct kui_vmu_native_discovery d,zero_d={0};struct kui_vmu_native_layout l,zero_l={0};
    CHECK(kui_vmu_native_discover_layout(p,n,BASE,&d,&l));CHECK(l.version==expected_version);
    bool infile;CHECK(source_buffers(p,n,&d,&l,&infile));CHECK(infile==(expected_version==KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION));
    live(p,n,&d,&l);struct kui_vmu_native_runtime_config c=config();
    c.lease_address=0x0cfd0000u;c.lease_bytes=0x20000u;c.required_lease_bytes=0x20000u;
    CHECK(kui_vmu_native_layout_bind(&l,&d,read_memory,NULL,&c));
    if(infile) {
        uint32_t addresses[2]={l.tx_base,l.rx_base},lengths[2]={0x6000u,0xc000u};
        for(unsigned ring=0;ring<2u;ring++) {
            bool inside;CHECK(ring_image_location(BASE,(uint32_t)n,addresses[ring],lengths[ring],&inside));
            if(!inside) continue;
            size_t start=phys(addresses[ring])-phys(BASE);
            const size_t offsets[]={0u,31u,1023u,lengths[ring]/2u,lengths[ring]-1u};
            for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
                size_t at_=start+offsets[i];CHECK(!p[at_]);p[at_]=1u;
                struct kui_vmu_native_discovery mutated_d;struct kui_vmu_native_layout mutated_l;
                CHECK(!kui_vmu_native_discover_layout(p,n,BASE,&mutated_d,&mutated_l));
                CHECK(!memcmp(&mutated_d,&zero_d,sizeof(mutated_d)) && !memcmp(&mutated_l,&zero_l,sizeof(mutated_l)));
                p[at_]=0u;
            }
        }
        CHECK(kui_vmu_native_discover_layout(p,n,BASE,&d,&l));CHECK(l.version==expected_version);
    }
    printf("In-file buffers: version %u verified for %s\n",expected_version,path);free(p);
}
int main(int argc,char **argv) {
    memory=calloc(1u,0x1000000u);CHECK(memory!=NULL);geometry_tests();source_tests();binding_tests();explicit_tests();
    for(int i=1;i<argc;i++) image_tests(argv[i],i==1?KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION:KUI_VMU_NATIVE_LAYOUT_VERSION);
    printf("Native in-file ring proof/binding: %u checks passed\n",checks);free(memory);return 0;
}
