/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);abort();}} while(0)
static struct kui_armada_ce_vmu_write card;
static struct {
    uint8_t bytes[256][512];
    unsigned reads,commits,reenter;
    int read_result,commit_result,write_before_error;
} disk;
static uint8_t reply[526],frame[140];
static size_t produced;

static int read_block(void *context,uint16_t block,uint8_t data[512]) {
    CHECK(context==&disk && block<256u);++disk.reads;
    /* Exercise the documented private-unused-tail behavior on failures. */
    memcpy(data,disk.bytes[block],512u);
    if(disk.reenter) {
        const uint8_t probe[4]={1,2,0,0};uint8_t out[116];size_t n=99u;
        unsigned requests=card.requests;
        CHECK(kui_armada_ce_vmu_write_handle(&card,probe,sizeof(probe),out,sizeof(out),&n)==-3);
        CHECK(!n && card.requests==requests && card.handling==1u);
    }
    return disk.read_result;
}
static int commit_block(void *context,uint16_t block,const uint8_t data[512]) {
    CHECK(context==&disk && block<256u && data==card.protocol.staged);++disk.commits;
    if(disk.reenter) {
        const uint8_t probe[4]={1,2,0,0};uint8_t out[116];size_t n=99u;
        uint8_t immutable[512];memcpy(immutable,data,sizeof(immutable));
        CHECK(kui_armada_ce_vmu_write_handle(&card,probe,sizeof(probe),out,sizeof(out),&n)==-3);
        CHECK(!n && card.handling==1u && !memcmp(immutable,data,sizeof(immutable)));
    }
    if(!disk.commit_result || disk.write_before_error) memcpy(disk.bytes[block],data,512u);
    return disk.commit_result;
}
static struct kui_vmu_emulation_io io(void) {
    const struct kui_vmu_emulation_io result={&disk,read_block,commit_block};return result;
}
static void fresh(void) {
    memset(&card,0,sizeof(card));memset(&disk,0,sizeof(disk));
    for(unsigned b=0;b<256u;b++) for(unsigned i=0;i<512u;i++)
        disk.bytes[b][i]=(uint8_t)(b+i*7u);
    struct kui_vmu_emulation_io callbacks=io();
    CHECK(!kui_armada_ce_vmu_write_init(&card,&callbacks));
    CHECK(card.self==&card && card.protocol.address==2u && !disk.reads && !disk.commits);
}
static unsigned request(unsigned command,unsigned block,unsigned phase,const uint8_t *data) {
    memset(frame,0,sizeof(frame));frame[0]=(uint8_t)command;frame[1]=2u;
    if(command==1u || command==2u) return 4u;
    frame[3]=(uint8_t)(command==12u?34u:2u);frame[7]=2u;
    frame[9]=(uint8_t)phase;frame[10]=(uint8_t)(block>>8);frame[11]=(uint8_t)block;
    if(command==12u && data) memcpy(frame+12u,data,128u);
    return command==12u?140u:12u;
}
static int run(unsigned bytes,size_t capacity) {
    uint8_t original[140];memcpy(original,frame,sizeof(frame));
    memset(reply,0xa5,sizeof(reply));produced=999u;
    int result=kui_armada_ce_vmu_write_handle(&card,frame,bytes,reply+1u,capacity,&produced);
    CHECK(!memcmp(frame,original,sizeof(frame)));
    CHECK(reply[0]==0xa5u && reply[capacity+1u]==0xa5u);
    if(result==1) {
        CHECK(produced>=4u && produced<=capacity && produced==4u+4u*reply[4]);
        CHECK(reply[2]==0u && reply[3]==2u);
    } else CHECK(!produced);
    return result;
}
static void expected(unsigned command,unsigned block,unsigned phase,const uint8_t *data,
    unsigned response,unsigned bytes) {
    unsigned n=request(command,block,phase,data);CHECK(run(n,524u)==(response?1:0));
    CHECK(produced==bytes);if(response) CHECK(reply[1]==response);
}
static void phase(unsigned block,unsigned p,const uint8_t data[512]) {
    expected(12u,block,p,data+p*128u,7u,4u);
}
static void stage(unsigned block,const uint8_t data[512]) {
    for(unsigned p=0;p<4u;p++) phase(block,p,data);
}
static void transaction_order_and_bytes(void) {
    uint8_t data[512],old[512];fresh();
    for(unsigned i=0;i<512u;i++) data[i]=(uint8_t)(i*13u+i/128u);
    memcpy(old,disk.bytes[42],512u);
    expected(13u,42u,4u,NULL,0xfbu,8u);CHECK(reply[8]==2u && !disk.commits);
    expected(12u,42u,1u,data+128u,0xfbu,8u);CHECK(!card.protocol.active);
    phase(42u,0u,data);phase(42u,1u,data);phase(42u,0u,data);
    CHECK(card.protocol.phases==3u);
    expected(12u,42u,3u,data+384u,0xfbu,8u);CHECK(card.protocol.phases==3u);
    expected(12u,43u,2u,data+256u,0xfbu,8u);CHECK(card.protocol.staged_block==42u);
    uint8_t staged[512];memcpy(staged,card.protocol.staged,512u);
    unsigned n=request(12u,42u,1u,data+128u);frame[12]^=1u;
    CHECK(run(n,524u)==1 && reply[1]==0xfbu && !memcmp(staged,card.protocol.staged,512u));
    phase(42u,2u,data);phase(42u,2u,data);phase(42u,3u,data);
    CHECK(card.protocol.phases==15u && !disk.commits);
    expected(11u,42u,0u,NULL,8u,524u);CHECK(!memcmp(reply+13u,old,512u));
    expected(13u,42u,0u,NULL,0u,0u); /* Active write may not be read-finalized. */
}

