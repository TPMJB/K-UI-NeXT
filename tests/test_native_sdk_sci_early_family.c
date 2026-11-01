/* SPDX-License-Identifier: GPL-3.0-only */
/* Early Shinobi SDK SCI interrupt family. The original executable is supplied
 * externally (argv[1]); none of its bytes are embedded here. Later arguments
 * are probed with a live model seeded from their own images and, for the
 * early titles, with the state their own install routine leaves when it is
 * executed instruction by instruction over one main-RAM model, which the
 * route then reads as both the image and the live memory, as on console. */
#include "kui/native_sdk_sci_route.h"
#include "kui/native_sdk_sci_family.h"
#include "kui/hash.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD KUI_NATIVE_SDK_SCI_ROUTE_LOAD
#define CALLBACK UINT32_C(0x8c007300)
#define FIXTURE_BYTES 0x4000u
#define E_AT 0x400u
#define I_AT (E_AT+0x34u)
#define C_AT (I_AT+0x34u)
#define EVENT_AT (C_AT+0x2d4u)
#define D_AT (EVENT_AT+0xcu)
#define INIT_AT 0x1802u
#define INSTALL_AT (INIT_AT-0x15au)
#define N_AT (C_AT+0x14cu)
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"early SDK SCI check failed at %u\n",__LINE__);exit(1);}} while(0)
static unsigned checks,reads,fail_read;
static uint8_t *image,*live,stub600[0x34];
static size_t bytes,stub_at,default_at;
static uint32_t vbr,eri,rxi,area,live_common;
/* Roles that only the nesting handler's admission depends on: the install
 * routine's four literals and the nesting handler's event word. */
static size_t nesting_roles[5];

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put(uint8_t *p,uint32_t value) {for(unsigned i=0u;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));}
static uint32_t role(uint32_t address) {return (address&UINT32_C(0x1fffffff))|area;}
static bool read_word(void *context,uint32_t address,uint32_t *out) {
    CHECK(context==live && out && !(address&3u));
    ++reads;if(fail_read && reads==fail_read)return false;
    if(address>=vbr+0x600u && address<vbr+0x634u) {*out=word(stub600+address-(vbr+0x600u));return true;}
    if(address==vbr+0x25cu) {*out=eri;return true;}
    if(address==vbr+0x260u) {*out=rxi;return true;}
    if(address<LOAD || address-LOAD>bytes || bytes-(address-LOAD)<4u)return false;
    *out=word(live+address-LOAD);return true;
}
static void seed(void) {
    memcpy(live,image,bytes);memcpy(stub600,image+stub_at,sizeof(stub600));
    put(stub600+0x2cu,UINT32_C(0xfedcba98)); /* the stub's mutable R0 scratch */
    eri=rxi=role(LOAD+(uint32_t)default_at);reads=fail_read=0u;
    live_common=role(LOAD+(uint32_t)stub_at+0x34u);
}
static uint32_t plain_handler(void) {return role(LOAD+(uint32_t)stub_at+0x34u);}
static uint32_t nesting_handler(void) {return role(LOAD+(uint32_t)stub_at+0x34u+0x14cu);}
/* The install routine writes its selected handler into both stub templates
 * in the executable, then copies them to the vector table. */
static void templates(uint32_t handler) {
    put(image+stub_at-0x34u+0x30u,handler);put(image+stub_at+0x30u,handler);
}
/* Its nesting mode, as it leaves it: both templates name the nesting
 * handler and the live INTEVT copy is taken from them. Callers restore the
 * plain templates with templates(plain_handler()). */
