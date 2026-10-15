/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/network_rip.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independent Python struct/zlib/hashlib vectors, including HMAC response.
 * This test never calls the production frame encoder to build its input. */
static const char challenge[]=
    "4b4e5231010101003412000000000000000000000000000000000000000000003800000000000000d9c653cc00000000"
    "22222222222222222222222222222222e8030000000000003333333333333333333333333333333333333333333333333333333333333333"
    "0000000000000000000000000000000000000000000000000000000000000000";
static const char authenticated[]=
    "4b4e523101020100341200000000000001000000000000000000000000000000180000000000000002bb1e9a00000000"
    "22222222222222222222222222222222e803000000000000de5d8a7eb4410405d503e75a4caaad55a809cf6a2c94573368899f43823fcb27";
static const char synced[]=
    "4b4e52310105010034120000000000000200000000000000000000000000000000000000000000007098911f00000000"
    "8cbfcd67e1f8b745486d2055189b6ccbf99f9a422136f0aaf0d4eba44459f8f7";
static struct model {
    uint8_t input[1024],output[2048];size_t length,at,written;
    uint64_t now;unsigned closes,polls;bool stop,poll_fail,receive_fail,send_fail,connect_wait,close_fail;
} m;
static unsigned nibble(char c){return c<='9'?(unsigned)(c-'0'):(unsigned)(c-'a'+10);}
static void append(const char *text){for(size_t i=0;i<strlen(text);i+=2)m.input[m.length++]=(uint8_t)(nibble(text[i])*16+nibble(text[i+1]));}
static uint64_t now(void *ctx){(void)ctx;return m.now;}
static void pause_ms(void *ctx,unsigned ms){(void)ctx;m.now+=ms?ms:1;}
static bool cancel(void *ctx){(void)ctx;return m.stop;}
static bool poll_net(void *ctx){(void)ctx;++m.polls;return !m.poll_fail;}
static bool connect_net(void *ctx,unsigned s,uint16_t local,const uint8_t ip[4],uint16_t port){(void)ctx;assert(s==0&&local&&ip[0]==10&&port==7412);return true;}
static bool close_net(void *ctx,unsigned s){(void)ctx;assert(s==0);++m.closes;return !m.close_fail;}
static bool state(void *ctx,unsigned s,uint8_t *out){(void)ctx;(void)s;*out=m.connect_wait?KUI_NET_CONNECTING:KUI_NET_ESTABLISHED;return true;}
static bool received(void *ctx,unsigned s,size_t *out){(void)ctx;(void)s;*out=m.length-m.at;if(*out>3)*out=3;return true;}
static bool receive_net(void *ctx,unsigned s,void *out,size_t n){(void)ctx;(void)s;assert(n<=3&&n<=m.length-m.at);if(m.receive_fail)return false;memcpy(out,m.input+m.at,n);m.at+=n;return true;}
static bool room(void *ctx,unsigned s,size_t *out){(void)ctx;(void)s;*out=2;return true;}
static bool send_net(void *ctx,unsigned s,const void *p,size_t n){(void)ctx;(void)s;assert(n<=2&&m.written+n<=sizeof(m.output));if(m.send_fail)return false;memcpy(m.output+m.written,p,n);m.written+=n;return true;}
static const struct kui_net_sockets net={.count=1,.connect=connect_net,.close=close_net,.state=state,.received=received,.receive=receive_net,.room=room,.send=send_net};
static const struct kui_network_rip_hooks hooks={.now_ms=now,.pause=pause_ms,.cancel=cancel,.poll=poll_net};
static struct kui_network_rip_config config(void){struct kui_network_rip_config c={.host={10,0,0,2},.port=7412};memset(c.key,0x11,32);memset(c.volume,0x22,16);return c;}
static void configuration(void){
    static const char good[]="# secret is never logged\r\n host = 10.0.0.2\r\nport=7412\nkey=1111111111111111111111111111111111111111111111111111111111111111\nvolume=22222222222222222222222222222222 # pinned\n";
    struct kui_network_rip_config c,expected=config();memset(&c,0xa5,sizeof(c));
    assert(kui_network_rip_config_parse(&c,good,sizeof(good)-1));
    assert(!memcmp(c.host,expected.host,4)&&c.port==expected.port&&!memcmp(c.key,expected.key,32)&&!memcmp(c.volume,expected.volume,16));
    static const char *const bad[]={"host=10.0.0.999","port=0","port=65536","host=127.0.0.1","host=224.1.1.1","unknown=x","key=11","volume=22","host=10.0.0.2\nhost=10.0.0.3","port=+12"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        char text[1024];int n=snprintf(text,sizeof(text),"%s\n%s\n",good,bad[i]);assert(n>0&&(size_t)n<sizeof(text));
        memset(&c,0xa5,sizeof(c));struct kui_network_rip_config before=c;
        assert(!kui_network_rip_config_parse(&c,text,(size_t)n)&&!memcmp(&c,&before,sizeof(c)));
    }
    memset(&c,0xa5,sizeof(c));struct kui_network_rip_config before=c;
    assert(!kui_network_rip_config_parse(&c,good,sizeof(good))&&!memcmp(&c,&before,sizeof(c)));
    assert(!kui_network_rip_config_parse(&c,good,0));assert(!kui_network_rip_config_parse(&c,good,KUI_NETWORK_RIP_CONFIG_CAP+1));
    expected=config();memset(expected.key,0,32);assert(!kui_network_rip_config_valid(&expected));
    puts("PASS strict bounded PC destination configuration and unchanged failure output");
}
static void clear_model(void){memset(&m,0,sizeof(m));}
static void expect_failed(struct kui_network_rip *c,const struct kui_network_rip_config *cfg,const char *message){
    assert(!kui_network_rip_open(c,&net,0,cfg,&hooks));assert(!c->live&&!c->connected&&m.closes==1&&strstr(c->problem,message));
    struct kui_media_ops media;kui_network_rip_media(c,&media);assert(!media.blocks&&!media.write&&!media.read&&!media.sync);
    kui_network_rip_close(c);for(unsigned i=0;i<32;i++)assert(!c->config.key[i]);assert(m.closes==1);
}
int main(void){
    configuration();struct kui_network_rip *c=calloc(1,sizeof(*c));assert(c);struct kui_network_rip_config cfg=config();
    clear_model();append(challenge);append(authenticated);append(synced);
    assert(kui_network_rip_open(c,&net,0,&cfg,&hooks));assert(c->live&&c->blocks==1000&&c->session==0x1234&&c->sequence==1&&m.polls>50);
    struct kui_media_ops media;kui_network_rip_media(c,&media);assert(media.blocks(media.ctx)==1000&&!media.sync(media.ctx));
    assert(m.at==m.length&&c->sequence==2&&m.written==112+80);
    uint8_t out[512];memset(out,0xa5,sizeof(out));assert(media.read(media.ctx,1000,1,out)&&c->live);assert(media.write(media.ctx,0,0,out)&&c->live);
    m.stop=true;kui_network_rip_close(c);assert(!c->live&&!c->connected&&m.closes==1);for(unsigned i=0;i<32;i++)assert(!c->config.key[i]);
    puts("PASS independent authenticated vectors and fragmented socket I/O");
    clear_model();m.connect_wait=true;expect_failed(c,&cfg,"timed out");assert(m.now==KUI_NETWORK_RIP_TIMEOUT_MS);
    clear_model();m.stop=true;expect_failed(c,&cfg,"stopped");
    clear_model();m.poll_fail=true;expect_failed(c,&cfg,"lease failed");
    clear_model();append(challenge);m.receive_fail=true;expect_failed(c,&cfg,"transfer failed");
    clear_model();append(challenge);m.send_fail=true;expect_failed(c,&cfg,"transfer failed");
    clear_model();append(challenge);m.input[50]^=1;expect_failed(c,&cfg,"checksum failed");
    clear_model();append(challenge);append(authenticated);m.input[m.length-1]^=1;expect_failed(c,&cfg,"authentication failed");
    clear_model();append(challenge);m.input[32]=0xff;m.input[33]=0xff;expect_failed(c,&cfg,"Invalid");
    clear_model();append(challenge);m.length-=1;expect_failed(c,&cfg,"timed out");
    clear_model();append(challenge);cfg.volume[0]^=1;expect_failed(c,&cfg,"identity");cfg=config();
    clear_model();append(challenge);append(authenticated);m.input[136+16]=2;expect_failed(c,&cfg,"Invalid");
    for(unsigned corrupt_crc=0;corrupt_crc<2;corrupt_crc++){
        clear_model();append(challenge);append(authenticated);
        append("4b4e52310103010034120000000000000200000000000000000000000100000000020000000000007100327500000000");
        memset(m.input+m.length,0x44,512);m.length+=512;
        if(corrupt_crc)m.input[m.length-1]^=1;
        memset(m.input+m.length,0,32);m.length+=32;
        assert(kui_network_rip_open(c,&net,0,&cfg,&hooks));kui_network_rip_media(c,&media);
        memset(out,0xa5,sizeof(out));assert(media.read(media.ctx,0,1,out)&&!c->live&&!c->connected);
        assert(strstr(c->problem,corrupt_crc?"checksum":"authentication"));
        for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0xa5);
        kui_network_rip_close(c);
    }
    puts("PASS cancellation, lease failure, short/disconnected I/O, malformed bounds, CRC, authentication, stale volume and sequence guards");
    free(c);return 0;
}