static void retries_restart_and_commit(void) {
    uint8_t data[512],replacement[512],old[512];fresh();
    memset(data,0x73,sizeof(data));memset(replacement,0x96,sizeof(replacement));
    memcpy(old,disk.bytes[42],512u);
    phase(41u,0u,data);phase(41u,1u,data);phase(42u,0u,data);
    CHECK(card.protocol.phases==1u && card.protocol.staged_block==42u);
    phase(42u,1u,data);phase(42u,0u,replacement);
    CHECK(card.protocol.phases==1u && !memcmp(card.protocol.staged,replacement,128u));
    for(unsigned p=1;p<4u;p++) phase(42u,p,replacement);
    disk.commit_result=1;expected(13u,42u,4u,NULL,0xfcu,4u);
    CHECK(disk.commits==1u && card.protocol.commit_attempted && !card.commits && card.busy==1u);
    CHECK(!memcmp(disk.bytes[42],old,512u));
    expected(12u,42u,0u,data,0xfcu,4u);CHECK(!memcmp(card.protocol.staged,replacement,512u));
    for(unsigned p=0;p<4u;p++) phase(42u,p,replacement);
    disk.commit_result=-1;disk.write_before_error=1;
    expected(13u,42u,4u,NULL,0xfbu,8u);CHECK(reply[8]==8u && card.io_errors==1u);
    CHECK(card.protocol.active && card.protocol.commit_attempted && !card.commits);
    CHECK(!memcmp(disk.bytes[42],replacement,512u)); /* Device succeeded, ACK was lost. */
    expected(12u,43u,0u,data,0xfcu,4u);
    disk.commit_result=0;disk.write_before_error=0;
    expected(13u,42u,4u,NULL,7u,4u);
    CHECK(disk.commits==3u && card.commits==1u && !card.protocol.active);
    expected(13u,42u,4u,NULL,7u,4u);CHECK(disk.commits==3u && card.commits==1u);
    expected(13u,42u,0u,NULL,0u,0u); /* No stale read proof survives commit. */
    expected(11u,42u,0u,NULL,8u,524u);CHECK(!memcmp(reply+13u,replacement,512u));
    for(unsigned p=0;p<2u;p++) expected(13u,42u,p,NULL,7u,4u);
    CHECK(disk.commits==3u && card.read_finalizations==2u);
    for(unsigned b=0;b<256u;b++) {
        for(unsigned i=0;i<512u;i++) data[i]=(uint8_t)(b^i^(i>>8));
        stage(b,data);expected(13u,b,4u,NULL,7u,4u);
        expected(11u,b,0u,NULL,8u,524u);CHECK(!memcmp(reply+13u,data,512u));
    }
}