static void seed_nesting(void) {templates(nesting_handler());seed();live_common=nesting_handler();}
static void zero(const void *p,size_t n) {
    const uint8_t *b=p;for(size_t i=0u;i<n;i++)CHECK(!b[i]);
}
/* Every live admission agrees with the image-only finder. */
static int discover(struct kui_native_sdk_sci_route_plan *out) {
    int result=kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,UINT32_C(0x600000f0),
        CALLBACK,read_word,live,out);
    struct kui_native_sdk_sci_family family;unsigned before=reads;
    int found=kui_native_sdk_sci_family_find(image,bytes,LOAD,&family);
    CHECK(reads==before);
    if(result==1) {
        /* The finder's roles are unchanged: the plain handler as both. */
        uint32_t common=role(LOAD+(uint32_t)stub_at+0x34u);
        CHECK(found==1 && family.vector==LOAD+(uint32_t)stub_at && family.normal==common &&
              family.fpu==common && family.original==out->previous && out->common==live_common);
    } else if(found!=1) {CHECK(found==result);zero(&family,sizeof(family));}
    if(result!=1)zero(out,sizeof(*out));
    return result;
}
static void good(void) {
    seed();struct kui_native_sdk_sci_route_plan out;
    CHECK(discover(&out)==1);
    CHECK(out.version==KUI_NATIVE_SDK_SCI_ROUTE_VERSION && out.bytes==sizeof(out) && out.vbr==vbr &&
          out.eri_slot==vbr+0x25cu && out.rxi_slot==vbr+0x260u && out.callback==CALLBACK &&
          out.previous==role(LOAD+(uint32_t)default_at));
    CHECK(!memcmp(image,live,bytes));
}
static uint8_t *file_bytes(const char *path,size_t *length) {
    FILE *input=fopen(path,"rb");CHECK(input);
    CHECK(!fseek(input,0,SEEK_END));long size=ftell(input);CHECK(size>0);
    CHECK(!fseek(input,0,SEEK_SET));uint8_t *p=malloc((size_t)size);CHECK(p);
    CHECK(fread(p,1u,(size_t)size,input)==(size_t)size);CHECK(!fclose(input));
    *length=(size_t)size;return p;
}
/* The fixture offset of the install routine's MOV.L @(disp,PC) literal. */
static size_t install_literal(size_t instruction) {
    uint32_t at=LOAD+INSTALL_AT+(uint32_t)instruction;
    unsigned op=image[INSTALL_AT+instruction]|(unsigned)image[INSTALL_AT+instruction+1u]<<8;
    CHECK((op&0xf000u)==0xd000u);
    return (((at+4u)&~3u)+4u*(op&255u))-LOAD;
}
/* Locate an original's early roles by its INTEVT idiom, independently of the
 * production finder, then rebuild them in a compact fixture. */
static void fixture(const uint8_t *source,size_t n) {
    size_t idiom=0u;unsigned count=0u;
    for(size_t at=0u;at+8u<=n;at+=2u)
        if(source[at]==0xffu && source[at+1u]==0xe1u && word(source+at+2u)==UINT32_C(0x41184128) &&
           source[at+6u]==0x28u && source[at+7u]==0x71u) {idiom=at;++count;}
    CHECK(count==1u && idiom>=0x46u);
    size_t i=idiom-0x12u,e=i-0x34u,c=i+0x34u;
    uint32_t event=word(source+i+0x28u),source_area=event&UINT32_C(0xe0000000);
    size_t ev=((event&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))-LOAD,d=ev+0xcu;
    size_t init=0u;count=0u;
    for(size_t at=0u;at+0x2eu<=n;at+=2u)
        if(source[at]==0x11u && source[at+1u]==0x95u && source[at+2u]==0x11u && source[at+3u]==0x94u &&
           ((word(source+at+0x2au)&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))-LOAD==d) {init=at;++count;}
    CHECK(count==1u && ev==c+0x2d4u && d+0xc4u<=n);
    bytes=FIXTURE_BYTES;stub_at=I_AT;default_at=D_AT;vbr=UINT32_C(0x8c00e000);area=UINT32_C(0x80000000);
    image=calloc(1u,bytes);live=malloc(bytes);CHECK(image && live);
    memcpy(image+E_AT,source+e,0x34u+0x34u+0x14cu);
    memcpy(image+D_AT,source+d,0xc4u);memcpy(image+INIT_AT,source+init,0x2eu);
    /* The nesting handler and the install routine's code. */
    CHECK(init>=0x15au && c+0x14cu+0x180u<=n);
    memcpy(image+N_AT,source+c+0x14cu,0x180u);memcpy(image+INSTALL_AT,source+init-0x15au,0x84u);
    uint32_t fe=LOAD+EVENT_AT,fc=LOAD+C_AT;
    for(size_t s=E_AT;s<=I_AT;s+=0x34u) {put(image+s+0x28u,fe);put(image+s+0x2cu,0u);put(image+s+0x30u,fc);}
    put(image+C_AT+0x148u,fe);put(image+N_AT+0x17cu,fe);
    put(image+D_AT+0xb8u,fe+4u);put(image+D_AT+0xbcu,fe-8u);put(image+D_AT+0xc0u,fe-4u);
    put(image+INIT_AT+0x2au,LOAD+D_AT);
    put(image+install_literal(0x1eu),LOAD+I_AT+0x30u);put(image+install_literal(0x2eu),LOAD+E_AT+0x30u);
    put(image+install_literal(0x30u),LOAD+N_AT);put(image+install_literal(0x42u),LOAD+C_AT);
    nesting_roles[0]=install_literal(0x1eu);nesting_roles[1]=install_literal(0x2eu);
    nesting_roles[2]=install_literal(0x30u);nesting_roles[3]=install_literal(0x42u);
    nesting_roles[4]=N_AT+0x17cu;
    /* The literal pool lies outside every other fingerprinted window. */
    for(size_t i=0u;i<4u;i++)CHECK(nesting_roles[i]>=INSTALL_AT+0x84u && nesting_roles[i]+4u<=INIT_AT);
    (void)source_area;
}
static const size_t roles[]={E_AT+0x28u,E_AT+0x30u,I_AT+0x28u,I_AT+0x30u,C_AT+0x148u,
    D_AT+0xb8u,D_AT+0xbcu,D_AT+0xc0u,INIT_AT+0x2au};
