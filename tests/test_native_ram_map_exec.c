/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the linked RAM-map fill (stage) and report (resident) on a model of
 * main RAM with a write-back data cache that the P2 alias bypasses. A small
 * integer SH-4 interpreter runs only the instructions these two functions
 * use and stops on anything else. Usage: test-ram-map STAGE.elf RESIDENT.elf */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_BASE UINT32_C(0x0c000000)
#define RAM_BYTES UINT32_C(0x01000000)
#define LINE 32u
#define KEY UINT32_C(0x4b55494e)
#define PAGE UINT32_C(0x20000)
#define PAGES (RAM_BYTES/PAGE)
#define STAGE UINT32_C(0x8ce00000)
#define STACK_PAGE UINT32_C(0x8cfe0000)
#define RETURN UINT32_C(0x8c00f000) /* Never executed: the call's sentinel PR. */
#define STEP_LIMIT 100000000ul /* Per call: a 16 MiB scan is about 50M. */

static uint8_t ram[RAM_BYTES],copy[RAM_BYTES],cache[RAM_BYTES];
static uint8_t state[RAM_BYTES/LINE]; /* 0 invalid, 1 clean, 2 dirty */
static bool cache_on,ocbp_is_nop;
static unsigned long checks,steps,purges;

static void fail(const char *what,uint32_t value) {
    fprintf(stderr,"RAM map model: %s (0x%08x)\n",what,(unsigned)value);exit(1);
}
#define CHECK(x) do {++checks;if(!(x)) fail("check failed: " #x,__LINE__);} while(0)

static uint32_t at(uint32_t address,unsigned bytes) {
    uint32_t area=address>>29,physical=address&UINT32_C(0x1fffffff);
    if((area!=0u && area!=4u && area!=5u) || physical<RAM_BASE ||
       physical-RAM_BASE>RAM_BYTES-bytes || (address&(bytes-1u))) fail("access outside main RAM",address);
    return physical-RAM_BASE;
}
static bool cached(uint32_t address) {return cache_on && address>>29!=5u;}
static void allocate(uint32_t offset) {
    uint32_t line=offset/LINE;
    if(!state[line]) {memcpy(cache+line*LINE,ram+line*LINE,LINE);state[line]=1u;}
}
static uint32_t rd(uint32_t address,unsigned bytes) {
    uint32_t offset=at(address,bytes),value=0;const uint8_t *from=ram;
    if(cached(address)) {allocate(offset);from=cache;}
    for(unsigned i=0;i<bytes;i++) value|=(uint32_t)from[offset+i]<<(8u*i);
    return value;
}
static void wr(uint32_t address,uint32_t value,unsigned bytes) {
    uint32_t offset=at(address,bytes);uint8_t *to=ram;
    if(cached(address)) {allocate(offset);state[offset/LINE]=2u;to=cache;}
    for(unsigned i=0;i<bytes;i++) to[offset+i]=(uint8_t)(value>>(8u*i));
}
static void purge(uint32_t address) {
    uint32_t line=at(address&~(LINE-1u),1u)/LINE;++purges;
    if(ocbp_is_nop) return;
    if(state[line]==2u) memcpy(ram+line*LINE,cache+line*LINE,LINE);
    state[line]=0u;
}
static uint16_t fetch(uint32_t pc) {uint32_t o=at(pc,2u);return (uint16_t)(ram[o]|ram[o+1u]<<8);}

