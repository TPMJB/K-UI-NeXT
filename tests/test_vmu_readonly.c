/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_readonly.h"
#include "kui/vmu_image.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct kui_vmu_readonly card;
/* These host-only reference bytes are never part of the backend state. */
static uint8_t formatted[256][512];
static struct kui_vmu_image_workspace workspace;
static unsigned memory_writes,memory_syncs;

static int memory_read(void *ctx,uint16_t block,void *out) {
    assert(ctx==formatted && block<256u);
    memcpy(out,formatted[block],512u);return 0;
}
static int memory_write(void *ctx,uint16_t block,const void *in) {
    assert(ctx==formatted && block<256u);++memory_writes;
    memcpy(formatted[block],in,512u);return 0;
}
static int memory_sync(void *ctx) {assert(ctx==formatted);++memory_syncs;return 0;}

static void request(uint8_t out[12],unsigned command,unsigned block) {
    memset(out,0,12u);out[0]=(uint8_t)command;out[1]=1u;
    if(command==10u || command==11u) {
        out[3]=2u;out[7]=2u;out[11]=(uint8_t)block;
    }
}

static int through_protocol(void *ctx,uint16_t block,void *out) {
    struct kui_vmu_readonly *s=ctx;
    uint8_t frame[12],reply[524];size_t n=0;
    request(frame,11u,block);
    if(kui_vmu_readonly_handle(s,frame,sizeof(frame),reply,sizeof(reply),&n)!=1 || n!=524u)
        return -1;
    assert(reply[0]==8u && reply[1]==0u && reply[2]==1u && reply[3]==130u);
    assert(!memcmp(reply+4,frame+4,8u));memcpy(out,reply+12,512u);return 0;
}

static void blank_generator_and_protocol_reads(void) {
    const struct kui_vmu_image_io io={formatted,memory_read,memory_write,memory_sync};
    uint8_t block[514],response[526],frame[12];size_t n;
    memset(formatted,0,sizeof(formatted));
    assert(kui_vmu_image_format_blank(&io,sizeof(formatted),NULL,&workspace)==0);
    assert(memory_writes==256u && memory_syncs==2u);
    assert(kui_vmu_readonly_init(&card)==0 && sizeof(card)<=2048u);
    for(unsigned i=0;i<256u;i++) {
        memset(block,0x5a,sizeof(block));
        assert(kui_vmu_image_blank_block((uint16_t)i,block+1)==0);
        assert(block[0]==0x5au && block[513]==0x5au);
        assert(!memcmp(block+1,formatted[i],512u));
        request(frame,11u,i);memset(response,0xa5,sizeof(response));n=999u;
        assert(kui_vmu_readonly_handle(&card,frame,12u,response+1,524u,&n)==1 && n==524u);
        assert(response[0]==0xa5u && response[525]==0xa5u);
        assert(response[1]==8u && response[2]==0u && response[3]==1u && response[4]==130u);
        assert(!memcmp(response+5,frame+4,8u));
        assert(!memcmp(response+13,formatted[i],512u));
    }
    assert(card.reads==256u && card.requests==256u && !card.commit_attempts);
    struct kui_vmu_image_summary summary;
    const struct kui_vmu_image_io protocol_io={&card,through_protocol,NULL,NULL};
    assert(kui_vmu_image_validate(&protocol_io,131072u,&workspace,&summary)==0);
    assert(!summary.files && !summary.game_files && !summary.used_blocks && summary.free_blocks==200u);
    assert(card.reads==526u && card.requests==526u && !card.commit_attempts && !card.faulted);
    assert(memory_writes==256u && memory_syncs==2u); /* Protocol never enters formatter I/O. */
    memset(block,0x5a,sizeof(block));
    assert(kui_vmu_image_blank_block(256u,block+1)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_blank_block(UINT16_MAX,block+1)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_blank_block(0u,NULL)==KUI_VMU_IMAGE_ARGUMENT);
    for(unsigned i=0;i<sizeof(block);i++) assert(block[i]==0x5au);
}

static void information_shapes(void) {
    uint8_t frame[12],response[524];size_t n;
    assert(kui_vmu_readonly_init(&card)==0);
    const unsigned commands[]={1u,2u,10u};
    const unsigned lengths[]={116u,196u,32u};
    for(unsigned i=0;i<3u;i++) {
        request(frame,commands[i],0u);memset(response,0xa5,sizeof(response));
        assert(kui_vmu_readonly_handle(&card,frame,i<2u?4u:12u,response,sizeof(response),&n)==1);
        assert(n==lengths[i] && response[0]==(i==0u?5u:i==1u?6u:8u));
        assert(response[1]==0u && response[2]==1u && response[3]==(lengths[i]-4u)/4u);
        assert(!memcmp(response+4,"\0\0\0\2",4u));
        assert(response[n]==0xa5u);
        if(commands[i]==1u || commands[i]==2u) {
            static const char name[]="K-UI Read-Only Blank";
            assert(!memcmp(response+22u,name,sizeof(name)-1u));
            for(unsigned byte=22u+sizeof(name)-1u;byte<52u;byte++) assert(response[byte]==' ');
        }
        if(commands[i]==2u) {
            static const char version[]="K-UI read-only synthetic blank card; no persistence";
            assert(!memcmp(response+116u,version,sizeof(version)-1u));
            for(unsigned byte=116u+sizeof(version)-1u;byte<196u;byte++) assert(response[byte]==' ');
        }
    }
    assert(card.requests==3u && !card.reads && !card.commit_attempts);
}

