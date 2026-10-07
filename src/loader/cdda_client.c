/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_client.h"
#include "kui/cdda_control.h"
struct client_call {const struct cdda_client_exports *api;struct cdda_client_request request;};
static uint32_t client_sp(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_host_client_sp(void);return cdda_host_client_sp();
#else
    uint32_t sp;__asm__ __volatile__("mov r15,%0":"=r"(sp));return sp;
#endif
}
static uint32_t thunk(void *opaque) {
    struct client_call *c=opaque;
    return c->api->call(c->api->context,&c->request);
}
static bool invoke(struct client_call *c,uint32_t op,uint32_t value,uint32_t aux,
                   uint32_t expected,uint32_t *checks) {
    c->request.op=op;c->request.value=value;c->request.aux=aux;
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_host_client_pc(void);c->request.pc=cdda_host_client_pc();
#else
    c->request.pc=(uint32_t)(uintptr_t)&cdda_client_entry;
#endif
    c->request.sp=client_sp();
    if(c->api->probe(thunk,c)) return false;
    ++*checks;return c->request.result==expected;
}
static bool wait_ticks(const struct cdda_client_exports *api,uint32_t duration) {
    uint32_t first=api->clock(api->context);
    for(uint32_t polls=0;polls<2000000u;polls++)
        if(api->clock(api->context)-first>=duration) return true;
    return false;
}
static bool work(struct client_call *c,uint32_t first,uint32_t duration,uint32_t *checks) {
    volatile uint32_t state=0x6b617461u;
    for(uint32_t rounds=0;rounds<200000u;rounds++) {
        for(unsigned i=0;i<128u;i++) {state^=state<<13;state^=state>>17;state^=state<<5;}
        if(!wait_ticks(c->api,62344u) ||
           !invoke(c,CDDA_CLIENT_SERVICE,0,0,KUI_CDDA_SERVICE_OK,checks)) return false;
        if(c->api->clock(c->api->context)-first>=duration) return state!=0;
    }
    return false;
}
uint32_t cdda_client_entry(const void *opaque) {
    const struct cdda_client_exports *api=opaque;
    if(!api || api->magic!=CDDA_CLIENT_MAGIC || api->revision!=1u || api->bytes!=sizeof(*api) ||
       !api->call || !api->clock || !api->probe || !api->epoch) return 1u;
    struct client_call c={.api=api,.request={.epoch=api->epoch}};
    uint32_t checks=0,stales=0,first=api->clock(api->context);
    if(!invoke(&c,CDDA_CLIENT_SERVICE,0,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_REENTRY,0,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_MARK,2,0,KUI_CDDA_SERVICE_OK,&checks)) return 2u;
    if(!work(&c,first,374061600u,&checks) ||
       !invoke(&c,CDDA_CLIENT_MARK,3,0,KUI_CDDA_SERVICE_OK,&checks)) return 3u;
    if(!invoke(&c,CDDA_CLIENT_PAUSE,0,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_STATUS,KUI_CDDA_CONTROL_PAUSED,0,KUI_CDDA_SERVICE_OK,&checks)) return 4u;
    uint32_t paused=c.request.frame;
    /* Keep servicing the stopped clock/owner during one second of silence. */
    uint32_t pause=api->clock(api->context);
    if(!work(&c,pause,KUI_CDDA_TMU_HZ,&checks) ||
       !invoke(&c,CDDA_CLIENT_STATUS,KUI_CDDA_CONTROL_PAUSED,0,KUI_CDDA_SERVICE_OK,&checks) ||
       c.request.frame!=paused || !invoke(&c,CDDA_CLIENT_RESUME,0,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_SEEK,6u*44100u+4410u+12345u,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_STATUS,KUI_CDDA_CONTROL_PLAYING,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_MARK,4,0,KUI_CDDA_SERVICE_OK,&checks)) return 5u;
    if(!work(&c,first,1122184800u,&checks) ||
       !invoke(&c,CDDA_CLIENT_MARK,5,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_ARM_GAP,0,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !wait_ticks(api,2369057u) ||
       !invoke(&c,CDDA_CLIENT_SERVICE,0,0,KUI_CDDA_SERVICE_DEADLINE,&checks)) return 6u;
    uint32_t old=c.request.epoch;
    if(!invoke(&c,CDDA_CLIENT_RESTART,0,0,KUI_CDDA_SERVICE_OK,&checks) || c.request.epoch<=old) return 7u;
    uint32_t current=c.request.epoch;c.request.epoch=old;
    if(!invoke(&c,CDDA_CLIENT_SERVICE,0,0,KUI_CDDA_SERVICE_STALE,&checks)) return 8u;
    stales++;if(!invoke(&c,CDDA_CLIENT_STOP,0,0,KUI_CDDA_SERVICE_STALE,&checks)) return 9u;
    stales++;c.request.epoch=current;
    if(!invoke(&c,CDDA_CLIENT_SERVICE,0,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_STATUS,KUI_CDDA_CONTROL_PLAYING,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_MARK,6,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_STOP,0,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_STATUS,KUI_CDDA_CONTROL_STOPPED,0,KUI_CDDA_SERVICE_OK,&checks) ||
       !invoke(&c,CDDA_CLIENT_REPORT,checks,stales,KUI_CDDA_SERVICE_OK,&checks)) return 10u;
    return 0u;
}