static void alias(uint32_t to) {
    for(size_t i=0u;i<sizeof(roles)/sizeof(roles[0]);i++)
        put(image+roles[i],(word(image+roles[i])&UINT32_C(0x1fffffff))|to);
    for(size_t i=0u;i<sizeof(nesting_roles)/sizeof(nesting_roles[0]);i++)
        put(image+nesting_roles[i],(word(image+nesting_roles[i])&UINT32_C(0x1fffffff))|to);
    area=to;
}
static void good_nesting(void) {
    seed_nesting();struct kui_native_sdk_sci_route_plan out;
    CHECK(discover(&out)==1 && out.common==role(LOAD+N_AT) && out.vbr==vbr &&
          out.previous==role(LOAD+(uint32_t)default_at));
    CHECK(!memcmp(image,live,bytes));templates(plain_handler());
}
static int nesting_result(void) {
    struct kui_native_sdk_sci_route_plan out;int result=discover(&out);
    templates(plain_handler());return result;
}
/* The nesting handler is admitted only with its exact routine and code; the
 * plain handler's admission never depends on them. */
static void nesting_checks(void) {
    struct kui_native_sdk_sci_route_plan out;
    good_nesting();good();
    unsigned mutations=0u;
    const size_t windows[][2]={{N_AT,0x180u},{INSTALL_AT,0x84u}};
    for(size_t w=0u;w<2u;w++)
        for(size_t at=0u;at<windows[w][1];at++)for(unsigned bit=0u;bit<8u;bit+=3u) {
            image[windows[w][0]+at]^=(uint8_t)(1u<<bit);
            seed_nesting();CHECK(nesting_result()==-1);
            seed();CHECK(discover(&out)==1);
            image[windows[w][0]+at]^=(uint8_t)(1u<<bit);++mutations;
        }
    for(size_t i=0u;i<sizeof(nesting_roles)/sizeof(nesting_roles[0]);i++)
        for(unsigned bit=0u;bit<32u;bit++) {
            uint32_t saved=word(image+nesting_roles[i]);put(image+nesting_roles[i],saved^(UINT32_C(1)<<bit));
            seed_nesting();CHECK(nesting_result()==-1);seed();CHECK(discover(&out)==1);
            put(image+nesting_roles[i],saved);++mutations;
        }
    /* Its live code, read before any slot. */
    for(size_t at=0u;at<0x180u;at++) {
        seed_nesting();live[N_AT+at]^=1u;CHECK(nesting_result()==-1);
        seed();live[N_AT+at]^=1u;CHECK(discover(&out)==1);
    }
    /* The live copy must equal its templates, and both templates must name
     * the same reviewed handler. The nesting handler in the copy alone, with
     * plain templates, is not a state the install routine leaves. */
    seed_nesting();put(stub600+0x30u,plain_handler());CHECK(nesting_result()==-1);
    seed();put(stub600+0x30u,nesting_handler());CHECK(discover(&out)==-1);
    put(image+stub_at-0x34u+0x30u,nesting_handler());seed();CHECK(discover(&out)==-1);templates(plain_handler());
    put(image+stub_at+0x30u,nesting_handler());seed();CHECK(discover(&out)==-1);templates(plain_handler());
    const uint32_t others[]={LOAD+N_AT+4u,LOAD+N_AT-4u,LOAD+C_AT+4u,LOAD+N_AT+0x180u,
        (LOAD+N_AT)^UINT32_C(0x20000000),0u};
    for(size_t i=0u;i<sizeof(others)/sizeof(others[0]);i++) {
        templates(others[i]);seed();CHECK(discover(&out)==-1);templates(plain_handler());
        seed();put(stub600+0x30u,others[i]);CHECK(discover(&out)==-1);
    }
    seed_nesting();CHECK(nesting_result()==1);unsigned total=reads;
    for(unsigned fail=1u;fail<=total;fail++) {
        seed_nesting();fail_read=fail;CHECK(nesting_result()==-1);CHECK(reads==fail);
    }
    good_nesting();good();
    printf("early SDK SCI nesting handler: %u image mutations, %u live code bytes\n",mutations,0x180u);
}
static void fixture_checks(void) {
    struct kui_native_sdk_sci_route_plan out;
    good();
    /* Every fingerprinted image byte matters. */
    const size_t windows[][2]={{E_AT,0x34u},{I_AT,0x34u},{C_AT,0x14cu},{D_AT,0xc4u},{INIT_AT,0x2eu}};
    for(size_t w=0u;w<sizeof(windows)/sizeof(windows[0]);w++)
        for(size_t at=0u;at<windows[w][1];at++)for(unsigned bit=0u;bit<8u;bit++) {
            image[windows[w][0]+at]^=(uint8_t)(1u<<bit);
            seed();CHECK(discover(&out)<=0);
            image[windows[w][0]+at]^=(uint8_t)(1u<<bit);
        }
    good();
    /* Live copies: only the scratch word may differ. */
    for(size_t at=0u;at<sizeof(stub600);at++) {
        seed();stub600[at]^=1u;int result=discover(&out);
        CHECK(at>=0x2cu && at<0x30u?result==1:result==-1);
    }
    /* The nesting handler is admitted by nesting_checks(). */
    seed();put(stub600+0x30u,LOAD+C_AT+0x150u);CHECK(discover(&out)==-1);
    for(size_t at=0u;at<0x14cu;at++) {seed();live[C_AT+at]^=1u;CHECK(discover(&out)==-1);}
    for(size_t at=0u;at<0xc4u;at++) {seed();live[D_AT+at]^=1u;CHECK(discover(&out)==-1);}
    for(unsigned bit=0u;bit<32u;bit++) {
        seed();eri^=UINT32_C(1)<<bit;CHECK(discover(&out)==-1);
        seed();rxi^=UINT32_C(1)<<bit;CHECK(discover(&out)==-1);
    }
    seed();eri=rxi=CALLBACK;CHECK(discover(&out)==-1);
    seed();CHECK(discover(&out)==1);unsigned total=reads;
    for(unsigned fail=1u;fail<=total;fail++) {
        seed();fail_read=fail;CHECK(discover(&out)==-1);CHECK(reads==fail);
    }
    /* Roles must share one U0 or P1 area. */
    alias(0u);good();good_nesting();
    for(size_t i=0u;i<sizeof(roles)/sizeof(roles[0]);i++) {
        uint32_t saved=word(image+roles[i]);put(image+roles[i],saved|UINT32_C(0x80000000));
        seed();CHECK(discover(&out)==-1);put(image+roles[i],saved);
    }
    for(size_t i=0u;i<sizeof(nesting_roles)/sizeof(nesting_roles[0]);i++) {
        uint32_t saved=word(image+nesting_roles[i]);put(image+nesting_roles[i],saved|UINT32_C(0x80000000));
        seed_nesting();CHECK(nesting_result()==-1);seed();CHECK(discover(&out)==1);
        put(image+nesting_roles[i],saved);
    }
    alias(UINT32_C(0xa0000000));seed();CHECK(discover(&out)==-1);
    alias(UINT32_C(0x80000000));good();good_nesting();
    /* Uniqueness, absence and the moved default. */
    memcpy(image+0x3000u,image+I_AT,0x34u);seed();CHECK(discover(&out)==-1);memset(image+0x3000u,0,0x34u);
    memcpy(image+0x1a02u,image+INIT_AT,0x2eu);seed();CHECK(discover(&out)==-1);memset(image+0x1a02u,0,0x2eu);
    put(image+INIT_AT+0x2au,LOAD+D_AT+4u);seed();CHECK(discover(&out)==-1);put(image+INIT_AT+0x2au,LOAD+D_AT);
    memcpy(image+0x2400u,image+D_AT,0xc4u);default_at=0x2400u;put(image+INIT_AT+0x2au,LOAD+0x2400u);good();
    memset(image+0x2400u,0,0xc4u);default_at=D_AT;put(image+INIT_AT+0x2au,LOAD+D_AT);good();
    uint8_t *saved=image;image=calloc(1u,bytes);CHECK(image);seed();
    CHECK(discover(&out)==0 && !reads);free(image);image=saved;
    /* Owner context is checked exactly as for the full family. */
    CHECK(kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,UINT32_C(0x60000070),CALLBACK,
        read_word,live,&out)==-1);
    CHECK(kui_native_sdk_sci_route_discover(image,bytes,LOAD,vbr,UINT32_C(0x600000f0),CALLBACK+1u,
        read_word,live,&out)==-1);
    good();
}
/* A minimal SH-4 interpreter for the early install routine: one 16 MiB main
 * RAM with physical aliasing, CCR, SR and VBR, and every opcode it executes.
 * Anything else, any access outside RAM or CCR, a PC-relative instruction in
 * a delay slot, or a runaway loop fails the test. */