/* ELF32 little-endian SH: PT_LOAD segments into RAM, and symbol lookup. */
struct image {uint8_t *data;size_t bytes;};
static uint32_t u32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t u16(const uint8_t *p) {return (uint16_t)(p[0]|p[1]<<8);}
static struct image load_file(const char *path) {
    FILE *f=fopen(path,"rb");struct image i={0};
    if(!f) fail("cannot open ELF",0);
    fseek(f,0,SEEK_END);i.bytes=(size_t)ftell(f);fseek(f,0,SEEK_SET);
    if(!(i.data=malloc(i.bytes)) || fread(i.data,1,i.bytes,f)!=i.bytes) fail("cannot read ELF",0);
    fclose(f);
    if(i.bytes<52u || memcmp(i.data,"\177ELF\1\1",6) || u16(i.data+18)!=42u) fail("not an SH ELF32",0);
    return i;
}
static void load_segments(const struct image *e) {
    uint32_t ph=u32(e->data+28);unsigned count=u16(e->data+44),size=u16(e->data+42);
    for(unsigned k=0;k<count;k++) {
        const uint8_t *h=e->data+ph+k*size;
        if(u32(h)!=1u) continue;
        uint32_t offset=u32(h+4),vaddr=u32(h+8),filesz=u32(h+16),memsz=u32(h+20);
        if(offset>e->bytes || filesz>e->bytes-offset || filesz>memsz) fail("bad segment",vaddr);
        for(uint32_t i=0;i<memsz;i++) ram[at(vaddr+i,1u)]=i<filesz?e->data[offset+i]:0u;
    }
}
static uint32_t symbol(const struct image *e,const char *name) {
    uint32_t sh=u32(e->data+32);unsigned count=u16(e->data+48),size=u16(e->data+46);
    for(unsigned k=0;k<count;k++) {
        const uint8_t *s=e->data+sh+k*size;
        if(u32(s+4)!=2u) continue; /* SHT_SYMTAB */
        const uint8_t *strings=e->data+u32(e->data+sh+u32(s+24)*size+16);
        uint32_t base=u32(s+16),bytes=u32(s+20);
        for(uint32_t i=0;i+16u<=bytes;i+=16u)
            if(!strcmp((const char *)strings+u32(e->data+base+i),name)) return u32(e->data+base+i+4);
    }
    fail("missing symbol",0);
    return 0;
}

