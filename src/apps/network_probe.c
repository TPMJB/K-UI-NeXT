/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent bounded diagnostic protocol, RFC 2131/2132, RFC 826/792.
 * No socket stack, general DHCP client, or persistent network configuration. */
#include "kui/network_probe.h"
#include <stdio.h>
#include <string.h>
static uint16_t u16(const uint8_t *p){return (uint16_t)((unsigned)p[0]*256+p[1]);}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(24-i*8));}
static uint32_t sum(uint32_t s,const uint8_t *p,size_t n){while(n>1){s+=u16(p);p+=2;n-=2;}if(n)s+=(unsigned)*p<<8;return s;}
static uint16_t finish(uint32_t s){while(s>>16)s=(s&65535)+(s>>16);return (uint16_t)~s;}
static uint16_t checksum(const uint8_t *p,size_t n){return finish(sum(0,p,n));}
static bool usable(const uint8_t p[4]){return p[0] && p[0]!=127 && p[0]<224 && u32(p)!=UINT32_MAX;}
static bool mask_valid(const uint8_t p[4]){uint32_t v=u32(p),n=~v;return v && n && !(n&(n+1));}
bool kui_network_parse_ipv4(const char *text,uint8_t out[4]) {
    uint8_t result[4];if(!text||!out)return false;
    for(unsigned i=0;i<4;i++){unsigned n=0,digits=0;while(*text>='0'&&*text<='9'){if(++digits>3)return false;n=n*10+(unsigned)(*text++-'0');}if(!digits||n>255)return false;result[i]=(uint8_t)n;if(i<3){if(*text++!='.')return false;}else if(*text)return false;}
    memcpy(out,result,4);return true;
}
bool kui_network_config_valid(const struct kui_network_config *c){
    if(!c||!usable(c->ip)||!mask_valid(c->mask))return false;
    uint32_t host=u32(c->ip)&~u32(c->mask);if(!host||host==~u32(c->mask))return false;
    if(u32(c->gateway) && (!usable(c->gateway)||u32(c->gateway)==u32(c->ip)||((u32(c->gateway)^u32(c->ip))&u32(c->mask))))return false;
    return !u32(c->dns)||usable(c->dns);
}
static void failed(struct kui_network_probe *p,const char *why){p->stage=KUI_NET_FAILED;snprintf(p->failure,sizeof(p->failure),"%s",why);}
static void stage(struct kui_network_probe *p,enum kui_network_stage s,uint64_t now){p->stage=s;p->stage_ms=now;p->last_send_ms=0;p->sends=0;}
void kui_network_probe_begin(struct kui_network_probe *p,const uint8_t mac[6],uint32_t xid,const struct kui_network_config *c,uint64_t now){
    memset(p,0,sizeof(*p));memcpy(p->mac,mac,6);p->transaction=xid?xid:1;p->start_ms=now;p->dhcp=c==NULL;
    if(c){p->config=*c;if(!kui_network_config_valid(c)){failed(p,"Invalid static IPv4 configuration");return;}}
    stage(p,c?KUI_NET_CONFLICT:KUI_NET_DISCOVER,now);
}
static void ethernet(struct kui_network_probe *p,uint8_t *f,const uint8_t *dst,unsigned proto){memcpy(f,dst,6);memcpy(f+6,p->mac,6);put16(f+12,proto);}
static void ip_header(uint8_t *ip,unsigned n,unsigned proto,const uint8_t src[4],const uint8_t dst[4]){ip[0]=0x45;put16(ip+2,n);ip[8]=64;ip[9]=(uint8_t)proto;memcpy(ip+12,src,4);memcpy(ip+16,dst,4);put16(ip+10,checksum(ip,20));}
static size_t dhcp_frame(struct kui_network_probe *p,uint8_t *f){
    static const uint8_t all[6]={255,255,255,255,255,255},zero[4]={0};
    memset(f,0,KUI_NETWORK_FRAME_MAX);ethernet(p,f,all,0x800);uint8_t *b=f+42;
    b[0]=1;b[1]=1;b[2]=6;put32(b+4,p->transaction);put16(b+10,0x8000);memcpy(b+28,p->mac,6);put32(b+236,UINT32_C(0x63825363));
    size_t k=240;b[k++]=53;b[k++]=1;b[k++]=(uint8_t)(p->stage==KUI_NET_DISCOVER?1:3);
    b[k++]=61;b[k++]=7;b[k++]=1;memcpy(b+k,p->mac,6);k+=6;
    if(p->stage==KUI_NET_REQUEST){b[k++]=50;b[k++]=4;memcpy(b+k,p->config.ip,4);k+=4;b[k++]=54;b[k++]=4;memcpy(b+k,p->server,4);k+=4;}
    b[k++]=55;b[k++]=4;b[k++]=1;b[k++]=3;b[k++]=6;b[k++]=51;b[k++]=255;if(k<300)k=300;
    put16(f+34,68);put16(f+36,67);put16(f+38,(unsigned)k+8);ip_header(f+14,(unsigned)k+28,17,zero,all);return k+42;
}
static size_t arp_frame(struct kui_network_probe *p,uint8_t *f,bool probe){
    static const uint8_t all[6]={255,255,255,255,255,255};memset(f,0,60);ethernet(p,f,all,0x806);put16(f+14,1);put16(f+16,0x800);f[18]=6;f[19]=4;put16(f+20,1);memcpy(f+22,p->mac,6);
    if(!probe)memcpy(f+28,p->config.ip,4);
    memcpy(f+38,probe?p->config.ip:p->config.gateway,4);return 60;
}
static size_t echo_frame(struct kui_network_probe *p,uint8_t *f){
    memset(f,0,60);ethernet(p,f,p->peer_mac,0x800);ip_header(f+14,40,1,p->config.ip,p->config.gateway);
    uint8_t *e=f+34;e[0]=8;put16(e+4,0x4b55);put16(e+6,1);memcpy(e+8,"KUI-TEST",8);put32(e+16,p->transaction);put16(e+2,checksum(e,20));return 60;
}
size_t kui_network_probe_step(struct kui_network_probe *p,uint8_t f[KUI_NETWORK_FRAME_MAX],uint64_t now){
    if(!p||!f||p->stage>=KUI_NET_DONE)return 0;
    uint64_t age=now-p->stage_ms;
    if(p->stage==KUI_NET_CONFLICT&&age>=3000){if(!u32(p->config.gateway)){stage(p,KUI_NET_DONE,now);return 0;}stage(p,KUI_NET_ARP,now);age=0;}
    uint64_t limit=p->stage==KUI_NET_DISCOVER||p->stage==KUI_NET_REQUEST?15000:5000;
    if(age>=limit){failed(p,p->stage==KUI_NET_DISCOVER?"No DHCP offer within 15 seconds":p->stage==KUI_NET_REQUEST?"No DHCP ACK within 15 seconds":p->stage==KUI_NET_ARP?"Gateway did not answer ARP":"Gateway did not answer ICMP (may filter ping)");return 0;}
    uint64_t interval=p->stage==KUI_NET_DISCOVER||p->stage==KUI_NET_REQUEST?4000:1000;
    if(p->sends&&now-p->last_send_ms<interval)return 0;
    p->last_send_ms=now;++p->sends;
    if(p->stage==KUI_NET_DISCOVER||p->stage==KUI_NET_REQUEST)return dhcp_frame(p,f);
    if(p->stage==KUI_NET_CONFLICT||p->stage==KUI_NET_ARP)return arp_frame(p,f,p->stage==KUI_NET_CONFLICT);
    return echo_frame(p,f);
}
/* Strict DHCP option bounds. Duplicate singleton options are rejected; valid
 * concatenated router/DNS lists use only the first address. Option overload is
 * deliberately unsupported and reported as no usable reply, never guessed. */
