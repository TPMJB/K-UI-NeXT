/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_bios_batch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0x8c500000u
#define END (BASE+65536u)
#define PARAM BASE
#define STATUS (BASE+64u)
#define DRIVE (BASE+96u)
#define DEST (BASE+512u)
static unsigned checks,maps,parameter_maps,validation_maps,write_maps,reads,audio_actions;
static uint8_t memory[65536];
static bool map_disabled;
static struct kui_cdda_bios_batch bios;
static struct kui_cdda_control audio;
static uint32_t data_generation;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint32_t get(uint32_t address) {
    const uint8_t *p=memory+address-BASE;
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put(uint32_t address,uint32_t value) {
    uint8_t *p=memory+address-BASE;
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static uint8_t pattern(uint32_t offset) {
    uint32_t x=offset^0x9e3779b9u;
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return (uint8_t)x;
}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing) {
    CHECK(context==memory && address>=BASE && address<END && bytes<=END-address);
    maps++;
    if(writing==0) parameter_maps++;else if(writing==1) write_maps++;else {
        CHECK(writing==KUI_CDDA_BIOS_BATCH_MAP_VALIDATE);validation_maps++;
    }
    return map_disabled?NULL:memory+address-BASE;
}
static int32_t call(uint32_t function,uint32_t a,uint32_t b) {
    return kui_cdda_bios_batch_dispatch(&bios,a,b,0,function);
}
static void fixture(void) {
    memset(memory,0x5a,sizeof(memory));maps=parameter_maps=validation_maps=write_maps=reads=audio_actions=0;
    map_disabled=false;data_generation=1000u;
    struct kui_cdda_bios_batch_ops ops={memory,map};
    CHECK(kui_cdda_bios_batch_init(&bios,&ops,529200u,KUI_CDDA_BIOS_BATCH_DATA_BYTES,BASE,END)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(kui_cdda_control_init(&audio,529200u)==KUI_CDDA_CONTROL_OK);
}
static void params(uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    put(PARAM,a);put(PARAM+4u,b);put(PARAM+8u,c);put(PARAM+12u,d);
}
static void words(uint32_t err1,uint32_t err2,uint32_t bytes,uint32_t ata) {
    CHECK(get(STATUS)==err1 && get(STATUS+4u)==err2 && get(STATUS+8u)==bytes && get(STATUS+12u)==ata);
}
static void no_nested_effects(void) {
    struct kui_cdda_bios_batch saved=bios;
    unsigned before=maps;uint8_t bytes[16];memcpy(bytes,memory+STATUS-BASE,16);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_STOP,UINT32_MAX)==0);
    CHECK(call(KUI_GD_CHECK,bios.work.handle,STATUS)==4);
    CHECK(call(KUI_GD_EXEC,0,0)==-1 && call(KUI_GD_ABORT,bios.work.handle,0)==-1);
    CHECK(call(KUI_GD_INIT,0,0)==-1 && call(KUI_GD_RESET,0,0)==-1 && call(KUI_GD_DRIVE,DRIVE,0)==-1);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)) && maps==before && !memcmp(bytes,memory+STATUS-BASE,16));
}
/* The native owner model deliberately splits the physical copy and completion.
 * Accepted progress is invisible until the full checked lease has completed. */
