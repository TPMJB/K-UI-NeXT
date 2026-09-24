/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/network_probe.h"
#include <kos/net.h>
#include <kos/thread.h>
#include <kos/irq.h>
#include <kos/timer.h>
#include <dc/net/broadband_adapter.h>
#include <dc/net/lan_adapter.h>
#include <stdio.h>
#include <string.h>
/* Input ownership is exclusive while every other interface is stopped. Packet
 * processing happens on the I/O worker; the driver callback only copies a
 * bounded frame under the SH-4 interrupt lock. No KOS socket core is started. */
static netif_t *receiving;
static struct {uint8_t bytes[KUI_NETWORK_FRAME_MAX];unsigned length;} queue[4];
static unsigned head,tail,dropped;
static int receive(netif_t *n,const uint8_t *data,int bytes){
    if(n!=receiving||bytes<14||bytes>(int)KUI_NETWORK_FRAME_MAX)return 0;
    irq_mask_t flags=irq_disable();unsigned next=(head+1)%4;
    if(next==tail)++dropped;else{memcpy(queue[head].bytes,data,(size_t)bytes);queue[head].length=(unsigned)bytes;head=next;}
    irq_restore(flags);return 0;
}
static netif_t *find(const char *name){netif_t *n;LIST_FOREACH(n,net_get_if_list(),if_list)if(n->name&&!strcmp(n->name,name))return n;return NULL;}
static void emit(struct kui_app_status *out,kui_app_progress_fn progress,const char *message){snprintf(out->message,sizeof(out->message),"%s",message);if(progress)progress(out);}
static const char *phase(enum kui_network_stage s){switch(s){case KUI_NET_DISCOVER:return "Requesting DHCP offer (15 second limit)";case KUI_NET_REQUEST:return "Requesting DHCP lease (15 second limit)";case KUI_NET_CONFLICT:return "Checking address conflict (3 seconds)";case KUI_NET_ARP:return "Testing gateway ARP (5 second limit)";case KUI_NET_ECHO:return "Testing gateway ping (5 second limit)";default:return "Network test finished";}}
void kui_network_connect_run(const struct kui_network_config *config,struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress){
    if(!out)return;
    memset(out,0,sizeof(*out));netif_t *n=NULL,*item;unsigned owned=0;bool initialized=false,started=false,target=false;net_input_func old=NULL;
    struct kui_network_probe p;memset(&p,0,sizeof(p));p.stage=KUI_NET_FAILED;_Alignas(32) uint8_t frame[KUI_NETWORK_FRAME_MAX];
    if(config&&!kui_network_config_valid(config)){emit(out,progress,"Invalid static IPv4 configuration");++out->errors;goto done;}
    if(cancel&&cancel())goto stop;
    LIST_FOREACH(item,net_get_if_list(),if_list)if(item->flags&NETIF_RUNNING){emit(out,progress,"An interface is already running; connection test refused");++out->errors;goto done;}
    n=find("bba");if(!n)n=find("la");
    if(!n){if(bba_init()==0){n=find("bba");owned=1;}else bba_shutdown();}
    if(cancel&&cancel())goto stop;
    if(!n){if(la_init()==0){n=find("la");owned=2;}else la_shutdown();}
    if(n&&!(n->flags&NETIF_DETECTED)&&n->if_detect)n->if_detect(n);
    if(!n||!(n->flags&NETIF_DETECTED)||(n->flags&NETIF_NOETH)||!n->if_init||!n->if_start||!n->if_stop||!n->if_shutdown||!n->if_tx){emit(out,progress,"No supported Ethernet adapter detected");++out->errors;goto done;}
    emit(out,progress,"Starting adapter (KOS link wait: up to 10 seconds)");
    if(!(n->flags&NETIF_INITIALIZED)){if(n->if_init(n)<0){emit(out,progress,"Adapter initialization failed");++out->errors;goto done;}initialized=true;}
    if(cancel&&cancel())goto stop;
    head=tail=dropped=0;receiving=n;old=net_input_set_target(receive);target=true;
    if(n->if_start(n)<0){emit(out,progress,"Adapter link did not start");++out->errors;goto done;}started=true;
    uint64_t now=timer_ms_gettime64();kui_network_probe_begin(&p,n->mac_addr,(uint32_t)now^UINT32_C(0x4b554900),config,now);
    enum kui_network_stage last=KUI_NET_FAILED;
    while(p.stage<KUI_NET_DONE){
        if(cancel&&cancel())goto stop;
        /* BBA owns an RX worker and LAN receives in its IRQ handler. Calling
         * if_rx_poll here would race those drivers' receive state. */
        for(unsigned count=0;count<4;count++){
            irq_mask_t flags=irq_disable();unsigned size=0;
            if(tail!=head){size=queue[tail].length;memcpy(frame,queue[tail].bytes,size);tail=(tail+1)%4;}
            irq_restore(flags);if(!size)break;kui_network_probe_receive(&p,frame,size,timer_ms_gettime64());
        }
        now=timer_ms_gettime64();size_t bytes=kui_network_probe_step(&p,frame,now);
        if(bytes){int result=n->if_tx(n,frame,(int)bytes,NETIF_NOBLOCK);if(result!=NETIF_TX_OK&&result!=NETIF_TX_AGAIN){snprintf(p.failure,sizeof(p.failure),"Adapter packet transmit failed");p.stage=KUI_NET_FAILED;}}
        if(last!=p.stage){last=p.stage;emit(out,progress,phase(p.stage));if(log)log("Network: %s",out->message);}
        thd_sleep(10);
    }
    out->complete=true;out->passed=p.stage==KUI_NET_DONE;
    if(!out->passed){++out->errors;emit(out,progress,p.failure);}else emit(out,progress,p.echo_reply?"Address acquired; gateway ping replied":"Address acquired; no gateway advertised");
    out->line_count=8;
    snprintf(out->lines[0],KUI_APP_LINE_CAP,"Address: %u.%u.%u.%u (%s)",p.config.ip[0],p.config.ip[1],p.config.ip[2],p.config.ip[3],p.dhcp?(p.leased?"DHCP ACK":"no lease"):"temporary static");
    snprintf(out->lines[1],KUI_APP_LINE_CAP,"Gateway: %u.%u.%u.%u",p.config.gateway[0],p.config.gateway[1],p.config.gateway[2],p.config.gateway[3]);
    snprintf(out->lines[2],KUI_APP_LINE_CAP,"DNS advertised: %u.%u.%u.%u",p.config.dns[0],p.config.dns[1],p.config.dns[2],p.config.dns[3]);
    snprintf(out->lines[3],KUI_APP_LINE_CAP,"Gateway ARP: %s; ICMP: %s",p.arp_reply?"replied":"not proven",p.echo_reply?"replied":"not proven");
    snprintf(out->lines[4],KUI_APP_LINE_CAP,"Ping reply: %lu ms; received %u; ignored %u",(unsigned long)p.echo_ms,p.received,p.ignored);
    snprintf(out->lines[5],KUI_APP_LINE_CAP,"Receive queue drops: %u; test session closed",dropped);
    snprintf(out->lines[6],KUI_APP_LINE_CAP,"DNS lookup and Internet access were not tested");
    snprintf(out->lines[7],KUI_APP_LINE_CAP,"Saved settings and console flash are unchanged");
    goto done;
stop:out->stopped=true;out->passed=false;emit(out,progress,"Network connection test stopped");
done:
    if(started&&n->if_stop(n)<0){++out->errors;out->passed=false;emit(out,progress,"Adapter stop failed; reboot before network use");}
    if(target){receiving=NULL;net_input_set_target(old);}
    if(initialized&&n->if_shutdown(n)<0){++out->errors;out->passed=false;emit(out,progress,"Adapter shutdown failed; reboot before network use");}
    if(owned){if(owned==1)bba_shutdown();else la_shutdown();if(n)net_unreg_device(n);}
    if(log){log("Network connection test: %s",out->message);for(unsigned i=0;i<out->line_count;i++)log("%s",out->lines[i]);}
    if(progress)progress(out);
}
