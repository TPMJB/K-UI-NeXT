/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_batch_client.h"
#include "kui/cdda_bios_batch.h"
#include "kui/cdda_clock.h"
#include <stddef.h>

static const uint32_t sizes[8]={1u,2u,3u,4u,7u,8u,15u,16u};
typedef int32_t (*bios_vector_function)(uint32_t,uint32_t,uint32_t,uint32_t);
struct vector_call {uint32_t r4,r5,r6,r7;int32_t result;};
struct batch_client {
    const struct cdda_batch_client_exports *api;
    uint32_t calls,execs,checks,drives,read_checked,refusals,stales,cancels,resets;
    uint32_t progress_polls,coverage_sector,coverage_class,full_passes;
    uint32_t partial_cancels,partial_resets,boundary_reads,random_reads,random_state,class_counts[8];
    uint32_t params[4],status[4],drive[2],first;
    _Alignas(32) uint8_t buffer[32u+KUI_CDDA_BIOS_BATCH_MAX_BYTES+32u];
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
static bool vector(struct batch_client *client,uint32_t function,uint32_t r4,uint32_t r5,int32_t *result) {
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
static bool expect(struct batch_client *client,uint32_t function,uint32_t r4,uint32_t r5,int32_t expected) {
    int32_t result;return vector(client,function,r4,r5,&result) && result==expected;
}
static bool diagnostic(struct batch_client *client,uint32_t op,uint32_t value,
                       struct cdda_batch_client_diagnostic *out) {
    struct cdda_batch_client_diagnostic request={
        .op=op,.value=value,.vector_calls=client->calls,.execs=client->execs,
        .checks=client->checks,.drive_checks=client->drives,.read_checked=client->read_checked,
        .expected_refusals=client->refusals,.stale_checks=client->stales,.cancels=client->cancels,
        .resets=client->resets,.full_passes=client->full_passes,
        .full_pass_bytes=client->full_passes*KUI_CDDA_BIOS_BATCH_DATA_BYTES,
        .partial_cancels=client->partial_cancels,.partial_resets=client->partial_resets,
        .progress_polls=client->progress_polls,.boundary_reads=client->boundary_reads,
        .random_reads=client->random_reads};
    for(unsigned i=0;i<8u;i++) request.class_counts[i]=client->class_counts[i];
#ifdef CDDA_HARNESS_HOST_TEST
    request.pc=cdda_batch_host_client_pc();request.sp=cdda_batch_host_client_sp();
#else
    request.pc=(uint32_t)(uintptr_t)&cdda_batch_client_entry;
    __asm__ __volatile__("mov r15,%0":"=r"(request.sp));
#endif
    if(client->api->diagnostic(client->api->context,&request) || request.result) return false;
    if(out) *out=request;
    return true;
}
static bool wait_ticks(struct batch_client *client,uint32_t duration) {
    uint32_t first=client->api->clock(client->api->context);
    for(uint32_t polls=0;polls<2000000u;polls++)
        if(client->api->clock(client->api->context)-first>=duration) return true;
    return false;
}
static bool cpu_slice(struct batch_client *client) {
    volatile uint32_t state=0x62617463u;
    for(unsigned i=0;i<128u;i++) {state^=state<<13;state^=state>>17;state^=state<<5;}
    return state && wait_ticks(client,62344u);
}
static bool within_limit(struct batch_client *client) {
    return client->api->clock(client->api->context)-client->first<120u*KUI_CDDA_TMU_HZ;
}
static uint32_t params_address(struct batch_client *client) {
    return guest_address(client->params,sizeof(client->params),false);
}
static bool request(struct batch_client *client,uint32_t command,uint32_t address,uint32_t *handle) {
    int32_t result;
    if(!vector(client,KUI_GD_REQUEST,command,address,&result) || result<=0) return false;
    *handle=(uint32_t)result;return true;
}
static bool check(struct batch_client *client,uint32_t handle,int32_t *result) {
    return vector(client,KUI_GD_CHECK,handle,guest_address(client->status,16u,true),result);
}
static bool unknown(struct batch_client *client,uint32_t handle) {
    int32_t status;
    return check(client,handle,&status) && status==KUI_GD_NOT_FOUND && !client->status[0] &&
        !client->status[1] && !client->status[2] && !client->status[3];
}
static bool drive(struct batch_client *client,uint32_t expected) {
    return expect(client,KUI_GD_DRIVE,guest_address(client->drive,8u,true),0u,0) &&
        client->drive[0]==expected && client->drive[1]==0x80u;
}
static bool control_done(struct batch_client *client,uint32_t handle) {
    int32_t status;
    if(!check(client,handle,&status) || status!=KUI_GD_PROCESSING || client->status[0] ||
       client->status[1] || client->status[2] || client->status[3]!=4u) return false;
    for(unsigned polls=0;polls<2048u;polls++) {
        if(!within_limit(client) || !cpu_slice(client) || !expect(client,KUI_GD_EXEC,0u,0u,0) ||
           !check(client,handle,&status)) return false;
        if(status==KUI_GD_PROCESSING) {
            if(client->status[0] || client->status[1] || client->status[2] || client->status[3]!=4u) return false;
            continue;
        }
        return status==KUI_GD_COMPLETED && !client->status[0] && !client->status[1] &&
            !client->status[2] && !client->status[3];
    }
    return false;
}
static bool control(struct batch_client *client,uint32_t command) {
    uint32_t handle;return request(client,command,0u,&handle) && control_done(client,handle);
}
static bool play(struct batch_client *client) {
    uint32_t handle;client->params[0]=1u;client->params[1]=1u;client->params[2]=15u;
    if(!request(client,KUI_CDDA_BIOS_BATCH_PLAY,params_address(client),&handle)) return false;
    client->params[0]=2u;client->params[1]=2u;client->params[2]=1u;
    return control_done(client,handle);
}
static uint8_t expected_byte(uint32_t at) {
    uint32_t x=at^0x9e3779b9u;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return (uint8_t)x;
}
static void prepare_read(struct batch_client *client,uint32_t sector,uint32_t count) {
    uint32_t bytes=count*2048u;
    for(unsigned i=0;i<sizeof(client->buffer);i++) client->buffer[i]=0xccu;
    for(unsigned i=0;i<32u;i++) {client->buffer[i]=0xa5u;client->buffer[32u+bytes+i]=0x5au;}
    client->params[0]=KUI_CDDA_BIOS_BATCH_DATA_FIRST_FAD+sector;client->params[1]=count;
    client->params[2]=guest_address(client->buffer,sizeof(client->buffer),true)+32u;client->params[3]=0u;
}
static bool verify_buffer(const struct batch_client *client,uint32_t offset,uint32_t bytes,uint32_t done) {
    if(done>bytes) return false;
    for(unsigned i=0;i<32u;i++) if(client->buffer[i]!=0xa5u || client->buffer[32u+bytes+i]!=0x5au) return false;
    for(uint32_t i=0;i<done;i++) if(client->buffer[32u+i]!=expected_byte(offset+i)) return false;
    for(uint32_t i=done;i<bytes;i++) if(client->buffer[32u+i]!=0xccu) return false;
    return true;
}
static bool progress(struct batch_client *client,int32_t status,uint32_t offset,uint32_t bytes,uint32_t *done) {
    uint32_t next=client->status[2];
    if(next<*done || next>bytes || next%2048u || next-*done>2048u ||
       !verify_buffer(client,offset,bytes,next)) return false;
    if(status==KUI_GD_PROCESSING) {
        if(client->status[0] || client->status[1] || client->status[3]!=4u) return false;
        if(next) client->progress_polls++;
    } else if(status==KUI_GD_COMPLETED) {
        if(client->status[0] || client->status[1] || client->status[3] || next!=bytes) return false;
    } else return false;
    uint32_t delta=next-*done;
    if(client->read_checked>UINT32_MAX-delta) return false;
    client->read_checked+=delta;*done=next;return true;
}
static bool begin_read(struct batch_client *client,uint32_t sector,uint32_t count,uint32_t *handle) {
    if(!count || count>16u || sector>4096u-count) return false;
    prepare_read(client,sector,count);
    if(!request(client,KUI_GD_PIOREAD,params_address(client),handle) ||
       !verify_buffer(client,sector*2048u,count*2048u,0u)) return false;
    client->params[0]=0u;client->params[1]=17u;client->params[2]=0u;client->params[3]=1u;
    int32_t status;
    return check(client,*handle,&status) && status==KUI_GD_PROCESSING && !client->status[0] &&
        !client->status[1] && !client->status[2] && client->status[3]==4u;
}
static bool read_batch(struct batch_client *client,uint32_t sector,uint32_t count,bool duplicate) {
    uint32_t handle,done=0,bytes=count*2048u;
    if(!begin_read(client,sector,count,&handle)) return false;
    for(unsigned polls=0;polls<2048u;polls++) {
        int32_t status;
        if(!within_limit(client) || !cpu_slice(client) || !expect(client,KUI_GD_EXEC,0u,0u,0) ||
           !check(client,handle,&status) || !progress(client,status,sector*2048u,bytes,&done)) return false;
        if(status==KUI_GD_PROCESSING) continue;
        for(unsigned i=0;i<8u;i++) if(count==sizes[i]) client->class_counts[i]++;
        return !duplicate || unknown(client,handle);
    }
    return false;
}
static bool partial_read(struct batch_client *client,bool reset) {
    uint32_t handle,done=0,sector=reset?2039u:137u;
    if(!begin_read(client,sector,16u,&handle) || !drive(client,0u)) return false;
    if(!reset && !expect(client,KUI_GD_REQUEST,KUI_GD_STOP,0u,0)) return false;
    for(unsigned polls=0;polls<2048u;polls++) {
        int32_t status;
        if(!within_limit(client) || !cpu_slice(client) || !expect(client,KUI_GD_EXEC,0u,0u,0) ||
           !check(client,handle,&status) || status!=KUI_GD_PROCESSING ||
           !progress(client,status,sector*2048u,32768u,&done)) return false;
        if(!done) continue;
        if(done!=2048u) return false;
        if(reset) {
            if(!expect(client,KUI_GD_RESET,0u,0u,0) || !unknown(client,handle) || !drive(client,2u)) return false;
            client->resets++;client->partial_resets++;
        } else {
            if(!expect(client,KUI_GD_ABORT,handle,0u,0) || !check(client,handle,&status) ||
               status!=KUI_GD_FAILED || client->status[0]!=1u || client->status[1]!=KUI_GD_ERROR_CANCELLED ||
               client->status[2]!=2048u || client->status[3] || !unknown(client,handle) || !drive(client,3u)) return false;
            client->cancels++;client->partial_cancels++;
        }
        return verify_buffer(client,sector*2048u,32768u,2048u);
    }
    return false;
}
static bool negatives(struct batch_client *client) {
    prepare_read(client,0u,1u);client->params[1]=0u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0)) return false;
    client->params[1]=17u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0)) return false;
    client->params[0]=KUI_CDDA_BIOS_BATCH_DATA_END_FAD-1u;client->params[1]=2u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0)) return false;
    client->params[0]=KUI_CDDA_BIOS_BATCH_DATA_FIRST_FAD-1u;client->params[1]=1u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0)) return false;
    client->params[0]=KUI_CDDA_BIOS_BATCH_DATA_FIRST_FAD;client->params[1]=16u;client->params[2]=0x8c31fff0u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0)) return false;
    client->params[2]=UINT32_MAX-1023u;
    return expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0) &&
        verify_buffer(client,0u,2048u,0u);
}
static bool next_read(struct batch_client *client) {
    if(client->coverage_sector<4096u) {
        uint32_t count=sizes[client->coverage_class%8u],remaining=4096u-client->coverage_sector;
        if(count>remaining) count=remaining;
        bool duplicate=!client->coverage_sector;
        if(!read_batch(client,client->coverage_sector,count,duplicate)) return false;
        client->coverage_sector+=count;client->coverage_class++;
        if(client->coverage_sector==4096u) client->full_passes++;
        return true;
    }
    if(client->boundary_reads<24u) {
        uint32_t which=client->boundary_reads/3u,count=sizes[which];
        uint32_t sector=client->boundary_reads%3u?4096u-count:0u;
        if(!read_batch(client,sector,count,false)) return false;
        client->boundary_reads++;return true;
    }
    uint32_t count=sizes[client->random_reads%8u],state=client->random_state;
    state^=state<<13;state^=state>>17;state^=state<<5;client->random_state=state;
    uint32_t sector=state%(4096u-count+1u);
    if(!read_batch(client,sector,count,false)) return false;
    client->random_reads++;return true;
}
static bool work_to(struct batch_client *client,uint32_t duration) {
    for(unsigned rounds=0;rounds<50000u;rounds++) {
        if(!within_limit(client) || !next_read(client)) return false;
        if(client->api->clock(client->api->context)-client->first>=duration) return true;
    }
    return false;
}
static bool pause_work(struct batch_client *client) {
    uint32_t first=client->api->clock(client->api->context);
    for(unsigned rounds=0;rounds<1000u;rounds++) {
        if(!cpu_slice(client) || !expect(client,KUI_GD_EXEC,0u,0u,0)) return false;
        if(client->api->clock(client->api->context)-first>=KUI_CDDA_TMU_HZ) return true;
    }
    return false;
}
uint32_t cdda_batch_client_entry(const void *opaque) {
    const struct cdda_batch_client_exports *api=opaque;
    if(!api || api->magic!=CDDA_BATCH_CLIENT_MAGIC || api->revision!=CDDA_BATCH_CLIENT_REVISION ||
       api->bytes!=sizeof(*api) || !api->clock || !api->probe || !api->diagnostic) return 1u;
    struct batch_client client={.api=api,.random_state=0x47524439u};
    client.first=api->clock(api->context);
    if(!negatives(&client) || !play(&client) || !drive(&client,3u) || !partial_read(&client,false) ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_ARM_REENTRY,0u,NULL) || !cpu_slice(&client) ||
       !expect(&client,KUI_GD_EXEC,0u,0u,0) || !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,2u,NULL)) return 2u;
    if(!work_to(&client,30u*KUI_CDDA_TMU_HZ) || !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,3u,NULL)) return 3u;
    struct cdda_batch_client_diagnostic paused,after;
    if(!control(&client,KUI_CDDA_BIOS_BATCH_PAUSE) || !drive(&client,1u) ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_SNAPSHOT,0u,&paused) || paused.state!=KUI_CDDA_CONTROL_PAUSED ||
       paused.frame==paused.prefetch || !pause_work(&client) || !drive(&client,1u) ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_SNAPSHOT,0u,&after) || after.state!=KUI_CDDA_CONTROL_PAUSED ||
       after.frame!=paused.frame || !control(&client,KUI_CDDA_BIOS_BATCH_RELEASE) || !drive(&client,3u) ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,4u,NULL)) return 4u;
    if(!work_to(&client,60u*KUI_CDDA_TMU_HZ) || !control(&client,KUI_GD_STOP) || !drive(&client,2u) ||
       !partial_read(&client,true) || !play(&client) || !drive(&client,3u) ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,5u,NULL)) return 5u;
    if(!work_to(&client,90u*KUI_CDDA_TMU_HZ)) return 6u;
    for(unsigned rounds=0;rounds<50000u && (client.full_passes!=1u || client.boundary_reads!=24u || client.random_reads<8u);rounds++)
        if(!within_limit(&client) || !next_read(&client)) return 6u;
    if(client.full_passes!=1u || client.coverage_sector!=4096u || client.boundary_reads!=24u ||
       client.random_reads<8u || !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,6u,NULL)) return 6u;
    for(unsigned i=0;i<8u;i++) if(client.class_counts[i]<16u) return 7u;
    if(!control(&client,KUI_GD_STOP) || !drive(&client,2u) || client.refusals!=8u || client.stales!=3u ||
       client.cancels!=1u || client.resets!=1u || client.partial_cancels!=1u || client.partial_resets!=1u ||
       client.drives!=11u || client.progress_polls<32u ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_REPORT,client.calls,NULL) ||
       !diagnostic(&client,CDDA_BATCH_CLIENT_MARK,7u,NULL)) return 7u;
    return 0u;
}
