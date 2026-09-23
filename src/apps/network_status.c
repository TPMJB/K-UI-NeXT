/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/network_test.h"
#include <stdio.h>
#include <string.h>

static void address(char *out,const char *name,const uint8_t ip[4]) {
    if(!(ip[0]|ip[1]|ip[2]|ip[3])) snprintf(out,KUI_APP_LINE_CAP,"%s: not configured",name);
    else snprintf(out,KUI_APP_LINE_CAP,"%s: %u.%u.%u.%u",name,
        (unsigned)ip[0],(unsigned)ip[1],(unsigned)ip[2],(unsigned)ip[3]);
}
void kui_network_describe(struct kui_app_status *out,const struct kui_network_snapshot *s) {
    if(!out) return;
    memset(out,0,sizeof(*out));out->complete=true;out->done=out->total=1;
    if(!s || !s->present) {
        snprintf(out->message,sizeof(out->message),"No BBA or LAN adapter detected");
        out->line_count=4;
        snprintf(out->lines[0],KUI_APP_LINE_CAP,"Upstream KOS BBA/LAN detection completed");
        snprintf(out->lines[1],KUI_APP_LINE_CAP,"A stock dial-up modem is not an Ethernet adapter");
        snprintf(out->lines[2],KUI_APP_LINE_CAP,"Link and IP configuration unavailable");
        snprintf(out->lines[3],KUI_APP_LINE_CAP,"DHCP and Internet reachability were not tested");
        return;
    }
    snprintf(out->message,sizeof(out->message),"%s",s->ethernet?
        "Adapter inspection complete":"Non-Ethernet interface found");
    /* 'passed' means confirmed carrier on a detected Ethernet interface only.
     * Keep it false when this bounded inspection cannot test the link. */
    out->passed=s->ethernet && s->link==KUI_NETWORK_LINK_UP;
    out->line_count=11;
    snprintf(out->lines[0],KUI_APP_LINE_CAP,"Adapter: %.63s",s->description[0]?s->description:s->name);
    snprintf(out->lines[1],KUI_APP_LINE_CAP,"Interface %.23s: %s; %s",s->name,
        s->initialized?"initialized":"detected only",s->running?"driver started":"driver not started");
    snprintf(out->lines[2],KUI_APP_LINE_CAP,"Link: %s",!s->ethernet?"not an Ethernet interface":
        s->link==KUI_NETWORK_LINK_UP?"carrier present":
        s->link==KUI_NETWORK_LINK_DOWN?"no carrier":"not checked (no active PHY query)");
    address(out->lines[3],"IPv4",s->ip);address(out->lines[4],"Netmask",s->netmask);
    address(out->lines[5],"Gateway",s->gateway);address(out->lines[6],"DNS",s->dns);
    snprintf(out->lines[7],KUI_APP_LINE_CAP,"Addresses are KOS saved/current configuration");
    snprintf(out->lines[8],KUI_APP_LINE_CAP,"No configuration files or flash settings were changed");
    snprintf(out->lines[9],KUI_APP_LINE_CAP,"DHCP was not requested by this test");
    snprintf(out->lines[10],KUI_APP_LINE_CAP,"Internet reachability was not tested");
}