#define RAM_BASE UINT32_C(0x0c000000)
#define RAM_BYTES UINT32_C(0x01000000)
#define CCR UINT32_C(0xff00001c)
struct sh {uint8_t *ram;uint32_t r[16],pc,pr,sr,vbr,ccr;unsigned steps;};
static uint8_t *sh_at(struct sh *c,uint32_t address,uint32_t size) {
    uint32_t at=address&UINT32_C(0x1fffffff);
    CHECK((address>>29)!=7u && !(address&(size-1u)) && at>=RAM_BASE && at-RAM_BASE<=RAM_BYTES-size);
    return c->ram+(at-RAM_BASE);
}
static uint32_t sh_read(struct sh *c,uint32_t address,uint32_t size) {
    if(address==CCR) {CHECK(size==4u);return c->ccr;}
    const uint8_t *p=sh_at(c,address,size);
    return size==4u?word(p):size==2u?(uint32_t)p[0]|(uint32_t)p[1]<<8:p[0];
}
static void sh_write(struct sh *c,uint32_t address,uint32_t value) {
    if(address==CCR) {c->ccr=value;return;}
    put(sh_at(c,address,4u),value);
}
static uint32_t sext8(uint32_t v) {v&=255u;return v&128u?v|UINT32_C(0xffffff00):v;}
static uint32_t sext12(uint32_t v) {v&=0xfffu;return v&0x800u?v|UINT32_C(0xfffff000):v;}
static uint32_t sext16(uint32_t v) {v&=0xffffu;return v&0x8000u?v|UINT32_C(0xffff0000):v;}
static void sh_t(struct sh *c,bool t) {c->sr=(c->sr&~UINT32_C(1))|(t?1u:0u);}
static void sh_plain(struct sh *c,uint32_t op,uint32_t pc) {
    const unsigned n=(op>>8)&15u,m=(op>>4)&15u;uint32_t *r=c->r;
    switch(op>>12) {
    case 0x0:
        if(op==0x0009u)return;
        if((op&0xf0ffu)==0x0002u) {r[n]=c->sr;return;}
        if((op&0xf0ffu)==0x0022u) {r[n]=c->vbr;return;}
        if((op&0xf0ffu)==0x00a3u)return; /* OCBP: this model has no cache */
        break;
    case 0x2:
        switch(op&15u) {
        case 0x2:sh_write(c,r[n],r[m]);return;
        case 0x6:r[n]-=4u;sh_write(c,r[n],r[m]);return;
        case 0x9:r[n]&=r[m];return;
        case 0xb:r[n]|=r[m];return;
        }
        break;
    case 0x3:
        switch(op&15u) {
        case 0x0:sh_t(c,r[n]==r[m]);return;
        case 0x3:sh_t(c,(int32_t)r[n]>=(int32_t)r[m]);return;
        case 0x6:sh_t(c,r[n]>r[m]);return;
        case 0x8:r[n]-=r[m];return;
        case 0xc:r[n]+=r[m];return;
        }
        break;
    case 0x4:
        switch(op&0xffu) {
        case 0x01:sh_t(c,r[n]&1u);r[n]>>=1;return;
        case 0x08:r[n]<<=2;return;
        case 0x09:r[n]>>=2;return;
        case 0x0e:c->sr=r[n];return;
        case 0x10:r[n]-=1u;sh_t(c,!r[n]);return;
        case 0x21:sh_t(c,r[n]&1u);r[n]=(r[n]>>1)|(r[n]&UINT32_C(0x80000000));return;
        }
        break;
    case 0x6:
        switch(op&15u) {
        case 0x2:r[n]=sh_read(c,r[m],4u);return;
        case 0x3:r[n]=r[m];return;
        case 0x6: {uint32_t v=sh_read(c,r[m],4u);r[m]+=4u;r[n]=v;return;}
        }
        break;
    case 0x7:r[n]+=sext8(op);return;
    case 0x9:r[n]=sext16(sh_read(c,pc+4u+2u*(op&255u),2u));return;
    case 0xc:
        if(n==9u) {r[0]&=op&255u;return;}
        if(n==11u) {r[0]|=op&255u;return;}
        break;
    case 0xd:r[n]=sh_read(c,((pc+4u)&~UINT32_C(3))+4u*(op&255u),4u);return;
    case 0xe:r[n]=sext8(op);return;
    }
    fprintf(stderr,"early SDK SCI interpreter: opcode %04x at %08x is not modelled\n",(unsigned)op,(unsigned)pc);
    CHECK(false);
}
static void sh_run(struct sh *c,uint32_t entry,uint32_t sentinel) {
    c->pc=entry;c->pr=sentinel;c->steps=0u;
    while(c->pc!=sentinel) {
        CHECK(++c->steps<100000u);
        const uint32_t pc=c->pc,op=sh_read(c,pc,2u),high=op>>8;
        uint32_t target=0u;bool branch=true,slot=true;
        if((op>>12)==0xau)target=pc+4u+2u*sext12(op);
        else if(op==0x000bu)target=c->pr;
        else if(high==0x89u || high==0x8bu || high==0x8du || high==0x8fu) {
            const bool taken=((c->sr&1u)!=0u)==(high==0x89u || high==0x8du);
            slot=high==0x8du || high==0x8fu;
            target=taken?pc+4u+2u*sext8(op):pc+(slot?4u:2u);
        } else branch=false;
        if(!branch) {sh_plain(c,op,pc);c->pc=pc+2u;continue;}
        if(slot) {
            const uint32_t delay=sh_read(c,pc+2u,2u);
            CHECK((delay>>12)!=0x9u && (delay>>12)!=0xdu && (delay>>8)!=0xc7u);
            sh_plain(c,delay,pc+2u);
        }
        c->pc=target;
    }
}
static bool ram_read(void *context,uint32_t address,uint32_t *out) {
    struct sh *c=context;uint32_t at=address&UINT32_C(0x1fffffff);
    if((address&3u) || (address>>29)==7u || at<RAM_BASE || at-RAM_BASE>RAM_BYTES-4u)return false;
    *out=word(c->ram+(at-RAM_BASE));return true;
}
/* The image's own install routine, called as its hardware init calls it
 * (mode, interrupt stack) at the boot VBR, over one RAM that holds the
 * executable at its load address. The route then reads that same memory as
 * the image and as live state, as the stage does on console. Returns the
 * route's result; the plan's handler goes to *handler. */