static void refused(const uint8_t *frame,size_t bytes) {
    uint8_t response[524];size_t n=999u;
    kui_vmu_emulation_t before=card.protocol;
    uint32_t reads=card.reads,requests=card.requests,refusals=card.refused;
    memset(response,0xa5,sizeof(response));
    assert(kui_vmu_readonly_handle(&card,frame,bytes,response,sizeof(response),&n)==KUI_VMU_READONLY_REFUSED);
    assert(n==0u && card.refused==refusals+1u && card.reads==reads && card.requests==requests);
    assert(!card.commit_attempts && !memcmp(&before,&card.protocol,sizeof(before)));
    for(unsigned i=0;i<sizeof(response);i++) assert(response[i]==0xa5u);
}

static void rejects_mutations_and_unknowns_before_protocol(void) {
    uint8_t frame[204];assert(kui_vmu_readonly_init(&card)==0);
    for(unsigned command=0;command<256u;command++) {
        if(command==1u || command==2u || command==10u || command==11u) continue;
        request(frame,11u,0u);frame[0]=(uint8_t)command;refused(frame,12u);
    }
    /* A real LCD update is CMD12 with 50words, never a storage request. */
    memset(frame,0,sizeof(frame));frame[0]=12u;frame[1]=1u;frame[3]=50u;frame[7]=4u;
    refused(frame,sizeof(frame));
    /* Complete storage writes and final sync still never enter the protocol. */
    memset(frame,0x5a,sizeof(frame));frame[0]=12u;frame[1]=1u;frame[2]=0u;frame[3]=34u;
    refused(frame,140u);
    request(frame,11u,0u);frame[0]=13u;frame[9]=4u;refused(frame,12u);
    assert(!card.protocol.active && !card.protocol.killed && !card.protocol.read_valid);
}

static void strict_packet_fields(void) {
    uint8_t frame[16];assert(kui_vmu_readonly_init(&card)==0);
    for(unsigned command=1u;command<=11u;command++) {
        if(command!=1u && command!=2u && command!=10u && command!=11u) continue;
        size_t expected=command<3u?4u:12u;
        for(size_t bytes=0;bytes<=16u;bytes++) {
            if(bytes==expected) continue;
            uint8_t *short_frame=malloc(bytes?bytes:1u);assert(short_frame);
            request(frame,command,0u);memset(frame+12,0,4u);
            memcpy(short_frame,frame,bytes);refused(short_frame,bytes);free(short_frame);
        }
        for(unsigned words=0;words<256u;words++) {
            if(words==(command<3u?0u:2u)) continue;
            request(frame,command,0u);frame[3]=(uint8_t)words;refused(frame,expected);
        }
        for(unsigned address=0;address<256u;address++) {
            request(frame,command,0u);frame[1]=(uint8_t)address;
            if(address!=1u) refused(frame,expected);
            request(frame,command,0u);frame[2]=(uint8_t)address;
            if(address!=0u) refused(frame,expected);
        }
    }
    for(unsigned command=10u;command<=11u;command++) {
        for(unsigned byte=4u;byte<12u;byte++) {
            if(command==11u && byte==11u) continue; /* Every low block byte is valid. */
            request(frame,command,0u);frame[byte]^=1u;refused(frame,12u);
        }
        request(frame,command,0u);frame[4]=2u;frame[7]=0u;refused(frame,12u); /* Wrong endian. */
        request(frame,command,0u);frame[7]=4u;refused(frame,12u); /* LCD. */
    }
}

static void capacities_and_fault_sentinel(void) {
    uint8_t frame[12],response[524],zero[512]={0};size_t n;
    const unsigned commands[]={1u,2u,10u,11u},required[]={116u,196u,32u,524u};
    assert(kui_vmu_readonly_init(&card)==0);
    for(unsigned i=0;i<4u;i++) {
        request(frame,commands[i],0u);
        for(unsigned cap=0;cap<required[i];cap++) {
            struct kui_vmu_readonly before=card;memset(response,0xa5,sizeof(response));n=999u;
            assert(kui_vmu_readonly_handle(&card,frame,i<2u?4u:12u,response,cap,&n)==KUI_VMU_READONLY_CAPACITY);
            assert(n==0u && !memcmp(&before,&card,sizeof(before)));
            for(unsigned byte=0;byte<sizeof(response);byte++) assert(response[byte]==0xa5u);
        }
    }
    assert(card.protocol.io.commit_block(card.protocol.io.context,0u,zero)==KUI_VMU_EMU_IO_ERROR);
    assert(card.commit_attempts==1u && card.faulted);
    request(frame,1u,0u);memset(response,0xa5,sizeof(response));n=999u;
    assert(kui_vmu_readonly_handle(&card,frame,4u,response,sizeof(response),&n)==KUI_VMU_READONLY_INTERNAL);
    assert(!n && !card.requests && !card.reads && card.commit_attempts==1u);
    for(unsigned byte=0;byte<sizeof(response);byte++) assert(response[byte]==0xa5u);
}

