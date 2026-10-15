/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture_network.h"
#include "kui/network_w5500.h"
#include "kui/network_wifi.h"
#include "ff.h"
#include <kos/thread.h>
#include <kos/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct kui_capture_network {
    struct kui_network_rip client;
    struct kui_w5500_session wired;
    struct kui_wifi_session *wifi;
    kui_cancel_fn cancel;
    kui_log_fn log;
    void (*stage)(const char *);
    uint64_t renew_after;
    uint8_t ip[4];
    char problem[128];
};
static void clear(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
bool kui_capture_network_config_load(struct kui_network_rip_config *out, char problem[128]) {
    FIL f;char text[KUI_NETWORK_RIP_CONFIG_CAP];UINT got=0;bool ok=false;
    struct kui_network_rip_config parsed={0};
    if(problem)problem[0]=0;
    if(!out||f_open(&f,KUI_CAPTURE_NETWORK_CONFIG,FA_READ)!=FR_OK){
        if(problem)snprintf(problem,128,"Cannot read /KUI/network-rip.cfg on the local card");
        return false;
    }
    FSIZE_t bytes=f_size(&f);
    if(bytes&&bytes<=sizeof(text)&&f_read(&f,text,(UINT)bytes,&got)==FR_OK&&got==bytes)
        ok=kui_network_rip_config_parse(&parsed,text,got);
    if(f_close(&f)!=FR_OK)ok=false;
    clear(text,sizeof(text));
    if(ok)*out=parsed;
    clear(&parsed,sizeof(parsed));
    if(!ok&&problem)snprintf(problem,128,"Invalid or unreadable /KUI/network-rip.cfg");
    return ok;
}
static void stage(struct kui_capture_network *s,const char *text){if(s->stage)s->stage(text);if(s->log)s->log("PC destination: %s",text);}
static uint64_t now_ms(void *ctx){(void)ctx;return timer_ms_gettime64();}
static void pause_ms(void *ctx,unsigned ms){(void)ctx;thd_sleep(ms);}
static bool cancelled(void *ctx){struct kui_capture_network *s=ctx;return s->cancel&&s->cancel();}
static bool problem(struct kui_capture_network *s,const char *text){snprintf(s->problem,sizeof(s->problem),"%s",text);return false;}
static void wifi_stage(void *ctx,const char *text){stage(ctx,text);}
static bool poll_network(void *ctx) {
    struct kui_capture_network *s=ctx;
    if(s->wifi){
        if(!kui_wifi_session_step(s->wifi))return problem(s,s->wifi->problem);
        if(s->wifi->host.wifi.state!=KWM_WIFI_ONLINE||memcmp(s->ip,s->wifi->host.wifi.ip,4))
            return problem(s,"Wi-Fi address or connection changed; resume explicitly");
        return true;
    }
    uint64_t now=timer_ms_gettime64();
    if(!kui_w5500_session_expires_in(&s->wired,now))return problem(s,"The network address lease expired; resume explicitly");
    if(kui_w5500_session_renew_in(&s->wired,now)||now<s->renew_after)return true;
    bool refused=false;
    if(kui_w5500_session_renew(&s->wired,1,2000,&refused,s->cancel)){
        if(memcmp(s->ip,s->wired.config.ip,4))return problem(s,"The network address changed; resume explicitly");
        if(s->log)s->log("PC destination: network address lease renewed");
    }else if(refused)return problem(s,"The network refused address renewal; resume explicitly");
    else s->renew_after=timer_ms_gettime64()+60000u;
    if(!kui_w5500_session_expires_in(&s->wired,timer_ms_gettime64()))return problem(s,"The network address lease expired; resume explicitly");
    return true;
}
bool kui_capture_network_end(struct kui_capture_network *s) {
    if(!s)return true;
    kui_media_set(NULL);
    bool ok=s->client.live&&!s->problem[0];
    kui_network_rip_close(&s->client);
    if(s->client.problem[0])ok=false;
    if(!ok&&s->log){
        const char *why=kui_capture_network_problem(s);
        s->log("PC destination: %s",why[0]?why:"Final PC volume sync and close were not acknowledged");
    }
    if(s->wifi){kui_wifi_session_end(s->wifi);clear(s->wifi,sizeof(*s->wifi));free(s->wifi);}
    kui_w5500_session_end(&s->wired);
    clear(s,sizeof(*s));free(s);
    return ok;
}
struct kui_capture_network *kui_capture_network_start(const struct kui_net_ports *ports,
    const struct kui_network_rip_config *config,kui_log_fn log,kui_cancel_fn cancel,
    void (*progress)(const char *),char out_problem[128]) {
    if(out_problem)out_problem[0]=0;
    kui_media_set(NULL);
    if(!ports||(!ports->w5500&&!ports->wifi)||!kui_network_rip_config_valid(config)){
        if(out_problem)snprintf(out_problem,128,"PC destination needs a configured W5500 or Wi-Fi board");
        return NULL;
    }
    struct kui_capture_network *s=calloc(1,sizeof(*s));
    if(!s){if(out_problem)snprintf(out_problem,128,"Insufficient memory for PC destination");return NULL;}
    s->cancel=cancel;s->log=log;s->stage=progress;
    struct kui_net_sockets net={0};bool wired=false;
    if(cancelled(s)){problem(s,"PC capture stopped");goto failed;}
    if(ports->w5500){
        stage(s,"Looking for a W5500 on the SCI port");
        wired=kui_w5500_session_find(&s->wired,ports->w5500,log);
        if(!wired&&(s->wired.version==KUI_W5500_VERSION||!ports->wifi)){problem(s,s->wired.problem);goto failed;}
    }
    if(wired){
        /* TCP slot 0 streams bounded frames; slot 1 is kept for DHCP.
         * Unused sockets have no buffers/listeners. */
        static const uint8_t sizes[KUI_W5500_SOCKETS]={8,2,0,0,0,0,0,0};
        if(!kui_w5500_buffers(&s->wired.chip,sizes,sizes)){problem(s,"The W5500 stopped answering");goto failed;}
        stage(s,"Waiting for the network cable and DHCP address");
        if(!kui_w5500_session_link(&s->wired,cancel)||!kui_w5500_session_dhcp(&s->wired,true,NULL,log,cancel,NULL)){
            problem(s,s->wired.problem);goto failed;
        }
        memcpy(s->ip,s->wired.config.ip,4);kui_w5500_session_sockets(&s->wired,&net);
    }else{
        stage(s,"Looking for the Wi-Fi board on the SCI port");
        s->wifi=calloc(1,sizeof(*s->wifi));
        if(!s->wifi){problem(s,"Insufficient memory for the Wi-Fi board");goto failed;}
        if(!kui_wifi_session_find(s->wifi,ports->wifi,log)||
           !kui_wifi_session_online(s->wifi,KUI_WIFI_JOIN_MS,cancel,wifi_stage,s)){
            problem(s,s->wifi->problem);goto failed;
        }
        memcpy(s->ip,s->wifi->host.wifi.ip,4);kui_wifi_session_sockets(s->wifi,&net);
    }
    stage(s,"Authenticating the PC destination volume");
    struct kui_network_rip_hooks hooks={s,now_ms,pause_ms,cancelled,poll_network};
    if(!kui_network_rip_open(&s->client,&net,0,config,&hooks)){if(!s->problem[0])problem(s,kui_network_rip_problem(&s->client));goto failed;}
    struct kui_media_ops media;kui_network_rip_media(&s->client,&media);kui_media_set(&media);
    stage(s,"PC destination authenticated; capture writes directly to its volume image");
    return s;
failed:
    if(out_problem)snprintf(out_problem,128,"%s",s->problem);
    kui_capture_network_end(s);return NULL;
}
const char *kui_capture_network_problem(const struct kui_capture_network *s) {
    return s?(s->problem[0]?s->problem:kui_network_rip_problem(&s->client)):"PC destination unavailable";
}