static struct kui_cdda_bios_batch_work execute(bool success) {
    struct kui_cdda_bios_batch_work claim;
    CHECK(kui_cdda_bios_batch_take(&bios,&claim)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(kui_cdda_bios_batch_validate_work(&bios,&claim)==KUI_CDDA_BIOS_BATCH_OK);
    no_nested_effects();uint32_t before=bios.completed_bytes;
    if(claim.kind==KUI_CDDA_BIOS_BATCH_AUDIO && success) {
        struct kui_cdda_control_action action;
        enum kui_cdda_control_result result=kui_cdda_control_request(&audio,&claim.audio,&action);
        if(result!=KUI_CDDA_CONTROL_OK) success=false;
        else if(action.pending) {
            audio_actions++;
            CHECK(kui_cdda_control_complete(&audio,action.epoch,true)==KUI_CDDA_CONTROL_OK);
        }
        CHECK(kui_cdda_bios_batch_observe_audio(&bios,audio.current.state)==KUI_CDDA_BIOS_BATCH_OK);
    } else if(claim.kind==KUI_CDDA_BIOS_BATCH_DATA) {
        CHECK(claim.bytes==2048u && claim.committed==before && claim.total_bytes==bios.work.bytes);
        reads++;data_generation++;
        for(uint32_t i=0;i<claim.bytes;i++) memory[claim.destination-BASE+i]=pattern(claim.offset+i);
    }
    CHECK(bios.completed_bytes==before);
    CHECK(kui_cdda_bios_batch_complete(&bios,&claim,success,success?claim.bytes:0u)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(bios.completed_bytes==before+(success?claim.bytes:0u));
    return claim;
}
static void complete_sizes(void) {
    fixture();
    for(uint32_t count=1;count<=16u;count++) for(unsigned boundary=0;boundary<2u;boundary++) {
        uint32_t fad=boundary?49246u-count:45150u;
        uint32_t destination=END-count*2048u;
        memset(memory+destination-BASE,0x5a,count*2048u);
        uint8_t guard=memory[destination-BASE-1u];
        params(fad,count,destination,0);unsigned before_reads=reads,before_maps=validation_maps;
        int32_t handle=call(KUI_GD_REQUEST,16,PARAM);CHECK(handle>0);
        CHECK(validation_maps==before_maps+1u && reads==before_reads && !audio_actions);
        struct kui_cdda_bios_batch_work full=bios.work;
        CHECK(full.bytes==count*2048u && full.total_bytes==full.bytes && !full.chunk && !full.committed);
        params(0,UINT32_MAX,0,1); /* A queued request owns copied parameters. */
        uint32_t last_chunk=0;
        for(uint32_t index=0;index<count;index++) {
            CHECK(call(KUI_GD_EXEC,0,0)==0 && reads==before_reads+index);
            CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==KUI_GD_PROCESSING);words(0,0,index*2048u,4);
            CHECK(call(KUI_GD_REQUEST,29,0)==0);
            struct kui_cdda_bios_batch_work claim=execute(true);
            CHECK(claim.offset==(fad-45150u+index)*2048u && claim.destination==destination+index*2048u);
            CHECK(claim.handle==(uint32_t)handle && claim.epoch==full.epoch && claim.chunk>last_chunk);
            CHECK(claim.total_bytes==full.bytes && claim.committed==index*2048u && !memcmp(&bios.work,&full,sizeof(full)));
            CHECK(bios.state==(index+1u<count?KUI_CDDA_BIOS_BATCH_QUEUED:KUI_CDDA_BIOS_BATCH_TERMINAL));
            last_chunk=claim.chunk;
        }
        CHECK(reads==before_reads+count && memory[destination-BASE-1u]==guard);
        for(uint32_t i=0;i<count*2048u;i++) CHECK(memory[destination-BASE+i]==pattern((fad-45150u)*2048u+i));
        CHECK(call(KUI_GD_REQUEST,29,0)==0); /* Held terminal owns the slot. */
        CHECK(call(KUI_GD_CHECK,(uint32_t)handle+1u,STATUS)==KUI_GD_NOT_FOUND);words(0,0,0,0);
        CHECK(bios.state==KUI_CDDA_BIOS_BATCH_TERMINAL);
        CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==KUI_GD_COMPLETED);words(0,0,count*2048u,0);
        CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==KUI_GD_NOT_FOUND);words(0,0,0,0);
    }
}
static void refuse(uint32_t command,uint32_t address) {
    struct kui_cdda_bios_batch saved=bios;struct kui_cdda_control sound=audio;
    unsigned before_reads=reads,before_actions=audio_actions;
    CHECK(call(KUI_GD_REQUEST,command,address)==0);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)) && !memcmp(&audio,&sound,sizeof(audio)));
    CHECK(reads==before_reads && audio_actions==before_actions);
}
static void bounds(void) {
    fixture();
    for(uint32_t repeat=1;repeat<15u;repeat++) {params(1,1,repeat,0);refuse(20,PARAM);}
    uint32_t bad_tracks[][2]={{0,1},{1,0},{2,2},{1,2},{2,1},{UINT32_MAX,UINT32_MAX}};
    for(unsigned i=0;i<sizeof(bad_tracks)/sizeof(bad_tracks[0]);i++) {
        params(bad_tracks[i][0],bad_tracks[i][1],0,0);refuse(20,PARAM);
    }
    for(uint32_t command=0;command<256u;command++)
        if(command!=20u && command!=22u && command!=23u && command!=33u && command!=29u && command!=16u) refuse(command,PARAM);
    uint32_t bad_addresses[]={0,BASE-4u,BASE+1u,END-8u,END,0x8d000000u,0x4c500000u,UINT32_MAX};
    params(1,1,0,0);
    for(unsigned i=0;i<sizeof(bad_addresses)/sizeof(bad_addresses[0]);i++) refuse(20,bad_addresses[i]);
    uint32_t bad_counts[]={0,17,32,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad_counts)/sizeof(bad_counts[0]);i++) {params(45150,bad_counts[i],DEST,0);refuse(16,PARAM);}
    uint32_t bad_fads[]={0,150,225,45149,49246,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad_fads)/sizeof(bad_fads[0]);i++) {params(bad_fads[i],1,DEST,0);refuse(16,PARAM);}
    for(uint32_t count=2;count<=16u;count++) {params(49247u-count,count,DEST,0);refuse(16,PARAM);}
    uint32_t bad_dest[]={0,BASE-2u,DEST+1u,END-32766u,END,UINT32_MAX,0x4c500200u};
    for(unsigned i=0;i<sizeof(bad_dest)/sizeof(bad_dest[0]);i++) {params(45150,16,bad_dest[i],0);refuse(16,PARAM);}
    params(45150,1,DEST,1);refuse(16,PARAM);
    uint32_t areas[]={0x0c500000u,BASE,0xac500000u};
    for(unsigned i=0;i<3u;i++) for(unsigned j=0;j<3u;j++) {
        params(49230,16,areas[j]+512u,0);
        int32_t handle=call(KUI_GD_REQUEST,16,areas[i]);CHECK(handle>0 && bios.work.destination==DEST);
        for(unsigned chunk=0;chunk<16u;chunk++) execute(true);
        CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==2);words(0,0,32768u,0);
    }
    params(1,1,0,0);map_disabled=true;refuse(20,PARAM);map_disabled=false;
    unsigned before=maps;CHECK(kui_cdda_bios_batch_dispatch(&bios,20,PARAM,1,0)==-1 && maps==before);
}
static void mutate(struct kui_cdda_bios_batch_work *w,unsigned which) {
    switch(which) {
    case 0:w->handle++;break;case 1:w->epoch++;break;case 2:w->command++;break;
    case 3:w->kind=(enum kui_cdda_bios_batch_kind)(w->kind+1);break;case 4:w->track++;break;
    case 5:w->offset++;break;case 6:w->destination++;break;case 7:w->bytes++;break;
    case 8:w->audio.command=(enum kui_cdda_control_command)(w->audio.command+1);break;
    case 9:w->audio.first++;break;case 10:w->audio.end++;break;case 11:w->audio.frame++;break;
    case 12:w->audio.repeat=!w->audio.repeat;break;case 13:w->total_bytes++;break;
    case 14:w->committed++;break;default:w->chunk++;break;
    }
}
static void stale_chunks_and_faults(void) {
    fixture();params(45150,3,DEST,0);int32_t handle=call(KUI_GD_REQUEST,16,PARAM);CHECK(handle==1);
    struct kui_cdda_bios_batch_work old=execute(true),claim;
    CHECK(kui_cdda_bios_batch_validate_work(&bios,&old)==KUI_CDDA_BIOS_BATCH_STALE);
    CHECK(kui_cdda_bios_batch_take(&bios,&claim)==KUI_CDDA_BIOS_BATCH_OK);
    struct kui_cdda_bios_batch saved=bios;unsigned before_maps=maps,before_reads=reads;
    CHECK(old.handle==claim.handle && old.epoch==claim.epoch && old.chunk!=claim.chunk);
    CHECK(kui_cdda_bios_batch_validate_work(&bios,&old)==KUI_CDDA_BIOS_BATCH_STALE);
    CHECK(kui_cdda_bios_batch_complete(&bios,&old,true,2048)==KUI_CDDA_BIOS_BATCH_STALE);
    CHECK(kui_cdda_bios_batch_validate_work(&bios,&bios.work)==KUI_CDDA_BIOS_BATCH_STALE);
    for(unsigned field=0;field<16u;field++) {
        struct kui_cdda_bios_batch_work bad=claim;mutate(&bad,field);
        CHECK(kui_cdda_bios_batch_validate_work(&bios,&bad)==KUI_CDDA_BIOS_BATCH_STALE);
        CHECK(kui_cdda_bios_batch_complete(&bios,&bad,true,bad.bytes)==KUI_CDDA_BIOS_BATCH_STALE);
        CHECK(!memcmp(&bios,&saved,sizeof(bios)));
    }
    CHECK(kui_cdda_bios_batch_complete(&bios,&claim,true,2047)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_complete(&bios,&claim,true,4096)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_complete(&bios,&claim,false,1)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)) && maps==before_maps && reads==before_reads);
    CHECK(kui_cdda_bios_batch_complete(&bios,&bios.pending,false,0)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(bios.completed_bytes==2048u && bios.state==KUI_CDDA_BIOS_BATCH_TERMINAL);
    saved=bios;map_disabled=true;
    CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==-1 && !memcmp(&bios,&saved,sizeof(bios)));
    map_disabled=false;CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==-1);words(1,KUI_GD_ERROR_IO,2048,0);
    CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==0);words(0,0,0,0);
    for(uint32_t prefix=0;prefix<16u;prefix++) {
        params(45150,16,DEST,0);handle=call(KUI_GD_REQUEST,16,PARAM);CHECK(handle>0);
        for(uint32_t i=0;i<prefix;i++) execute(true);
        unsigned physical=reads;
        CHECK(call(KUI_GD_ABORT,(uint32_t)handle+1u,0)==-1);
        CHECK(call(KUI_GD_ABORT,(uint32_t)handle,0)==0 && reads==physical);
        CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==-1);words(1,KUI_GD_ERROR_CANCELLED,prefix*2048u,0);
        CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==0);
        params(45150,16,DEST,0);handle=call(KUI_GD_REQUEST,16,PARAM);CHECK(handle>0);
        for(uint32_t i=0;i<prefix;i++) execute(true);
        execute(false);
        CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==-1);words(1,KUI_GD_ERROR_IO,prefix*2048u,0);
    }
}
static void audio_and_reset_independence(void) {
    fixture();params(1,1,15,0);CHECK(call(KUI_GD_REQUEST,20,PARAM)==1);
    struct kui_cdda_control untouched=audio;CHECK(!memcmp(&audio,&untouched,sizeof(audio)) && !audio_actions);
    params(2,2,1,0);execute(true);
    CHECK(call(KUI_GD_CHECK,1,STATUS)==2 && audio.current.state==KUI_CDDA_CONTROL_PLAYING);
    CHECK(kui_cdda_control_observe(&audio,audio.current.epoch,1234u)==KUI_CDDA_CONTROL_OK);
    struct kui_cdda_control playing=audio;uint32_t owner_epoch=audio.current.epoch;
    params(45150,16,DEST,0);int32_t handle=call(KUI_GD_REQUEST,16,PARAM);CHECK(handle>0);
    struct kui_cdda_bios_batch_work retired={0};
    for(unsigned i=0;i<3u;i++) retired=execute(true);
    uint32_t generation=bios.chunk_generation,old_epoch=bios.work.epoch,data=data_generation;
    CHECK(call(KUI_GD_RESET,0,0)==0);
    CHECK(bios.completed_bytes==0 && bios.chunk_generation==generation && bios.epoch_generation>old_epoch);
    CHECK(!memcmp(&audio,&playing,sizeof(audio)) && audio.current.epoch==owner_epoch && data_generation==data);
    CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==0);words(0,0,0,0);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==3);
    params(45150,2,DEST,0);CHECK(call(KUI_GD_REQUEST,16,PARAM)>handle);
    struct kui_cdda_bios_batch_work claim;
    CHECK(kui_cdda_bios_batch_take(&bios,&claim)==KUI_CDDA_BIOS_BATCH_OK && claim.chunk>generation);
    CHECK(kui_cdda_bios_batch_complete(&bios,&retired,false,0)==KUI_CDDA_BIOS_BATCH_STALE);
    CHECK(kui_cdda_bios_batch_complete(&bios,&claim,true,2048)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(!memcmp(&audio,&playing,sizeof(audio)));
    uint32_t request_handle=bios.work.handle;execute(true);
    CHECK(call(KUI_GD_CHECK,request_handle,STATUS)==2);words(0,0,4096,0);
    uint32_t commands[]={22u,23u,33u,33u},drive[]={1u,3u,2u,2u};
    for(unsigned i=0;i<4u;i++) {
        handle=call(KUI_GD_REQUEST,commands[i],UINT32_MAX);CHECK(handle>0);execute(true);
        CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==2);
        CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==drive[i]);
    }
    CHECK(audio_actions==4u); /* Second STOP produces no physical action. */
    params(1,1,0,0);handle=call(KUI_GD_REQUEST,20,PARAM);execute(true);
    CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==2);
    CHECK(kui_cdda_control_observe(&audio,audio.current.epoch,44100u)==KUI_CDDA_CONTROL_ENDED);
    CHECK(kui_cdda_bios_batch_observe_audio(&bios,audio.current.state)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==1u && audio.current.frame==312228u);
    CHECK(kui_cdda_bios_batch_observe_audio(&bios,KUI_CDDA_CONTROL_FAULT)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==9u);
    CHECK(kui_cdda_bios_batch_observe_audio(&bios,(enum kui_cdda_control_state)19)==KUI_CDDA_BIOS_BATCH_INVALID);
}
static void invalid_alias_exhaustion(void) {
    fixture();struct kui_cdda_bios_batch saved=bios;
    struct kui_cdda_bios_batch_ops ops={memory,map},bad_ops={memory,NULL};
    CHECK(kui_cdda_bios_batch_init(NULL,&ops,529200,8388608,BASE,END)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_init(&bios,NULL,529200,8388608,BASE,END)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_init(&bios,&bad_ops,529200,8388608,BASE,END)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_init(&bios,&ops,312227,8388608,BASE,END)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_init(&bios,&ops,529200,8388607,BASE,END)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_init(&bios,&ops,529200,8388608,0xac500000u,END)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_init(&bios,&ops,529200,8388608,BASE,BASE)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_init(&bios,&ops,529200,8388608,BASE+1u,END)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)));
    struct kui_cdda_bios_batch_work out;memset(&out,0x5a,sizeof(out));struct kui_cdda_bios_batch_work original=out;
    CHECK(kui_cdda_bios_batch_take(&bios,&out)==KUI_CDDA_BIOS_BATCH_NOTHING && !memcmp(&out,&original,sizeof(out)));
    CHECK(kui_cdda_bios_batch_take(&bios,NULL)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_validate_work(&bios,NULL)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(call(KUI_GD_REQUEST,29,0)==1);saved=bios;
    CHECK(kui_cdda_bios_batch_take(&bios,&bios.work)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(kui_cdda_bios_batch_take(&bios,&bios.pending)==KUI_CDDA_BIOS_BATCH_INVALID);
    struct {uint32_t before[2];struct kui_cdda_bios_batch owner;} overlap={.before={55,66},.owner=bios};
    CHECK(kui_cdda_bios_batch_take(&overlap.owner,(struct kui_cdda_bios_batch_work *)overlap.before)==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(overlap.before[0]==55 && overlap.before[1]==66 && !memcmp(&overlap.owner,&bios,sizeof(bios)));
    CHECK(kui_cdda_bios_batch_take(&bios,(struct kui_cdda_bios_batch_work *)(UINTPTR_MAX-3u))==KUI_CDDA_BIOS_BATCH_INVALID);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)));
    CHECK(kui_cdda_bios_batch_take(&bios,&out)==KUI_CDDA_BIOS_BATCH_OK);saved=bios;
    CHECK(kui_cdda_bios_batch_take(&bios,&original)==KUI_CDDA_BIOS_BATCH_BUSY && !memcmp(&bios,&saved,sizeof(bios)));
    CHECK(kui_cdda_bios_batch_validate_work(&bios,&out)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(kui_cdda_bios_batch_complete(&bios,&out,true,0)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(call(KUI_GD_CHECK,out.handle,STATUS)==2);saved=bios;
    bios.handle_generation=INT32_MAX;struct kui_cdda_bios_batch exhausted=bios;
    CHECK(call(KUI_GD_REQUEST,29,0)==0 && !memcmp(&bios,&exhausted,sizeof(bios)));
    bios=saved;bios.epoch_generation=UINT32_MAX;exhausted=bios;
    CHECK(call(KUI_GD_REQUEST,29,0)==0 && call(KUI_GD_INIT,0,0)==-1 && call(KUI_GD_RESET,0,0)==-1);
    CHECK(!memcmp(&bios,&exhausted,sizeof(bios)));
    bios=saved;bios.handle_generation=INT32_MAX-1u;bios.epoch_generation=UINT32_MAX-1u;
    CHECK(call(KUI_GD_REQUEST,29,0)==INT32_MAX);CHECK(kui_cdda_bios_batch_take(&bios,&out)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(out.epoch==UINT32_MAX && kui_cdda_bios_batch_complete(&bios,&out,true,0)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(call(KUI_GD_CHECK,INT32_MAX,STATUS)==2);exhausted=bios;
    CHECK(call(KUI_GD_REQUEST,29,0)==0 && !memcmp(&bios,&exhausted,sizeof(bios)));
    bios=saved;bios.chunk_generation=UINT32_MAX-1u;
    CHECK(call(KUI_GD_REQUEST,29,0)>0);CHECK(kui_cdda_bios_batch_take(&bios,&out)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(out.chunk==UINT32_MAX && kui_cdda_bios_batch_complete(&bios,&out,true,0)==KUI_CDDA_BIOS_BATCH_OK);
    CHECK(call(KUI_GD_CHECK,out.handle,STATUS)==2 && call(KUI_GD_RESET,0,0)==0);
    exhausted=bios;CHECK(call(KUI_GD_REQUEST,29,0)==0 && !memcmp(&bios,&exhausted,sizeof(bios)));
    bios=saved;CHECK(call(KUI_GD_REQUEST,29,0)>0);bios.chunk_generation=UINT32_MAX;exhausted=bios;
    CHECK(kui_cdda_bios_batch_take(&bios,&original)==KUI_CDDA_BIOS_BATCH_OVERFLOW);
    CHECK(!memcmp(&bios,&exhausted,sizeof(bios)));
    bios=saved;bios.chunk_generation=UINT32_MAX-15u;params(45150,16,DEST,0);refuse(16,PARAM);
    bios.chunk_generation=UINT32_MAX-16u;
    CHECK(call(KUI_GD_REQUEST,16,PARAM)>0);
    for(unsigned index=0;index<16u;index++) execute(true);
    CHECK(bios.state==KUI_CDDA_BIOS_BATCH_TERMINAL && bios.chunk_generation==UINT32_MAX);
    CHECK(call(KUI_GD_CHECK,bios.work.handle,STATUS)==2);words(0,0,32768u,0);
}
static void range_properties(void) {
    fixture();uint32_t rng=0x39da162bu;
    for(unsigned iteration=0;iteration<50000u;iteration++) {
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        uint32_t fad=iteration%3u?45150u+rng%4100u:rng;
        uint32_t count=iteration%7u?1u+rng%20u:rng;
        uint32_t destination=iteration%11u?DEST:END-rng%33000u;
        params(fad,count,destination,0);
        bool expected=fad>=45150u && fad<49246u && count>=1u && count<=16u &&
            (uint64_t)fad+count<=49246u && !(destination%2u) && destination>=BASE &&
            (uint64_t)destination+(uint64_t)count*2048u<=END;
        struct kui_cdda_bios_batch saved=bios;
        int32_t handle=call(KUI_GD_REQUEST,16,PARAM);
        CHECK((handle>0)==expected && !reads && !audio_actions);
        if(!expected) CHECK(!memcmp(&bios,&saved,sizeof(bios)));
        else {
            for(uint32_t index=0;index<count;index++) {
                struct kui_cdda_bios_batch_work claim;
                CHECK(kui_cdda_bios_batch_take(&bios,&claim)==KUI_CDDA_BIOS_BATCH_OK);
                CHECK(claim.offset==(fad-45150u+index)*2048u && claim.offset<=8388608u-2048u);
                CHECK(claim.destination==destination+index*2048u && claim.total_bytes==count*2048u);
                CHECK(claim.bytes==2048u && claim.committed==index*2048u);
                CHECK(kui_cdda_bios_batch_complete(&bios,&claim,true,2048u)==KUI_CDDA_BIOS_BATCH_OK);
                CHECK(bios.completed_bytes==(index+1u)*2048u);
            }
            CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==2);words(0,0,count*2048u,0);
            CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==0);
        }
    }
}
int main(void) {
    complete_sizes();bounds();stale_chunks_and_faults();audio_and_reset_independence();
    invalid_alias_exhaustion();range_properties();
    printf("PASS CDDA BIOS batch: %u checks; 1..16-sector whole ranges, unique chunks, committed prefix, cancellation, stale work, independent audio and faults\n",checks);
    return 0;
}