static void read_finalization_and_reentry(void) {
    uint8_t data[512];memset(data,0x37,sizeof(data));fresh();
    expected(13u,42u,0u,NULL,0u,0u);
    for(unsigned p=0;p<2u;p++) {
        expected(11u,42u,0u,NULL,8u,524u);
        expected(13u,42u,p,NULL,7u,4u);expected(13u,42u,p,NULL,7u,4u);
    }
    unsigned callbacks=disk.reads+disk.commits;
    request(13u,42u,0u,NULL);CHECK(run(12u,4u)==1 && produced==4u);
    CHECK(disk.reads+disk.commits==callbacks);
    request(13u,43u,0u,NULL);CHECK(run(12u,524u)==0 && !card.protocol.read_valid);
    expected(11u,42u,0u,NULL,8u,524u);
    request(11u,42u,0u,NULL);frame[7]=4u;CHECK(run(12u,524u)==0 && !card.protocol.read_valid);
    expected(11u,42u,0u,NULL,8u,524u);
    request(12u,42u,0u,data);frame[3]=33u;CHECK(run(140u,524u)==0 && !card.protocol.read_valid);
    phase(42u,0u,data);expected(11u,42u,0u,NULL,8u,524u);
    request(13u,42u,1u,NULL);CHECK(run(12u,524u)==0 && card.protocol.active && !card.protocol.read_valid);
    for(unsigned p=1;p<4u;p++) phase(42u,p,data);
    expected(11u,42u,0u,NULL,8u,524u);expected(13u,42u,4u,NULL,7u,4u);
    CHECK(!card.protocol.read_valid);request(13u,42u,1u,NULL);CHECK(run(12u,524u)==0);
    disk.read_result=1;expected(11u,42u,0u,NULL,0xfcu,4u);CHECK(!card.protocol.read_valid);
    disk.read_result=-1;expected(11u,42u,0u,NULL,0xfbu,8u);CHECK(!card.protocol.read_valid);
    disk.read_result=0;disk.reenter=1;expected(11u,42u,0u,NULL,8u,524u);
    CHECK(card.protocol.read_valid && !card.handling);
    stage(43u,data);expected(13u,43u,4u,NULL,7u,4u);CHECK(card.commits==2u && !card.handling);
}

