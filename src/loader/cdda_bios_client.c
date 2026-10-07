/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_bios_client.h"
#include "kui/cdda_bios.h"
#include "kui/cdda_clock.h"
#include <stddef.h>

typedef int32_t (*bios_vector_function)(uint32_t,uint32_t,uint32_t,uint32_t);
struct vector_call {uint32_t r4,r5,r6,r7;int32_t result;};
struct bios_client {
    const struct cdda_bios_client_exports *api;
    uint32_t calls,execs,checks,drives,read_checked,refusals,stales,cancels,resets,read_sequence;
    uint32_t params[4],status[4],drive[2];
    _Alignas(32) uint8_t buffer[32u+2048u+32u];
};
static uint32_t guest_address(const void *pointer,uint32_t bytes,bool writing) {
#ifdef CDDA_HARNESS_HOST_TEST
    return cdda_bios_host_address(pointer,bytes,writing);
#else
    (void)bytes;(void)writing;return (uint32_t)(uintptr_t)pointer;
#endif
}
static uint32_t vector_thunk(void *opaque) {
    struct vector_call *call=opaque;
#ifdef CDDA_HARNESS_HOST_TEST
    call->result=cdda_bios_host_vector_call(call->r4,call->r5,call->r6,call->r7);
#else
    bios_vector_function vector=*(bios_vector_function volatile *)(uintptr_t)KUI_GD_VECTOR_ADDRESS;
    if(!vector) {call->result=-1;return UINT32_MAX;}
    call->result=vector(call->r4,call->r5,call->r6,call->r7);
#endif
    return (uint32_t)call->result;
}
static bool vector(struct bios_client *client,uint32_t function,uint32_t r4,uint32_t r5,int32_t *result) {
    struct vector_call call={r4,r5,0u,function,0};
    if(client->api->probe(vector_thunk,&call)) return false;
    client->calls++;
    if(function==KUI_GD_EXEC) client->execs++;
    if(function==KUI_GD_CHECK) client->checks++;
    if(function==KUI_GD_DRIVE) client->drives++;
    if((function==KUI_GD_REQUEST && !call.result) ||
       (function!=KUI_GD_REQUEST && call.result==-1)) client->refusals++;
    if(function==KUI_GD_CHECK && call.result==KUI_GD_NOT_FOUND) client->stales++;
    *result=call.result;return true;
}
static bool expect(struct bios_client *client,uint32_t function,uint32_t r4,uint32_t r5,int32_t expected) {
    int32_t result;
    return vector(client,function,r4,r5,&result) && result==expected;
}
static bool diagnostic(struct bios_client *client,uint32_t op,uint32_t value,
                       struct cdda_bios_client_diagnostic *out) {
    struct cdda_bios_client_diagnostic request={
        .op=op,.value=value,.vector_calls=client->calls,.execs=client->execs,
        .checks=client->checks,.drive_checks=client->drives,.read_checked=client->read_checked,
        .expected_refusals=client->refusals,.stale_checks=client->stales,
        .cancels=client->cancels,.resets=client->resets};
#ifdef CDDA_HARNESS_HOST_TEST
    request.pc=cdda_bios_host_client_pc();request.sp=cdda_bios_host_client_sp();
#else
    request.pc=(uint32_t)(uintptr_t)&cdda_bios_client_entry;
    __asm__ __volatile__("mov r15,%0":"=r"(request.sp));
#endif
    if(client->api->diagnostic(client->api->context,&request) || request.result) return false;
    if(out) *out=request;
    return true;
}
static bool wait_ticks(struct bios_client *client,uint32_t duration) {
    uint32_t first=client->api->clock(client->api->context);
    for(uint32_t polls=0;polls<2000000u;polls++)
        if(client->api->clock(client->api->context)-first>=duration) return true;
    return false;
}
static bool cpu_slice(struct bios_client *client) {
    volatile uint32_t state=0x62696f73u;
    for(unsigned i=0;i<128u;i++) {state^=state<<13;state^=state>>17;state^=state<<5;}
    return state && wait_ticks(client,62344u); /* ceil5ms at documented TMU reference. */
}
static uint32_t params_address(struct bios_client *client,uint32_t bytes) {
    if(bytes>sizeof(client->params)) return 0u;
    return guest_address(client->params,sizeof(client->params),false);
}
static bool request(struct bios_client *client,uint32_t command,uint32_t address,uint32_t *handle) {
    int32_t result;
    if(!vector(client,KUI_GD_REQUEST,command,address,&result) || result<=0) return false;
    *handle=(uint32_t)result;return true;
}
static bool check(struct bios_client *client,uint32_t handle,int32_t *result) {
    return vector(client,KUI_GD_CHECK,handle,guest_address(client->status,16u,true),result);
}
static bool completed(struct bios_client *client,uint32_t handle,uint32_t bytes) {
    int32_t status;
    if(!check(client,handle,&status) || status!=KUI_GD_PROCESSING || client->status[0] ||
       client->status[1] || client->status[2] || client->status[3]!=4u) return false;
    for(unsigned polls=0;polls<2048u;polls++) {
        if(!cpu_slice(client) || !expect(client,KUI_GD_EXEC,0u,0u,0) || !check(client,handle,&status)) return false;
        if(status==KUI_GD_PROCESSING) {
            if(client->status[0] || client->status[1] || client->status[2] || client->status[3]!=4u) return false;
            continue;
        }
        return status==KUI_GD_COMPLETED && !client->status[0] && !client->status[1] &&
            client->status[2]==bytes && !client->status[3];
    }
    return false;
}
static bool unknown(struct bios_client *client,uint32_t handle) {
    int32_t status;
    return check(client,handle,&status) && status==KUI_GD_NOT_FOUND && !client->status[0] &&
        !client->status[1] && !client->status[2] && !client->status[3];
}
static bool drive(struct bios_client *client,uint32_t expected) {
    return expect(client,KUI_GD_DRIVE,guest_address(client->drive,8u,true),0u,0) &&
        client->drive[0]==expected && client->drive[1]==0x80u;
}
static bool control(struct bios_client *client,uint32_t command) {
    uint32_t handle;
    return request(client,command,0u,&handle) && completed(client,handle,0u);
}
static bool play(struct bios_client *client,uint32_t repeat) {
    uint32_t handle;
    client->params[0]=1u;client->params[1]=1u;client->params[2]=repeat;
    if(!request(client,KUI_CDDA_BIOS_PLAY,params_address(client,12u),&handle)) return false;
    /* REQUEST must have copied the authorized values. EXEC must not reread
     * this deliberately changed client parameter buffer. */
    client->params[0]=2u;client->params[1]=2u;client->params[2]=1u;
    return completed(client,handle,0u);
}
static uint8_t expected_byte(uint32_t at) {
    uint32_t x=at^0x9e3779b9u;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return (uint8_t)x;
}
static void prepare_read(struct bios_client *client,uint32_t *offset) {
    for(unsigned i=0;i<32u;i++) {client->buffer[i]=0xa5u;client->buffer[2080u+i]=0x5au;}
    for(unsigned i=0;i<2048u;i++) client->buffer[32u+i]=0xccu;
    uint32_t sector=(client->read_sequence*37u+17u)%4096u;
    client->read_sequence++;*offset=sector*2048u;
    client->params[0]=KUI_CDDA_BIOS_DATA_FIRST_FAD+sector;client->params[1]=1u;
    client->params[2]=guest_address(client->buffer,sizeof(client->buffer),true)+32u;
    client->params[3]=0u;
}
static bool untouched(const struct bios_client *client) {
    for(unsigned i=0;i<32u;i++) if(client->buffer[i]!=0xa5u || client->buffer[2080u+i]!=0x5au) return false;
    for(unsigned i=0;i<2048u;i++) if(client->buffer[32u+i]!=0xccu) return false;
    return true;
}
static bool read_sector(struct bios_client *client,bool duplicate,bool busy_refusal) {
    uint32_t handle,offset;prepare_read(client,&offset);
    if(!request(client,KUI_GD_PIOREAD,params_address(client,16u),&handle) || !untouched(client)) return false;
    client->params[0]=0u;client->params[1]=2u;client->params[2]=0u;client->params[3]=1u;
    if(busy_refusal && (!expect(client,KUI_GD_REQUEST,KUI_GD_STOP,0u,0) || !drive(client,0u) ||
                       !untouched(client))) return false;
    if(!completed(client,handle,2048u)) return false;
    for(unsigned i=0;i<32u;i++) if(client->buffer[i]!=0xa5u || client->buffer[2080u+i]!=0x5au) return false;
    for(unsigned i=0;i<2048u;i++) if(client->buffer[32u+i]!=expected_byte(offset+i)) return false;
    if(client->read_checked>UINT32_MAX-2048u) return false;
    client->read_checked+=2048u;
    return !duplicate || unknown(client,handle);
}
static bool canceled_read(struct bios_client *client) {
    uint32_t handle,offset;prepare_read(client,&offset);(void)offset;
    int32_t status;
    if(!request(client,KUI_GD_PIOREAD,params_address(client,16u),&handle) ||
       !check(client,handle,&status) || status!=KUI_GD_PROCESSING || client->status[0] ||
       client->status[1] || client->status[2] || client->status[3]!=4u ||
       !expect(client,KUI_GD_ABORT,handle,0u,0) || !untouched(client) ||
       !check(client,handle,&status) || status!=KUI_GD_FAILED || client->status[0]!=1u ||
       client->status[1]!=KUI_GD_ERROR_CANCELLED || client->status[2] || client->status[3] ||
       !unknown(client,handle) || !drive(client,2u)) return false;
    client->cancels++;return true;
}
static bool reset_read(struct bios_client *client) {
    uint32_t handle,offset;prepare_read(client,&offset);(void)offset;
    if(!request(client,KUI_GD_PIOREAD,params_address(client,16u),&handle) || !drive(client,0u) ||
       !expect(client,KUI_GD_RESET,0u,0u,0) || !untouched(client) ||
       !unknown(client,handle) || !drive(client,2u)) return false;
    client->resets++;return true;
}
static bool negatives(struct bios_client *client) {
    if(!expect(client,KUI_GD_REQUEST,KUI_CDDA_BIOS_PLAY,0u,0)) return false;
    client->params[0]=2u;client->params[1]=2u;client->params[2]=15u;
    if(!expect(client,KUI_GD_REQUEST,KUI_CDDA_BIOS_PLAY,params_address(client,12u),0)) return false;
    client->params[0]=1u;client->params[1]=1u;client->params[2]=1u;
    if(!expect(client,KUI_GD_REQUEST,KUI_CDDA_BIOS_PLAY,params_address(client,12u),0)) return false;
    client->params[1]=2u;client->params[2]=15u;
    if(!expect(client,KUI_GD_REQUEST,KUI_CDDA_BIOS_PLAY,params_address(client,12u),0)) return false;
    uint32_t offset;prepare_read(client,&offset);(void)offset;
    client->params[1]=2u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client,16u),0)) return false;
    client->params[1]=1u;client->params[0]=KUI_CDDA_BIOS_AUDIO_FIRST_FAD;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client,16u),0)) return false;
    client->params[0]=KUI_CDDA_BIOS_DATA_FIRST_FAD;client->params[2]=0x8c000000u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client,16u),0)) return false;
    client->params[2]=guest_address(client->buffer,sizeof(client->buffer),true)+32u;client->params[3]=1u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client,16u),0) ||
       !expect(client,KUI_GD_REQUEST,21u,0u,0)) return false;
    client->params[0]=0u;client->params[1]=0x1000u;client->params[2]=0u;client->params[3]=2352u;
    return expect(client,KUI_GD_DATATYPE,params_address(client,16u),0u,-1) && untouched(client);
}
static bool work(struct bios_client *client,uint32_t first,uint32_t duration,bool reading) {
    for(unsigned rounds=0;rounds<50000u;rounds++) {
        if(reading) {
            if(!read_sector(client,false,false)) return false;
        } else if(!cpu_slice(client) || !expect(client,KUI_GD_EXEC,0u,0u,0)) return false;
        if(client->api->clock(client->api->context)-first>=duration) return true;
    }
    return false;
}
uint32_t cdda_bios_client_entry(const void *opaque) {
    const struct cdda_bios_client_exports *api=opaque;
    if(!api || api->magic!=CDDA_BIOS_CLIENT_MAGIC || api->revision!=CDDA_BIOS_CLIENT_REVISION ||
       api->bytes!=sizeof(*api) || !api->clock || !api->probe || !api->diagnostic) return 1u;
    struct bios_client client={.api=api};
    uint32_t first=api->clock(api->context);
    if(!negatives(&client) || !control(&client,KUI_GD_NOP) ||
       !read_sector(&client,true,true) || !canceled_read(&client) ||
       !diagnostic(&client,CDDA_BIOS_CLIENT_MARK,2u,NULL)) return 2u;
    if(!play(&client,0u)) return 3u;
    uint32_t one_shot=api->clock(api->context);
    if(!work(&client,one_shot,KUI_CDDA_TMU_HZ,true) || !drive(&client,1u) ||
       !diagnostic(&client,CDDA_BIOS_CLIENT_MARK,3u,NULL)) return 3u;
    if(!play(&client,15u) || !drive(&client,3u) || !expect(&client,KUI_GD_RESET,0u,0u,-1) ||
       !drive(&client,3u) || !diagnostic(&client,CDDA_BIOS_CLIENT_ARM_REENTRY,0u,NULL) ||
       !cpu_slice(&client) || !expect(&client,KUI_GD_EXEC,0u,0u,0) ||
       !work(&client,first,20u*KUI_CDDA_TMU_HZ,true) ||
       !diagnostic(&client,CDDA_BIOS_CLIENT_MARK,4u,NULL)) return 4u;
    struct cdda_bios_client_diagnostic paused,after;
    if(!control(&client,KUI_CDDA_BIOS_PAUSE) || !drive(&client,1u) ||
       !expect(&client,KUI_GD_RESET,0u,0u,-1) || !drive(&client,1u) ||
       !diagnostic(&client,CDDA_BIOS_CLIENT_SNAPSHOT,0u,&paused) ||
       paused.state!=KUI_CDDA_CONTROL_PAUSED || paused.frame==paused.prefetch) return 5u;
    uint32_t pause=api->clock(api->context);
    if(!work(&client,pause,KUI_CDDA_TMU_HZ,false) || !drive(&client,1u) ||
       !diagnostic(&client,CDDA_BIOS_CLIENT_SNAPSHOT,0u,&after) ||
       after.state!=KUI_CDDA_CONTROL_PAUSED || after.frame!=paused.frame ||
       !control(&client,KUI_CDDA_BIOS_RELEASE) || !drive(&client,3u) ||
       !diagnostic(&client,CDDA_BIOS_CLIENT_MARK,5u,NULL)) return 5u;
    if(!work(&client,first,40u*KUI_CDDA_TMU_HZ,true) || !control(&client,KUI_GD_STOP) ||
       !drive(&client,2u) || !reset_read(&client) || !play(&client,15u) || !drive(&client,3u) ||
       !diagnostic(&client,CDDA_BIOS_CLIENT_MARK,6u,NULL)) return 6u;
    if(!work(&client,first,60u*KUI_CDDA_TMU_HZ,true) || !control(&client,KUI_GD_STOP) || !drive(&client,2u) ||
       client.read_checked<65536u || client.refusals!=14u || client.stales!=3u ||
       client.cancels!=1u || client.resets!=1u || client.drives!=14u ||
       !diagnostic(&client,CDDA_BIOS_CLIENT_REPORT,client.calls,NULL) ||
       !diagnostic(&client,CDDA_BIOS_CLIENT_MARK,7u,NULL)) return 7u;
    return 0u;
}
