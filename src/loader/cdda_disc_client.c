/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_disc_client.h"
#include "kui/cdda_disc_bios.h"
#include "kui/cdda_clock.h"
#include <stddef.h>

typedef int32_t (*bios_vector_function)(uint32_t,uint32_t,uint32_t,uint32_t);
struct vector_call {uint32_t r4,r5,r6,r7;int32_t result;};
struct disc_client {
    const struct cdda_disc_client_exports *api;
    uint32_t calls,execs,checks,drives,read_checked,refusals,stales,polls,actions;
    uint32_t toc_checks,map_checks,eof_checks,track_switches,last_track,last_epoch;
    uint32_t params[4],status[4],drive[2],first,limit;
    _Alignas(32) uint8_t buffer[32u+32768u+32u];
    _Alignas(32) uint8_t toc[32u+408u+32u];
};
static uint32_t guest_address(const void *pointer,uint32_t bytes,bool writing) {
#ifdef CDDA_HARNESS_HOST_TEST
    return cdda_disc_host_address(pointer,bytes,writing);
#else
    (void)bytes;(void)writing;return (uint32_t)(uintptr_t)pointer;
#endif
}
static uint32_t vector_thunk(void *opaque) {
    struct vector_call *call=opaque;
#ifdef CDDA_HARNESS_HOST_TEST
    call->result=cdda_disc_host_vector_call(call->r4,call->r5,call->r6,call->r7);
#else
    bios_vector_function vector=*(bios_vector_function volatile *)(uintptr_t)KUI_GD_VECTOR_ADDRESS;
    if(!vector) {call->result=-1;return UINT32_MAX;}
    call->result=vector(call->r4,call->r5,call->r6,call->r7);
#endif
    return (uint32_t)call->result;
}
static bool vector(struct disc_client *client,uint32_t function,uint32_t r4,uint32_t r5,int32_t *result) {
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
static bool expect(struct disc_client *client,uint32_t function,uint32_t r4,uint32_t r5,int32_t expected) {
    int32_t result;return vector(client,function,r4,r5,&result) && result==expected;
}
static bool diagnostic(struct disc_client *client,uint32_t op,uint32_t value,uint32_t aux,
                       struct cdda_disc_client_diagnostic *out) {
    struct cdda_disc_client_diagnostic request={
        .op=op,.value=value,.aux=aux,.vector_calls=client->calls,.execs=client->execs,
        .checks=client->checks,.drive_checks=client->drives,.read_checked=client->read_checked,
        .expected_refusals=client->refusals,.stale_checks=client->stales,.polls=client->polls,
        .actions=client->actions,.toc_checks=client->toc_checks,.map_checks=client->map_checks,
        .eof_checks=client->eof_checks,.track_switches=client->track_switches};
#ifdef CDDA_HARNESS_HOST_TEST
    request.pc=cdda_disc_host_client_pc();request.sp=cdda_disc_host_client_sp();
#else
    request.pc=(uint32_t)(uintptr_t)&cdda_disc_client_entry;
    __asm__ __volatile__("mov r15,%0":"=r"(request.sp));
#endif
    if(client->api->diagnostic(client->api->context,&request) || request.result) return false;
    if(out) *out=request;
    return true;
}
static bool wait_ticks(struct disc_client *client,uint32_t duration) {
    uint32_t first=client->api->clock(client->api->context);
    for(uint32_t polls=0;polls<2000000u;polls++)
        if(client->api->clock(client->api->context)-first>=duration) return true;
    return false;
}
static bool within_limit(struct disc_client *client) {
    return client->api->clock(client->api->context)-client->first<client->limit;
}
static bool service(struct disc_client *client) {
    volatile uint32_t state=0x64697363u;
    for(unsigned i=0;i<128u;i++) {state^=state<<13;state^=state>>17;state^=state<<5;}
    return state && within_limit(client) && wait_ticks(client,62344u) &&
        expect(client,KUI_GD_EXEC,0u,0u,0);
}
static uint32_t params_address(struct disc_client *client) {
    return guest_address(client->params,sizeof(client->params),false);
}
static bool request(struct disc_client *client,uint32_t command,uint32_t address,uint32_t *handle) {
    int32_t result;
    if(!vector(client,KUI_GD_REQUEST,command,address,&result) || result<=0) return false;
    *handle=(uint32_t)result;return true;
}
static bool check(struct disc_client *client,uint32_t handle,int32_t *result) {
    return vector(client,KUI_GD_CHECK,handle,guest_address(client->status,16u,true),result);
}
static bool unknown(struct disc_client *client,uint32_t handle) {
    int32_t status;
    return check(client,handle,&status) && status==KUI_GD_NOT_FOUND && !client->status[0] &&
        !client->status[1] && !client->status[2] && !client->status[3];
}
static bool drive(struct disc_client *client,uint32_t expected) {
    return expect(client,KUI_GD_DRIVE,guest_address(client->drive,8u,true),0u,0) &&
        client->drive[0]==expected && client->drive[1]==0x80u;
}
static bool completed(struct disc_client *client,uint32_t handle,uint32_t bytes) {
    int32_t status;
    if(!check(client,handle,&status) || status!=KUI_GD_PROCESSING || client->status[0] ||
       client->status[1] || client->status[2] || client->status[3]!=4u) return false;
    for(unsigned polls=0;polls<2048u;polls++) {
        if(!service(client) || !check(client,handle,&status)) return false;
        if(status==KUI_GD_PROCESSING) {
            if(client->status[0] || client->status[1] || client->status[2] || client->status[3]!=4u) return false;
            continue;
        }
        return status==KUI_GD_COMPLETED && !client->status[0] && !client->status[1] &&
            client->status[2]==bytes && !client->status[3];
    }
    return false;
}
static bool control(struct disc_client *client,uint32_t command,
                    struct cdda_disc_client_diagnostic *out) {
    uint32_t handle;
    struct cdda_disc_client_diagnostic current;
    if(!request(client,command,0u,&handle) || !completed(client,handle,0u) ||
       !diagnostic(client,CDDA_DISC_CLIENT_SNAPSHOT,0u,0u,&current) ||
       current.epoch<=client->last_epoch) return false;
    client->last_epoch=current.epoch;client->actions++;
    if(out) *out=current;
    return true;
}
static bool play(struct disc_client *client,uint32_t track,uint32_t repeat,uint32_t *saved_handle) {
    uint32_t handle;client->params[0]=track;client->params[1]=track;client->params[2]=repeat;
    if(!request(client,KUI_CDDA_DISC_BIOS_PLAY,params_address(client),&handle)) return false;
    /* Execution must use the immutable copied request, not these changed words. */
    client->params[0]=0u;client->params[1]=99u;client->params[2]=1u;
    struct cdda_disc_client_diagnostic current;
    if(!completed(client,handle,0u) ||
       !diagnostic(client,CDDA_DISC_CLIENT_SNAPSHOT,0u,0u,&current) ||
       current.state!=KUI_CDDA_CONTROL_PLAYING || current.track!=track ||
       current.epoch<=client->last_epoch || current.loops) return false;
    client->last_epoch=current.epoch;
    if(client->last_track && client->last_track!=track) client->track_switches++;
    client->last_track=track;client->actions++;
    if(saved_handle) *saved_handle=handle;
    return drive(client,3u);
}
static bool wait_audio(struct disc_client *client,uint32_t track,uint32_t frames,
                       uint32_t turns,uint32_t minimum_frame,bool eof) {
    for(unsigned polls=0;polls<2000000u;polls++) {
        struct cdda_disc_client_diagnostic current;
        if(!service(client) || !diagnostic(client,CDDA_DISC_CLIENT_SNAPSHOT,0u,0u,&current) ||
           current.track!=track || current.epoch!=client->last_epoch) return false;
        if(eof) {
            if(current.state==KUI_CDDA_CONTROL_EOF) {
                if(current.frame!=frames || current.loops || !drive(client,1u)) return false;
                client->eof_checks++;return true;
            }
            if(current.state!=KUI_CDDA_CONTROL_PLAYING || current.frame>=frames || current.loops) return false;
        } else {
            if(current.state!=KUI_CDDA_CONTROL_PLAYING || current.frame>=frames) return false;
            if(current.loops>=turns && current.frame>=minimum_frame) return true;
        }
    }
    return false;
}
static bool pause_resume(struct disc_client *client,uint32_t track,uint32_t frames) {
    struct cdda_disc_client_diagnostic paused,after,resumed;
    if(!control(client,KUI_CDDA_DISC_BIOS_PAUSE,&paused) || !drive(client,1u) ||
       paused.state!=KUI_CDDA_CONTROL_PAUSED || paused.track!=track || paused.frame>=frames ||
       paused.frame==paused.prefetch) return false;
    uint32_t first=client->api->clock(client->api->context);bool done=false;
    for(unsigned polls=0;polls<2000000u;polls++) {
        if(!service(client)) return false;
        if(client->api->clock(client->api->context)-first>=KUI_CDDA_TMU_HZ) {done=true;break;}
    }
    if(!done || !drive(client,1u) ||
       !diagnostic(client,CDDA_DISC_CLIENT_SNAPSHOT,0u,0u,&after) ||
       after.state!=KUI_CDDA_CONTROL_PAUSED || after.track!=track || after.frame!=paused.frame ||
       after.epoch!=paused.epoch || after.loops!=paused.loops ||
       !control(client,KUI_CDDA_DISC_BIOS_RELEASE,&resumed) || !drive(client,3u) ||
       resumed.state!=KUI_CDDA_CONTROL_PLAYING || resumed.track!=track ||
       resumed.frame<paused.frame || resumed.frame>=frames || resumed.loops!=paused.loops) return false;
    return true;
}
static bool map_query(struct disc_client *client,uint32_t fad,uint32_t count,bool accepted) {
    struct cdda_disc_client_diagnostic query;
    if(!diagnostic(client,CDDA_DISC_CLIENT_MAP_QUERY,fad,count,&query) ||
       (query.query_result==0u)!=accepted) return false;
    client->map_checks++;return true;
}

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool metadata(struct disc_client *client) {
    static const uint32_t first[6]={150u,170u,45150u,45300u,45450u,45825u};
    static const uint32_t end[6]={166u,245u,45214u,45450u,45675u,45857u};
    static const uint32_t control[6]={4u,0u,4u,0u,0u,4u};
    static const uint32_t stride[6]={2048u,2352u,2048u,2352u,2352u,2352u};
    static const uint32_t offset[6]={0u,0u,0u,512u,1024u,0u};
    for(unsigned i=0;i<6u;i++) {
        struct cdda_disc_client_diagnostic query;
        if(!diagnostic(client,CDDA_DISC_CLIENT_TRACK_QUERY,i+1u,0u,&query) ||
           query.query_result || query.map_count!=6u || query.map_complete!=1u ||
           query.track!=i+1u || query.control!=control[i] || query.start_fad!=first[i] ||
           query.end_fad!=end[i] || query.stride!=stride[i] || query.backing_offset!=offset[i] ||
           query.file_bytes!=offset[i]+(end[i]-first[i])*stride[i] ||
           query.source_frames!=(control[i]?0u:(end[i]-first[i])*588u)) return false;
        client->map_checks++;
    }
    struct cdda_disc_client_diagnostic absent;
    if(!diagnostic(client,CDDA_DISC_CLIENT_TRACK_QUERY,7u,0u,&absent) || !absent.query_result) return false;
    client->map_checks++;
    return map_query(client,45150u,64u,true) && map_query(client,45300u,150u,true) &&
        map_query(client,45450u,225u,true) && map_query(client,45825u,32u,true) &&
        map_query(client,45675u,1u,false) && map_query(client,45213u,2u,false) &&
        map_query(client,45674u,2u,false) && map_query(client,149u,1u,false) &&
        map_query(client,45857u,1u,false);
}
static uint32_t toc_word(uint32_t area,unsigned index) {
    static const uint32_t fad[6]={150u,170u,45150u,45300u,45450u,45825u};
    static const uint32_t control[6]={4u,0u,4u,0u,0u,4u};
    if(index<99u) {
        if(index>=6u || (!area && index>=2u) || (area && index<2u)) return UINT32_MAX;
        return control[index]<<28|0x01000000u|fad[index];
    }
    if(index==99u) return area?0x41030000u:0x41010000u;
    if(index==100u) return area?0x41060000u:0x01020000u;
    return area?0x41000000u|45857u:0x01000000u|245u;
}
static bool toc(struct disc_client *client,uint32_t area,bool duplicate) {
    for(unsigned i=0;i<sizeof(client->toc);i++) client->toc[i]=0xccu;
    for(unsigned i=0;i<32u;i++) {client->toc[i]=0xa5u;client->toc[440u+i]=0x5au;}
    client->params[0]=area;client->params[1]=guest_address(client->toc,sizeof(client->toc),true)+32u;
    uint32_t handle;
    if(!request(client,KUI_GD_GETTOC2,params_address(client),&handle)) return false;
    client->params[0]=2u;client->params[1]=0u;
    for(unsigned i=0;i<32u;i++) if(client->toc[i]!=0xa5u || client->toc[440u+i]!=0x5au) return false;
    for(unsigned i=32u;i<440u;i++) if(client->toc[i]!=0xccu) return false;
    if(!completed(client,handle,408u)) return false;
    for(unsigned i=0;i<32u;i++) if(client->toc[i]!=0xa5u || client->toc[440u+i]!=0x5au) return false;
    for(unsigned i=0;i<102u;i++) if(word(client->toc+32u+i*4u)!=toc_word(area,i)) return false;
    client->toc_checks++;return !duplicate || unknown(client,handle);
}
static uint8_t expected_byte(uint32_t track,uint32_t at) {
    uint32_t x=at^(track*0x9e3779b9u);x^=x>>16;x*=0x7feb352du;
    x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (uint8_t)x;
}
static void prepare_read(struct disc_client *client,uint32_t fad,uint32_t count) {
    for(unsigned i=0;i<sizeof(client->buffer);i++) client->buffer[i]=0xccu;
    for(unsigned i=0;i<32u;i++) {client->buffer[i]=0xa5u;client->buffer[32u+count*2048u+i]=0x5au;}
    client->params[0]=fad;client->params[1]=count;
    client->params[2]=guest_address(client->buffer,sizeof(client->buffer),true)+32u;client->params[3]=0u;
}
static bool verify_read(const struct disc_client *client,uint32_t track,uint32_t offset,uint32_t bytes,uint32_t done) {
    if(done>bytes) return false;
    for(unsigned i=0;i<32u;i++) if(client->buffer[i]!=0xa5u || client->buffer[32u+bytes+i]!=0x5au) return false;
    for(uint32_t i=0;i<done;i++) if(client->buffer[32u+i]!=expected_byte(track,offset+i)) return false;
    for(uint32_t i=done;i<bytes;i++) if(client->buffer[32u+i]!=0xccu) return false;
    return true;
}
static bool read_batch(struct disc_client *client,uint32_t track,uint32_t first,uint32_t sector,bool duplicate) {
    uint32_t handle,done=0,offset=sector*2048u;const uint32_t bytes=32768u;
    prepare_read(client,first+sector,16u);
    if(!request(client,KUI_GD_PIOREAD,params_address(client),&handle) ||
       !verify_read(client,track,offset,bytes,0u)) return false;
    client->params[0]=0u;client->params[1]=17u;client->params[2]=0u;client->params[3]=1u;
    int32_t status;
    if(!check(client,handle,&status) || status!=KUI_GD_PROCESSING || client->status[0] ||
       client->status[1] || client->status[2] || client->status[3]!=4u) return false;
    for(unsigned rounds=0;rounds<2048u;rounds++) {
        if(!service(client) || !check(client,handle,&status)) return false;
        uint32_t next=client->status[2];
        if(next<done || next>bytes || next%2048u || next-done>2048u ||
           !verify_read(client,track,offset,bytes,next) || client->status[0] || client->status[1]) return false;
        if(status==KUI_GD_PROCESSING) {
            if(client->status[3]!=4u) return false;
            if(next>done) client->polls++;
        } else if(status!=KUI_GD_COMPLETED || next!=bytes || client->status[3]) return false;
        client->read_checked+=next-done;done=next;
        if(status==KUI_GD_COMPLETED) return !duplicate || unknown(client,handle);
    }
    return false;
}
static bool negatives(struct disc_client *client) {
    client->params[0]=0u;client->params[1]=0u;client->params[2]=15u;
    if(!expect(client,KUI_GD_REQUEST,KUI_CDDA_DISC_BIOS_PLAY,params_address(client),0)) return false;
    client->params[0]=7u;client->params[1]=7u;
    if(!expect(client,KUI_GD_REQUEST,KUI_CDDA_DISC_BIOS_PLAY,params_address(client),0)) return false;
    client->params[0]=3u;client->params[1]=3u;
    if(!expect(client,KUI_GD_REQUEST,KUI_CDDA_DISC_BIOS_PLAY,params_address(client),0)) return false;
    client->params[0]=4u;client->params[1]=5u;
    if(!expect(client,KUI_GD_REQUEST,KUI_CDDA_DISC_BIOS_PLAY,params_address(client),0)) return false;
    client->params[1]=4u;client->params[2]=1u;
    if(!expect(client,KUI_GD_REQUEST,KUI_CDDA_DISC_BIOS_PLAY,params_address(client),0)) return false;
    prepare_read(client,45675u,1u);
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0)) return false;
    client->params[0]=45213u;client->params[1]=2u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0)) return false;
    client->params[0]=45300u;client->params[1]=1u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0)) return false;
    client->params[0]=45150u;client->params[2]=0x8c000000u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_PIOREAD,params_address(client),0) ||
       !expect(client,KUI_GD_REQUEST,21u,params_address(client),0)) return false;
    client->params[0]=2u;client->params[1]=guest_address(client->toc,sizeof(client->toc),true)+32u;
    if(!expect(client,KUI_GD_REQUEST,KUI_GD_GETTOC2,params_address(client),0)) return false;
    client->params[0]=1u;client->params[1]=0x8c000000u;
    return expect(client,KUI_GD_REQUEST,KUI_GD_GETTOC2,params_address(client),0) &&
        verify_read(client,3u,0u,2048u,0u);
}
uint32_t cdda_disc_client_entry(const void *opaque) {
    const struct cdda_disc_client_exports *api=opaque;
    if(!api || api->magic!=CDDA_DISC_CLIENT_MAGIC || api->revision!=CDDA_DISC_CLIENT_REVISION ||
       api->bytes!=sizeof(*api) || !api->clock || !api->probe || !api->diagnostic) return 1u;
    struct disc_client client={.api=api,.limit=60u*KUI_CDDA_TMU_HZ};
    client.first=api->clock(api->context);uint32_t nop;
    if(!metadata(&client) || !toc(&client,0u,true) || !toc(&client,1u,false) ||
       !negatives(&client) || !request(&client,KUI_GD_NOP,0u,&nop) || !completed(&client,nop,0u) ||
       !unknown(&client,nop) || !diagnostic(&client,CDDA_DISC_CLIENT_MARK,2u,0u,NULL)) return 2u;
    if(!play(&client,4u,0u,NULL) || !wait_audio(&client,4u,88200u,0u,0u,true) ||
       !diagnostic(&client,CDDA_DISC_CLIENT_MARK,3u,0u,NULL)) return 3u;
    if(!play(&client,5u,0u,NULL) || !wait_audio(&client,5u,132300u,0u,0u,true) ||
       !diagnostic(&client,CDDA_DISC_CLIENT_MARK,4u,0u,NULL)) return 4u;
    if(!play(&client,4u,15u,NULL) || !diagnostic(&client,CDDA_DISC_CLIENT_ARM_REENTRY,0u,0u,NULL) ||
       !wait_audio(&client,4u,88200u,2u,0u,false) || !pause_resume(&client,4u,88200u)) return 5u;
    uint32_t resumed=api->clock(api->context);bool advanced=false;
    for(unsigned polls=0;polls<2000000u;polls++) {
        if(!service(&client)) return 5u;
        if(api->clock(api->context)-resumed>=KUI_CDDA_TMU_HZ/2u) {advanced=true;break;}
    }
    if(!advanced || !diagnostic(&client,CDDA_DISC_CLIENT_MARK,5u,0u,NULL)) return 5u;
    if(!play(&client,5u,15u,NULL) || !wait_audio(&client,5u,132300u,2u,0u,false)) return 6u;
    for(unsigned sector=0;sector<64u;sector+=16u)
        if(!read_batch(&client,3u,45150u,sector,false)) return 6u;
    for(unsigned sector=0;sector<32u;sector+=16u)
        if(!read_batch(&client,6u,45825u,sector,sector==16u)) return 6u;
    struct cdda_disc_client_diagnostic active;
    if(!drive(&client,3u) || !diagnostic(&client,CDDA_DISC_CLIENT_SNAPSHOT,0u,0u,&active) ||
       active.state!=KUI_CDDA_CONTROL_PLAYING || active.track!=5u || active.epoch!=client.last_epoch ||
       active.frame>=132300u || active.loops<2u ||
       !diagnostic(&client,CDDA_DISC_CLIENT_MARK,6u,0u,NULL)) return 6u;
    struct cdda_disc_client_diagnostic stopped;
    if(!control(&client,KUI_GD_STOP,&stopped) || stopped.state!=KUI_CDDA_CONTROL_STOPPED ||
       !drive(&client,2u) || client.read_checked!=196608u || client.refusals!=12u ||
       client.stales!=3u || client.actions!=7u || client.drives!=11u || client.polls!=90u ||
       client.toc_checks!=2u || client.map_checks!=16u || client.eof_checks!=2u || client.track_switches!=3u ||
       !diagnostic(&client,CDDA_DISC_CLIENT_REPORT,client.calls,0u,NULL) ||
       !diagnostic(&client,CDDA_DISC_CLIENT_MARK,7u,0u,NULL)) return 7u;
    return 0u;
}

