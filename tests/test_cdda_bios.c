/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_bios.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0x8c500000u
#define END (BASE+8192u)
#define PARAM BASE
#define STATUS (BASE+64u)
#define DRIVE (BASE+96u)
#define DEST (BASE+512u)
static unsigned checks,maps,parameter_maps,validation_maps,write_maps,reads,audio_actions;
static uint8_t memory[8192];
static bool map_disabled;
static struct kui_cdda_bios bios;
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
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing) {
    CHECK(context==memory && address>=BASE && address<END && bytes<=END-address);
    maps++;
    if(writing==0) parameter_maps++;else if(writing==1) write_maps++;else {
        CHECK(writing==KUI_CDDA_BIOS_MAP_VALIDATE);validation_maps++;
    }
    return map_disabled?NULL:memory+address-BASE;
}
static int32_t call(uint32_t function,uint32_t a,uint32_t b) {
    return kui_cdda_bios_dispatch(&bios,a,b,0,function);
}
static void reset_fixture(void) {
    memset(memory,0x5a,sizeof(memory));maps=parameter_maps=validation_maps=write_maps=reads=audio_actions=0;
    map_disabled=false;data_generation=1000u;
    struct kui_cdda_bios_ops ops={memory,map};
    CHECK(kui_cdda_bios_init(&bios,&ops,529200u,KUI_CDDA_BIOS_DATA_BYTES,BASE,END)==KUI_CDDA_BIOS_OK);
    CHECK(kui_cdda_control_init(&audio,529200u)==KUI_CDDA_CONTROL_OK);
}
static void params(uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    put(PARAM,a);put(PARAM+4u,b);put(PARAM+8u,c);put(PARAM+12u,d);
}
static void status_words(uint32_t err1,uint32_t err2,uint32_t bytes,uint32_t ata) {
    CHECK(get(STATUS)==err1 && get(STATUS+4u)==err2 && get(STATUS+8u)==bytes && get(STATUS+12u)==ata);
}
static void nested_refusals(void) {
    struct kui_cdda_bios saved=bios;
    unsigned before=maps;uint8_t bytes[16];memcpy(bytes,memory+STATUS-BASE,16);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_STOP,UINT32_MAX)==0);
    CHECK(call(KUI_GD_CHECK,bios.work.handle,STATUS)==4);
    CHECK(call(KUI_GD_EXEC,0,0)==-1);
    CHECK(call(KUI_GD_ABORT,bios.work.handle,0)==-1);
    CHECK(call(KUI_GD_INIT,0,0)==-1 && call(KUI_GD_RESET,0,0)==-1);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==-1);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)) && maps==before && !memcmp(bytes,memory+STATUS-BASE,16));
}
/* An explicit owner model: only claimed/validated work can invoke physical
 * callbacks. Request/check/reset have no access to these counters or cursors. */