static int executed(const uint8_t *file,size_t n,size_t init,uint32_t mode,uint32_t stack,
    uint32_t *handler) {
    struct sh c;memset(&c,0,sizeof(c));c.ram=calloc(1u,RAM_BYTES);CHECK(c.ram);
    uint8_t *ram_image=c.ram+((LOAD&UINT32_C(0x1fffffff))-RAM_BASE);memcpy(ram_image,file,n);
    c.vbr=vbr;c.sr=UINT32_C(0x600000e0);c.ccr=UINT32_C(0x0000090b);c.r[4]=mode;c.r[5]=stack;
    const uint32_t sp=UINT32_C(0x8c00f000),sentinel=UINT32_C(0x8c00e800);c.r[15]=sp;
    for(unsigned i=8u;i<15u;i++)c.r[i]=UINT32_C(0x5a5a0000)+i;
    sh_run(&c,LOAD+(uint32_t)init-0x15au,sentinel);
    /* It returns with SP, R8-R14 and the caller's interrupt mask, BL clear,
     * and the instruction cache invalidation bit written to CCR. */
    CHECK(c.r[15]==sp && (c.sr&0xf0u)==0xe0u && !(c.sr&UINT32_C(0x10000000)) && (c.ccr&0x800u));
    for(unsigned i=8u;i<15u;i++)CHECK(c.r[i]==UINT32_C(0x5a5a0000)+i);
    const uint32_t chosen=mode==UINT32_C(0x80000000)?nesting_handler():plain_handler();
    const uint32_t event=word(file+stub_at+0x28u);
    const size_t event_at=((event&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))-LOAD;
    uint8_t *vector=c.ram+((vbr&UINT32_C(0x1fffffff))-RAM_BASE);
    /* What it leaves: both templates in the executable name the chosen
     * handler, the nesting count is clear, the stack word is set only in
     * nesting mode, the three copies equal the templates, and the handler
     * table holds the default. Nothing else in the executable changes. */
    CHECK(word(ram_image+stub_at-0x34u+0x30u)==chosen && word(ram_image+stub_at+0x30u)==chosen);
    CHECK(!word(ram_image+event_at+4u) &&
          word(ram_image+event_at-4u)==(mode==UINT32_C(0x80000000)?stack:0u));
    CHECK(!memcmp(vector+0x100u,ram_image+stub_at-0x34u,0x34u) &&
          !memcmp(vector+0x400u,ram_image+stub_at-0x34u,0x34u) &&
          !memcmp(vector+0x600u,ram_image+stub_at,0x34u));
    for(uint32_t slot=0x1c8u;slot<=0x2bcu;slot+=4u)CHECK(word(vector+slot)==role(LOAD+(uint32_t)default_at));
    for(size_t at=0u;at<n;at++)
        CHECK(ram_image[at]==file[at] || at-(stub_at-0x34u+0x30u)<4u || at-(stub_at+0x30u)<4u ||
              at-(event_at-4u)<4u || at-(event_at+4u)<4u);
    struct kui_native_sdk_sci_route_plan out;
    int result=kui_native_sdk_sci_route_discover(ram_image,n,LOAD,vbr,UINT32_C(0x600000f0),CALLBACK,
        ram_read,&c,&out);
    *handler=result==1?out.common:0u;
    if(result==1) {
        CHECK(out.common==chosen && out.vbr==vbr && out.eri_slot==vbr+0x25cu && out.rxi_slot==vbr+0x260u &&
              out.callback==CALLBACK && out.previous==role(LOAD+(uint32_t)default_at));
        /* The same memory with the selected handler's code, the live copy's
         * common word or one template changed is refused. */
        const size_t code=((chosen&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))-LOAD;
        ram_image[code+0x10u]^=1u;
        CHECK(kui_native_sdk_sci_route_discover(ram_image,n,LOAD,vbr,UINT32_C(0x600000f0),CALLBACK,
            ram_read,&c,&out)==-1);
        ram_image[code+0x10u]^=1u;
        const uint32_t other=chosen==plain_handler()?nesting_handler():plain_handler();
        put(vector+0x630u,other);
        CHECK(kui_native_sdk_sci_route_discover(ram_image,n,LOAD,vbr,UINT32_C(0x600000f0),CALLBACK,
            ram_read,&c,&out)==-1);
        put(vector+0x630u,chosen);put(ram_image+stub_at-0x34u+0x30u,other);
        CHECK(kui_native_sdk_sci_route_discover(ram_image,n,LOAD,vbr,UINT32_C(0x600000f0),CALLBACK,
            ram_read,&c,&out)==-1);
        put(ram_image+stub_at-0x34u+0x30u,chosen);
        CHECK(kui_native_sdk_sci_route_discover(ram_image,n,LOAD,vbr,UINT32_C(0x600000f0),CALLBACK,
            ram_read,&c,&out)==1 && out.common==chosen);
    }
    free(c.ram);return result;
}
/* The unique table initializer (the install routine's own last part). */
static size_t initializer_at(const uint8_t *p,size_t n) {
    size_t init=0u;unsigned count=0u;
    for(size_t at=0u;at+0x2eu<=n;at+=2u)
        if(p[at]==0x11u && p[at+1u]==0x95u && p[at+2u]==0x11u && p[at+3u]==0x94u &&
           ((word(p+at+0x2au)&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))-LOAD==default_at) {init=at;++count;}
    CHECK(count==1u && init>=0x15au);
    return init;
}
static void probe(const char *path) {
    size_t n;uint8_t *p=file_bytes(path,&n);
    uint8_t *saved_image=image,*saved_live=live;size_t saved_bytes=bytes,saved_stub=stub_at,saved_default=default_at;
    uint32_t saved_area=area;
    image=p;bytes=n;live=malloc(n);CHECK(live);
    struct kui_native_sdk_sci_family family;int found=kui_native_sdk_sci_family_find(p,n,LOAD,&family);
    int result=found,nesting=0,ran_nesting=0,ran_plain=0;
    if(found==1 && family.vector-LOAD+0x34u<=n && family.normal==family.fpu) {
        stub_at=family.vector-LOAD;area=family.original&UINT32_C(0xe0000000);
        default_at=((family.original&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))-LOAD;
        vbr=UINT32_C(0x8c00f400);seed();
        struct kui_native_sdk_sci_route_plan out;result=discover(&out);
        /* Both install modes: the game chooses one before its hardware init. */
        seed_nesting();nesting=discover(&out);templates(plain_handler());
        if(nesting==1)CHECK(out.common==family.normal+0x14cu);
        /* The install routine itself, in both modes. */
        const size_t init=initializer_at(p,n);uint32_t handler;
        ran_nesting=executed(p,n,init,UINT32_C(0x80000000),UINT32_C(0x0c573700),&handler);
        if(ran_nesting==1)CHECK(handler==nesting_handler());
        ran_plain=executed(p,n,init,0u,0u,&handler);
        if(ran_plain==1)CHECK(handler==plain_handler());
    }
    if(found==1 && family.normal==family.fpu)
        printf("%s: early family %s (finder %d, live model %d, nesting handler %d;"
            " after its own install routine: nesting mode %d, plain mode %d)\n",path,
            result==1?"admitted":"not admitted",found,result,nesting,ran_nesting,ran_plain);
    else printf("%s: early family not admitted (finder %d)\n",path,found);
    free(live);free(p);image=saved_image;live=saved_live;bytes=saved_bytes;stub_at=saved_stub;
    default_at=saved_default;area=saved_area;vbr=UINT32_C(0x8c00e000);
}
int main(int argc,char **argv) {
    CHECK(argc>=2);size_t n;uint8_t *source=file_bytes(argv[1],&n);fixture(source,n);free(source);
    fixture_checks();nesting_checks();
    for(int i=2;i<argc;i++)probe(argv[i]);
    free(live);free(image);
    printf("early SDK SCI family: %u checks passed; fingerprints, live stub/common/default, slots, aliases, read faults\n",checks);
    return 0;
}