static bool dhcp_options(const uint8_t *b,size_t n,struct kui_network_config *c,uint8_t server[4],unsigned *type,uint32_t *lease){
    bool ended=false;unsigned seen=0;*type=0;*lease=0;memset(c,0,sizeof(*c));memcpy(c->ip,b+16,4);memset(server,0,4);
    for(size_t k=240;k<n;){unsigned tag=b[k++];if(!tag)continue;if(tag==255){ended=true;break;}if(k>=n)return false;unsigned len=b[k++];if(len>n-k)return false;unsigned bit=tag==1?1:tag==3?2:tag==6?4:tag==51?8:tag==53?16:tag==54?32:0;
        if(tag==52)return false;
        if(bit&&(seen&bit))return false;
        seen|=bit;
        if(tag==53){if(len!=1)return false;*type=b[k];}
        else if(tag==54){if(len!=4)return false;memcpy(server,b+k,4);}
        else if(tag==1){if(len!=4)return false;memcpy(c->mask,b+k,4);}
        else if(tag==3||tag==6){if(!len||len%4)return false;memcpy(tag==3?c->gateway:c->dns,b+k,4);}
        else if(tag==51){if(len!=4)return false;*lease=u32(b+k);}
        k+=len;
    }
    return ended&&*type&&usable(server);
}
void kui_network_probe_receive(struct kui_network_probe *p,const uint8_t *f,size_t n,uint64_t now){
    static const uint8_t all[6]={255,255,255,255,255,255};
    if(!p||!f||p->stage>=KUI_NET_DONE)return;
    ++p->received;
    if(n<14||(memcmp(f,p->mac,6)&&memcmp(f,all,6))||!memcmp(f+6,p->mac,6))goto ignore;
    if(u16(f+12)==0x806){
        if(n<42||u16(f+14)!=1||u16(f+16)!=0x800||f[18]!=6||f[19]!=4||memcmp(f+6,f+22,6))goto ignore;
        unsigned op=u16(f+20);if(op!=1&&op!=2)goto ignore;
        if((p->stage==KUI_NET_CONFLICT||p->stage==KUI_NET_ARP||p->stage==KUI_NET_ECHO)&&
            (!memcmp(f+28,p->config.ip,4)||(u32(f+28)==0&&!memcmp(f+38,p->config.ip,4)))){failed(p,"IPv4 address conflict detected; test stopped");return;}
        if(p->stage==KUI_NET_ARP&&op==2&&!memcmp(f+28,p->config.gateway,4)&&!memcmp(f+38,p->config.ip,4)&&!memcmp(f+32,p->mac,6)){
            memcpy(p->peer_mac,f+22,6);p->arp_reply=true;stage(p,KUI_NET_ECHO,now);return;
        }goto ignore;
    }
    if(u16(f+12)!=0x800||n<34)goto ignore;
    const uint8_t *ip=f+14;size_t ihl=(ip[0]&15u)*4u,len=u16(ip+2);
    if((ip[0]>>4)!=4||ihl<20||len<ihl||len>n-14||ihl>n-14||checksum(ip,ihl)||!ip[8]||(u16(ip+6)&0xbfff))goto ignore;
    if(p->stage==KUI_NET_DISCOVER||p->stage==KUI_NET_REQUEST){
        if(ip[9]!=17||len<ihl+8)goto ignore;
        const uint8_t *u=ip+ihl;size_t un=u16(u+4);
        if(u16(u)!=67||u16(u+2)!=68||un!=len-ihl||un<248)goto ignore;
        if(u16(u+6)&&finish(sum(sum(0,ip+12,8)+17+(uint32_t)un,u,un)))goto ignore;
        const uint8_t *b=u+8;if(b[0]!=2||b[1]!=1||b[2]!=6||u32(b+4)!=p->transaction||memcmp(b+28,p->mac,6)||u32(b+236)!=UINT32_C(0x63825363))goto ignore;
        struct kui_network_config c;uint8_t server[4];unsigned type;uint32_t lease;
        if(!dhcp_options(b,un-8,&c,server,&type,&lease))goto ignore;
        if(!usable(ip+12)||(memcmp(ip+16,all,4)&&memcmp(ip+16,c.ip,4)))goto ignore;
        if(p->stage==KUI_NET_REQUEST&&memcmp(server,p->server,4))goto ignore;
        if(type==6&&p->stage==KUI_NET_REQUEST){failed(p,"DHCP server rejected the requested address");return;}
        if(!kui_network_config_valid(&c)||!lease)goto ignore;
        if(type==2&&p->stage==KUI_NET_DISCOVER){p->config=c;memcpy(p->server,server,4);stage(p,KUI_NET_REQUEST,now);return;}
        if(type==5&&p->stage==KUI_NET_REQUEST&&!memcmp(c.ip,p->config.ip,4)){p->config=c;p->leased=true;stage(p,KUI_NET_CONFLICT,now);return;}
    }else if(p->stage==KUI_NET_ECHO){
        if(ip[9]!=1||memcmp(ip+12,p->config.gateway,4)||memcmp(ip+16,p->config.ip,4)||len!=ihl+20)goto ignore;
        const uint8_t *e=ip+ihl;if(e[0]||e[1]||checksum(e,20)||u16(e+4)!=0x4b55||u16(e+6)!=1||memcmp(e+8,"KUI-TEST",8)||u32(e+16)!=p->transaction)goto ignore;
        p->echo_ms=now-p->last_send_ms;p->echo_reply=true;stage(p,KUI_NET_DONE,now);return;
    }
ignore:++p->ignored;
}