static void invalid_api_arguments(void) {
    uint8_t frame[12],response[524];size_t n=999u;struct kui_vmu_readonly unready={0};
    request(frame,1u,0u);assert(kui_vmu_readonly_init(NULL)==KUI_VMU_READONLY_ARGUMENT);
    assert(kui_vmu_readonly_init(&card)==0);
    assert(kui_vmu_readonly_handle(NULL,frame,4u,response,sizeof(response),&n)==KUI_VMU_READONLY_ARGUMENT && !n);
    assert(kui_vmu_readonly_handle(&unready,frame,4u,response,sizeof(response),&n)==KUI_VMU_READONLY_ARGUMENT);
    assert(kui_vmu_readonly_handle(&card,NULL,4u,response,sizeof(response),&n)==KUI_VMU_READONLY_ARGUMENT);
    assert(kui_vmu_readonly_handle(&card,frame,4u,NULL,sizeof(response),&n)==KUI_VMU_READONLY_ARGUMENT);
    assert(kui_vmu_readonly_handle(&card,frame,4u,response,sizeof(response),NULL)==KUI_VMU_READONLY_ARGUMENT);
    assert(!card.requests && !card.reads && !card.refused && !card.commit_attempts);
}

struct readonly_source {unsigned reads;int result;};
static int fixed_read(void *context,uint16_t block,uint8_t out[512]) {
    struct readonly_source *source=context;assert(block<256u);++source->reads;
    for(unsigned i=0;i<512u;i++) out[i]=(uint8_t)(block^i);
    return source->result;
}
static void fixed_image_reader_is_readonly_and_fail_closed(void) {
    struct readonly_source source={0,0};uint8_t frame[12],response[524];size_t n;
    assert(kui_vmu_readonly_init_reader(NULL,fixed_read,&source)==KUI_VMU_READONLY_ARGUMENT);
    assert(kui_vmu_readonly_init_reader(&card,NULL,&source)==KUI_VMU_READONLY_ARGUMENT);
    assert(kui_vmu_readonly_init_reader(&card,fixed_read,&source)==0 && !source.reads);
    request(frame,2u,0u);
    assert(kui_vmu_readonly_handle(&card,frame,4u,response,sizeof(response),&n)==1);
    assert(!memcmp(response+22u,"K-UI Read-Only SD",16u));
    assert(!memcmp(response+116u,"K-UI read-only fixed SD image; no writes or persistence",53u));
    assert(!source.reads);
    request(frame,11u,42u);
    assert(kui_vmu_readonly_handle(&card,frame,12u,response,523u,&n)==KUI_VMU_READONLY_CAPACITY && !source.reads);
    for(unsigned block=0;block<256u;block++) {
        request(frame,11u,block);
        assert(kui_vmu_readonly_handle(&card,frame,12u,response,sizeof(response),&n)==1 && n==524u);
        for(unsigned i=0;i<512u;i++) assert(response[12u+i]==(uint8_t)(block^i));
    }
    assert(source.reads==256u && card.reads==256u && !card.commit_attempts);
    request(frame,11u,0u);frame[0]=12u;refused(frame,12u);assert(source.reads==256u);
    for(unsigned fault=0;fault<2u;fault++) {
        source=(struct readonly_source){0,fault?1:-1};
        assert(kui_vmu_readonly_init_reader(&card,fixed_read,&source)==0);
        request(frame,11u,42u);
        assert(kui_vmu_readonly_handle(&card,frame,12u,response,sizeof(response),&n)==KUI_VMU_READONLY_INTERNAL);
        assert(!n && source.reads==1u && card.faulted && !card.reads && !card.requests);
        assert(kui_vmu_readonly_handle(&card,frame,12u,response,sizeof(response),&n)==KUI_VMU_READONLY_INTERNAL);
        assert(!n && source.reads==1u && !card.commit_attempts); /* No implicit busy retry. */
    }
}

int main(void) {
    blank_generator_and_protocol_reads();information_shapes();
    rejects_mutations_and_unknowns_before_protocol();strict_packet_fields();
    capacities_and_fault_sentinel();invalid_api_arguments();
    fixed_image_reader_is_readonly_and_fail_closed();
    puts("VMU read-only blank-card backend: all blocks, canonical image, strict refusals and commit sentinel passed");
    return 0;
}
