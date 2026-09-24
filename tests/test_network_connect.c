/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/network_probe.h"
#include <kos/net.h>
#include <kos/irq.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct netif_list list;static netif_t adapter,other;
static uint64_t now;static bool present;static unsigned inits,starts,stops,ends,txs,unregs,cancel_at,callbacks,detects;static int fault;
static int old_input(netif_t*n,const uint8_t*p,int s){(void)n;(void)p;(void)s;return 0;}
static net_input_func input=old_input;
static int init(netif_t*n){++inits;if(fault==1)return -1;n->flags|=NETIF_INITIALIZED;return 0;}
static int start(netif_t*n){++starts;if(fault==2)return -1;n->flags|=NETIF_RUNNING;return 0;}
static int stop(netif_t*n){++stops;n->flags&=~NETIF_RUNNING;return 0;}
static int end(netif_t*n){++ends;n->flags&=~NETIF_INITIALIZED;return 0;}
static int tx(netif_t*n,const uint8_t*p,int s,int blocking){(void)n;assert(p&&s>=60&&blocking==NETIF_NOBLOCK&&((uintptr_t)p%32)==0);++txs;return fault==3?-1:0;}
static int poll(netif_t*n){(void)n;assert(!"RX polling races the driver worker/IRQ");return 0;}
static int detect(netif_t*n){++detects;n->flags|=NETIF_DETECTED;return 0;}
static void reset(void){memset(&adapter,0,sizeof(adapter));memset(&other,0,sizeof(other));LIST_INIT(&list);now=0;present=true;inits=starts=stops=ends=txs=unregs=cancel_at=callbacks=detects=0;fault=0;input=old_input;adapter.name="bba";adapter.flags=NETIF_DETECTED|NETIF_REGISTERED;adapter.if_detect=detect;adapter.if_init=init;adapter.if_start=start;adapter.if_stop=stop;adapter.if_shutdown=end;adapter.if_tx=tx;adapter.if_rx_poll=poll;adapter.mac_addr[0]=2;adapter.ip_addr[0]=192;adapter.ip_addr[3]=50;}
int bba_init(void){if(!present)return -1;LIST_INSERT_HEAD(&list,&adapter,if_list);return 0;}
int la_init(void){return -1;}
int bba_shutdown(void){if(adapter.flags&NETIF_RUNNING)stop(&adapter);if(adapter.flags&NETIF_INITIALIZED)end(&adapter);return 0;}
int la_shutdown(void){return 0;}
struct netif_list *net_get_if_list(void){return &list;}
int net_unreg_device(netif_t*n){++unregs;LIST_REMOVE(n,if_list);return 0;}
net_input_func net_input_set_target(net_input_func t){net_input_func old=input;input=t;++callbacks;return old;}
irq_mask_t irq_disable(void){return 0;}
void irq_restore(irq_mask_t state){assert(state==0);}
uint64_t timer_ms_gettime64(void){return now;}
void thd_sleep(unsigned ms){now+=ms;}
static bool cancel(void){return cancel_at&&now>=cancel_at;}
int main(void){struct kui_app_status out;struct kui_network_config c={{192,168,1,50},{255,255,255,0},{0},{0}};
    reset();kui_network_connect_run(&c,&out,NULL,cancel,NULL);assert(out.passed&&out.complete&&txs==3&&now>=3000&&unregs==1&&stops==1&&ends==1&&callbacks==2&&input==old_input&&LIST_EMPTY(&list));assert(adapter.ip_addr[0]==192&&adapter.ip_addr[3]==50);
    reset();kui_network_connect_run(NULL,&out,NULL,cancel,NULL);assert(!out.passed&&out.errors==1&&strstr(out.message,"offer")&&now>=15000&&now<15100&&unregs==1&&input==old_input);
    reset();cancel_at=20;kui_network_connect_run(NULL,&out,NULL,cancel,NULL);assert(out.stopped&&!out.passed&&stops==1&&ends==1&&unregs==1&&input==old_input);
    reset();present=false;kui_network_connect_run(NULL,&out,NULL,cancel,NULL);assert(out.errors==1&&!inits&&!starts&&!callbacks);
    for(int f=1;f<=3;f++){reset();fault=f;kui_network_connect_run(NULL,&out,NULL,cancel,NULL);assert(out.errors==1&&!out.passed&&unregs==1&&input==old_input&&LIST_EMPTY(&list));}
    reset();LIST_INSERT_HEAD(&list,&adapter,if_list);adapter.flags|=NETIF_INITIALIZED;kui_network_connect_run(&c,&out,NULL,cancel,NULL);assert(out.passed&&!inits&&!ends&&!unregs&&stops==1&&adapter.flags&NETIF_INITIALIZED);
    reset();other.flags=NETIF_RUNNING;LIST_INSERT_HEAD(&list,&other,if_list);kui_network_connect_run(NULL,&out,NULL,cancel,NULL);assert(out.errors==1&&!inits&&!starts&&!callbacks&&!unregs&&LIST_FIRST(&list)==&other);
    reset();LIST_INSERT_HEAD(&list,&adapter,if_list);adapter.flags&=~NETIF_DETECTED;kui_network_connect_run(&c,&out,NULL,cancel,NULL);assert(out.passed&&detects==1&&!unregs);
    puts("PASS network lifecycle: temporary session, no IP mutation, prior callback/driver restoration, timeout, stop, active-owner refusal");return 0;}