/* The interpreter: every instruction the two functions compile to. */
struct cpu {uint32_t r[16],pc,pr;bool t;};
static uint32_t hook_line,hook_values;
static char shown[64];
static uint32_t shown_values[8];
static unsigned shown_count;
static int32_t s8(uint32_t v) {return (int32_t)(int8_t)(uint8_t)v;}
static int32_t s12(uint32_t v) {return v&0x800u?(int32_t)v-0x1000:(int32_t)v;}
static void plain(struct cpu *c,uint16_t op,uint32_t pc) {
    unsigned n=(op>>8)&15u,m=(op>>4)&15u;uint32_t *r=c->r;
    switch(op>>12) {
    case 0x0:
        if(op==0x0009u) return;
        if((op&0xf0ffu)==0x00a3u) {purge(r[n]);return;}
        if((op&0xf0ffu)==0x0029u) {r[n]=c->t;return;}
        break;
    case 0x1: wr(r[n]+4u*(op&15u),r[m],4u);return;
    case 0x2:
        switch(op&15u) {
        case 2: wr(r[n],r[m],4u);return;
        case 6: r[n]-=4u;wr(r[n],r[m],4u);return;
        case 8: c->t=!(r[n]&r[m]);return;
        case 9: r[n]&=r[m];return;
        case 10: r[n]^=r[m];return;
        case 11: r[n]|=r[m];return;
        }
        break;
    case 0x3:
        switch(op&15u) {
        case 0: c->t=r[n]==r[m];return;
        case 2: c->t=r[n]>=r[m];return;
        case 3: c->t=(int32_t)r[n]>=(int32_t)r[m];return;
        case 6: c->t=r[n]>r[m];return;
        case 7: c->t=(int32_t)r[n]>(int32_t)r[m];return;
        case 8: r[n]-=r[m];return;
        case 12: r[n]+=r[m];return;
        }
        break;
    case 0x4:
        switch(op&0xffu) {
        case 0x00: c->t=r[n]>>31;r[n]<<=1;return;
        case 0x01: c->t=r[n]&1u;r[n]>>=1;return;
        case 0x08: r[n]<<=2;return;
        case 0x09: r[n]>>=2;return;
        case 0x10: --r[n];c->t=!r[n];return;
        case 0x18: r[n]<<=8;return;
        case 0x19: r[n]>>=8;return;
        case 0x22: r[n]-=4u;wr(r[n],c->pr,4u);return;
        case 0x26: c->pr=rd(r[n],4u);r[n]+=4u;return;
        case 0x28: r[n]<<=16;return;
        case 0x29: r[n]>>=16;return;
        }
        break;
    case 0x5: r[n]=rd(r[m]+4u*(op&15u),4u);return;
    case 0x6:
        switch(op&15u) {
        case 2: r[n]=rd(r[m],4u);return;
        case 3: r[n]=r[m];return;
        case 6: r[n]=rd(r[m],4u);if(n!=m) r[m]+=4u;return;
        case 7: r[n]=~r[m];return;
        case 12: r[n]=r[m]&0xffu;return;
        case 13: r[n]=r[m]&0xffffu;return;
        }
        break;
    case 0x7: r[n]+=(uint32_t)s8(op);return;
    case 0x8:
        if(n==8u) {c->t=r[0]==(uint32_t)s8(op);return;}
        break;
    case 0x9: r[n]=(uint32_t)(int32_t)(int16_t)rd(pc+4u+2u*(op&255u),2u);return;
    case 0xc:
        switch(n) {
        case 7: r[0]=((pc+4u)&~3u)+4u*(op&255u);return;
        case 8: c->t=!(r[0]&(op&255u));return;
        case 9: r[0]&=op&255u;return;
        case 11: r[0]|=op&255u;return;
        }
        break;
    case 0xd: r[n]=rd(((pc+4u)&~3u)+4u*(op&255u),4u);return;
    case 0xe: r[n]=(uint32_t)s8(op);return;
    }
    fail("instruction outside the modelled set",op);
}
static bool pc_relative(uint16_t op) {return op>>12==0x9u || op>>12==0xdu || op>>8==0xc7u;}
/* Returns 0 for a plain instruction, 1 for a branch taken now, 2 for a branch
 * taken after its delay slot, 3 for a delayed branch not taken. */
static int branch(struct cpu *c,uint16_t op,uint32_t pc,uint32_t *target) {
    switch(op>>8) {
    case 0x89: if(c->t) {*target=pc+4u+2u*(uint32_t)s8(op);return 1;} *target=pc+2u;return 1;
    case 0x8b: if(!c->t) {*target=pc+4u+2u*(uint32_t)s8(op);return 1;} *target=pc+2u;return 1;
    case 0x8d: if(c->t) {*target=pc+4u+2u*(uint32_t)s8(op);return 2;} return 3;
    case 0x8f: if(!c->t) {*target=pc+4u+2u*(uint32_t)s8(op);return 2;} return 3;
    }
    if(op>>12==0xau) {*target=pc+4u+2u*(uint32_t)s12(op&0xfffu);return 2;}
    if(op>>12==0xbu) {c->pr=pc+4u;*target=pc+4u+2u*(uint32_t)s12(op&0xfffu);return 2;}
    if(op==0x000bu) {*target=c->pr;return 2;}
    if((op&0xf0ffu)==0x402bu) {*target=c->r[(op>>8)&15u];return 2;}
    if((op&0xf0ffu)==0x400bu) {*target=c->r[(op>>8)&15u];c->pr=pc+4u;return 2;}
    return 0;
}
static bool hook(struct cpu *c) {
    if(c->pc==hook_line || c->pc==hook_values) {
        unsigned i=0;
        for(;i+1u<sizeof(shown) && (shown[i]=(char)rd(c->r[4]+i,1u));i++) {}
        shown[i]=0;shown_count=0;
        if(c->pc==hook_values) {
            CHECK(c->r[6]<=8u);shown_count=c->r[6];
            for(unsigned k=0;k<shown_count;k++) shown_values[k]=rd(c->r[5]+4u*k,4u);
        }
        c->pc=c->pr;return true;
    }
    return false;
}
static void run(uint32_t entry,uint32_t stack) {
    struct cpu c;memset(&c,0,sizeof(c));
    for(unsigned i=0;i<16u;i++) c.r[i]=UINT32_C(0x5a5a0000)+i;
    c.r[15]=stack;c.pr=RETURN;c.pc=entry;
    uint32_t saved[16];memcpy(saved,c.r,sizeof(saved));
    for(unsigned long count=0;c.pc!=RETURN;++steps) {
        if(++count>STEP_LIMIT) fail("step limit",c.pc);
        if(hook(&c)) continue;
        uint32_t pc=c.pc,target=0;uint16_t op=fetch(pc);
        switch(branch(&c,op,pc,&target)) {
        case 0: plain(&c,op,pc);c.pc=pc+2u;break;
        case 1: c.pc=target;break;
        case 3: c.pc=pc+2u;break;
        default: {
            uint16_t slot=fetch(pc+2u);uint32_t ignored;
            if(pc_relative(slot) || branch(&c,slot,pc+2u,&ignored)) fail("unmodelled delay slot",slot);
            plain(&c,slot,pc+2u);c.pc=target;
        }
        }
    }
    /* The SH C ABI: R8..R15 survive the call. */
    for(unsigned i=8u;i<16u;i++) CHECK(c.r[i]==saved[i]);
}