static struct kui_cdda_bios_work execute(bool succeed) {
    struct kui_cdda_bios_work work;
    CHECK(kui_cdda_bios_take(&bios,&work)==KUI_CDDA_BIOS_OK);
    CHECK(kui_cdda_bios_validate_work(&bios,&work)==KUI_CDDA_BIOS_OK);
    nested_refusals();
    if(work.kind==KUI_CDDA_BIOS_AUDIO && succeed) {
        struct kui_cdda_control_action action;
        enum kui_cdda_control_result result=kui_cdda_control_request(&audio,&work.audio,&action);
        if(result!=KUI_CDDA_CONTROL_OK) succeed=false;
        else if(action.pending) {
            audio_actions++;
            CHECK(kui_cdda_control_complete(&audio,action.epoch,true)==KUI_CDDA_CONTROL_OK);
        }
        CHECK(kui_cdda_bios_observe_audio(&bios,audio.current.state)==KUI_CDDA_BIOS_OK);
    } else if(work.kind==KUI_CDDA_BIOS_DATA) {
        reads++;data_generation++;
        if(succeed) for(uint32_t i=0;i<work.bytes;i++) memory[work.destination-BASE+i]=(uint8_t)(work.offset+i);
    }
    CHECK(kui_cdda_bios_complete(&bios,&work,succeed,succeed?work.bytes:0u)==KUI_CDDA_BIOS_OK);
    return work;
}
static void play_and_independence(void) {
    reset_fixture();params(1,1,15,0);
    struct kui_cdda_control untouched=audio;
    int32_t handle=call(KUI_GD_REQUEST,KUI_CDDA_BIOS_PLAY,PARAM);
    CHECK(handle==1 && !reads && !audio_actions && !memcmp(&audio,&untouched,sizeof(audio)));
    CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==KUI_GD_PROCESSING);status_words(0,0,0,4);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==0 && get(DRIVE+4u)==0x80);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_STOP,0)==0 && !reads && !audio_actions);
    params(2,2,1,UINT32_MAX); /* Guest memory may change after submission. */
    CHECK(call(KUI_GD_EXEC,0,0)==0 && !reads && !audio_actions);
    struct kui_cdda_bios_work play=execute(true);
    CHECK(play.handle==1 && play.epoch==1 && play.track==1 && play.kind==KUI_CDDA_BIOS_AUDIO);
    CHECK(play.audio.first==268128u && play.audio.end==312228u && play.audio.repeat);
    CHECK(audio.current.state==KUI_CDDA_CONTROL_PLAYING && audio.current.epoch==1 && audio_actions==1);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_NOP,0)==0); /* Terminal result owns slot. */
    CHECK(call(KUI_GD_CHECK,1,STATUS)==KUI_GD_COMPLETED);status_words(0,0,0,0);
    CHECK(call(KUI_GD_CHECK,1,STATUS)==KUI_GD_NOT_FOUND);status_words(0,0,0,0);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==3);
    CHECK(kui_cdda_control_observe(&audio,audio.current.epoch,1234u)==KUI_CDDA_CONTROL_OK);
    untouched=audio;uint32_t external_data=data_generation;
    params(KUI_CDDA_BIOS_DATA_FIRST_FAD+17u,1,DEST,0);
    handle=call(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM);
    CHECK(handle==2 && bios.work.offset==17u*2048u && bios.work.destination==DEST);
    CHECK(data_generation==external_data && !reads && audio_actions==1 && !memcmp(&audio,&untouched,sizeof(audio)));
    CHECK(call(KUI_GD_CHECK,2,STATUS)==KUI_GD_PROCESSING && !reads);
    params(0,0,0,0);struct kui_cdda_bios_work data=execute(true);
    CHECK(data.handle!=play.handle && data.epoch!=play.epoch && data_generation==external_data+1u);
    CHECK(data_generation!=data.handle && audio.current.epoch!=data.handle && !memcmp(&audio,&untouched,sizeof(audio)));
    CHECK(reads==1 && audio_actions==1);
    for(unsigned i=0;i<2048u;i++) CHECK(memory[DEST-BASE+i]==(uint8_t)(17u*2048u+i));
    CHECK(call(KUI_GD_CHECK,2,STATUS)==KUI_GD_COMPLETED);status_words(0,0,2048,0);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==3);
    CHECK(call(KUI_GD_INIT,0,0)==0 && !memcmp(&audio,&untouched,sizeof(audio)));
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==3);
    uint32_t expected_commands[]={KUI_CDDA_BIOS_PAUSE,KUI_CDDA_BIOS_RELEASE,KUI_GD_STOP,KUI_GD_STOP};
    uint32_t expected_drive[]={1u,3u,2u,2u};
    for(unsigned i=0;i<4u;i++) {
        handle=call(KUI_GD_REQUEST,expected_commands[i],UINT32_MAX);CHECK(handle>2);
        execute(true);CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==KUI_GD_COMPLETED);
        CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==expected_drive[i]);
    }
    CHECK(audio_actions==4 && reads==1); /* Second STOP is idempotent. */
    params(1,1,0,0);handle=call(KUI_GD_REQUEST,KUI_CDDA_BIOS_PLAY,PARAM);execute(true);
    CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==KUI_GD_COMPLETED);
    CHECK(kui_cdda_control_observe(&audio,audio.current.epoch,44100u)==KUI_CDDA_CONTROL_ENDED);
    CHECK(kui_cdda_bios_observe_audio(&bios,audio.current.state)==KUI_CDDA_BIOS_OK);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==1 && audio.current.frame==312228u);
}
static void refuse_request(uint32_t command,uint32_t address) {
    struct kui_cdda_bios saved=bios;struct kui_cdda_control sound=audio;
    unsigned old_reads=reads,old_actions=audio_actions;
    CHECK(call(KUI_GD_REQUEST,command,address)==0);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)) && !memcmp(&audio,&sound,sizeof(audio)));
    CHECK(reads==old_reads && audio_actions==old_actions);
}
static void request_ranges(void) {
    reset_fixture();
    for(uint32_t repeat=1;repeat<15u;repeat++) {params(1,1,repeat,0);refuse_request(KUI_CDDA_BIOS_PLAY,PARAM);}
    const uint32_t bad_tracks[][2]={{0,1},{1,0},{2,2},{1,2},{2,1},{99,99},{UINT32_MAX,UINT32_MAX}};
    for(unsigned i=0;i<sizeof(bad_tracks)/sizeof(bad_tracks[0]);i++) {
        params(bad_tracks[i][0],bad_tracks[i][1],0,0);refuse_request(KUI_CDDA_BIOS_PLAY,PARAM);
    }
    params(1,1,16,0);refuse_request(KUI_CDDA_BIOS_PLAY,PARAM);
    params(1,1,UINT32_MAX,0);refuse_request(KUI_CDDA_BIOS_PLAY,PARAM);
    for(unsigned command=0;command<256u;command++)
        if(command!=20u && command!=22u && command!=23u && command!=33u && command!=29u && command!=16u)
            refuse_request(command,PARAM);
    const uint32_t bad_addresses[]={0,0x8c000000u,BASE-4u,PARAM+1u,END-8u,END,0xacffffffu,
        0x8d000000u,0x4c500000u,0xa5000000u,UINT32_MAX};
    params(1,1,0,0);
    for(unsigned i=0;i<sizeof(bad_addresses)/sizeof(bad_addresses[0]);i++)
        refuse_request(KUI_CDDA_BIOS_PLAY,bad_addresses[i]);
    const uint32_t bad_counts[]={0,2,64,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad_counts)/sizeof(bad_counts[0]);i++) {
        params(45150,bad_counts[i],DEST,0);refuse_request(KUI_GD_PIOREAD,PARAM);
    }
    const uint32_t bad_fads[]={0,149,150,225,45149,49246,720000,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad_fads)/sizeof(bad_fads[0]);i++) {
        params(bad_fads[i],1,DEST,0);refuse_request(KUI_GD_PIOREAD,PARAM);
    }
    params(45150,1,DEST,1);refuse_request(KUI_GD_PIOREAD,PARAM);
    const uint32_t bad_dest[]={0,BASE-2u,DEST+1u,END-2046u,END,UINT32_MAX,0x4c500200u};
    for(unsigned i=0;i<sizeof(bad_dest)/sizeof(bad_dest[0]);i++) {
        params(45150,1,bad_dest[i],0);refuse_request(KUI_GD_PIOREAD,PARAM);
    }
    uint32_t aliases[]={0x0c500000u,BASE,0xac500000u};
    for(unsigned i=0;i<3u;i++) for(unsigned j=0;j<3u;j++) {
        params(49245u,1,(aliases[j]+512u),0);
        int32_t handle=call(KUI_GD_REQUEST,KUI_GD_PIOREAD,aliases[i]);CHECK(handle>0);
        struct kui_cdda_bios_work work=execute(true);
        CHECK(work.offset==KUI_CDDA_BIOS_DATA_BYTES-2048u && work.destination==DEST);
        CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==KUI_GD_COMPLETED);status_words(0,0,2048,0);
    }
    params(45150,1,END-2048u,0);CHECK(call(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM)>0);
    execute(true);CHECK(call(KUI_GD_CHECK,bios.work.handle,STATUS)==KUI_GD_COMPLETED);
    params(1,1,0,0);map_disabled=true;refuse_request(KUI_CDDA_BIOS_PLAY,PARAM);map_disabled=false;
    unsigned before=maps;CHECK(kui_cdda_bios_dispatch(&bios,20,PARAM,1,0)==-1 && maps==before);
    for(unsigned function=5;function<32u;function++)
        if(function!=KUI_GD_ABORT && function!=KUI_GD_RESET)
            CHECK(call(function,PARAM,PARAM)==-1);
}
static void mutate_work(struct kui_cdda_bios_work *work,unsigned which) {
    switch(which) {
    case 0:work->handle++;break;case 1:work->epoch++;break;case 2:work->command++;break;
    case 3:work->kind=(enum kui_cdda_bios_kind)(work->kind+1);break;case 4:work->track++;break;
    case 5:work->offset++;break;case 6:work->destination++;break;case 7:work->bytes++;break;
    case 8:work->audio.command=(enum kui_cdda_control_command)(work->audio.command+1);break;
    case 9:work->audio.first++;break;case 10:work->audio.end++;break;case 11:work->audio.frame++;break;
    default:work->audio.repeat=!work->audio.repeat;break;
    }
}
static void lifecycle_faults(void) {
    reset_fixture();params(1,1,15,0);CHECK(call(KUI_GD_REQUEST,20,PARAM)==1);execute(true);
    CHECK(call(KUI_GD_CHECK,1,STATUS)==2);struct kui_cdda_control playing=audio;
    params(45150,1,DEST,0);int32_t handle=call(KUI_GD_REQUEST,16,PARAM);
    struct kui_cdda_bios_work canceled=bios.work;
    CHECK(call(KUI_GD_ABORT,(uint32_t)handle+1u,0)==-1 && bios.state==KUI_CDDA_BIOS_QUEUED);
    CHECK(call(KUI_GD_ABORT,(uint32_t)handle,0)==0);
    CHECK(kui_cdda_bios_validate_work(&bios,&canceled)==KUI_CDDA_BIOS_STALE);
    CHECK(kui_cdda_bios_complete(&bios,&canceled,true,2048)==KUI_CDDA_BIOS_STALE);
    CHECK(call(KUI_GD_REQUEST,29,0)==0 && reads==0 && !memcmp(&audio,&playing,sizeof(audio)));
    CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==-1);status_words(1,KUI_GD_ERROR_CANCELLED,0,0);
    CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==0);
    params(45150,1,DEST,0);handle=call(KUI_GD_REQUEST,16,PARAM);canceled=bios.work;
    CHECK(call(KUI_GD_RESET,0,0)==0 && reads==0 && !memcmp(&audio,&playing,sizeof(audio)));
    CHECK(call(KUI_GD_CHECK,(uint32_t)handle,STATUS)==0);
    CHECK(call(KUI_GD_REQUEST,16,PARAM)>handle);
    struct kui_cdda_bios_work work;CHECK(kui_cdda_bios_take(&bios,&work)==KUI_CDDA_BIOS_OK);
    struct kui_cdda_bios saved=bios;
    CHECK(kui_cdda_bios_complete(&bios,&canceled,false,0)==KUI_CDDA_BIOS_STALE);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)));
    for(unsigned i=0;i<13u;i++) {
        struct kui_cdda_bios_work bad=work;mutate_work(&bad,i);
        CHECK(kui_cdda_bios_validate_work(&bios,&bad)==KUI_CDDA_BIOS_STALE);
        CHECK(kui_cdda_bios_complete(&bios,&bad,true,2048)==KUI_CDDA_BIOS_STALE);
        CHECK(!memcmp(&bios,&saved,sizeof(bios)));
    }
    CHECK(kui_cdda_bios_complete(&bios,&work,true,2047)==KUI_CDDA_BIOS_INVALID);
    CHECK(kui_cdda_bios_complete(&bios,&work,false,1)==KUI_CDDA_BIOS_INVALID);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)));
    CHECK(kui_cdda_bios_complete(&bios,&work,false,0)==KUI_CDDA_BIOS_OK);
    saved=bios;map_disabled=true;
    CHECK(call(KUI_GD_CHECK,work.handle,STATUS)==-1 && !memcmp(&bios,&saved,sizeof(bios)));
    map_disabled=false;CHECK(call(KUI_GD_CHECK,work.handle,STATUS)==-1);status_words(1,KUI_GD_ERROR_IO,0,0);
    CHECK(call(KUI_GD_CHECK,work.handle,STATUS)==0);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==3); /* Queue failure alone cannot falsify audio state. */
    CHECK(kui_cdda_bios_observe_audio(&bios,KUI_CDDA_CONTROL_FAULT)==KUI_CDDA_BIOS_OK);
    CHECK(call(KUI_GD_DRIVE,DRIVE,0)==0 && get(DRIVE)==9);
    CHECK(call(KUI_GD_REQUEST,29,UINT32_MAX)>0);work=execute(true);
    CHECK(work.kind==KUI_CDDA_BIOS_NOOP && !work.bytes && reads==0);
    CHECK(call(KUI_GD_CHECK,work.handle,STATUS)==2);status_words(0,0,0,0);
    CHECK(kui_cdda_bios_observe_audio(&bios,(enum kui_cdda_control_state)19)==KUI_CDDA_BIOS_INVALID);
    saved=bios;bios.handle_generation=INT32_MAX;struct kui_cdda_bios exhausted=bios;
    CHECK(call(KUI_GD_REQUEST,29,0)==0 && !memcmp(&bios,&exhausted,sizeof(bios)));
    bios=saved;bios.epoch_generation=UINT32_MAX;exhausted=bios;
    CHECK(call(KUI_GD_REQUEST,29,0)==0 && call(KUI_GD_INIT,0,0)==-1 && call(KUI_GD_RESET,0,0)==-1);
    CHECK(!memcmp(&bios,&exhausted,sizeof(bios)));
    bios=saved;bios.handle_generation=INT32_MAX-1u;bios.epoch_generation=UINT32_MAX-1u;
    CHECK(call(KUI_GD_REQUEST,29,0)==INT32_MAX);
    CHECK(kui_cdda_bios_take(&bios,&work)==KUI_CDDA_BIOS_OK && work.epoch==UINT32_MAX);
    CHECK(kui_cdda_bios_complete(&bios,&work,true,0)==KUI_CDDA_BIOS_OK);
    CHECK(call(KUI_GD_CHECK,INT32_MAX,STATUS)==KUI_GD_COMPLETED);
    exhausted=bios;CHECK(call(KUI_GD_REQUEST,29,0)==0 && !memcmp(&bios,&exhausted,sizeof(bios)));
}
static void invalid_and_alias(void) {
    reset_fixture();struct kui_cdda_bios_ops ops={memory,map};struct kui_cdda_bios saved=bios;
    CHECK(kui_cdda_bios_init(NULL,&ops,529200,KUI_CDDA_BIOS_DATA_BYTES,BASE,END)==KUI_CDDA_BIOS_INVALID);
    CHECK(kui_cdda_bios_init(&bios,NULL,529200,KUI_CDDA_BIOS_DATA_BYTES,BASE,END)==KUI_CDDA_BIOS_INVALID);
    CHECK(kui_cdda_bios_init(&bios,&ops,KUI_CDDA_BIOS_AUDIO_END_FRAME-1u,KUI_CDDA_BIOS_DATA_BYTES,BASE,END)==KUI_CDDA_BIOS_INVALID);
    CHECK(kui_cdda_bios_init(&bios,&ops,529200,KUI_CDDA_BIOS_DATA_BYTES-1u,BASE,END)==KUI_CDDA_BIOS_INVALID);
    CHECK(kui_cdda_bios_init(&bios,&ops,529200,KUI_CDDA_BIOS_DATA_BYTES,0xac500000u,END)==KUI_CDDA_BIOS_INVALID);
    CHECK(kui_cdda_bios_init(&bios,&ops,529200,KUI_CDDA_BIOS_DATA_BYTES,BASE,BASE)==KUI_CDDA_BIOS_INVALID);
    CHECK(kui_cdda_bios_init(&bios,&ops,529200,KUI_CDDA_BIOS_DATA_BYTES,BASE+1u,END)==KUI_CDDA_BIOS_INVALID);
    CHECK(!memcmp(&bios,&saved,sizeof(bios)));
    struct kui_cdda_bios_work out;memset(&out,0x5a,sizeof(out));struct kui_cdda_bios_work original=out;
    CHECK(kui_cdda_bios_take(&bios,&out)==KUI_CDDA_BIOS_NOTHING && !memcmp(&out,&original,sizeof(out)));
    CHECK(kui_cdda_bios_take(&bios,NULL)==KUI_CDDA_BIOS_INVALID);
    CHECK(kui_cdda_bios_validate_work(&bios,NULL)==KUI_CDDA_BIOS_INVALID);
    CHECK(call(KUI_GD_REQUEST,29,0)==1);saved=bios;
    CHECK(kui_cdda_bios_take(&bios,&bios.work)==KUI_CDDA_BIOS_INVALID && !memcmp(&bios,&saved,sizeof(bios)));
    struct {uint32_t before;struct kui_cdda_bios owner;} overlap={.before=55,.owner=bios};
    CHECK(kui_cdda_bios_take(&overlap.owner,(struct kui_cdda_bios_work *)&overlap.before)==KUI_CDDA_BIOS_INVALID);
    CHECK(overlap.before==55 && !memcmp(&overlap.owner,&bios,sizeof(bios)));
    CHECK(kui_cdda_bios_take(&bios,&out)==KUI_CDDA_BIOS_OK);saved=bios;
    CHECK(kui_cdda_bios_take(&bios,&original)==KUI_CDDA_BIOS_BUSY && !memcmp(&bios,&saved,sizeof(bios)));
}
static void range_properties(void) {
    uint32_t rng=0x39da162bu;
    reset_fixture();
    for(unsigned i=0;i<50000u;i++) {
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        uint32_t fad=i%3u?45150u+(rng%4096u):rng;
        uint32_t count=i%7u?1u:rng,destination=i%11u?DEST:END-(rng%4096u);
        params(fad,count,destination,0);
        uint64_t distance=(uint64_t)destination-BASE;
        bool expected=fad>=45150u && fad<49246u && count==1u && !(destination%2u) &&
            destination>=BASE && distance+2048u<=sizeof(memory);
        struct kui_cdda_bios saved=bios;
        int32_t handle=call(KUI_GD_REQUEST,16,PARAM);
        CHECK((handle>0)==expected && !reads && !audio_actions);
        if(!expected) CHECK(!memcmp(&bios,&saved,sizeof(bios)));
        else {
            struct kui_cdda_bios_work work;
            CHECK(kui_cdda_bios_take(&bios,&work)==KUI_CDDA_BIOS_OK);
            CHECK(work.offset==(fad-45150u)*2048u && work.offset<=KUI_CDDA_BIOS_DATA_BYTES-2048u);
            CHECK(kui_cdda_bios_complete(&bios,&work,true,2048u)==KUI_CDDA_BIOS_OK);
            CHECK(call(KUI_GD_CHECK,work.handle,STATUS)==2);status_words(0,0,2048,0);
            CHECK(call(KUI_GD_CHECK,work.handle,STATUS)==0);
        }
    }
}
int main(void) {
    play_and_independence();request_ranges();lifecycle_faults();invalid_and_alias();range_properties();
    printf("PASS CDDA BIOS: %u checks; copied GD requests, track/FAD bounds, independent audio/data tokens, terminal acknowledgments, stale/reentrant work and faults\n",checks);
    return 0;
}
