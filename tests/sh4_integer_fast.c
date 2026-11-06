/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded test-only integer decoder. Executes supplied SH words, not rewritten
 * hash/copy functions. Coherent RAM aliases and observed cache operations are
 * modeled; no cycle, pipeline, interrupt or cache-effect claims are made. */
#include <stdint.h>
#include <string.h>
#define RAM_BYTES 0x1000000u
struct sh4_fast {
    uint32_t r[16],pc,pr,sr,mach,macl,gbr,vbr,spc,ssr,ccr;
    uint64_t steps,reads,writes;
    uint32_t error_pc,error_op,error_address,error_kind;
    uint32_t cache_count,cache_lines[1024],cache_ops[1024],cache_last,cache_jumps,cache_opcode;
    uint32_t ccr_writes,ccr_writer_pc,ccr_last_write,break_pc;
    uint32_t watch_first[4],watch_end[4],watch_writes[4],minimum_stack;
    uint32_t track_first,track_end;uint8_t track_bits[1024];
    uint8_t memory[RAM_BYTES];
};
static void error(struct sh4_fast *c,uint32_t kind,uint32_t address) {
    if(!c->error_kind){c->error_kind=kind;c->error_pc=c->pc;c->error_address=address;}
}
static int ram(struct sh4_fast *c,uint32_t a,unsigned n) {
    uint32_t area=a&0xe0000000u,p=a&0x1fffffffu;
    if((area!=0u && area!=0x80000000u && area!=0xa0000000u) ||
       p<0x0c000000u || p>0x0d000000u-n || (n>1u && (a&(n-1u)))) {
        error(c,1u,a);return 0;
    }
    return 1;
}
static uint32_t rd(struct sh4_fast *c,uint32_t a,unsigned n) {
    ++c->reads;
    if(a==0xff00001cu && n==4u)return c->ccr;
    if(!ram(c,a,n))return 0;
    const uint8_t *p=c->memory+((a&0x1fffffffu)-0x0c000000u);uint32_t v=0;
    for(unsigned i=0;i<n;i++)v|=(uint32_t)p[i]<<(8u*i);
    return v;
}
static void wr(struct sh4_fast *c,uint32_t a,uint32_t v,unsigned n) {
    ++c->writes;
    if(a==0xff00001cu && n==4u) {
        c->ccr=v&~0x808u;c->ccr_last_write=v;++c->ccr_writes;c->ccr_writer_pc=c->pc;return;
    }
    if(!ram(c,a,n))return;
    uint32_t p=a&0x1fffffffu;
    for(unsigned i=0;i<4u;i++)if(p<c->watch_end[i] && c->watch_first[i]<p+n)++c->watch_writes[i];
    if(p<c->track_end && c->track_first<p+n)for(unsigned i=0;i<n;i++)
        if(p+i>=c->track_first && p+i<c->track_end) {
            uint32_t bit=p+i-c->track_first;
            if(bit>=8192u){error(c,4u,a);return;}
            c->track_bits[bit/8u]|=(uint8_t)(1u<<(bit%8u));
        }
    if(p>=0x0cfe0000u && p+n<=0x0cff0000u && p<c->minimum_stack)c->minimum_stack=p;
    uint8_t *out=c->memory+(p-0x0c000000u);
    for(unsigned i=0;i<n;i++)out[i]=(uint8_t)(v>>(8u*i));
}
static void flag(struct sh4_fast *c,int t){c->sr=(c->sr&~1u)|(uint32_t)!!t;}
static int32_t sx8(uint32_t x){return (int8_t)x;}
static int32_t sx16(uint32_t x){return (int16_t)x;}
static int32_t sx12(uint32_t x){return (x&0x800u)?(int32_t)(x|0xfffff000u):(int32_t)x;}
static void plain(struct sh4_fast *c,uint16_t op,uint32_t pc) {
    unsigned n=op>>8&15u,m=op>>4&15u;uint32_t *r=c->r,v;uint64_t wide;
    ++c->steps;
    if(op==0x0009u)return;
    if(op==0x0008u){flag(c,0);return;}
    if(op==0x0018u){flag(c,1);return;}
    if((op>>12)==0xeu){r[n]=(uint32_t)sx8(op&255u);return;}
    if((op>>12)==7u){r[n]+=(uint32_t)sx8(op&255u);return;}
    if((op>>12)==0xdu){r[n]=rd(c,((pc+4u)&~3u)+(op&255u)*4u,4u);return;}
    if((op>>12)==9u){r[n]=(uint32_t)sx16(rd(c,pc+4u+(op&255u)*2u,2u));return;}
    if((op>>12)==1u){wr(c,r[n]+(op&15u)*4u,r[m],4u);return;}
    if((op>>12)==5u){r[n]=rd(c,r[m]+(op&15u)*4u,4u);return;}
    if((op&0xff00u)==0x8000u){wr(c,r[m]+(op&15u),r[0],1u);return;}
    if((op&0xff00u)==0x8100u){wr(c,r[m]+(op&15u)*2u,r[0],2u);return;}
    if((op&0xff00u)==0x8400u){r[0]=(uint32_t)sx8(rd(c,r[m]+(op&15u),1u));return;}
    if((op&0xff00u)==0x8500u){r[0]=(uint32_t)sx16(rd(c,r[m]+(op&15u)*2u,2u));return;}
    if((op&0xff00u)==0x8800u){flag(c,r[0]==(uint32_t)sx8(op&255u));return;}
    if((op&0xff00u)==0xc800u){flag(c,!(r[0]&(op&255u)));return;}
    if((op&0xff00u)==0xc900u){r[0]&=op&255u;return;}
    if((op&0xff00u)==0xca00u){r[0]^=op&255u;return;}
    if((op&0xff00u)==0xcb00u){r[0]|=op&255u;return;}
    switch(op&0xf00fu) {
    case 0x6003u:r[n]=r[m];return;
    case 0x6000u:r[n]=(uint32_t)sx8(rd(c,r[m],1u));return;
    case 0x6001u:r[n]=(uint32_t)sx16(rd(c,r[m],2u));return;
    case 0x6002u:r[n]=rd(c,r[m],4u);return;
    case 0x6004u:v=(uint32_t)sx8(rd(c,r[m],1u));if(n!=m)++r[m];r[n]=v;return;
    case 0x6005u:v=(uint32_t)sx16(rd(c,r[m],2u));if(n!=m)r[m]+=2u;r[n]=v;return;
    case 0x6006u:v=rd(c,r[m],4u);if(n!=m)r[m]+=4u;r[n]=v;return;
    case 0x6007u:r[n]=~r[m];return;
    case 0x6008u:v=r[m];r[n]=(v&0xffff0000u)|((v&255u)<<8)|((v>>8)&255u);return;
    case 0x6009u:v=r[m];r[n]=(v>>16)|(v<<16);return;
    case 0x600au:wide=(uint64_t)r[m]+(c->sr&1u);r[n]=0u-(uint32_t)wide;flag(c,wide!=0u);return;
    case 0x600bu:r[n]=0u-r[m];return;
    case 0x600cu:r[n]=r[m]&255u;return;
    case 0x600du:r[n]=r[m]&65535u;return;
    case 0x600eu:r[n]=(uint32_t)sx8(r[m]);return;
    case 0x600fu:r[n]=(uint32_t)sx16(r[m]);return;
    case 0x2000u:wr(c,r[n],r[m],1u);return;
    case 0x2001u:wr(c,r[n],r[m],2u);return;
    case 0x2002u:wr(c,r[n],r[m],4u);return;
    case 0x2004u:case 0x2005u:case 0x2006u:
        if(n==m)break;
        v=(op&15u)==4u?1u:(op&15u)==5u?2u:4u;r[n]-=v;wr(c,r[n],r[m],v);return;
    case 0x2008u:flag(c,!(r[n]&r[m]));return;
    case 0x2009u:r[n]&=r[m];return;
    case 0x200au:r[n]^=r[m];return;
    case 0x200bu:r[n]|=r[m];return;
    case 0x200cu:v=r[n]^r[m];flag(c,!(v&255u)||!(v&0xff00u)||!(v&0xff0000u)||!(v&0xff000000u));return;
    case 0x3000u:flag(c,r[n]==r[m]);return;
    case 0x3002u:flag(c,r[n]>=r[m]);return;
    case 0x3003u:flag(c,(int32_t)r[n]>=(int32_t)r[m]);return;
    case 0x3006u:flag(c,r[n]>r[m]);return;
    case 0x3007u:flag(c,(int32_t)r[n]>(int32_t)r[m]);return;
    case 0x3008u:r[n]-=r[m];return;
    case 0x300au:wide=(uint64_t)r[m]+(c->sr&1u);v=r[n];r[n]=v-(uint32_t)wide;flag(c,(uint64_t)v<wide);return;
    case 0x300cu:r[n]+=r[m];return;
    case 0x300eu:wide=(uint64_t)r[n]+r[m]+(c->sr&1u);r[n]=(uint32_t)wide;flag(c,wide>>32);return;
    case 0x0004u:wr(c,r[0]+r[n],r[m],1u);return;
    case 0x0005u:wr(c,r[0]+r[n],r[m],2u);return;
    case 0x0006u:wr(c,r[0]+r[n],r[m],4u);return;
    case 0x0007u:c->macl=(uint32_t)((uint64_t)r[n]*r[m]);return;
    case 0x000cu:r[n]=(uint32_t)sx8(rd(c,r[0]+r[m],1u));return;
    case 0x000du:r[n]=(uint32_t)sx16(rd(c,r[0]+r[m],2u));return;
    case 0x000eu:r[n]=rd(c,r[0]+r[m],4u);return;
    case 0x400cu:case 0x400du:
        v=r[m];if(v&0x80000000u) {unsigned shift=(0u-v)&31u;
            if((op&15u)==0xcu)r[n]=(uint32_t)((int32_t)r[n]>>(shift?shift:31u));
            else r[n]=shift?r[n]>>shift:0u;
        } else r[n]<<=v&31u;
        return;
    }
    switch(op&0xf0ffu) {
    case 0x4000u:v=r[n];r[n]<<=1;flag(c,v>>31);return;
    case 0x4001u:v=r[n];r[n]>>=1;flag(c,v&1u);return;
    case 0x4004u:v=r[n];r[n]=(v<<1)|(v>>31);flag(c,v>>31);return;
    case 0x4005u:v=r[n];r[n]=(v>>1)|(v<<31);flag(c,v&1u);return;
    case 0x4008u:r[n]<<=2;return;
    case 0x4009u:r[n]>>=2;return;
    case 0x4010u:--r[n];flag(c,!r[n]);return;
    case 0x4011u:flag(c,(int32_t)r[n]>=0);return;
    case 0x4015u:flag(c,(int32_t)r[n]>0);return;
    case 0x4018u:r[n]<<=8;return;
    case 0x4019u:r[n]>>=8;return;
    case 0x4020u:v=r[n];r[n]<<=1;flag(c,v>>31);return;
    case 0x4021u:v=r[n];r[n]=(uint32_t)((int32_t)v>>1);flag(c,v&1u);return;
    case 0x4024u:v=r[n];r[n]=(v<<1)|(c->sr&1u);flag(c,v>>31);return;
    case 0x4025u:v=r[n];r[n]=(v>>1)|((c->sr&1u)<<31);flag(c,v&1u);return;
    case 0x4028u:r[n]<<=16;return;
    case 0x4029u:r[n]>>=16;return;
    case 0x0002u:r[n]=c->sr;return;
    case 0x000au:r[n]=c->mach;return;
    case 0x001au:r[n]=c->macl;return;
    case 0x002au:r[n]=c->pr;return;
    case 0x0029u:r[n]=c->sr&1u;return;
    case 0x400eu:c->sr=r[n];return;
    case 0x400au:c->mach=r[n];return;
    case 0x401au:c->macl=r[n];return;
    case 0x402au:c->pr=r[n];return;
    case 0x4022u:r[n]-=4u;wr(c,r[n],c->pr,4u);return;
    case 0x4026u:c->pr=rd(c,r[n],4u);r[n]+=4u;return;
    case 0x0083u:case 0x00a3u:case 0x00b3u:case 0x00e3u:
        if(c->cache_count<1024u) {
            c->cache_lines[c->cache_count]=r[n];c->cache_ops[c->cache_count]=op&0xffu;
        }
        if(c->cache_count && r[n]!=c->cache_last+32u)++c->cache_jumps;
        if(!c->cache_count)c->cache_opcode=op&0xffu;
        else if(c->cache_opcode!=(op&0xffu))c->cache_opcode=UINT32_MAX;
        c->cache_last=r[n];++c->cache_count;return;
    }
    c->error_op=op;error(c,2u,pc);
}
int sh4_fast_run(struct sh4_fast *c,uint32_t stop,uint64_t budget) {
    uint64_t first=c->steps;
    while(c->pc!=stop && c->pc!=c->break_pc && !c->error_kind && c->steps-first<budget) {
        uint32_t pc=c->pc,op=rd(c,pc,2u),target=0u;int branch=0,delay=0;unsigned n=op>>8&15u;
        if((op>>12)==0xau || (op>>12)==0xbu) {
            target=pc+4u+2u*(uint32_t)sx12(op&4095u);branch=delay=1;
            if((op>>12)==0xbu)c->pr=pc+4u;
        } else if((op&0xf0ffu)==0x400bu || (op&0xf0ffu)==0x402bu) {
            target=c->r[n];branch=delay=1;if((op&0xf0ffu)==0x400bu)c->pr=pc+4u;
        } else if((op&0xf0ffu)==0x0003u || (op&0xf0ffu)==0x0023u) {
            target=pc+4u+c->r[n];branch=delay=1;if((op&0xf0ffu)==0x0003u)c->pr=pc+4u;
        } else if(op==0x000bu) {target=c->pr;branch=delay=1;
        } else if((op>>8)==0x89u || (op>>8)==0x8bu || (op>>8)==0x8du || (op>>8)==0x8fu) {
            int taken=!!(c->sr&1u)==((op>>8)==0x89u || (op>>8)==0x8du);
            delay=(op>>8)==0x8du || (op>>8)==0x8fu;
            target=taken?pc+4u+2u*(uint32_t)sx8(op&255u):pc+(delay?4u:2u);branch=1;
        }
        if(branch) {++c->steps;if(delay)plain(c,(uint16_t)rd(c,pc+2u,2u),pc+2u);c->pc=target;
        } else {plain(c,(uint16_t)op,pc);c->pc=pc+2u;}
    }
    if(c->error_kind)return -1;
    if(c->pc==c->break_pc)return 1;
    if(c->pc!=stop){error(c,3u,c->pc);return -1;}
    return 0;
}
