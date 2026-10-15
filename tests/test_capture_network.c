/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture_network.h"
#include "kui/network_w5500.h"
#include "kui/network_wifi.h"
#include "ff.h"
#include <stdarg.h>
/* Shared independent wire fixtures and bounded socket model. */
#define main portable_unit_main
#include "test_network_rip.c"
#undef main

static struct kui_media_ops registered;
static unsigned wired_end,wifi_end,renews,wifi_steps;
static bool absent,wiring_fault,buffers_fail,wifi_only,link_fail,dhcp_fail,wifi_fail,wifi_offline,wifi_address_change;
static unsigned renew_mode;
static bool file_close_fail,file_read_fail;
static const char *file_text;
static size_t file_length;
static char last_log[256];
static const char closed[]=
    "4b4e5231010601003412000000000000030000000000000000000000000000000000000000000000d7867f2700000000"
    "ead1ebf1d7b7bff8ed26b3d600a01320009b69f5f8081d8d217c3bee33f60840";
static const struct kui_w5500_port wired_port={0};
static const struct kui_wifi_port wifi_port={0};
uint64_t timer_ms_gettime64(void){return m.now;}
void thd_sleep(unsigned ms){m.now+=ms?ms:1;}
void kui_media_set(const struct kui_media_ops *ops){registered=ops?*ops:(struct kui_media_ops){0};}
bool kui_w5500_session_find(struct kui_w5500_session *s,const struct kui_w5500_port *port,kui_log_fn log){
    (void)log;s->port=port;s->version=wiring_fault?KUI_W5500_VERSION:0;
    if(absent||wiring_fault||wifi_only){strcpy(s->problem,"W5500 unavailable");return false;}
    s->open=s->found=true;s->version=KUI_W5500_VERSION;return true;
}
bool kui_w5500_buffers(struct kui_w5500 *w,const uint8_t rx[8],const uint8_t tx[8]){
    (void)w;assert(rx[0]==8&&tx[0]==8&&rx[1]==2&&tx[1]==2);for(unsigned i=2;i<8;i++)assert(!rx[i]&&!tx[i]);return !buffers_fail;
}
bool kui_w5500_session_link(struct kui_w5500_session *s,kui_cancel_fn cancel_fn){(void)cancel_fn;if(link_fail)strcpy(s->problem,"cable missing");return !link_fail;}
bool kui_w5500_session_dhcp(struct kui_w5500_session *s,bool lease_only,struct kui_network_probe *probe,kui_log_fn log,kui_cancel_fn cancel_fn,void(*stage_fn)(enum kui_network_stage)){
    (void)probe;(void)log;(void)cancel_fn;(void)stage_fn;assert(lease_only);
    if(dhcp_fail){strcpy(s->problem,"DHCP failed");return false;}s->leased=true;s->lease_seconds=3600;s->leased_ms=m.now;s->config.ip[0]=10;s->config.ip[3]=2;return true;
}
uint32_t kui_w5500_session_expires_in(const struct kui_w5500_session *s,uint64_t t){uint64_t used=(t-s->leased_ms)/1000;return used>=s->lease_seconds?0:s->lease_seconds-(uint32_t)used;}
uint32_t kui_w5500_session_renew_in(const struct kui_w5500_session *s,uint64_t t){uint64_t used=(t-s->leased_ms)/1000;return used>=s->lease_seconds/2?0:s->lease_seconds/2-(uint32_t)used;}
bool kui_w5500_session_renew(struct kui_w5500_session *s,unsigned udp,unsigned limit,bool *refused,kui_cancel_fn cancel_fn){
    (void)cancel_fn;assert(udp==1&&limit==2000);++renews;*refused=renew_mode==1;
    if(renew_mode==1||renew_mode==2)return false;
    s->leased_ms=m.now;if(renew_mode==3)s->config.ip[3]^=1;return true;
}
void kui_w5500_session_sockets(struct kui_w5500_session *s,struct kui_net_sockets *out){(void)s;*out=net;}
void kui_w5500_session_end(struct kui_w5500_session *s){assert(!registered.blocks);if(s->open){++wired_end;s->open=false;}}
bool kui_wifi_session_find(struct kui_wifi_session *s,const struct kui_wifi_port *port,kui_log_fn log){
    (void)log;s->port=port;if(wifi_fail){strcpy(s->problem,"Wi-Fi unavailable");return false;}s->open=s->found=true;return true;
}
bool kui_wifi_session_online(struct kui_wifi_session *s,unsigned limit,kui_cancel_fn cancel_fn,void(*stage_fn)(void*,const char*),void *ctx){
    (void)cancel_fn;(void)stage_fn;(void)ctx;assert(limit==KUI_WIFI_JOIN_MS);s->host.wifi.state=KWM_WIFI_ONLINE;s->host.wifi.ip[0]=10;s->host.wifi.ip[3]=2;return true;
}
bool kui_wifi_session_step(struct kui_wifi_session *s){++wifi_steps;if(wifi_offline)s->host.wifi.state=KWM_WIFI_LOST;if(wifi_address_change)s->host.wifi.ip[3]^=1;return true;}
void kui_wifi_session_sockets(struct kui_wifi_session *s,struct kui_net_sockets *out){(void)s;*out=net;}
void kui_wifi_session_end(struct kui_wifi_session *s){assert(!registered.blocks);if(s->open){++wifi_end;s->open=false;}}
FRESULT f_open(FIL *f,const TCHAR *path,BYTE mode){assert(!strcmp(path,KUI_CAPTURE_NETWORK_CONFIG)&&mode==FA_READ);memset(f,0,sizeof(*f));f->obj.objsize=file_length;return file_text?FR_OK:FR_NO_FILE;}
FRESULT f_read(FIL *f,void *out,UINT n,UINT *got){(void)f;if(file_read_fail){*got=0;return FR_DISK_ERR;}memcpy(out,file_text,n);*got=n;return FR_OK;}
FRESULT f_close(FIL *f){(void)f;return file_close_fail?FR_DISK_ERR:FR_OK;}
static bool stop_fn(void){return m.stop;}
static void record_log(const char *format,...){va_list args;va_start(args,format);vsnprintf(last_log,sizeof(last_log),format,args);va_end(args);}
static void reset(void){
    clear_model();registered=(struct kui_media_ops){0};wired_end=wifi_end=renews=wifi_steps=0;
    last_log[0]=0;
    absent=wiring_fault=buffers_fail=wifi_only=link_fail=dhcp_fail=wifi_fail=wifi_offline=wifi_address_change=false;renew_mode=0;
    append(challenge);append(authenticated);append(synced);
}
static struct kui_capture_network *start_session(void){
    struct kui_net_ports ports={&wired_port,&wifi_port};struct kui_network_rip_config cfg=config();char why[128];
    struct kui_capture_network *s=kui_capture_network_start(&ports,&cfg,record_log,stop_fn,NULL,why);assert(s&&!why[0]&&registered.blocks&&registered.blocks(registered.ctx)==1000);return s;
}
static void finish(struct kui_capture_network *s){m.stop=true;assert(!kui_capture_network_end(s));assert(!registered.blocks&&!registered.ctx);}
static void final_close(void){
    assert(kui_capture_network_end(NULL));
    reset();append(closed);struct kui_capture_network *s=start_session();
    assert(kui_capture_network_end(s)&&m.at==m.length&&m.closes==1&&wired_end==1&&!registered.ctx);
    assert(!strstr(last_log,"failed")&&!strstr(last_log,"not acknowledged"));
    /* All preceding work succeeded; failures first appear during end(). */
    reset();s=start_session();m.send_fail=true;
    assert(!kui_capture_network_end(s)&&wired_end==1&&!registered.ctx&&strstr(last_log,"transfer failed"));
    reset();s=start_session();
    assert(!kui_capture_network_end(s)&&wired_end==1&&!registered.ctx&&strstr(last_log,"timed out"));
    assert(m.at==m.length&&m.written==112+80+80); /* SYNC ack arrived; CLOSE ack did not. */
    reset();append(closed);s=start_session();m.input[m.length-1]^=1;
    assert(!kui_capture_network_end(s)&&wired_end==1&&!registered.ctx&&strstr(last_log,"authentication failed"));
    reset();append(closed);s=start_session();m.close_fail=true;
    assert(!kui_capture_network_end(s)&&wired_end==1&&!registered.ctx&&strstr(last_log,"socket close failed"));
    puts("PASS final PC SYNC/CLOSE success and late failure reporting before session free");
}
static void file_configuration(void){
    static const char text[]="host=10.0.0.2\nport=7412\nkey=1111111111111111111111111111111111111111111111111111111111111111\nvolume=22222222222222222222222222222222\n";
    struct kui_network_rip_config parsed,before;char why[128];file_text=text;file_length=sizeof(text)-1;file_close_fail=file_read_fail=false;
    assert(kui_capture_network_config_load(&parsed,why)&&!why[0]&&parsed.port==7412);
    memset(&parsed,0xa5,sizeof(parsed));before=parsed;file_close_fail=true;
    assert(!kui_capture_network_config_load(&parsed,why)&&!memcmp(&parsed,&before,sizeof(parsed))&&why[0]);file_close_fail=false;
    file_read_fail=true;assert(!kui_capture_network_config_load(&parsed,why)&&!memcmp(&parsed,&before,sizeof(parsed)));file_read_fail=false;
    file_length=KUI_NETWORK_RIP_CONFIG_CAP+1;assert(!kui_capture_network_config_load(&parsed,why)&&!memcmp(&parsed,&before,sizeof(parsed)));
    file_text=NULL;assert(!kui_capture_network_config_load(&parsed,why)&&strstr(why,"local card"));
}
int main(void){
    file_configuration();
    final_close();
    reset();struct kui_capture_network *s=start_session();assert(!wifi_steps);m.now=1800000;assert(!registered.sync(registered.ctx)&&renews==1);finish(s);assert(wired_end==1&&!wifi_end);
    reset();s=start_session();m.now=3600000;assert(registered.sync(registered.ctx)&&strstr(kui_capture_network_problem(s),"expired"));finish(s);assert(wired_end==1);
    for(unsigned mode=1;mode<=3;mode++){
        reset();s=start_session();renew_mode=mode;m.now=1800000;int r=registered.sync(registered.ctx);
        if(mode==2)assert(!r&&renews==1);else assert(r&&renews==1&&strstr(kui_capture_network_problem(s),mode==1?"refused":"changed"));finish(s);
    }
    reset();wifi_only=true;s=start_session();assert(wifi_steps);finish(s);assert(wifi_end==1&&!wired_end);
    reset();wifi_only=true;s=start_session();wifi_offline=true;assert(registered.sync(registered.ctx)&&strstr(kui_capture_network_problem(s),"changed"));finish(s);
    reset();wifi_only=true;s=start_session();wifi_address_change=true;assert(registered.sync(registered.ctx));finish(s);
    for(unsigned mode=0;mode<5;mode++){
        reset();if(mode==0)wiring_fault=true;if(mode==1)buffers_fail=true;if(mode==2)link_fail=true;if(mode==3)dhcp_fail=true;if(mode==4){wifi_only=true;wifi_fail=true;}
        struct kui_net_ports ports={&wired_port,&wifi_port};struct kui_network_rip_config cfg=config();char why[128];
        assert(!kui_capture_network_start(&ports,&cfg,NULL,stop_fn,NULL,why)&&why[0]&&!registered.blocks);
    }
    puts("PASS native PC session media ownership, bounded config read, adapters, renewal, expiry and Wi-Fi loss cleanup");return 0;
}
