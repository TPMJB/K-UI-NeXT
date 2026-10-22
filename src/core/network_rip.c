/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/network_rip.h"
#include "kui/hash.h"
#include <stdio.h>
#include <string.h>

#define HEADER 48u
#define TAG 32u
enum { CHALLENGE = 1, AUTH, READ, WRITE, SYNC, CLOSE };
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t)u32(p+4)<<32; }
static void put32(uint8_t *p, uint32_t x) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(x>>(8*i)); }
static void put64(uint8_t *p, uint64_t x) { put32(p,(uint32_t)x);put32(p+4,(uint32_t)(x>>32)); }
static bool nonzero(const uint8_t *p, size_t n) { uint8_t x=0;while(n--)x|=*p++;return x!=0; }
static bool same(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t x=0;while(n--)x|=*a++ ^ *b++;return x==0;
}
static void wipe(void *p, size_t n) { volatile uint8_t *b=p;while(n--)*b++=0; }
bool kui_network_rip_config_valid(const struct kui_network_rip_config *c) {
    return c && c->port && c->host[0] && c->host[0]<224 && c->host[0]!=127 &&
        !(c->host[0]==169 && c->host[1]==254) && nonzero(c->key,32) && nonzero(c->volume,16);
}
static int hex(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static bool unhex(uint8_t *out, size_t n, const char *p) {
    if(strlen(p)!=2*n)return false;
    for(size_t i=0;i<n;i++){int a=hex(p[2*i]),b=hex(p[2*i+1]);if(a<0||b<0)return false;out[i]=(uint8_t)(a*16+b);}return true;
}
static bool decimal(const char **p, unsigned max, unsigned *out) {
    unsigned n=0,digits=0;
    while(**p>='0'&&**p<='9'){unsigned d=(unsigned)(*(*p)++-'0');if(n>(max-d)/10)return false;n=n*10+d;++digits;}
    *out=n;return digits!=0;
}
bool kui_network_rip_config_parse(struct kui_network_rip_config *out, const char *text, size_t bytes) {
    if(!out||!text||!bytes||bytes>KUI_NETWORK_RIP_CONFIG_CAP||memchr(text,0,bytes))return false;
    struct kui_network_rip_config c={0};unsigned seen=0;size_t at=0;
    while(at<bytes){
        char line[160];size_t n=0;
        while(at<bytes&&text[at]!='\n'){if(n+1>=sizeof(line))goto invalid;line[n++]=text[at++];}
        if(at<bytes)++at;
        line[n]=0;
        char *p=line;while(*p==' '||*p=='\t'||*p=='\r')++p;
        char *end=strchr(p,'#');if(end)*end=0;end=p+strlen(p);
        while(end>p&&(end[-1]==' '||end[-1]=='\t'||end[-1]=='\r'))*--end=0;
        if(!*p)continue;
        char *eq=strchr(p,'=');if(!eq)goto invalid;*eq++=0;
        end=p+strlen(p);while(end>p&&(end[-1]==' '||end[-1]=='\t'))*--end=0;
        while(*eq==' '||*eq=='\t')++eq;
        unsigned bit=0;
        if(!strcmp(p,"host")){
            bit=1;const char *q=eq;
            for(unsigned i=0;i<4;i++){unsigned v;if(!decimal(&q,255,&v))goto invalid;c.host[i]=(uint8_t)v;if(i<3&&*q++!='.')goto invalid;}
            if(*q)goto invalid;
        }else if(!strcmp(p,"port")){
            bit=2;const char *q=eq;unsigned v;if(!decimal(&q,65535,&v)||*q||!v)goto invalid;c.port=(uint16_t)v;
        }else if(!strcmp(p,"key")){bit=4;if(!unhex(c.key,32,eq))goto invalid;}
        else if(!strcmp(p,"volume")){bit=8;if(!unhex(c.volume,16,eq))goto invalid;}
        else goto invalid;
        if(seen&bit)goto invalid;
        seen|=bit;
    }
    if(seen!=15||!kui_network_rip_config_valid(&c))goto invalid;
    *out=c;wipe(&c,sizeof(c));return true;
invalid: wipe(&c,sizeof(c));return false;
}
static bool fail(struct kui_network_rip *c, const char *why) {
    if(!c->problem[0])snprintf(c->problem,sizeof(c->problem),"%s",why);
    c->live=false;
    if(c->connected){c->net.close(c->net.ctx,c->socket);c->connected=false;}
    return false;
}
static bool service(struct kui_network_rip *c, uint64_t since) {
    if(c->hooks.cancel&&c->hooks.cancel(c->hooks.ctx))return fail(c,"PC capture stopped");
    if(c->hooks.poll&&!c->hooks.poll(c->hooks.ctx))return fail(c,"PC capture network or address lease failed");
    if(c->hooks.now_ms(c->hooks.ctx)-since>=KUI_NETWORK_RIP_TIMEOUT_MS)return fail(c,"PC capture network timed out");
    return true;
}
static bool transfer(struct kui_network_rip *c, void *data, size_t n, bool sending, uint64_t since) {
    uint8_t *p=data;
    while(n){
        if(!service(c,since))return false;
        uint8_t state;size_t amount=0;
        if(!c->net.state(c->net.ctx,c->socket,&state))return fail(c,"PC capture socket failed");
        bool ok=sending?c->net.room(c->net.ctx,c->socket,&amount):c->net.received(c->net.ctx,c->socket,&amount);
        if(!ok)return fail(c,"PC capture socket failed");
        if(amount>n)amount=n;
        if(state!=KUI_NET_ESTABLISHED && !(state==KUI_NET_PEER_CLOSED&&!sending&&amount))return fail(c,"PC capture disconnected; resume explicitly");
        if(amount){
            ok=sending?c->net.send(c->net.ctx,c->socket,p,amount):c->net.receive(c->net.ctx,c->socket,p,amount);
            if(!ok)return fail(c,"PC capture transfer failed; resume explicitly");
            p+=amount;n-=amount;
        }else c->hooks.pause(c->hooks.ctx,1);
    }
    return true;
}
static void mac(const uint8_t key[32], const uint8_t header[HEADER], const uint8_t *p, size_t n, uint8_t out[TAG]) {
    uint8_t pad[64],inner[32];struct kui_sha256 s;
    memset(pad,0x36,sizeof(pad));for(unsigned i=0;i<32;i++)pad[i]^=key[i];
    kui_sha256_init(&s);kui_sha256_update(&s,pad,64);kui_sha256_update(&s,header,HEADER);kui_sha256_update(&s,p,n);kui_sha256_digest(&s,inner);
    memset(pad,0x5c,sizeof(pad));for(unsigned i=0;i<32;i++)pad[i]^=key[i];
    kui_sha256_init(&s);kui_sha256_update(&s,pad,64);kui_sha256_update(&s,inner,32);kui_sha256_digest(&s,out);
    wipe(pad,sizeof(pad));wipe(inner,sizeof(inner));wipe(&s,sizeof(s));
}
static uint32_t checksum(uint8_t h[HEADER], const uint8_t *p, size_t n) {
    uint32_t stored=u32(h+40);put32(h+40,0);uint32_t crc=kui_crc32(kui_crc32(0,h,HEADER),p,n);put32(h+40,stored);return crc;
}
static bool receive_frame(struct kui_network_rip *c, unsigned op, uint64_t seq, uint32_t block, uint32_t count,
                          size_t bytes, bool challenge, uint64_t since) {
    uint8_t h[HEADER],tag[TAG],expected[TAG];
    if(!transfer(c,h,sizeof(h),false,since))return false;
    size_t received_bytes=u32(h+32);
    if(memcmp(h,"KNR1",4)||h[4]!=1||h[5]!=op||h[6]!=1||h[7]||u64(h+16)!=seq||
       u32(h+24)!=block||u32(h+28)!=count||
       (received_bytes!=bytes&&!(u32(h+36)&&!received_bytes))||received_bytes>sizeof(c->buffer)||u32(h+44)||
       (!challenge&&u64(h+8)!=c->session)||!u64(h+8))return fail(c,"Invalid PC capture response");
    bytes=received_bytes;
    if(!transfer(c,c->buffer,bytes,false,since)||!transfer(c,tag,sizeof(tag),false,since))return false;
    if(u32(h+40)!=checksum(h,c->buffer,bytes))return fail(c,"PC capture response checksum failed");
    if(challenge){memset(expected,0,sizeof(expected));c->session=u64(h+8);}
    else mac(c->config.key,h,c->buffer,bytes,expected);
    if(!same(tag,expected,sizeof(tag)))return fail(c,"PC capture authentication failed");
    if(u32(h+36))return fail(c,"PC capture server refused I/O");
    return true;
}
static bool request(struct kui_network_rip *c, unsigned op, uint32_t block, uint32_t count,
                    const uint8_t *data, size_t bytes, size_t reply) {
    if(!c->live||c->sequence==UINT64_MAX)return fail(c,"PC capture session is unavailable");
    uint8_t h[HEADER]={0},tag[TAG];uint64_t since=c->hooks.now_ms(c->hooks.ctx);
    memcpy(h,"KNR1",4);h[4]=1;h[5]=(uint8_t)op;put64(h+8,c->session);put64(h+16,++c->sequence);
    put32(h+24,block);put32(h+28,count);put32(h+32,(uint32_t)bytes);put32(h+40,checksum(h,data,bytes));
    mac(c->config.key,h,data,bytes,tag);
    return transfer(c,h,sizeof(h),true,since)&&transfer(c,(void *)data,bytes,true,since)&&transfer(c,tag,sizeof(tag),true,since)&&
        receive_frame(c,op,c->sequence,block,count,reply,false,since);
}
bool kui_network_rip_open(struct kui_network_rip *c, const struct kui_net_sockets *net, unsigned socket,
    const struct kui_network_rip_config *config, const struct kui_network_rip_hooks *hooks) {
    if(!c)return false;
    memset(c,0,sizeof(*c));
    if(!net||socket>=net->count||!net->connect||!net->close||!net->state||!net->received||!net->receive||!net->room||!net->send||
       !hooks||!hooks->now_ms||!hooks->pause||!kui_network_rip_config_valid(config)){
        snprintf(c->problem,sizeof(c->problem),"Invalid PC capture configuration");return false;
    }
    c->net=*net;c->socket=socket;c->config=*config;c->hooks=*hooks;
    c->connected=true;
    if(!net->connect(net->ctx,socket,49173,config->host,config->port))return fail(c,"Cannot connect to PC capture server");
    uint64_t since=hooks->now_ms(hooks->ctx);
    for(;;){
        uint8_t state;if(!service(c,since))return false;
        if(!net->state(net->ctx,socket,&state))return fail(c,"PC capture socket failed");
        if(state==KUI_NET_ESTABLISHED)break;
        if(state!=KUI_NET_CONNECTING&&state!=0x13u)return fail(c,"PC capture connection refused");
        hooks->pause(hooks->ctx,1);
    }
    if(net->keepalive&&!net->keepalive(net->ctx,socket,30))return fail(c,"PC capture keepalive failed");
    if(!receive_frame(c,CHALLENGE,0,0,0,56,true,since))return false;
    uint64_t blocks=u64(c->buffer+16);
    if(!same(config->volume,c->buffer,16)||!blocks||blocks>UINT32_MAX)return fail(c,"PC capture volume identity or capacity changed");
    uint8_t nonce[32];memcpy(nonce,c->buffer+24,sizeof(nonce));c->live=true;
    bool ok=request(c,AUTH,0,0,nonce,sizeof(nonce),24);wipe(nonce,sizeof(nonce));
    if(!ok)return false;
    if(!same(config->volume,c->buffer,16)||u64(c->buffer+16)!=blocks)return fail(c,"PC capture authenticated volume changed");
    c->blocks=blocks;return true;
}
static uint64_t blocks(void *ctx){struct kui_network_rip *c=ctx;return c->live?c->blocks:0;}
static bool range(struct kui_network_rip *c, uint32_t block, size_t count) {
    return c->live&&count&&count<=c->blocks&&(uint64_t)block<=c->blocks-count;
}
static int read_blocks(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    struct kui_network_rip *c=ctx;if(!data||!range(c,block,count))return -1;
    while(count){uint32_t n=count>KUI_NETWORK_RIP_BLOCKS?KUI_NETWORK_RIP_BLOCKS:(uint32_t)count;
        if(!request(c,READ,block,n,NULL,0,n*512u))return -1;
        memcpy(data,c->buffer,n*512u);data+=n*512u;block+=n;count-=n;
    }return 0;
}
static int write_blocks(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    struct kui_network_rip *c=ctx;if(!data||!range(c,block,count))return -1;
    while(count){uint32_t n=count>KUI_NETWORK_RIP_BLOCKS?KUI_NETWORK_RIP_BLOCKS:(uint32_t)count;
        if(!request(c,WRITE,block,n,data,n*512u,0))return -1;
        data+=n*512u;block+=n;count-=n;
    }return 0;
}
static int sync_blocks(void *ctx){return request(ctx,SYNC,0,0,NULL,0,0)?0:-1;}
void kui_network_rip_media(struct kui_network_rip *c, struct kui_media_ops *out) {
    if(out)*out=c&&c->live?(struct kui_media_ops){c,blocks,read_blocks,write_blocks,sync_blocks}:(struct kui_media_ops){0};
}
void kui_network_rip_close(struct kui_network_rip *c) {
    if(!c)return;
    if(c->live&&request(c,SYNC,0,0,NULL,0,0))(void)request(c,CLOSE,0,0,NULL,0,0);
    if(c->connected&&!c->net.close(c->net.ctx,c->socket)&&!c->problem[0])
        snprintf(c->problem,sizeof(c->problem),"PC capture socket close failed");
    c->connected=c->live=false;c->blocks=0;wipe(c->config.key,sizeof(c->config.key));wipe(c->buffer,sizeof(c->buffer));
}
const char *kui_network_rip_problem(const struct kui_network_rip *c){return c?c->problem:"PC capture unavailable";}
