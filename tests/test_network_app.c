/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/network_test.h"
#include <kos/net.h>
#include <dc/net/broadband_adapter.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct netif_list interfaces;
static netif_t bba,lan,modem;
static struct {
    bool bba_present,lan_present;
    unsigned bba_init,bba_end,lan_init,lan_end,unreg,phy_reads,cancel_calls,cancel_after;
    uint16_t phy;
} fake;
static void reset(void) {
    memset(&fake,0,sizeof(fake));LIST_INIT(&interfaces);
    memset(&bba,0,sizeof(bba));memset(&lan,0,sizeof(lan));memset(&modem,0,sizeof(modem));
    bba.name="bba";bba.descr="Broadband Adapter (HIT-0400)";
    lan.name="la";lan.descr="LAN Adapter (HIT-0300)";
    modem.name="ppp";modem.descr="Dial-up modem";
    uint8_t ip[]={192,168,1,42},mask[]={255,255,255,0},gateway[]={192,168,1,1},dns[]={192,168,1,2};
    memcpy(bba.ip_addr,ip,4);memcpy(bba.netmask,mask,4);memcpy(bba.gateway,gateway,4);memcpy(bba.dns,dns,4);
}
static void register_if(netif_t *n) {
    assert(!(n->flags&NETIF_REGISTERED));n->flags|=NETIF_REGISTERED|NETIF_DETECTED;
    LIST_INSERT_HEAD(&interfaces,n,if_list);
}
struct netif_list *net_get_if_list(void) {return &interfaces;}
int net_unreg_device(netif_t *n) {
    ++fake.unreg;assert(n->flags&NETIF_REGISTERED);LIST_REMOVE(n,if_list);n->flags&=~NETIF_REGISTERED;return 0;
}
int bba_init(void) {++fake.bba_init;if(!fake.bba_present)return -1;register_if(&bba);return 0;}
int la_init(void) {++fake.lan_init;if(!fake.lan_present)return -1;register_if(&lan);return 0;}
int bba_shutdown(void) {++fake.bba_end;bba.flags&=~(NETIF_RUNNING|NETIF_INITIALIZED);return 0;}
int la_shutdown(void) {++fake.lan_end;lan.flags&=~(NETIF_RUNNING|NETIF_INITIALIZED|NETIF_DETECTED);return 0;}
uint16_t g2_read_16(uint32_t address) {
    assert(address==UINT32_C(0xa1001764));++fake.phy_reads;
    return fake.phy_reads==1?0:fake.phy; /* first read may contain a latched loss */
}
static bool cancel(void) {return fake.cancel_after && ++fake.cancel_calls>=fake.cancel_after;}
static bool line(const struct kui_app_status *s,const char *text) {
    for(unsigned i=0;i<s->line_count;++i) if(strstr(s->lines[i],text)) return true;
    return false;
}
int main(void) {
    struct kui_app_status out;
    reset();fake.cancel_after=1;kui_network_app_run(&out,NULL,cancel);
    assert(out.stopped && !out.complete && !fake.bba_init && !fake.lan_init);

    reset();fake.bba_present=true;kui_network_app_run(&out,NULL,cancel);
    assert(out.complete && !out.passed && !out.errors && !out.stopped);
    assert(fake.bba_init==1 && fake.bba_end==1 && !fake.lan_init && fake.unreg==1 && !fake.phy_reads);
    assert(LIST_EMPTY(&interfaces));
    assert(line(&out,"192.168.1.42") && line(&out,"255.255.255.0") && line(&out,"Gateway: 192.168.1.1"));
    assert(line(&out,"DHCP was not requested") && line(&out,"Internet reachability was not tested"));
    assert(line(&out,"Link: not checked"));

    reset();fake.lan_present=true;kui_network_app_run(&out,NULL,cancel);
    assert(out.complete && !out.passed && line(&out,"HIT-0300") && line(&out,"IPv4: not configured"));
    assert(fake.bba_init==1 && fake.bba_end==1 && fake.lan_init==1 && fake.lan_end==1 && fake.unreg==1);
    assert(LIST_EMPTY(&interfaces) && !fake.phy_reads);

    reset();fake.bba_present=true;fake.cancel_after=2;kui_network_app_run(&out,NULL,cancel);
    assert(out.stopped && !out.complete && fake.bba_end==1 && fake.unreg==1 && !fake.lan_init);
    assert(LIST_EMPTY(&interfaces));

    /* An already active interface is inspected without reinitialization or
     * shutdown. Its RUNNING flag alone must not count as a cable-link test. */
    reset();register_if(&bba);bba.flags|=NETIF_INITIALIZED|NETIF_RUNNING;fake.phy=RT_MII_LINK;
    uint32_t before=bba.flags;kui_network_app_run(&out,NULL,cancel);
    assert(out.complete && out.passed && line(&out,"Link: carrier present"));
    assert(!fake.bba_init && !fake.bba_end && !fake.lan_init && !fake.unreg && fake.phy_reads==2);
    assert(bba.flags==before && LIST_FIRST(&interfaces)==&bba);
    fake.phy=0;fake.phy_reads=0;kui_network_app_run(&out,NULL,cancel);
    assert(!out.passed && line(&out,"Link: no carrier"));
    fake.phy=UINT16_MAX;fake.phy_reads=0;kui_network_app_run(&out,NULL,cancel);
    assert(!out.passed && line(&out,"Link: not checked"));

    reset();register_if(&lan);lan.flags|=NETIF_INITIALIZED|NETIF_RUNNING;
    kui_network_app_run(&out,NULL,cancel);
    assert(out.complete && !out.passed && !fake.phy_reads && !fake.bba_init && !fake.lan_init && !fake.unreg);

    reset();register_if(&modem);modem.flags|=NETIF_NOETH;
    kui_network_app_run(&out,NULL,cancel);
    assert(out.complete && !out.passed && strstr(out.message,"No BBA or LAN adapter"));
    assert(line(&out,"stock dial-up modem is not an Ethernet adapter"));
    assert(LIST_FIRST(&interfaces)==&modem && !fake.unreg && !fake.phy_reads);

    struct kui_network_snapshot noneth={.present=true,.ethernet=false,.link=KUI_NETWORK_LINK_UP};
    strcpy(noneth.name,"ppp");kui_network_describe(&out,&noneth);
    assert(!out.passed && line(&out,"not an Ethernet interface"));
    puts("PASS network app: bounded detection, truthful link/config results, cancellation, registration ownership");
    return 0;
}
