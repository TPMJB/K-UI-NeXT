/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_readonly.h"
#include "kui/vmu_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if KUI_ARMADA_CE_VMU_READONLY_HOST_TEST
static struct kui_armada_ce_vmu_readonly card;
static struct kui_vmu_image_workspace image_work;
static unsigned source_reads;
static int source_result;

static void packet(uint8_t out[140],unsigned command,unsigned block,unsigned phase) {
    memset(out,0,140u);out[0]=(uint8_t)command;out[1]=2u;
    if(command>=10u && command<=13u) {
        out[3]=(uint8_t)(command==12u?34u:2u);out[7]=2u;
        out[9]=(uint8_t)phase;out[10]=(uint8_t)(block>>8);out[11]=(uint8_t)block;
    }
}
static int image_reader(void *context,uint16_t block,uint8_t out[512]) {
    assert(context==&source_reads);++source_reads;
    for(unsigned i=0;i<512u;i++) out[i]=(uint8_t)(block^(i*7u));
    return source_result;
}
static void fresh(int fixed) {
    source_reads=0;source_result=0;
    assert(!(fixed?kui_armada_ce_vmu_readonly_init_reader(&card,image_reader,&source_reads):
        kui_armada_ce_vmu_readonly_init(&card)));
    assert(!source_reads && !card.read_valid && !card.faulted);
}
static void read_success(unsigned block,uint8_t response[524]) {
    uint8_t request[140];size_t bytes=0;
    packet(request,11u,block,0);
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,response,524u,&bytes)==1);
    assert(bytes==524u && response[0]==8u && !response[1] && response[2]==2u && response[3]==130u);
    assert(!memcmp(response+4u,request+4u,8u));
    assert(card.read_valid && card.read_block==block && !card.backend.commit_attempts);
}
static void finalization(unsigned block,unsigned phase) {
    uint8_t request[140],response[8];size_t bytes=99u;
    packet(request,13u,block,phase);memset(response,0xa5,sizeof(response));
    unsigned calls=source_reads;
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,response,4u,&bytes)==1);
    assert(bytes==4u && !memcmp(response,"\7\0\2\0",4u));
    for(unsigned i=4u;i<8u;i++) assert(response[i]==0xa5u);
    assert(source_reads==calls && !card.backend.commit_attempts && card.read_valid);
}
static void refused(const uint8_t *request,size_t request_bytes) {
    uint8_t response[524];size_t bytes=99u;
    unsigned reads=source_reads,successes=card.requests,finals=card.read_finalizations;
    kui_vmu_emulation_t protocol=card.backend.protocol;
    memset(response,0xa5,sizeof(response));
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,request_bytes,
        response,sizeof(response),&bytes)==0);
    assert(!bytes && source_reads==reads && card.requests==successes &&
        card.read_finalizations==finals && !card.backend.commit_attempts);
    assert(!memcmp(&protocol,&card.backend.protocol,sizeof(protocol)));
    for(unsigned i=0;i<sizeof(response);i++) assert(response[i]==0xa5u);
}
static int via_protocol(void *context,uint16_t block,void *out) {
    assert(context==&card);uint8_t response[524];
    read_success(block,response);memcpy(out,response+12u,512u);
    finalization(block,0u);return 0;
}

static void a2_metadata_and_canonical_blank(void) {
    uint8_t request[140],response[526];size_t bytes;
    const unsigned commands[]={1u,2u,10u},lengths[]={116u,196u,32u};
    fresh(1);
    for(unsigned i=0;i<3u;i++) {
        packet(request,commands[i],0,0);memset(response,0xa5,sizeof(response));
        assert(kui_armada_ce_vmu_readonly_handle(&card,request,i<2u?4u:12u,
            response+1u,lengths[i],&bytes)==1 && bytes==lengths[i]);
        assert(response[0]==0xa5u && response[lengths[i]+1u]==0xa5u);
        assert(response[1]==(i<2u?commands[i]+4u:8u) && !response[2] && response[3]==2u);
        assert(response[4]==(lengths[i]-4u)/4u && !memcmp(response+5u,"\0\0\0\2",4u));
        assert(!source_reads && !card.read_valid && !card.backend.commit_attempts);
    }
    /* Standard200-block geometry, expressed in the response's media fields. */
    assert(response[9]==255u && !response[10]);
    assert(response[13]==255u && response[15]==254u);
    assert(response[19]==253u && response[21]==13u && response[25]==200u);
    fresh(0);
    struct kui_vmu_image_summary summary;
    const struct kui_vmu_image_io io={&card,via_protocol,NULL,NULL};
    assert(!kui_vmu_image_validate(&io,131072u,&image_work,&summary));
    assert(!summary.files && !summary.game_files && !summary.used_blocks && summary.free_blocks==200u);
    assert(card.reads && card.reads==card.read_finalizations && !source_reads && !card.backend.commit_attempts);
    read_success(255u,response);
    for(unsigned i=0;i<16u;i++) assert(response[12u+i]==0x55u);
    assert(response[12u+0x46u]==254u && response[12u+0x50u]==200u);
    finalization(255u,0u);finalization(255u,0u);finalization(255u,1u);
}

