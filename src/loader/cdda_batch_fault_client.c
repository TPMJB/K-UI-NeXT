/* SPDX-License-Identifier: GPL-3.0-only */
/* Controlled fault companion: one confirmed READ prefix, one intentional
 * service omission and one terminal acknowledgment. No audio action bypasses
 * the owned BIOS vector. This is a separate client from profile09. */
#include "cdda_batch_client.h"
#include "kui/cdda_bios_batch.h"
#include "kui/cdda_clock.h"
#include <stddef.h>

#define FAULT_SECTOR 137u
#define FAULT_READ_BYTES 32768u
typedef int32_t (*bios_vector_function)(uint32_t,uint32_t,uint32_t,uint32_t);
struct vector_call {uint32_t r4,r5,r6,r7;int32_t result;};
struct fault_client {
    const struct cdda_batch_client_exports *api;
    uint32_t calls,execs,checks,drives,refusals,stales,progress_polls,read_checked;
    uint32_t params[4],status[4],drive[2];
    _Alignas(32) uint8_t buffer[32u+FAULT_READ_BYTES+32u];
};
static uint32_t guest_address(const void *pointer,uint32_t bytes,bool writing) {
#ifdef CDDA_HARNESS_HOST_TEST
    return cdda_batch_host_address(pointer,bytes,writing);
#else
    (void)bytes;(void)writing;return (uint32_t)(uintptr_t)pointer;
#endif
}
static uint32_t vector_thunk(void *opaque) {
    struct vector_call *call=opaque;
#ifdef CDDA_HARNESS_HOST_TEST
    call->result=cdda_batch_host_vector_call(call->r4,call->r5,call->r6,call->r7);
#else
    bios_vector_function vector=*(bios_vector_function volatile *)(uintptr_t)KUI_GD_VECTOR_ADDRESS;
    if(!vector) {call->result=-1;return UINT32_MAX;}
    call->result=vector(call->r4,call->r5,call->r6,call->r7);
#endif
    return (uint32_t)call->result;
}
static bool vector(struct fault_client *client,uint32_t function,uint32_t r4,uint32_t r5,int32_t *result) {
    struct vector_call call={r4,r5,0u,function,0};
    if(client->api->probe(vector_thunk,&call)) return false;
    client->calls++;
    if(function==KUI_GD_EXEC) client->execs++;
    if(function==KUI_GD_CHECK) client->checks++;
    if(function==KUI_GD_DRIVE) client->drives++;
    /* Keep the earlier clients' diagnostic convention: a failed terminal
     * CHECK is counted alongside refused EXEC, ABORT and REQUEST calls. */
    if((function==KUI_GD_REQUEST && !call.result) ||
       (function!=KUI_GD_REQUEST && call.result==-1)) client->refusals++;
    if(function==KUI_GD_CHECK && call.result==KUI_GD_NOT_FOUND) client->stales++;
    *result=call.result;return true;
}
static bool expect(struct fault_client *client,uint32_t function,uint32_t r4,uint32_t r5,int32_t expected) {
    int32_t result;return vector(client,function,r4,r5,&result) && result==expected;
}
static bool diagnostic(struct fault_client *client,uint32_t op,uint32_t value) {
    struct cdda_batch_client_diagnostic request={
        .op=op,.value=value,.vector_calls=client->calls,.execs=client->execs,.checks=client->checks,
        .drive_checks=client->drives,.read_checked=client->read_checked,.expected_refusals=client->refusals,
        .stale_checks=client->stales,.progress_polls=client->progress_polls};
#ifdef CDDA_HARNESS_HOST_TEST
    request.pc=cdda_batch_host_client_pc();request.sp=cdda_batch_host_client_sp();
#else
    request.pc=(uint32_t)(uintptr_t)&cdda_batch_client_entry;
    __asm__ __volatile__("mov r15,%0":"=r"(request.sp));
#endif
    return !client->api->diagnostic(client->api->context,&request) && !request.result;
}
static bool wait_ticks(struct fault_client *client,uint32_t duration) {
    uint32_t first=client->api->clock(client->api->context);
    for(uint32_t polls=0;polls<2000000u;polls++)
        if(client->api->clock(client->api->context)-first>=duration) return true;
    return false;
}
static bool cpu_slice(struct fault_client *client) {
    volatile uint32_t state=0x6661756cu;
    for(unsigned i=0;i<128u;i++) {state^=state<<13;state^=state>>17;state^=state<<5;}
    return state && wait_ticks(client,62344u);
}
static uint32_t params_address(struct fault_client *client) {
    return guest_address(client->params,sizeof(client->params),false);
}
static bool request(struct fault_client *client,uint32_t command,uint32_t address,uint32_t *handle) {
    int32_t result;
    if(!vector(client,KUI_GD_REQUEST,command,address,&result) || result<=0) return false;
    *handle=(uint32_t)result;return true;
}
static bool check(struct fault_client *client,uint32_t handle,int32_t *result) {
    return vector(client,KUI_GD_CHECK,handle,guest_address(client->status,16u,true),result);
}
static bool processing(struct fault_client *client,uint32_t handle,uint32_t bytes) {
    int32_t result;
    if(!check(client,handle,&result) || result!=KUI_GD_PROCESSING || client->status[0] ||
       client->status[1] || client->status[2]!=bytes || client->status[3]!=4u) return false;
    if(bytes) client->progress_polls++;
    return true;
}
static bool drive(struct fault_client *client,uint32_t expected) {
    return expect(client,KUI_GD_DRIVE,guest_address(client->drive,8u,true),0u,0) &&
        client->drive[0]==expected && client->drive[1]==0x80u;
}
static bool play(struct fault_client *client) {
    uint32_t handle;client->params[0]=1u;client->params[1]=1u;client->params[2]=15u;
    if(!request(client,KUI_CDDA_BIOS_BATCH_PLAY,params_address(client),&handle)) return false;
    client->params[0]=2u;client->params[1]=2u;client->params[2]=1u;
    if(!processing(client,handle,0u) || !cpu_slice(client) || !expect(client,KUI_GD_EXEC,0u,0u,0)) return false;
    int32_t result;
    return check(client,handle,&result) && result==KUI_GD_COMPLETED && !client->status[0] &&
        !client->status[1] && !client->status[2] && !client->status[3] && drive(client,3u);
}
static uint8_t expected_byte(uint32_t at) {
    uint32_t x=at^0x9e3779b9u;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return (uint8_t)x;
}
static void prepare_read(struct fault_client *client) {
    for(unsigned i=0;i<sizeof(client->buffer);i++) client->buffer[i]=0xccu;
    for(unsigned i=0;i<32u;i++) {client->buffer[i]=0xa5u;client->buffer[32u+FAULT_READ_BYTES+i]=0x5au;}
    client->params[0]=KUI_CDDA_BIOS_BATCH_DATA_FIRST_FAD+FAULT_SECTOR;client->params[1]=16u;
    client->params[2]=guest_address(client->buffer,sizeof(client->buffer),true)+32u;client->params[3]=0u;
}
static bool verify_buffer(const struct fault_client *client,uint32_t done) {
    if(done>FAULT_READ_BYTES) return false;
    for(unsigned i=0;i<32u;i++) if(client->buffer[i]!=0xa5u || client->buffer[32u+FAULT_READ_BYTES+i]!=0x5au) return false;
    for(uint32_t i=0;i<done;i++) if(client->buffer[32u+i]!=expected_byte(FAULT_SECTOR*2048u+i)) return false;
    for(uint32_t i=done;i<FAULT_READ_BYTES;i++) if(client->buffer[32u+i]!=0xccu) return false;
    return true;
}
uint32_t cdda_batch_client_entry(const void *opaque) {
    const struct cdda_batch_client_exports *api=opaque;
    if(!api || api->magic!=CDDA_BATCH_CLIENT_MAGIC || api->revision!=CDDA_BATCH_CLIENT_REVISION ||
       api->bytes!=sizeof(*api) || !api->clock || !api->probe || !api->diagnostic) return 1u;
    struct fault_client client={.api=api};
    uint32_t handle;
    if(!play(&client)) return 2u;
    prepare_read(&client);
    if(!request(&client,KUI_GD_PIOREAD,params_address(&client),&handle) || !verify_buffer(&client,0u)) return 2u;
    client.params[0]=0u;client.params[1]=17u;client.params[2]=0u;client.params[3]=1u;
    if(!processing(&client,handle,0u) || !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,2u)) return 2u;
    if(!diagnostic(&client,CDDA_BATCH_CLIENT_ARM_REENTRY,0u) || !cpu_slice(&client) ||
       !expect(&client,KUI_GD_EXEC,0u,0u,0) || !processing(&client,handle,2048u) ||
       !verify_buffer(&client,2048u) || !processing(&client,handle,2048u) || !verify_buffer(&client,2048u)) return 3u;
    client.read_checked=2048u;
    if(!diagnostic(&client,CDDA_BATCH_CLIENT_MARK,3u) || !diagnostic(&client,CDDA_BATCH_CLIENT_ARM_GAP,handle) ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,4u) || !wait_ticks(&client,KUI_CDDA_TMU_HZ/5u)) return 4u;
    int32_t result;
    if(!expect(&client,KUI_GD_EXEC,0u,0u,-1) || !check(&client,handle,&result) || result!=KUI_GD_FAILED ||
       client.status[0]!=1u || client.status[1]!=KUI_GD_ERROR_IO || client.status[2]!=2048u || client.status[3] ||
       !verify_buffer(&client,2048u) || !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,5u)) return 5u;
    if(!check(&client,handle,&result) || result!=KUI_GD_NOT_FOUND || client.status[0] || client.status[1] ||
       client.status[2] || client.status[3]) return 6u;
    client.params[0]=KUI_CDDA_BIOS_BATCH_DATA_FIRST_FAD+FAULT_SECTOR;client.params[1]=17u;
    client.params[2]=guest_address(client.buffer,sizeof(client.buffer),true)+32u;client.params[3]=0u;
    if(!expect(&client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(&client),0) ||
       !expect(&client,KUI_GD_ABORT,handle,0u,-1) || !drive(&client,9u) || !verify_buffer(&client,2048u) ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,6u)) return 6u;
    if(client.calls!=16u || client.execs!=3u || client.checks!=7u || client.drives!=2u ||
       client.refusals!=4u || client.stales!=1u || client.progress_polls!=2u ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_REPORT,client.calls) ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,7u)) return 7u;
    return 0u;
}
