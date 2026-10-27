/* SPDX-License-Identifier: GPL-3.0-only */
/* Complete CE readonly encoder plus a closed RX bus/store-failure model. */
#include "kui/armada_ce_vmu_a2_publish.h"
#include "kui/armada_ce_vmu_readonly.h"
#include "kui/vmu_image.h"
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
#include "kui/armada_ce_vmu_write.h"
#include "kui/vmu_deferred_io.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_armada_ce_vmu_a2_input in;
static struct kui_armada_ce_vmu_a2_publish_scratch scratch;
static struct kui_armada_ce_vmu_a2_publish_snapshot out;
static struct kui_armada_ce_vmu_readonly card;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
static struct kui_armada_ce_vmu_write writer;
static struct kui_vmu_deferred_io deferred;
#endif
struct event {uint32_t at,value;};
static struct fixture {
    uint8_t rx[0x2000],before[0x2000];
    struct event event[140];
    unsigned stores,backend_calls,admit_calls,stage,admit_refused,fail_at,failed_store_effect,corrupt;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    uint8_t committed[512];
    unsigned writable,commits,commit_attempts,committed_valid,post_admit_refused,error_code;
    unsigned use_deferred,convert_busy,queue_held,queue_mask,worker_context,fence,provider_reads;
    int commit_result,read_result;
#endif
} f;
static unsigned checks;
#define CHECK(c) do {++checks;assert(c);} while(0)
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(n>>(8u*i));
}
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
static int test_read(void *context,uint16_t block,uint8_t bytes[512]) {
    CHECK(context==&f && block==42u);
    if(f.use_deferred) {CHECK(f.worker_context && !f.queue_held && !f.queue_mask && !f.fence);++f.provider_reads;}
    if(f.read_result) return f.read_result;
    if(f.committed_valid) memcpy(bytes,f.committed,512u);
    else CHECK(kui_vmu_image_blank_block(block,bytes)==0);
    return 0;
}
static int test_commit(void *context,uint16_t block,const uint8_t bytes[512]) {
    CHECK(context==&f && block==42u && bytes==(f.use_deferred?deferred.bytes:writer.protocol.staged));
    if(f.use_deferred) CHECK(f.worker_context && !f.queue_held && !f.queue_mask && !f.fence);
    ++f.commit_attempts;
    if(f.commit_result) return f.commit_result;
    memcpy(f.committed,bytes,512u);f.committed_valid=1u;++f.commits;return 0;
}
static bool deferred_enter(void *context,uint32_t *saved) {
    CHECK(context==&f && !f.queue_held);*saved=f.queue_mask;f.queue_mask=0xf0u;f.queue_held=1u;return true;
}
static void deferred_leave(void *context,uint32_t saved) {
    CHECK(context==&f && f.queue_held && f.queue_mask==0xf0u);f.queue_held=0u;f.queue_mask=saved;
}
static bool deferred_worker_ready(void *context) {
    CHECK(context==&f);return f.worker_context && !f.queue_held && !f.queue_mask && !f.fence;
}
static void enable_deferred(void) {
    /* A fresh host fixture, before requests: bind the real CE adapter to the
     * real mailbox. This supplies no native hold or production SD provider. */
    const struct kui_vmu_emulation_io provider={&f,test_read,test_commit};
    const struct kui_vmu_deferred_guard guard={&f,deferred_enter,deferred_leave,deferred_worker_ready};
    memset(&deferred,0,sizeof(deferred));CHECK(!kui_vmu_deferred_io_init(&deferred,&provider,&guard));
    const struct kui_vmu_emulation_io queued={&deferred,kui_vmu_deferred_io_read,kui_vmu_deferred_io_commit};
    memset(&writer,0,sizeof(writer));CHECK(!kui_armada_ce_vmu_write_init(&writer,&queued));
    f.writable=f.use_deferred=f.convert_busy=1u;
}
static int run_worker(void) {
    CHECK(!f.fence && !f.queue_held && !f.queue_mask);f.worker_context=1u;
    int result=kui_vmu_deferred_io_work(&deferred);f.worker_context=0u;return result;
}
#endif
static void request(unsigned command,unsigned block) {
    memset(in.target.request,0,sizeof(in.target.request));
    in.target.request[0]=(uint8_t)command;in.target.request[1]=2u;
    in.target.request_bytes=command==1u || command==2u?4u:12u;
    if(in.target.request_bytes==12u) {
        in.target.request[3]=2u;in.target.request[7]=2u;
        in.target.request[11]=(uint8_t)block;
    }
    in.target.response_header=UINT32_MAX;
    put32(f.rx+in.target.rx_bus-in.pool_bus,UINT32_MAX);
    put32(f.rx+in.parent.rx_bus-in.pool_bus,in.parent.response_header);
}
static void reset(void) {
    memset(&f,0,sizeof(f));memset(&in,0,sizeof(in));memset(&scratch,0,sizeof(scratch));
    memset(&out,0,sizeof(out));memset(f.rx,0xa5,sizeof(f.rx));
    CHECK(kui_armada_ce_vmu_readonly_init(&card)==0);
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    const struct kui_vmu_emulation_io io={&f,test_read,test_commit};
    memset(&writer,0,sizeof(writer));CHECK(kui_armada_ce_vmu_write_init(&writer,&io)==0);
#endif
    in.pool_bus=0x0cfd1000u;in.pool_bytes=0x2000u;
    in.tx_bus=0x0cfd3000u;in.tx_bytes=0x34u;
    in.protected_bus=0x0cfca000u;in.protected_bytes=0x6000u;
    in.parent.request_header=0x01002009u;
    in.parent.response_header=0x03310008u; /* Physical A1 plus A5 remain set. */
    in.parent.request_function=0x01000000u;
    in.parent.response_function=0x01000000u;
    in.parent.rx_bus=in.pool_bus;in.parent.capacity=16u;
    in.target_present=1u;in.target.rx_bus=in.pool_bus+0x420u;
    in.target.capacity=1020u;
    request(1u,0);
    memcpy(f.before,f.rx,sizeof(f.rx));
}
static int backend(void *ctx,const uint8_t *p,size_t n,uint8_t *reply,size_t cap,size_t *produced) {
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    CHECK(ctx==&f && n<=140u && cap==(n==140u?384u:524u));
    CHECK((uintptr_t)p+n<=(uintptr_t)reply || (uintptr_t)reply+cap<=(uintptr_t)p);
    if(n==140u) CHECK(p==(const uint8_t *)(const void *)scratch.reply+384u && f.admit_calls==1u);
    ++f.backend_calls;f.stage=1u;
    int result=f.writable?kui_armada_ce_vmu_write_handle(&writer,p,n,reply,cap,produced):
        kui_armada_ce_vmu_readonly_handle(&card,p,n,reply,cap,produced);
#else
    CHECK(ctx==&f && n<=12u && cap==524u);++f.backend_calls;
    f.stage=1u;
    int result=kui_armada_ce_vmu_readonly_handle(&card,p,n,reply,cap,produced);
#endif
    switch(f.corrupt) {
    case 1:reply[2]=1u;break;
    case 2:reply[4]^=1u;break;
    case 3:reply[3]^=1u;break;
    case 4:return 0;
    case 5:*produced=525u;break;
    case 6:*produced=0u;break;
    case 7:reply[8]^=1u;break;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    case 8:reply[0]=0xfcu;reply[3]=1u;*produced=8u;break;
    case 9:reply[0]=0xfbu;reply[3]=1u;reply[4]=1u;reply[7]=8u;*produced=8u;break;
    case 10:reply[0]=0xfbu;reply[3]=1u;reply[4]=reply[5]=reply[6]=0u;reply[7]=32u;*produced=8u;break;
    case 11:
        reply[0]=0xfbu;reply[1]=0u;reply[2]=2u;reply[3]=1u;
        reply[4]=reply[5]=reply[6]=0u;reply[7]=(uint8_t)f.error_code;*produced=8u;break;
    case 12:reply[0]=0xfbu;reply[3]=0u;*produced=4u;break;
    case 13:*produced=0u;return KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED;
    case 14:*produced=4u;return KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED;
#endif
    default:break;
    }
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    /* An explicit private integration wrapper, not publisher-wide AGAIN
     * translation. The engine produced BUSY after enqueueing an immutable
     * job; the separate native hold must keep its caller from seeing it. */
    if(f.use_deferred && f.convert_busy && result==1 && *produced==4u &&
       le32(reply)==0x000200fcu && n==12u &&
       (p[0]==11u || (p[0]==13u && p[9]==4u))) {
        *produced=0u;return KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED;
    }
#endif
    return result;
}
static bool admit(void *ctx) {
    CHECK(ctx==&f && !f.stores);++f.admit_calls;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    unsigned command=in.target.request[0];
    bool mutation=in.target_present && (command==12u || (command==13u && in.target.request[9]==4u));
    if(mutation && f.admit_calls==1u) {
        CHECK(!f.backend_calls);return !f.admit_refused;
    }
    CHECK(!in.target_present || f.stage==1u);
    f.stage=2u;return !f.admit_refused && !f.post_admit_refused;
#else
    CHECK(!in.target_present || f.stage==1u);
    f.stage=2u;return !f.admit_refused;
#endif
}
static bool store32(void *ctx,uint32_t at,uint32_t value) {
    CHECK(ctx==&f && (at&0xe0000000u)==0xa0000000u && !(at&3u));
    CHECK(f.stage==2u && f.admit_calls);
    uint32_t bus=at&0x1fffffffu;
    CHECK(bus>=in.pool_bus && bus-in.pool_bus<=in.pool_bytes-4u);
    CHECK(bus==in.parent.rx_bus || (in.target_present && bus>=in.target.rx_bus &&
        bus-in.target.rx_bus<=in.target.capacity-4u));
    CHECK(f.stores<140u);f.event[f.stores].at=bus;f.event[f.stores].value=value;
    ++f.stores;bool ok=f.stores!=f.fail_at;
    if(ok || f.failed_store_effect) put32(f.rx+bus-in.pool_bus,value);
    return ok;
}
static unsigned publish(void) {
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    CHECK(!f.fence);f.fence=1u;
#endif
    unsigned result=(unsigned)kui_armada_ce_vmu_a2_publish(&in,backend,&f,admit,&f,store32,&f,&scratch,&out);
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    CHECK(f.fence && !f.queue_held && !f.queue_mask);f.fence=0u;
#endif
    return result;
}
static void unchanged(void) {CHECK(!memcmp(f.rx,f.before,sizeof(f.rx)));}
static void rejected(unsigned result) {
    memcpy(f.before,f.rx,sizeof(f.rx));
    CHECK(publish()==result);CHECK(!f.stores);unchanged();
    CHECK(out.fault[0]==result && out.status[3] && out.status[0]==1u);
    uint32_t fault[5];memcpy(fault,out.fault,sizeof(fault));
    CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_STATE && !f.stores);
    CHECK(!memcmp(fault,out.fault,sizeof(fault)));
}
static void metadata_and_parent_order(void) {
    const unsigned commands[]={1u,2u,10u};
    const unsigned sizes[]={116u,196u,32u};
    for(unsigned c=0;c<3u;c++) {
        reset();request(commands[c],0);
        CHECK(publish()==0);CHECK(f.backend_calls==1u && f.stores==sizes[c]/4u+1u);
        CHECK(out.counts[0]==KUI_ARMADA_CE_VMU_A2_PUBLISH_MAGIC && out.counts[1]==1u);
        CHECK(out.counts[2]==1u && out.counts[3]==1u && out.counts[4]==1u);
        CHECK(out.commands[c]==1u && out.status[1]==1u && out.status[2]==f.stores);
        CHECK(out.target[4]==sizes[c] && !out.fault[0] && !out.status[3]);
        for(unsigned i=0;i<sizes[c]/4u-1u;i++)
            CHECK(f.event[i].at==in.target.rx_bus+4u+4u*i);
        CHECK(f.event[sizes[c]/4u-1u].at==in.target.rx_bus);
        CHECK(f.event[f.stores-1u].at==in.parent.rx_bus);
        CHECK(le32(f.rx)==(in.parent.response_header|0x00020000u));
        CHECK((le32(f.rx)&0x00110000u)==0x00110000u);
        CHECK(!memcmp(f.rx+4u,f.before+4u,0x41cu));
        CHECK(!memcmp(f.rx+0x420u+sizes[c],f.before+0x420u+sizes[c],
            sizeof(f.rx)-0x420u-sizes[c]));
        CHECK(!card.backend.commit_attempts && !card.backend.reads);
    }
    for(unsigned c=0;c<3u;c++) {
        reset();in.target_present=0u;
        unsigned command=c==0u?1u:c==1u?2u:9u;
        unsigned bytes=c==0u?116u:c==1u?196u:16u;
        in.parent.request_header=command|(32u<<8)|(command==9u?1u<<24:0);
        in.parent.response_header=(command==1u?5u:command==2u?6u:8u)|
            (0x31u<<16)|((bytes/4u-1u)<<24);
        in.parent.capacity=bytes;
        put32(f.rx,in.parent.response_header);
        CHECK(kui_armada_ce_vmu_a2_publish(&in,NULL,NULL,admit,&f,store32,&f,&scratch,&out)==0);
        CHECK(!f.backend_calls && f.stores==1u && out.counts[3]==1u && !out.counts[4]);
        CHECK(le32(f.rx)==(in.parent.response_header|0x00020000u));
    }
}
static void complete_blank_image_and_ce_finalize(void) {
    uint8_t expected[512];reset();
    for(unsigned block=0;block<256u;block++) {
        f.stores=0;request(11u,block);
        CHECK(publish()==0 && f.stores==132u);
        CHECK(le32(f.rx+0x420u)==0x82020008u);
        CHECK(kui_vmu_image_blank_block((uint16_t)block,expected)==0);
        CHECK(!memcmp(f.rx+0x42cu,expected,512u));
        f.stores=0;request(13u,block);
        CHECK(publish()==0 && f.stores==2u);
        CHECK(le32(f.rx+0x420u)==0x00020007u);
        CHECK(f.event[0].at==in.target.rx_bus && f.event[1].at==in.parent.rx_bus);
    }
    CHECK(out.commands[3]==256u && out.commands[4]==256u);
    CHECK(out.counts[4]==512u && out.counts[2]==512u && !out.status[3]);
    CHECK(!card.backend.commit_attempts && !card.faulted);
}
static void request_and_physical_guards(void) {
    for(unsigned cmd=0;cmd<256u;cmd++) {
        if(cmd==1u || cmd==2u || cmd==10u || cmd==11u || cmd==13u) continue;
        reset();request(cmd,0);rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST);
        CHECK(!f.backend_calls);
    }
    reset();in.parent.response_header|=0x00020000u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_CONFLICT);CHECK(!f.backend_calls);
    reset();in.target.response_header=0x1c020005u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_CONFLICT);CHECK(!f.backend_calls);
    reset();in.parent.request_function=0x02000000u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_PARENT);
    reset();in.parent.response_function=0x02000000u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_PARENT);
    reset();in.parent.response_header^=0x01000000u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_PARENT);
    reset();in.parent.request_header=0x01006009u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_PARENT);
    reset();in.target.request[1]=1u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST);
    reset();in.target.request[2]=1u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST);
    reset();request(11u,0);in.target.request[8]=1u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST);
    reset();request(11u,0);in.target.request[10]=1u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST);
    reset();request(13u,0);in.target.request[9]=4u;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_BACKEND);