static void read_finalization_belongs_to_successful_block(void) {
    uint8_t request[140],response[524];
    fresh(1);packet(request,13u,42u,0);refused(request,12u);
    read_success(42u,response);assert(source_reads==1u);
    for(unsigned i=0;i<512u;i++) assert(response[12u+i]==(uint8_t)(42u^(i*7u)));
    finalization(42u,0u);finalization(42u,1u);
    packet(request,13u,43u,0u);refused(request,12u);assert(!card.read_valid);
    packet(request,13u,42u,0u);refused(request,12u);
    read_success(43u,response);packet(request,13u,42u,0u);refused(request,12u);
    read_success(42u,response);packet(request,11u,42u,1u);refused(request,12u);
    packet(request,13u,42u,0u);refused(request,12u);
    read_success(42u,response);packet(request,11u,256u,0u);refused(request,12u);
    packet(request,13u,42u,0u);refused(request,12u);
    for(unsigned cap=0;cap<524u;cap++) {
        size_t bytes=99u;read_success(42u,response);packet(request,11u,42u,0u);
        unsigned calls=source_reads;
        assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,response,cap,&bytes)==-2);
        assert(!bytes && source_reads==calls && !card.read_valid);
        packet(request,13u,42u,0u);refused(request,12u);
    }
    /* Transient output shortage for the final ACK is retry-safe. */
    read_success(42u,response);packet(request,13u,42u,0u);
    for(unsigned cap=0;cap<4u;cap++) {
        size_t bytes=99u;
        assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,response,cap,&bytes)==-2);
        assert(!bytes && card.read_valid);
    }
    finalization(42u,0u);
    source_result=-1;packet(request,11u,42u,0u);size_t bytes=99u;
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,response,sizeof(response),&bytes)==-3);
    assert(!bytes && card.faulted && !card.read_valid);
    unsigned calls=source_reads;packet(request,13u,42u,0u);
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,response,sizeof(response),&bytes)==-3);
    assert(!bytes && source_reads==calls && !card.backend.commit_attempts);
}

static void all_mutations_stay_readonly(void) {
    uint8_t request[140],response[524];
    fresh(1);
    for(unsigned command=3u;command<=4u;command++) {
        read_success(42u,response);packet(request,command,0u,0u);refused(request,4u);
        assert(!card.read_valid);
    }
    uint8_t lcd[204]={12u,2u,0u,50u,0u,0u,0u,4u};
    read_success(42u,response);refused(lcd,sizeof(lcd));assert(!card.read_valid);
    for(unsigned command=0;command<256u;command++) {
        if(command==1u || command==2u || command==10u || command==11u || command==13u) continue;
        read_success(42u,response);packet(request,command,42u,0u);
        refused(request,command==12u?140u:12u);assert(!card.read_valid);
        packet(request,13u,42u,0u);refused(request,12u);
    }
    /* Exact phase4 write commit is never acknowledged, even after a read. */
    read_success(42u,response);packet(request,13u,42u,4u);refused(request,12u);
    assert(!card.read_valid && !card.backend.protocol.active && !card.backend.protocol.killed);
    for(unsigned phase=0;phase<2u;phase++) for(unsigned at=1u;at<12u;at++) {
        if(at==9u) continue;
        read_success(42u,response);packet(request,13u,42u,phase);request[at]^=1u;
        refused(request,12u);
    }
    for(unsigned length=0;length<16u;length++) {
        if(length==12u) continue;
        read_success(42u,response);packet(request,13u,42u,0u);refused(request,length);
    }
    /* Physical A1 and nonhost frames never alter the A2 read proof. */
    read_success(42u,response);packet(request,11u,42u,0u);request[1]=1u;
    refused(request,12u);assert(card.read_valid);finalization(42u,0u);
    request[1]=2u;request[2]=0x40u;refused(request,12u);assert(card.read_valid);
    finalization(42u,0u);
    assert(!card.backend.commit_attempts && !card.backend.protocol.active && !card.backend.protocol.killed);
}

