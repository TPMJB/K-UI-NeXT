/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/network_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t mac[]={2,3,4,5,6,7},peer[]={8,9,10,11,12,13};
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(24-i*8));}
static uint16_t checksum(const uint8_t *p,size_t n){unsigned s=0;for(size_t i=0;i<n;i+=2)s+=(unsigned)p[i]*256+(i+1<n?p[i+1]:0);while(s>>16)s=(s&65535)+(s>>16);return (uint16_t)~s;}
static void ipfix(uint8_t *f,unsigned size){put16(f+16,size-14);put16(f+24,0);put16(f+24,checksum(f+14,20));}
static unsigned reply(struct kui_network_probe *p,uint8_t *f,unsigned type){
    memset(f,0,KUI_NETWORK_FRAME_MAX);memcpy(f,mac,6);memcpy(f+6,peer,6);put16(f+12,0x800);f[14]=0x45;f[22]=64;f[23]=17;
    uint8_t server[]={192,168,1,1},ip[]={192,168,1,42},mask[]={255,255,255,0};memcpy(f+26,server,4);memset(f+30,255,4);put16(f+34,67);put16(f+36,68);
    uint8_t *b=f+42;b[0]=2;b[1]=1;b[2]=6;put32(b+4,p->transaction);memcpy(b+16,ip,4);memcpy(b+28,mac,6);put32(b+236,UINT32_C(0x63825363));
    unsigned k=240;b[k++]=53;b[k++]=1;b[k++]=(uint8_t)type;b[k++]=54;b[k++]=4;memcpy(b+k,server,4);k+=4;
    b[k++]=1;b[k++]=4;memcpy(b+k,mask,4);k+=4;b[k++]=3;b[k++]=4;memcpy(b+k,server,4);k+=4;b[k++]=51;b[k++]=4;put32(b+k,3600);k+=4;b[k++]=255;
    put16(f+38,k+8);ipfix(f,k+42);return k+42;
}
static void arp_reply(struct kui_network_probe *p,uint8_t *f){memset(f,0,60);memcpy(f,mac,6);memcpy(f+6,peer,6);put16(f+12,0x806);put16(f+14,1);put16(f+16,0x800);f[18]=6;f[19]=4;put16(f+20,2);memcpy(f+22,peer,6);memcpy(f+28,p->config.gateway,4);memcpy(f+32,mac,6);memcpy(f+38,p->config.ip,4);}
int main(void){
    struct kui_network_probe p;uint8_t f[KUI_NETWORK_FRAME_MAX],good[KUI_NETWORK_FRAME_MAX],ip[4];unsigned n;
    assert(kui_network_parse_ipv4("192.168.1.42",ip)&&ip[3]==42);
    assert(!kui_network_parse_ipv4("192.168.1.256",ip)&&!kui_network_parse_ipv4("1.2.3.4x",ip)&&!kui_network_parse_ipv4("1..3.4",ip));
    struct kui_network_config c={{192,168,1,42},{255,255,255,0},{192,168,1,1},{192,168,1,1}};
    assert(kui_network_config_valid(&c));c.mask[2]=254;c.mask[3]=1;assert(!kui_network_config_valid(&c));c.mask[2]=255;c.mask[3]=0;
    c.gateway[2]=2;assert(!kui_network_config_valid(&c));c.gateway[2]=1;
    kui_network_probe_begin(&p,mac,123,NULL,0);n=(unsigned)kui_network_probe_step(&p,f,0);assert(n==342&&f[284]==1);
    assert(!kui_network_probe_step(&p,f,1));assert(kui_network_probe_step(&p,f,4000));
    n=reply(&p,good,2);
    for(unsigned cut=0;cut<n;cut++){kui_network_probe_receive(&p,good,cut,4001);assert(p.stage==KUI_NET_DISCOVER);}
    memcpy(f,good,n);f[46]^=1;kui_network_probe_receive(&p,f,n,4001);assert(p.stage==KUI_NET_DISCOVER);
    memcpy(f,good,n);f[70]^=1;kui_network_probe_receive(&p,f,n,4001);assert(p.stage==KUI_NET_DISCOVER);
    memcpy(f,good,n);f[24]^=1;kui_network_probe_receive(&p,f,n,4001);assert(p.stage==KUI_NET_DISCOVER);
    memcpy(f,good,n);f[20]=0x20;ipfix(f,n);kui_network_probe_receive(&p,f,n,4001);assert(p.stage==KUI_NET_DISCOVER);
    /* A nonzero UDP checksum is verified, including its IPv4 pseudo-header. */
    memcpy(f,good,n);uint8_t udp[1600]={0};memcpy(udp,f+26,8);udp[9]=17;put16(udp+10,n-34);memcpy(udp+12,f+34,n-34);put16(f+40,checksum(udp,n-34+12));
    memcpy(good,f,n);f[100]^=1;kui_network_probe_receive(&p,f,n,4001);assert(p.stage==KUI_NET_DISCOVER);
    kui_network_probe_receive(&p,good,n,4001);assert(p.stage==KUI_NET_REQUEST);
    n=(unsigned)kui_network_probe_step(&p,f,4002);assert(n&&f[284]==3);
    n=reply(&p,f,5);kui_network_probe_receive(&p,f,n,4003);assert(p.stage==KUI_NET_CONFLICT&&p.leased);
    assert(kui_network_probe_step(&p,f,4003)==60&&f[28]==0);
    assert(kui_network_probe_step(&p,f,7003)==60&&p.stage==KUI_NET_ARP);
    arp_reply(&p,f);kui_network_probe_receive(&p,f,60,7004);assert(p.stage==KUI_NET_ECHO&&p.arp_reply);
    assert(kui_network_probe_step(&p,f,7005)==60);memcpy(f,mac,6);memcpy(f+6,peer,6);memcpy(f+26,c.gateway,4);memcpy(f+30,c.ip,4);f[34]=0;put16(f+36,0);put16(f+36,checksum(f+34,20));ipfix(f,54);
    memcpy(good,f,60);f[50]^=1;put16(f+36,0);put16(f+36,checksum(f+34,20));kui_network_probe_receive(&p,f,60,7010);assert(p.stage==KUI_NET_ECHO);
    kui_network_probe_receive(&p,good,60,7012);assert(p.stage==KUI_NET_DONE&&p.echo_reply&&p.echo_ms==7);
    kui_network_probe_begin(&p,mac,123,NULL,0);kui_network_probe_step(&p,f,15000);assert(p.stage==KUI_NET_FAILED&&strstr(p.failure,"offer"));
    kui_network_probe_begin(&p,mac,123,NULL,0);n=reply(&p,f,2);kui_network_probe_receive(&p,f,n,1);n=reply(&p,f,6);kui_network_probe_receive(&p,f,n,2);assert(p.stage==KUI_NET_FAILED&&strstr(p.failure,"rejected"));
    kui_network_probe_begin(&p,mac,123,&c,0);arp_reply(&p,f);memcpy(f+28,c.ip,4);kui_network_probe_receive(&p,f,60,1);assert(p.stage==KUI_NET_FAILED&&strstr(p.failure,"conflict"));
    memset(c.gateway,0,4);kui_network_probe_begin(&p,mac,123,&c,0);kui_network_probe_step(&p,f,3000);assert(p.stage==KUI_NET_DONE&&!p.echo_reply);
    /* Malformed option lengths and duplicate singleton options never acquire a lease. */
    for(unsigned v=0;v<256;v++){kui_network_probe_begin(&p,mac,123,NULL,0);n=reply(&p,f,2);f[283]=(uint8_t)v;kui_network_probe_receive(&p,f,n,1);if(v!=1)assert(p.stage==KUI_NET_DISCOVER);}
    puts("PASS network protocol: DHCP, ARP conflict, gateway echo, exact transaction, malformed/truncated packets, deadlines");return 0;
}