static uint32_t exec_bytes_symbol,stage_end,fill,report;
static bool filled(uint32_t page,uint32_t exec_bytes) {
    uint32_t first=(UINT32_C(0x8c010000)+exec_bytes+PAGE-1u)&~(PAGE-1u);
    uint32_t stage_top=(stage_end+PAGE-1u)&~(PAGE-1u);
    return page>=first && !(page>=STAGE && page<stage_top) && page<STACK_PAGE;
}
static void seed(void) {
    uint32_t x=UINT32_C(0x2545f491);
    for(uint32_t i=0;i<RAM_BYTES;i++) {x^=x<<13;x^=x>>17;x^=x<<5;ram[i]=(uint8_t)x;}
    /* Random RAM never matches the pattern by accident. */
    for(uint32_t i=0;i<RAM_BYTES;i+=4u) if(u32(ram+i)==((UINT32_C(0x8c000000)+i)^KEY)) ram[i]^=1u;
    memset(state,0,sizeof(state));
}
/* The stage's last step: caches off, exactly as the relay runs it. */
static void run_fill(uint32_t exec_bytes) {
    cache_on=false;
    wr(exec_bytes_symbol,exec_bytes,4u);
    memcpy(copy,ram,RAM_BYTES);
    shown[0]=0;
    run(fill,UINT32_C(0x8cfeffe0));
    CHECK(!strcmp(shown,"RAM MAP TEST - FREE PAGES MARKED"));
    for(uint32_t page=0;page<PAGES;page++) {
        uint32_t base=UINT32_C(0x8c000000)+page*PAGE;bool expect=filled(base,exec_bytes);
        for(uint32_t i=0;i<PAGE;i+=4u) {
            uint32_t o=page*PAGE+i;
            if(expect) {if(u32(ram+o)!=((base+i)^KEY)) fail("fill missed a word",base+i);}
            else if(u32(ram+o)!=u32(copy+o)) fail("fill wrote outside its pages",base+i);
        }
        ++checks;
    }
}
/* Menu return: the game's cache is on and may hold dirty or stale lines. */
static void run_report(uint32_t expected[4]) {
    cache_on=true;
    unsigned long before=purges;
    shown[0]=0;
    run(report,UINT32_C(0x8c007cf0));
    CHECK(!strcmp(shown,"RAM") && shown_count==4u);
    CHECK(purges-before==RAM_BYTES/4u);
    for(unsigned k=0;k<4u;k++) CHECK(shown_values[k]==expected[k]);
}
static void expect_map(uint32_t exec_bytes,const uint32_t *touched,unsigned count,uint32_t out[4]) {
    memset(out,0,4u*sizeof(*out));
    for(uint32_t page=0;page<PAGES;page++) {
        uint32_t base=UINT32_C(0x8c000000)+page*PAGE;bool unused=filled(base,exec_bytes);
        for(unsigned k=0;k<count;k++) if((touched[k]&~(PAGE-1u))==base) unused=false;
        if(unused) out[page/32u]|=UINT32_C(1)<<(page%32u);
    }
}
int main(int argc,char **argv) {
    if(argc!=3) fail("usage: test-ram-map STAGE.elf RESIDENT.elf",0);
    struct image stage=load_file(argv[1]),resident=load_file(argv[2]);
    fill=symbol(&stage,"_ram_map_fill");exec_bytes_symbol=symbol(&stage,"_exec_bytes");
    hook_line=symbol(&stage,"_retail_display_line");stage_end=symbol(&stage,"__retail_stage_bss_end");
    report=symbol(&resident,"_ram_map_report");hook_values=symbol(&resident,"_retail_display_values");
    CHECK(symbol(&stage,"__kui_native_ram_map_stage")==1u);
    CHECK(symbol(&resident,"__kui_native_ram_map_resident")==1u);
    CHECK(stage_end>STAGE && stage_end<UINT32_C(0x8cf00000));

    /* Crazy Taxi 2's executable size, then sizes that end on, just past and
     * just before a page boundary, and the largest the stage accepts. */
    static const uint32_t sizes[]={1452148u,0x10000u,0x10004u,0x0fffcu,0xbff000u};
    for(unsigned s=0;s<sizeof(sizes)/sizeof(sizes[0]);s++) {
        seed();load_segments(&stage);load_segments(&resident);
        run_fill(sizes[s]);
        uint32_t expected[4];
        /* An untouched session: every filled page reads unused. */
        expect_map(sizes[s],NULL,0u,expected);
        run_report(expected);
        /* Game activity of every kind: a cached store left dirty (only
         * OCBP publishes it), an uncached store, DMA under a stale clean line
         * the CPU read first, one byte at a page's last word and a page's
         * first word. Only those pages lose their bit. */
        uint32_t touched[]={UINT32_C(0x8cd2a000),UINT32_C(0x8cd64104),UINT32_C(0x8ce81230),
                            UINT32_C(0x8cf9fffc),UINT32_C(0x8cc40000)};
        cache_on=true;
        wr(touched[0],0x11111111u,4u);
        wr(touched[1]|UINT32_C(0x20000000),0x22222222u,4u);
        (void)rd(touched[2],4u);ram[at(touched[2],4u)]^=0x80u;
        wr(touched[3]|UINT32_C(0x20000000),rd(touched[3]|UINT32_C(0x20000000),4u)^0x00010000u,4u);
        wr(touched[4],0u,1u);
        expect_map(sizes[s],touched,5u,expected);
        run_report(expected);
        /* The dirty store reached RAM through the report's own OCBP. */
        CHECK(u32(ram+at(touched[0],4u))==0x11111111u);
    }
    /* Without OCBP the dirty store escapes, while the uncached read still
     * sees DMA under a stale line: the purge is load-bearing for one case
     * only, and the model tells the two apart. */
    seed();load_segments(&stage);load_segments(&resident);run_fill(1452148u);
    cache_on=true;ocbp_is_nop=true;
    uint32_t dirty=UINT32_C(0x8cd2a000),stale=UINT32_C(0x8ce81230),expected[4];
    wr(dirty,0x11111111u,4u);(void)rd(stale,4u);ram[at(stale,4u)]^=0x80u;
    expect_map(1452148u,&stale,1u,expected);
    run_report(expected);
    ocbp_is_nop=false;
    printf("RAM map: %lu checks, %lu SH instructions executed from the linked fill and report\n",
        checks,steps);
    return 0;
}