static void metadata_capacity_and_invalid_api(void) {
    uint8_t request[140],response[524];size_t bytes=99u;
    const unsigned commands[]={1u,2u,10u},lengths[]={116u,196u,32u};
    fresh(1);
    for(unsigned i=0;i<3u;i++) {
        packet(request,commands[i],0,0);
        for(unsigned cap=0;cap<lengths[i];cap++) {
            struct kui_armada_ce_vmu_readonly before=card;
            memset(response,0xa5,sizeof(response));bytes=99u;
            assert(kui_armada_ce_vmu_readonly_handle(&card,request,i<2u?4u:12u,
                response,cap,&bytes)==-2);
            assert(!bytes && !memcmp(&before,&card,sizeof(card)) && !source_reads);
            for(unsigned j=0;j<sizeof(response);j++) assert(response[j]==0xa5u);
        }
    }
    packet(request,11u,0u,0u);
    assert(kui_armada_ce_vmu_readonly_handle(NULL,request,12u,response,sizeof(response),&bytes)==-1);
    assert(kui_armada_ce_vmu_readonly_handle(&card,NULL,12u,response,sizeof(response),&bytes)==-1);
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,NULL,sizeof(response),&bytes)==-1);
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,response,sizeof(response),NULL)==-1);
    struct kui_armada_ce_vmu_readonly before=card;
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,(uint8_t *)&card,524u,&bytes)==-1);
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,response,sizeof(response),(size_t *)&card)==-1);
    assert(!memcmp(&before,&card,sizeof(card)));
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,12u,request,sizeof(request),&bytes)==-1);
    assert(kui_armada_ce_vmu_readonly_handle(&card,request,SIZE_MAX,response,sizeof(response),&bytes)==-1);
    struct kui_armada_ce_vmu_readonly moved=card;
    assert(kui_armada_ce_vmu_readonly_handle(&moved,request,12u,response,sizeof(response),&bytes)==-1);
    assert(kui_armada_ce_vmu_readonly_init(NULL)==-1);
    assert(kui_armada_ce_vmu_readonly_init_reader(&card,NULL,NULL)==-1);
    assert(!memcmp(&before,&card,sizeof(card)) && !source_reads);
}

int main(void) {
    a2_metadata_and_canonical_blank();read_finalization_belongs_to_successful_block();
    all_mutations_stay_readonly();metadata_capacity_and_invalid_api();
    puts("Armada CE A2 read-only protocol: canonical blank filesystem, owner phase0/1 read finalization, failed-read revocation, all mutating-command refusals, private output bounds and fixed-reader faults passed");
    return 0;
}
#else
static unsigned unexpected_reads;
static int unexpected_reader(void *context,uint16_t block,uint8_t out[512]) {
    (void)context;(void)block;(void)out;++unexpected_reads;return -1;
}
int main(void) {
    struct kui_armada_ce_vmu_readonly s;
    uint8_t request[12]={11u,2u,0u,2u,0u,0u,0u,2u,0u,0u,0u,255u},out[524];
    size_t bytes=0;
    assert(!kui_armada_ce_vmu_readonly_init(&s));
    assert(kui_armada_ce_vmu_readonly_handle(&s,request,12u,out,sizeof(out),&bytes)==1);
    assert(bytes==524u && out[2]==2u && out[12]==0x55u && !unexpected_reads);
    request[0]=13u;
    assert(kui_armada_ce_vmu_readonly_handle(&s,request,12u,out,sizeof(out),&bytes)==1);
    assert(bytes==4u && !memcmp(out,"\7\0\2\0",4u));
    /* Inject a corrupted private optional-reader witness. The production
     * wrapper must stop before the retained native indirect reader branch. */
    s.backend.reader=unexpected_reader;request[0]=11u;
    assert(kui_armada_ce_vmu_readonly_handle(&s,request,12u,out,sizeof(out),&bytes)==-3);
    assert(!bytes && !unexpected_reads && s.faulted && !s.read_valid);
    assert(!kui_armada_ce_vmu_readonly_init(&s));
    s.backend.reader_context=&unexpected_reads;
    assert(kui_armada_ce_vmu_readonly_handle(&s,request,12u,out,sizeof(out),&bytes)==-3);
    assert(!bytes && !unexpected_reads && s.faulted && !s.read_valid);
    puts("Armada CE A2 production blank-only reader guards and owner phase0 ACK passed");
    return 0;
}
#endif