#else
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST);
#endif
    reset();request(13u,0);rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_BACKEND);
    CHECK(f.backend_calls==1u); /* Unsolicited read finalization cannot ACK. */
}
static void span_and_reply_guards(void) {
    reset();in.target.capacity=112u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN);
    reset();in.parent.capacity=12u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_PARENT);
    reset();in.target.capacity=1024u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN);
    reset();in.target.rx_bus=in.pool_bus+4u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN);
    reset();in.target.rx_bus=in.pool_bus+0x1fe0u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN);
    reset();in.target.rx_bus=in.parent.rx_bus;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_OVERLAP);
    reset();in.protected_bus=in.pool_bus;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_OVERLAP);
    reset();in.tx_bus=in.pool_bus;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_OVERLAP);
    reset();in.tx_bytes=0x1004u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN);
    reset();in.pool_bytes=0xfffffffcu;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN);
    reset();in.pool_bus=0xfffff000u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN);
    reset();in.target_present=2u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN);
    for(unsigned mode=1;mode<=7u;mode++) {
        reset();request(11u,0);f.corrupt=mode;
        rejected(mode==4u?KUI_ARMADA_CE_VMU_A2_PUBLISH_BACKEND:KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY);
        CHECK(f.backend_calls==1u);
    }
    reset();struct kui_armada_ce_vmu_a2_publish_snapshot previous=out;
    CHECK(kui_armada_ce_vmu_a2_publish(&in,backend,&f,admit,&f,store32,&f,
        (struct kui_armada_ce_vmu_a2_publish_scratch *)(void *)&in,&out)==
        KUI_ARMADA_CE_VMU_A2_PUBLISH_ARGUMENT);
    CHECK(!memcmp(&out,&previous,sizeof(out)) && !f.backend_calls && !f.stores);
    CHECK(kui_armada_ce_vmu_a2_publish(NULL,backend,&f,admit,&f,store32,&f,&scratch,&out)==1u);
    CHECK(kui_armada_ce_vmu_a2_publish(&in,backend,&f,admit,&f,NULL,&f,&scratch,&out)==1u);
    CHECK(kui_armada_ce_vmu_a2_publish(&in,backend,&f,NULL,&f,store32,&f,&scratch,&out)==1u);
    CHECK(kui_armada_ce_vmu_a2_publish(&in,backend,&f,admit,&f,store32,&f,&scratch,NULL)==1u);
    reset();f.admit_refused=1u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(f.backend_calls==1u && f.admit_calls==1u);
    reset();in.target_present=0;f.admit_refused=1u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(!f.backend_calls && f.admit_calls==1u);
}
static void every_store_failure_and_saturation(void) {
    for(unsigned effect=0;effect<2u;effect++) for(unsigned fail=1;fail<=132u;fail++) {
        reset();request(11u,255u);f.fail_at=fail;f.failed_store_effect=effect;
        CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE && f.stores==fail);
        CHECK(out.fault[0]==KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE);
        CHECK(out.status[2]==fail-1u && out.status[4]==fail);
        if(fail<=130u) CHECK(le32(f.rx+0x420u)==UINT32_MAX);
        if(fail<=131u || !effect) CHECK(le32(f.rx)==in.parent.response_header);
        if(fail==132u && effect) CHECK(le32(f.rx)==(in.parent.response_header|0x00020000u));
        uint32_t first[5];memcpy(first,out.fault,sizeof(first));
        CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_STATE && f.stores==fail);
        CHECK(!memcmp(first,out.fault,sizeof(first)));
    }
    reset();
    for(unsigned i=1;i<5u;i++) out.counts[i]=UINT32_MAX;
    for(unsigned i=0;i<5u;i++) out.commands[i]=UINT32_MAX;
    out.status[0]=UINT32_MAX;out.status[1]=UINT32_MAX;out.status[2]=UINT32_MAX;
    CHECK(publish()==0);
    for(unsigned i=1;i<5u;i++) CHECK(out.counts[i]==UINT32_MAX);
    for(unsigned i=0;i<5u;i++) CHECK(out.commands[i]==UINT32_MAX);
    CHECK(out.status[0]==UINT32_MAX && out.status[1]==UINT32_MAX && out.status[2]==UINT32_MAX);
}
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
static void write_frame(unsigned phase,const uint8_t data[128]) {
    request(12u,42u);in.target.request_bytes=140u;in.target.request[3]=34u;
    in.target.request[9]=(uint8_t)phase;memcpy(in.target.request+12u,data,128u);
    in.target.capacity=4u;f.stores=f.backend_calls=f.admit_calls=f.stage=0;
}
static void sync_frame(void) {
    request(13u,42u);in.target.request[9]=4u;in.target.capacity=4u;
    f.stores=f.backend_calls=f.admit_calls=f.stage=0;
}
static void stage_block(uint8_t data[512]) {
    for(unsigned i=0;i<512u;i++) data[i]=(uint8_t)(i*13u+i/128u);
    for(unsigned phase=0;phase<4u;phase++) {
        write_frame(phase,data+128u*phase);
        uint8_t before[140];memcpy(before,in.target.request,140u);
        CHECK(publish()==0 && f.stores==2u && f.admit_calls==2u);
        CHECK(le32(f.rx+0x420u)==0x00020007u && !f.commits);
        CHECK(!memcmp(before,in.target.request,140u));
        CHECK(!memcmp(before,(uint8_t *)(void *)scratch.reply+384u,140u));
        CHECK(writer.protocol.active && writer.protocol.phases==(1u<<(phase+1u))-1u);
    }
    CHECK(!memcmp(writer.protocol.staged,data,512u));
}
static void write_staging_and_reply_shapes(void) {
    uint8_t data[512];reset();f.writable=1u;stage_block(data);
    sync_frame();f.commit_result=KUI_VMU_EMU_IO_BUSY;
    CHECK(publish()==0 && f.stores==2u && f.admit_calls==2u);
    CHECK(le32(f.rx+0x420u)==0x000200fcu && !f.commits && writer.protocol.commit_attempted);
    CHECK(!out.commands[4]);
    write_frame(0u,data);in.target.request[12]^=1u;
    CHECK(publish()==0 && le32(f.rx+0x420u)==0x000200fcu);
    CHECK(!memcmp(writer.protocol.staged,data,512u));
    sync_frame();f.commit_result=0;
    CHECK(publish()==0 && le32(f.rx+0x420u)==0x00020007u && f.commits==1u);
    CHECK(!memcmp(f.committed,data,512u) && !writer.protocol.active);
    sync_frame();CHECK(publish()==0 && f.commits==1u);
    CHECK(out.commands[4]==2u);
    request(11u,42u);in.target.capacity=524u;f.stores=f.backend_calls=f.admit_calls=f.stage=0;
    CHECK(publish()==0 && f.stores==132u && !memcmp(f.rx+0x42cu,data,512u));
    for(unsigned capacity=4u;capacity<=8u;capacity+=4u) {
        reset();f.writable=1u;write_frame(2u,data);in.target.capacity=capacity;
        uint8_t untouched[4];memcpy(untouched,f.rx+0x424u,sizeof(untouched));
        CHECK(publish()==0 && f.stores==(capacity==4u?2u:3u) && out.target[4]==8u);
        CHECK(le32(f.rx+0x420u)==0x010200fbu);
        if(capacity==4u) CHECK(!memcmp(f.rx+0x424u,untouched,sizeof(untouched)));
        else CHECK(f.rx[0x427u]==2u);
    }
    for(unsigned result=1u;result<=2u;result++) {
        reset();f.writable=1u;request(11u,42u);
        f.read_result=result==1u?KUI_VMU_EMU_IO_BUSY:KUI_VMU_EMU_IO_ERROR;
        CHECK(publish()==0 && f.stores==(result==1u?2u:3u));
        CHECK(le32(f.rx+0x420u)==(result==1u?0x000200fcu:0x010200fbu));
        CHECK(!out.commands[3]);
    }
    for(unsigned corrupt=8u;corrupt<=10u;corrupt++) {
        reset();f.writable=1u;write_frame(0u,data);in.target.capacity=8u;f.corrupt=corrupt;
        rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY);
    }
    for(unsigned capacity=4u;capacity<=8u;capacity+=4u) {
        reset();f.writable=1u;stage_block(data);sync_frame();
        in.target.capacity=capacity;f.commit_result=KUI_VMU_EMU_IO_ERROR;
        if(capacity==4u) rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY);
        else {
            CHECK(publish()==0 && f.stores==3u);
            CHECK(le32(f.rx+0x420u)==0x010200fbu && f.rx[0x427u]==8u);
        }
        CHECK(f.commit_attempts==1u && !f.commits && writer.protocol.commit_attempted);
        CHECK(!memcmp(writer.protocol.staged,data,512u));
    }
    reset();f.writable=1u;stage_block(data);sync_frame();f.post_admit_refused=1u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(f.commits==1u && writer.protocol.committed_valid && !writer.protocol.active);
    for(unsigned failure=1u;failure<=2u;failure++) {
        reset();f.writable=1u;stage_block(data);sync_frame();f.fail_at=failure;
        CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE && f.stores==failure);
        CHECK(f.commits==1u && writer.protocol.committed_valid && !writer.protocol.active);
    }
}
static void write_error_header_truncation(void) {
    uint8_t data[128]={0};
    for(unsigned error=0;error<256u;error++) {
        reset();f.writable=1u;write_frame(0u,data);f.corrupt=11u;f.error_code=error;
        bool canonical=error==1u || error==2u || error==4u || error==8u || error==16u;
        if(!canonical) {rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY);continue;}
        memcpy(f.before,f.rx,sizeof(f.rx));
        CHECK(publish()==0 && f.backend_calls==1u && f.admit_calls==2u && f.stores==2u);
        CHECK(out.target[4]==8u && out.status[4]==2u && out.status[2]==2u);
        CHECK(f.event[0].at==in.target.rx_bus && f.event[0].value==0x010200fbu);
        CHECK(f.event[1].at==in.parent.rx_bus && f.event[1].value==(in.parent.response_header|0x00020000u));
        CHECK(le32(f.rx+0x420u)==0x010200fbu && !out.commands[4] && !f.commits);
        CHECK(scratch.reply[1]==error<<24); /* Full private error remains available. */
        for(unsigned i=0;i<sizeof(f.rx);i++)
            if(i>=4u && (i<0x420u || i>=0x424u)) CHECK(f.rx[i]==f.before[i]);
    }
    /* The exception validates the complete logical FILEERR8 before reducing
     * stores. It never accepts a shortened FILEERR or repairs word count. */
    const unsigned corruptions[]={1u,3u,5u,6u,8u,9u,10u,12u};
    for(unsigned i=0;i<sizeof(corruptions)/sizeof(corruptions[0]);i++) {
        reset();f.writable=1u;write_frame(2u,data);f.corrupt=corruptions[i];
        rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY);
    }
    for(unsigned failure=1u;failure<=2u;failure++) for(unsigned effect=0;effect<2u;effect++) {
        reset();f.writable=1u;write_frame(2u,data);f.fail_at=failure;f.failed_store_effect=effect;
        memcpy(f.before,f.rx,sizeof(f.rx));
        CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE && f.stores==failure);
        CHECK(out.target[4]==8u && out.fault[0]==KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE);
        CHECK(!memcmp(f.rx+0x424u,f.before+0x424u,sizeof(f.rx)-0x424u));
        CHECK(f.event[0].at==in.target.rx_bus && f.event[0].value==0x010200fbu);
    }
    reset();f.writable=1u;write_frame(2u,data);f.post_admit_refused=1u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(f.backend_calls==1u && f.admit_calls==2u && out.target[4]==8u);
}
static void fresh_deferred_visit(void) {
    /* Model a new publisher call after its caller has supplied a fresh owner
     * query. The admit stub is only this policy boundary, not an IRP lease. */
    f.stores=f.backend_calls=f.admit_calls=f.stage=0u;
}
static void pending_visit(unsigned admissions) {
    struct kui_armada_ce_vmu_a2_publish_snapshot previous=out;
    memcpy(f.before,f.rx,sizeof(f.rx));
    CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_PENDING);
    CHECK(!f.stores && f.backend_calls==1u && f.admit_calls==admissions);unchanged();
    CHECK(out.counts[1]==previous.counts[1]+1u && out.status[1]==previous.status[1]+1u);
    for(unsigned i=2u;i<5u;i++) CHECK(out.counts[i]==previous.counts[i]);
    CHECK(!memcmp(out.commands,previous.commands,sizeof(out.commands)));
    CHECK(out.status[0]==previous.status[0] && out.status[2]==previous.status[2]);
    CHECK(!out.status[3] && !out.status[4] && !out.target[4]);
    CHECK(!memcmp(out.fault,previous.fault,sizeof(out.fault)));
}
static void deferred_read_and_commit(void) {
    reset();enable_deferred();request(11u,42u);in.target.capacity=524u;
    pending_visit(1u);CHECK(deferred.state==KUI_VMU_DEFERRED_PENDING && deferred.submissions==1u);
    CHECK(!f.provider_reads && !writer.protocol.read_valid);
    fresh_deferred_visit();pending_visit(1u);
    CHECK(deferred.ticket==1u && deferred.submissions==1u && !f.provider_reads);
    f.fence=1u;CHECK(kui_vmu_deferred_io_work(&deferred)==-2);f.fence=0u;
    CHECK(!f.provider_reads);CHECK(run_worker()==1);
    CHECK(deferred.state==KUI_VMU_DEFERRED_DONE && f.provider_reads==1u && !f.stores);
    fresh_deferred_visit();CHECK(publish()==0 && f.stores==132u);
    uint8_t blank[512];CHECK(kui_vmu_image_blank_block(42u,blank)==0);
    CHECK(!memcmp(f.rx+0x42cu,blank,512u) && le32(f.rx+0x420u)==0x82020008u);
    CHECK(deferred.state==KUI_VMU_DEFERRED_IDLE && f.provider_reads==1u && writer.protocol.read_valid);
    CHECK(out.counts[2]==1u && out.counts[3]==1u && out.counts[4]==1u && out.commands[3]==1u);
    request(13u,42u);in.target.capacity=8u;fresh_deferred_visit();
    CHECK(publish()==0 && f.stores==2u && writer.read_finalizations==1u && f.provider_reads==1u);

    uint8_t data[512];reset();enable_deferred();stage_block(data);sync_frame();in.target.capacity=8u;
    pending_visit(2u);CHECK(deferred.state==KUI_VMU_DEFERRED_PENDING && deferred.operation==KUI_VMU_DEFERRED_COMMIT);
    CHECK(!memcmp(deferred.bytes,data,512u) && writer.protocol.commit_attempted && !f.commit_attempts);
    fresh_deferred_visit();pending_visit(2u);CHECK(deferred.submissions==1u);
    f.commit_result=KUI_VMU_EMU_IO_BUSY;CHECK(run_worker()==0);
    CHECK(deferred.state==KUI_VMU_DEFERRED_PENDING && !f.commits && deferred.provider_busy==1u);
    fresh_deferred_visit();pending_visit(2u);CHECK(f.commit_attempts==1u && !f.commits);
    f.commit_result=0;CHECK(run_worker()==1);
    CHECK(deferred.state==KUI_VMU_DEFERRED_DONE && f.commits==1u && !writer.protocol.committed_valid);
    fresh_deferred_visit();CHECK(publish()==0 && f.stores==2u);
    CHECK(le32(f.rx+0x420u)==0x00020007u && writer.protocol.committed_valid && writer.commits==1u);
    CHECK(deferred.state==KUI_VMU_DEFERRED_IDLE && !memcmp(f.committed,data,512u));
    sync_frame();in.target.capacity=8u;CHECK(publish()==0 && f.stores==2u);
    CHECK(f.commits==1u && f.commit_attempts==2u && deferred.submissions==1u); /* Lost final ACK. */
}
static void deferred_refusals_and_error_completion(void) {
    uint8_t data[512]={0};
    const unsigned wrong_commands[]={1u,10u,12u,13u};
    for(unsigned i=0;i<sizeof(wrong_commands)/sizeof(wrong_commands[0]);i++) {
        reset();f.writable=1u;
        if(wrong_commands[i]==12u) write_frame(0u,data);
        else request(wrong_commands[i],wrong_commands[i]==10u?0u:42u);
        f.corrupt=13u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_BACKEND);
        CHECK(f.backend_calls==1u);
    }
    for(unsigned sync=0;sync<2u;sync++) {
        reset();enable_deferred();
        if(sync) {stage_block(data);sync_frame();in.target.capacity=8u;}
        else {request(11u,42u);in.target.capacity=524u;}
        f.corrupt=14u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_BACKEND);
        CHECK(deferred.state==KUI_VMU_DEFERRED_PENDING && !f.commits && !f.provider_reads);
    }
    /* Losing final admission while deferring never publishes parent/A2 bytes. */
    reset();enable_deferred();stage_block(data);sync_frame();in.target.capacity=8u;
    f.post_admit_refused=1u;rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(f.admit_calls==2u && deferred.state==KUI_VMU_DEFERRED_PENDING && !f.commit_attempts);

    /* DONE may be consumed privately before a later owner check fails. This
     * does not recover a native held IRP or clear the publisher's fault. */
    reset();enable_deferred();request(11u,42u);in.target.capacity=524u;pending_visit(1u);
    CHECK(run_worker()==1);fresh_deferred_visit();f.post_admit_refused=1u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(deferred.state==KUI_VMU_DEFERRED_IDLE && f.provider_reads==1u && writer.protocol.read_valid);
    CHECK(!out.counts[2] && !out.counts[3] && !out.counts[4]);

    /* A changed source header fails before even consuming the worker result. */
    reset();enable_deferred();request(11u,42u);in.target.capacity=524u;pending_visit(1u);
    CHECK(run_worker()==1);fresh_deferred_visit();in.target.response_header=0u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_CONFLICT);
    CHECK(!f.backend_calls && deferred.state==KUI_VMU_DEFERRED_DONE && f.provider_reads==1u);

    for(unsigned sync=0;sync<2u;sync++) {
        reset();enable_deferred();
        if(sync) {stage_block(data);sync_frame();in.target.capacity=8u;}
        else {request(11u,42u);in.target.capacity=524u;}
        pending_visit(sync?2u:1u);f.commit_result=f.read_result=KUI_VMU_EMU_IO_ERROR;
        CHECK(run_worker()==-1 && deferred.state==KUI_VMU_DEFERRED_FAULT);
        fresh_deferred_visit();CHECK(publish()==0 && f.stores==3u);
        CHECK(le32(f.rx+0x420u)==0x010200fbu && f.rx[0x427u]==8u && !f.commits);
        CHECK(out.target[4]==8u); /* Real error, never DEFERRED or fake ACK. */
    }
    reset();enable_deferred();request(11u,42u);in.target.capacity=524u;f.convert_busy=0u;
    CHECK(publish()==0 && f.stores==2u && le32(f.rx+0x420u)==0x000200fcu);
    CHECK(out.target[4]==4u && deferred.state==KUI_VMU_DEFERRED_PENDING && !out.commands[3]);
}
static void write_staging_guards(void) {
    uint8_t data[512]={0};
    for(unsigned which=0;which<8u;which++) {
        reset();f.writable=1u;write_frame(0u,data);
        switch(which) {
        case 0:in.target.request_bytes=139u;break;
        case 1:in.target.request_bytes=141u;break;
        case 2:in.target.request[3]=33u;break;
        case 3:in.target.request[7]=1u;break;
        case 4:in.target.request[8]=1u;break;
        case 5:in.target.request[9]=4u;break;
        case 6:in.target.request[10]=1u;break;
        default:in.target.request[1]=1u;break;
        }
        rejected(which<2u?KUI_ARMADA_CE_VMU_A2_PUBLISH_ARGUMENT:KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST);
        CHECK(!f.backend_calls && !writer.protocol.active);
    }
    reset();f.writable=1u;write_frame(0u,data);f.admit_refused=1u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(!f.backend_calls && !writer.protocol.active);
    reset();f.writable=1u;write_frame(0u,data);f.post_admit_refused=1u;
    rejected(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(f.backend_calls==1u && writer.protocol.phases==1u && !f.commits);
    for(unsigned failure=1u;failure<=2u;failure++) {
        reset();f.writable=1u;write_frame(0u,data);f.fail_at=failure;
        CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE && f.stores==failure);
        CHECK(writer.protocol.phases==1u && !f.commits);
    }
}
#endif
int main(void) {
    metadata_and_parent_order();complete_blank_image_and_ce_finalize();
    request_and_physical_guards();span_and_reply_guards();every_store_failure_and_saturation();
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    write_staging_and_reply_shapes();write_error_header_truncation();deferred_read_and_commit();
    deferred_refusals_and_error_completion();write_staging_guards();
    printf("Gated WRITE staging: actual CE adapter, private140B tail, phase/commit BUSY/error, "
        "pre/post admission and lost-ACK store failures passed\n");
#endif
    printf("CE A2 publisher: %u checks; real readonly blank-card metadata/all256blocks/CEfinalization, "
        "payload-header-advertise ordering, physicalA1 preservation, immutable refusals, "
        "every store failure and saturation passed\n",checks);
    return 0;
}