static void shapes_and_capacity(void) {
    uint8_t data[512];memset(data,0x41,sizeof(data));fresh();
    const unsigned commands[]={1u,2u,10u,11u,12u,13u,13u};
    const unsigned phases[]={0u,0u,0u,0u,0u,0u,4u};
    const unsigned needed[]={116u,196u,32u,524u,8u,4u,8u};
    for(unsigned k=0;k<sizeof(commands)/sizeof(commands[0]);k++) {
        expected(11u,42u,0u,NULL,8u,524u);
        unsigned n=request(commands[k],commands[k]==10u?0u:42u,phases[k],data);
        for(unsigned cap=0;cap<needed[k];cap++) {
            struct kui_armada_ce_vmu_write before=card;
            unsigned calls=disk.reads+disk.commits;
            CHECK(run(n,cap)==-2 && !memcmp(&before,&card,sizeof(card)));
            CHECK(calls==disk.reads+disk.commits);
            for(unsigned j=0;j<sizeof(reply);j++) CHECK(reply[j]==0xa5u);
        }
    }
    for(unsigned cmd=0;cmd<256u;cmd++) {
        if(cmd==1u || cmd==2u || (cmd>=10u && cmd<=13u)) continue;
        unsigned n=request(cmd,42u,0u,data);unsigned calls=disk.reads+disk.commits;
        CHECK(run(n,524u)==0 && calls==disk.reads+disk.commits);
    }
    for(unsigned command=10u;command<=13u;command++) for(unsigned field=0;field<6u;field++) {
        unsigned n=request(command,command==10u?0u:42u,command==13u?4u:0u,data);
        if(field==0u) frame[3]^=1u;
        if(field==1u) frame[7]=4u;
        if(field==2u) frame[8]=1u;
        if(field==3u) frame[9]=(uint8_t)(command==12u?4u:command==13u?3u:1u);
        if(field==4u) frame[10]=1u;
        if(field==5u) n--;
        uint8_t before[512];memcpy(before,card.protocol.staged,512u);
        unsigned calls=disk.reads+disk.commits;
        CHECK(run(n,524u)==0 && calls==disk.reads+disk.commits);
        CHECK(!memcmp(before,card.protocol.staged,512u));
    }
    /* Different endpoints cannot revoke a good prior-read witness. */
    expected(11u,42u,0u,NULL,8u,524u);
    for(unsigned addr=0;addr<256u;addr++) if(addr!=2u) {
        request(12u,42u,0u,data);frame[1]=(uint8_t)addr;
        CHECK(run(140u,524u)==0 && card.protocol.read_valid);
    }
    request(12u,42u,0u,data);frame[2]=1u;CHECK(run(140u,524u)==0 && card.protocol.read_valid);
    /* Exact short allocations make malformed140-byte input bounds visible. */
    for(unsigned n=0;n<150u;n++) if(n!=140u) {
        uint8_t *short_frame=malloc(n?n:1u);CHECK(short_frame!=NULL);
        memset(short_frame,0,n);if(n) short_frame[0]=12u;if(n>1u) short_frame[1]=2u;
        if(n>3u) short_frame[3]=34u;
        produced=999u;CHECK(kui_armada_ce_vmu_write_handle(&card,short_frame,n,reply,524u,&produced)==0);
        CHECK(!produced);free(short_frame);
    }
}

