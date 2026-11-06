/* SPDX-License-Identifier: GPL-3.0-only */
/* Production probe and existing protocol, with generated blank blocks only.
 * No guest pointers, Maple MMIO, storage provider or timing model. */
#include "kui/vc2_ce_vmu_write_probe.h"
#include "kui/armada_ce_vmu_a2_publish.h"
#include "kui/armada_ce_vmu_readonly.h"
#include "kui/vmu_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);abort();}} while(0)
static struct {
    uint32_t before[8];
    struct kui_vc2_ce_vmu_write_probe card;
    uint32_t after[8];
} guarded;
#define card guarded.card
static uint8_t frame[144],response[526],data[512],baseline[524];
static size_t produced;

static void guards(void) {
    for(unsigned i=0u;i<8u;++i)
        CHECK(guarded.before[i]==UINT32_C(0xa5a5a5a5) && guarded.after[i]==UINT32_C(0xa5a5a5a5));
}
static void fresh(void) {
    memset(&guarded,0xa5,sizeof(guarded));memset(&card,0,sizeof(card));
    CHECK(!kui_vc2_ce_vmu_write_probe_pending(&card));
    CHECK(!kui_vc2_ce_vmu_write_probe_init(&card));
    struct kui_vc2_ce_vmu_write_probe old=card;
    CHECK(kui_vc2_ce_vmu_write_probe_init(&card)==KUI_ARMADA_CE_VMU_WRITE_INTERNAL);
    CHECK(!memcmp(&old,&card,sizeof(card)));
    for(unsigned i=0u;i<512u;++i)data[i]=(uint8_t)i;
    guards();
}
static unsigned request(unsigned command,unsigned block,unsigned phase) {
    memset(frame,0,sizeof(frame));frame[0]=(uint8_t)command;frame[1]=2u;
    if(command==1u || command==2u)return 4u;
    frame[3]=command==12u?34u:2u;frame[7]=2u;
    frame[9]=(uint8_t)phase;frame[10]=(uint8_t)(block>>8);frame[11]=(uint8_t)block;
    if(command==12u)memcpy(frame+12u,data+128u*(phase&3u),128u);
    return command==12u?140u:12u;
}
static int run(unsigned bytes,size_t capacity) {
    uint8_t saved[sizeof(frame)];memcpy(saved,frame,sizeof(frame));
    memset(response,0xa5,sizeof(response));produced=SIZE_MAX;
    int result=kui_vc2_ce_vmu_write_probe_handle(&card,frame,bytes,response+1u,capacity,&produced);
    CHECK(!memcmp(saved,frame,sizeof(frame)) && response[0]==0xa5u && response[capacity+1u]==0xa5u);
    CHECK(!card.endpoint.commits);guards();
    if(result==KUI_ARMADA_CE_VMU_WRITE_HANDLED) {
        CHECK(produced>=4u && produced<=capacity && produced==4u+4u*response[4]);
        CHECK(response[2]==0u && response[3]==2u);
    } else CHECK(!produced);
    return result;
}
static void phase(unsigned block,unsigned p) {
    CHECK(run(request(12u,block,p),524u)==1);
    CHECK(produced==4u && response[1]==7u);
}
static void complete_stage(unsigned block) {for(unsigned p=0u;p<4u;++p)phase(block,p);}
static void snapshot(uint32_t words[20]) {
    uint32_t out[22];for(unsigned i=0u;i<22u;++i)out[i]=UINT32_C(0xabababab);
    kui_vc2_ce_vmu_write_probe_snapshot(&card,out+1u);
    CHECK(out[0]==UINT32_C(0xabababab) && out[21]==UINT32_C(0xabababab));
    memcpy(words,out+1u,80u);
}
static void prior_read_contract(void) {
    fresh();
    struct kui_armada_ce_vmu_readonly old={0};CHECK(!kui_armada_ce_vmu_readonly_init(&old));
    const unsigned commands[]={1u,2u,10u,11u};
    for(unsigned i=0u;i<4u;++i) {
        unsigned bytes=request(commands[i],commands[i]==11u?255u:0u,0u);size_t n=0u;
        CHECK(kui_armada_ce_vmu_readonly_handle(&old,frame,bytes,baseline,sizeof(baseline),&n)==1);
        CHECK(run(bytes,524u)==1 && produced==n && !memcmp(response+1u,baseline,n));
    }
    for(unsigned block=0u;block<256u;++block) {
        CHECK(run(request(11u,block,0u),524u)==1 && produced==524u);
        CHECK(!kui_vmu_image_blank_block((uint16_t)block,baseline));
        CHECK(!memcmp(response+13u,baseline,512u));
        for(unsigned p=0u;p<=1u;++p) {
            CHECK(run(request(13u,block,p),4u)==1 && produced==4u && response[1]==7u);
        }
    }
    uint32_t words[20];snapshot(words);
    CHECK(words[0]==KUI_VC2_VMU_WRITE_PROBE_MAGIC && words[1]==1u && words[3]==257u);
    CHECK(!words[4] && !words[5] && !words[9] && !words[14]);
}
static void full_stage_and_no_commit_ack(void) {
    fresh();
    for(unsigned p=0u;p<4u;++p) {
        phase(42u,p);CHECK(card.endpoint.protocol.phases==(1u<<(p+1u))-1u);
        CHECK(!memcmp(card.endpoint.protocol.staged,data,(p+1u)*128u));
        CHECK(!card.endpoint.protocol.commit_attempted && !card.commit_attempts);
        CHECK(!kui_vc2_ce_vmu_write_probe_pending(&card));
    }
    /* A READ still sees the generated committed card, never volatile staging. */
    CHECK(run(request(11u,42u,0u),524u)==1);
    CHECK(!kui_vmu_image_blank_block(42u,baseline) && !memcmp(response+13u,baseline,512u));
    request(13u,42u,4u);struct kui_vc2_ce_vmu_write_probe before=card;
    CHECK(run(12u,7u)==KUI_ARMADA_CE_VMU_WRITE_CAPACITY);
    CHECK(!memcmp(&before,&card,sizeof(card)));
    CHECK(run(12u,8u)==KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED && !produced);
    CHECK(response[1]==0xfcu && kui_vc2_ce_vmu_write_probe_pending(&card));
    CHECK(card.commit_attempts==1u && card.endpoint.protocol.commit_attempted);
    CHECK(card.endpoint.protocol.active && card.endpoint.protocol.phases==15u);
    CHECK(!memcmp(card.endpoint.protocol.staged,data,512u));
    uint32_t words[20];snapshot(words);
    CHECK(words[4]==4u && words[5]==1u && words[6]==42u && words[7]==15u && words[9]==1u);
    /* zlib.crc32(bytes(range(256))*2), independent of the probe's CRC loop. */
    CHECK(words[8]==UINT32_C(0x1c613576));
    for(unsigned i=10u;i<14u;++i)CHECK(words[i]==1u);
    CHECK(!words[14] && words[15]==1u && !words[16]);
    CHECK(words[17]==UINT32_C(0x0200020d) && words[18]==UINT32_C(0x2a000400) && words[19]==8u);
    before=card;CHECK(run(request(13u,42u,4u),8u)==2 && !produced);
    CHECK(!memcmp(&before,&card,sizeof(card)) && card.commit_attempts==1u);
    CHECK(run(request(13u,42u,4u),7u)==KUI_ARMADA_CE_VMU_WRITE_CAPACITY);
    CHECK(!memcmp(&before,&card,sizeof(card)));
    CHECK(run(request(12u,42u,0u),524u)==0 && !produced);
    CHECK(run(request(13u,43u,4u),524u)==0 && !produced);
    CHECK(run(request(11u,42u,0u),524u)==0 && !produced);
    CHECK(!memcmp(card.endpoint.protocol.staged,data,512u) && card.commit_attempts==1u);
    CHECK(kui_vc2_ce_vmu_write_probe_pending(&card));
}
static void order_retries_and_invalid_sync(void) {
    fresh();
    CHECK(run(request(13u,42u,4u),524u)==1 && response[1]==0xfbu && response[8]==2u);
    CHECK(run(request(12u,42u,1u),524u)==1 && response[1]==0xfbu && !card.endpoint.protocol.active);
    phase(42u,0u);phase(42u,1u);phase(42u,0u);
    CHECK(card.endpoint.protocol.phases==3u);
    CHECK(run(request(12u,42u,3u),524u)==1 && response[1]==0xfbu && card.endpoint.protocol.phases==3u);
    uint8_t immutable[512];memcpy(immutable,card.endpoint.protocol.staged,512u);
    request(12u,42u,1u);frame[12]^=1u;
    CHECK(run(140u,524u)==1 && response[1]==0xfbu && !memcmp(immutable,card.endpoint.protocol.staged,512u));
    CHECK(run(request(12u,43u,2u),524u)==1 && response[1]==0xfbu);
    CHECK(run(request(13u,42u,4u),524u)==1 && response[1]==0xfbu);
    CHECK(!card.commit_attempts && !card.endpoint.protocol.commit_attempted);
    phase(42u,2u);phase(42u,2u);phase(42u,3u);
    CHECK(card.endpoint.protocol.phases==15u && !memcmp(card.endpoint.protocol.staged,data,512u));
    CHECK(run(request(13u,43u,4u),524u)==1 && response[1]==0xfbu && !card.commit_attempts);
    CHECK(run(request(13u,42u,3u),524u)==0 && !card.commit_attempts);
    CHECK(run(request(13u,42u,4u),524u)==2 && card.commit_attempts==1u);
    fresh();phase(42u,0u);phase(42u,1u);phase(43u,0u);
    CHECK(card.endpoint.protocol.staged_block==43u && card.endpoint.protocol.phases==1u);
    phase(43u,1u);request(12u,43u,0u);frame[12]^=1u;
    CHECK(run(140u,524u)==1 && response[1]==7u && card.endpoint.protocol.phases==1u);
}
static void malformed_shapes_and_capacities(void) {
    fresh();
    const unsigned commands[]={1u,2u,10u,11u,12u,13u,13u};
    const unsigned phases[]={0u,0u,0u,0u,0u,0u,4u};
    const unsigned needed[]={116u,196u,32u,524u,8u,4u,8u};
    for(unsigned k=0u;k<7u;++k) {
        CHECK(run(request(11u,42u,0u),524u)==1);
        unsigned bytes=request(commands[k],commands[k]==10u?0u:42u,phases[k]);
        for(unsigned cap=0u;cap<needed[k];++cap) {
            struct kui_vc2_ce_vmu_write_probe before=card;
            CHECK(run(bytes,cap)==-2 && !memcmp(&before,&card,sizeof(card)));
            for(unsigned i=0u;i<sizeof(response);++i)CHECK(response[i]==0xa5u);
        }
    }
    const unsigned byte_at[]={1u,2u,3u,7u,8u,9u,10u};
    for(unsigned k=0u;k<7u;++k) {
        fresh();phase(42u,0u);phase(42u,1u);
        uint8_t immutable[512];memcpy(immutable,card.endpoint.protocol.staged,512u);
        request(12u,42u,2u);frame[byte_at[k]]=0xffu;
        CHECK(run(140u,524u)==0 && !memcmp(immutable,card.endpoint.protocol.staged,512u));
        CHECK(!card.commit_attempts && !kui_vc2_ce_vmu_write_probe_pending(&card));
    }
    for(unsigned bytes=0u;bytes<144u;++bytes) {
        if(bytes==140u)continue;
        fresh();request(12u,42u,0u);
        CHECK(run(bytes,524u)==0 && !card.endpoint.protocol.active && !card.commit_attempts);
    }
    fresh();complete_stage(42u);
    for(unsigned command=0u;command<256u;++command) {
        if(command==1u || command==2u || (command>=10u && command<=13u))continue;
        request(13u,42u,4u);frame[0]=(uint8_t)command;
        CHECK(run(12u,524u)==0 && card.endpoint.protocol.phases==15u && !card.commit_attempts);
    }
    CHECK(run(request(13u,42u,4u),524u)==2 && card.commit_attempts==1u);
}
static void invalid_spans_and_snapshot(void) {
    fresh();unsigned bytes=request(1u,0u,0u);produced=123u;
    struct kui_vc2_ce_vmu_write_probe before=card;
    CHECK(kui_vc2_ce_vmu_write_probe_handle(NULL,frame,bytes,response,524u,&produced)==-1 && produced==123u);
    CHECK(kui_vc2_ce_vmu_write_probe_handle(&card,(const void *)(UINTPTR_MAX-2u),140u,response,524u,&produced)==-1);
    CHECK(kui_vc2_ce_vmu_write_probe_handle(&card,frame,bytes,response,SIZE_MAX,&produced)==-1);
    CHECK(kui_vc2_ce_vmu_write_probe_handle(&card,frame,bytes,(void *)&card,8u,&produced)==-1);
    CHECK(kui_vc2_ce_vmu_write_probe_handle(&card,(void *)&card,4u,response,524u,&produced)==-1);
    CHECK(kui_vc2_ce_vmu_write_probe_handle(&card,frame,bytes,response,524u,(void *)&card)==-1);
    CHECK(kui_vc2_ce_vmu_write_probe_handle(&card,frame,bytes,frame,4u,&produced)==-1);
    CHECK(produced==123u && !memcmp(&before,&card,sizeof(card)));
    kui_vc2_ce_vmu_write_probe_snapshot(&card,(void *)&card);
    CHECK(!memcmp(&before,&card,sizeof(card)));
    uint32_t words[20];memset(words,0xa5,sizeof(words));
    kui_vc2_ce_vmu_write_probe_snapshot(NULL,words);
    for(unsigned i=0u;i<20u;++i)CHECK(!words[i]);
    memset(&card,0,sizeof(card));CHECK(!kui_vc2_ce_vmu_write_probe_pending(&card));
    snapshot(words);for(unsigned i=0u;i<20u;++i)CHECK(!words[i]);
    guards();
}
int main(void) {
    prior_read_contract();full_stage_and_no_commit_ack();order_retries_and_invalid_sync();
    malformed_shapes_and_capacities();invalid_spans_and_snapshot();
    printf("PASS VC2 volatile phase probe: %u checks; exact prior blank reads/info, four128-byte ACKs, immutable full512-byte capture and CRC, phase4 DEFERRED/no final ACK/no SD, retries/order/malformed/capacity/alias guards\n",checks);
    return 0;
}
