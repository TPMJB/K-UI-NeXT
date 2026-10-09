/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_bios.h"
#include <stddef.h>

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void write32(uint8_t *p,uint32_t value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);p[2]=(uint8_t)(value>>16);p[3]=(uint8_t)(value>>24);
}
static bool valid(const struct kui_cdda_bios *b) {
    return b && b->initialized && b->state>=KUI_CDDA_BIOS_EMPTY && b->state<=KUI_CDDA_BIOS_TERMINAL;
}
static uint8_t *guest(struct kui_cdda_bios *b,uint32_t address,uint32_t bytes,uint32_t alignment,int writing) {
    uint32_t area=address&0xff000000u;
    if(area!=0x0c000000u && area!=0x8c000000u && area!=0xac000000u) return NULL;
    uint32_t p1=(address&0x00ffffffu)|0x8c000000u;
    if(!bytes || p1&(alignment-1u) || p1<b->guest_first || p1>=b->guest_end || bytes>b->guest_end-p1)
        return NULL;
    return b->ops.map(b->ops.context,p1,bytes,writing);
}
static bool aliases(const void *out,size_t bytes,const struct kui_cdda_bios *b) {
    uintptr_t address=(uintptr_t)out,owner=(uintptr_t)b;
    if(bytes>UINTPTR_MAX-address) return true;
    return address<=owner?owner-address<bytes:address-owner<sizeof(*b);
}
static void clear_queue(struct kui_cdda_bios *b) {
    b->state=KUI_CDDA_BIOS_EMPTY;b->status=KUI_GD_NOT_FOUND;b->error=b->completed_bytes=0;
    b->work=(struct kui_cdda_bios_work){0};
}
enum kui_cdda_bios_result kui_cdda_bios_init(struct kui_cdda_bios *b,const struct kui_cdda_bios_ops *ops,
    uint32_t source_frames,uint32_t data_bytes,uint32_t first,uint32_t end) {
    if(!b || !ops || !ops->map || source_frames<KUI_CDDA_BIOS_AUDIO_END_FRAME ||
       data_bytes!=KUI_CDDA_BIOS_DATA_BYTES || first<0x8c010000u || end>0x8d000000u ||
       first>=end || first&3u || end&3u) return KUI_CDDA_BIOS_INVALID;
    struct kui_cdda_bios_ops copied=*ops;
    *b=(struct kui_cdda_bios){.ops=copied,.guest_first=first,.guest_end=end,.drive_status=2,.initialized=true};
    return KUI_CDDA_BIOS_OK;
}
static int32_t request(struct kui_cdda_bios *b,uint32_t command,uint32_t address) {
    if(b->state!=KUI_CDDA_BIOS_EMPTY) return 0;
    struct kui_cdda_bios_work work={.command=command};
    uint32_t count=0,params[4]={0};
    switch(command) {
    case KUI_CDDA_BIOS_PLAY:count=3;work.kind=KUI_CDDA_BIOS_AUDIO;break;
    case KUI_CDDA_BIOS_PAUSE:case KUI_CDDA_BIOS_RELEASE:case KUI_GD_STOP:
        work.kind=KUI_CDDA_BIOS_AUDIO;break;
    case KUI_GD_PIOREAD:count=4;work.kind=KUI_CDDA_BIOS_DATA;break;
    case KUI_GD_NOP:work.kind=KUI_CDDA_BIOS_NOOP;break;
    default:return 0;
    }
    if(count) {
        const uint8_t *in=guest(b,address,count*4u,4,0);
        if(!in) return 0;
        for(unsigned i=0;i<count;i++) params[i]=read32(in+i*4u);
    }
    if(command==KUI_CDDA_BIOS_PLAY) {
        if(params[0]!=1u || params[1]!=1u || (params[2]!=0u && params[2]!=15u)) return 0;
        work.track=1u;
        work.audio=(struct kui_cdda_control_request){.command=KUI_CDDA_CONTROL_PLAY,
            .first=KUI_CDDA_BIOS_AUDIO_FIRST_FRAME,.end=KUI_CDDA_BIOS_AUDIO_END_FRAME,.repeat=params[2]==15u};
    } else if(command==KUI_GD_PIOREAD) {
        if(params[0]<KUI_CDDA_BIOS_DATA_FIRST_FAD || params[0]>=KUI_CDDA_BIOS_DATA_END_FAD ||
           params[1]!=1u || params[3] || !guest(b,params[2],2048u,2,KUI_CDDA_BIOS_MAP_VALIDATE)) return 0;
        work.track=2u;work.offset=(params[0]-KUI_CDDA_BIOS_DATA_FIRST_FAD)*2048u;
        work.destination=(params[2]&0x00ffffffu)|0x8c000000u;work.bytes=2048u;
    } else if(command==KUI_CDDA_BIOS_PAUSE) work.audio.command=KUI_CDDA_CONTROL_PAUSE;
    else if(command==KUI_CDDA_BIOS_RELEASE) work.audio.command=KUI_CDDA_CONTROL_RESUME;
    else if(command==KUI_GD_STOP) work.audio.command=KUI_CDDA_CONTROL_STOP;
    if(b->handle_generation>=INT32_MAX || b->epoch_generation==UINT32_MAX) return 0;
    work.handle=b->handle_generation+1u;work.epoch=b->epoch_generation+1u;
    b->handle_generation=work.handle;b->epoch_generation=work.epoch;b->work=work;
    b->state=KUI_CDDA_BIOS_QUEUED;b->status=KUI_GD_PROCESSING;b->error=b->completed_bytes=0;
    return (int32_t)work.handle;
}
static int32_t check(struct kui_cdda_bios *b,uint32_t handle,uint32_t address) {
    uint8_t *out=guest(b,address,16u,4u,1);
    if(!out) return KUI_GD_FAILED;
    if(!handle || handle!=b->work.handle || b->state==KUI_CDDA_BIOS_EMPTY) {
        for(unsigned i=0;i<4u;i++) write32(out+i*4u,0);
        return KUI_GD_NOT_FOUND;
    }
    write32(out,b->error?1u:0u);write32(out+4u,b->error);write32(out+8u,b->completed_bytes);
    write32(out+12u,b->state==KUI_CDDA_BIOS_QUEUED?4u:0u);
    int32_t status=b->status;
    if(b->state==KUI_CDDA_BIOS_TERMINAL) clear_queue(b);
    return status;
}
int32_t kui_cdda_bios_dispatch(struct kui_cdda_bios *b,uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    if(!valid(b) || r6) return -1;
    if(b->state==KUI_CDDA_BIOS_RUNNING)
        return r7==KUI_GD_REQUEST?0:r7==KUI_GD_CHECK?4:-1;
    switch(r7) {
    case KUI_GD_REQUEST:return request(b,r4,r5);
    case KUI_GD_CHECK:return check(b,r4,r5);
    case KUI_GD_EXEC:return 0;
    case KUI_GD_INIT:case KUI_GD_RESET:
        if(b->epoch_generation==UINT32_MAX) return -1;
        b->epoch_generation++;clear_queue(b);return 0;
    case KUI_GD_ABORT:
        if(b->state!=KUI_CDDA_BIOS_QUEUED || !r4 || r4!=b->work.handle) return -1;
        b->state=KUI_CDDA_BIOS_TERMINAL;b->status=KUI_GD_FAILED;b->error=KUI_GD_ERROR_CANCELLED;
        b->completed_bytes=0;return 0;
    case KUI_GD_DRIVE: {
        uint8_t *out=guest(b,r4,8u,4u,1);
        if(!out) return -1;
        write32(out,b->state==KUI_CDDA_BIOS_QUEUED?0u:b->drive_status);write32(out+4u,0x80u);return 0;
    }
    default:return -1;
    }
}
static bool same_work(const struct kui_cdda_bios_work *a,const struct kui_cdda_bios_work *b) {
    return a->handle==b->handle && a->epoch==b->epoch && a->command==b->command && a->kind==b->kind &&
        a->track==b->track && a->offset==b->offset && a->destination==b->destination && a->bytes==b->bytes &&
        a->audio.command==b->audio.command && a->audio.first==b->audio.first && a->audio.end==b->audio.end &&
        a->audio.frame==b->audio.frame && a->audio.repeat==b->audio.repeat;
}
enum kui_cdda_bios_result kui_cdda_bios_take(struct kui_cdda_bios *b,struct kui_cdda_bios_work *out) {
    if(!valid(b) || !out || aliases(out,sizeof(*out),b)) return KUI_CDDA_BIOS_INVALID;
    if(b->state==KUI_CDDA_BIOS_RUNNING) return KUI_CDDA_BIOS_BUSY;
    if(b->state!=KUI_CDDA_BIOS_QUEUED) return KUI_CDDA_BIOS_NOTHING;
    *out=b->work;b->state=KUI_CDDA_BIOS_RUNNING;return KUI_CDDA_BIOS_OK;
}
enum kui_cdda_bios_result kui_cdda_bios_validate_work(const struct kui_cdda_bios *b,
    const struct kui_cdda_bios_work *work) {
    if(!valid(b) || !work) return KUI_CDDA_BIOS_INVALID;
    return b->state==KUI_CDDA_BIOS_RUNNING && same_work(work,&b->work)?KUI_CDDA_BIOS_OK:KUI_CDDA_BIOS_STALE;
}
enum kui_cdda_bios_result kui_cdda_bios_complete(struct kui_cdda_bios *b,
    const struct kui_cdda_bios_work *work,bool success,uint32_t bytes) {
    enum kui_cdda_bios_result result=kui_cdda_bios_validate_work(b,work);
    if(result!=KUI_CDDA_BIOS_OK) return result;
    if(bytes!=(success?b->work.bytes:0u)) return KUI_CDDA_BIOS_INVALID;
    b->completed_bytes=bytes;b->error=success?KUI_GD_ERROR_NONE:KUI_GD_ERROR_IO;
    b->status=success?KUI_GD_COMPLETED:KUI_GD_FAILED;b->state=KUI_CDDA_BIOS_TERMINAL;
    return KUI_CDDA_BIOS_OK;
}
enum kui_cdda_bios_result kui_cdda_bios_observe_audio(struct kui_cdda_bios *b,enum kui_cdda_control_state state) {
    if(!valid(b)) return KUI_CDDA_BIOS_INVALID;
    uint32_t drive;
    switch(state) {
    case KUI_CDDA_CONTROL_STOPPED:drive=2u;break;
    case KUI_CDDA_CONTROL_PLAYING:drive=3u;break;
    case KUI_CDDA_CONTROL_PAUSED:case KUI_CDDA_CONTROL_EOF:drive=1u;break;
    case KUI_CDDA_CONTROL_FAULT:drive=9u;break;
    default:return KUI_CDDA_BIOS_INVALID;
    }
    b->drive_status=drive;return KUI_CDDA_BIOS_OK;
}