static void aliases_and_fixed_state(void) {
    fresh();struct kui_armada_ce_vmu_write before=card;
    struct kui_vmu_emulation_io callbacks=io();
    CHECK(kui_armada_ce_vmu_write_init(&card,&callbacks)==-3 && !memcmp(&before,&card,sizeof(card)));
    CHECK(kui_armada_ce_vmu_write_init(&card,(const void *)&card)==-1 && !memcmp(&before,&card,sizeof(card)));
    struct kui_armada_ce_vmu_write empty={0};callbacks.read_block=NULL;
    CHECK(kui_armada_ce_vmu_write_init(&empty,&callbacks)==-1 && !empty.started);
    callbacks=io();callbacks.commit_block=NULL;CHECK(kui_armada_ce_vmu_write_init(&empty,&callbacks)==-1);
    request(1u,0u,0u,NULL);
    union {size_t align;uint8_t bytes[640];} shared;memset(&shared,0x5a,sizeof(shared));
    for(unsigned which=0;which<6u;which++) {
        const uint8_t *in=frame;uint8_t *out=reply;size_t *n=&produced;
        if(which==0u) in=(const void *)&card;
        if(which==1u) out=(void *)&card;
        if(which==2u) n=(void *)&card;
        if(which==3u) {in=shared.bytes;out=shared.bytes+2u;}
        if(which==4u) {in=shared.bytes;n=(void *)shared.bytes;}
        if(which==5u) {out=shared.bytes;n=(void *)shared.bytes;}
        memset(reply,0xa5,sizeof(reply));produced=999u;
        CHECK(kui_armada_ce_vmu_write_handle(&card,in,4u,out,116u,n)==-1);
        CHECK(!memcmp(&before,&card,sizeof(card)) && produced==999u);
        for(unsigned i=0;i<sizeof(reply);i++) CHECK(reply[i]==0xa5u);
        for(unsigned i=0;i<sizeof(shared);i++) CHECK(shared.bytes[i]==0x5au);
    }
    CHECK(kui_armada_ce_vmu_write_handle(&card,(const void *)(UINTPTR_MAX-1u),4u,reply,116u,&produced)==-1);
    CHECK(produced==999u && !memcmp(&before,&card,sizeof(card)));
    CHECK(kui_armada_ce_vmu_write_handle(&card,frame,4u,(void *)(UINTPTR_MAX-1u),116u,&produced)==-1);
    CHECK(produced==999u && !memcmp(&before,&card,sizeof(card)));
    CHECK(kui_armada_ce_vmu_write_handle(&card,frame,4u,reply,116u,(void *)(UINTPTR_MAX-1u))==-1);
    CHECK(kui_armada_ce_vmu_write_handle((void *)(UINTPTR_MAX-1u),frame,4u,reply,116u,&produced)==-1);
    CHECK(produced==999u && !memcmp(&before,&card,sizeof(card)));
    for(unsigned field=0;field<9u;field++) {
        card=before;
        if(field==0u) card.self=&empty;
        if(field==1u) card.magic^=1u;
        if(field==2u) card.started=2u;
        if(field==3u) card.protocol.address=1u;
        if(field==4u) card.protocol.initialized=0u;
        if(field==5u) card.protocol.killed=1u;
        if(field==6u) card.protocol.io.context=&disk;
        if(field==7u) card.io.read_block=NULL;
        if(field==8u) card.io.commit_block=NULL;
        struct kui_armada_ce_vmu_write altered=card;
        produced=999u;memset(reply,0xa5,sizeof(reply));
        CHECK(kui_armada_ce_vmu_write_handle(&card,frame,4u,reply,116u,&produced)==-3);
        CHECK(produced==999u && !memcmp(&altered,&card,sizeof(card)));
        for(unsigned i=0;i<sizeof(reply);i++) CHECK(reply[i]==0xa5u);
    }
    card=before;struct kui_armada_ce_vmu_write moved=card;produced=999u;
    CHECK(kui_armada_ce_vmu_write_handle(&moved,frame,4u,reply,116u,&produced)==-3 && produced==999u);
    /* The original callback descriptor is borrowed only during init. */
    memset(&empty,0,sizeof(empty));callbacks=io();
    CHECK(!kui_armada_ce_vmu_write_init(&empty,&callbacks));
    memset(&callbacks,0,sizeof(callbacks));request(11u,42u,0u,NULL);
    CHECK(kui_armada_ce_vmu_write_handle(&empty,frame,12u,reply,524u,&produced)==1 && produced==524u);
    /* The planned publisher tail is a separate request span inside one object. */
    fresh();uint8_t private_reply[524],data[128],saved[140];memset(data,0x87,sizeof(data));
    request(12u,42u,0u,data);memcpy(private_reply+384u,frame,140u);memcpy(saved,frame,140u);
    CHECK(kui_armada_ce_vmu_write_handle(&card,private_reply+384u,140u,private_reply,384u,&produced)==1);
    CHECK(produced==4u && private_reply[0]==7u && !memcmp(saved,private_reply+384u,140u));
    struct kui_armada_ce_vmu_write admitted=card;produced=999u;
    CHECK(kui_armada_ce_vmu_write_handle(&card,private_reply+383u,140u,private_reply,384u,&produced)==-1);
    CHECK(produced==999u && !memcmp(&admitted,&card,sizeof(card)));
}
int main(void) {
    transaction_order_and_bytes();retries_restart_and_commit();read_finalization_and_reentry();
    shapes_and_capacity();aliases_and_fixed_state();
    printf("CE A2 writable protocol: %u checks, four-phase receipts/durable retries, CE read finalization, private bounds and fixed state (host state%zu)\n",checks,sizeof(card));
    return 0;
}
