/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_deferred_io.h"
#include <string.h>

#define DEFERRED_MAGIC UINT32_C(0x314a4d56)
static int span(const void *p,size_t n) {
    return p && n<=UINTPTR_MAX-(uintptr_t)p;
}
static int overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y?y-x<an:x-y<bn);
}
static void increment(uint32_t *p) {if(*p!=UINT32_MAX) ++*p;}
static int valid(const struct kui_vmu_deferred_io *s) {
    return span(s,sizeof(*s)) && s->self==s && s->magic==DEFERRED_MAGIC &&
        s->provider.read_block && s->provider.commit_block &&
        s->guard.enter && s->guard.leave && s->guard.worker_ready;
}
static int fault(struct kui_vmu_deferred_io *s) {
    if(s->state!=KUI_VMU_DEFERRED_FAULT) increment(&s->failures);
    s->state=KUI_VMU_DEFERRED_FAULT;return KUI_VMU_EMU_IO_ERROR;
}
int kui_vmu_deferred_io_init(struct kui_vmu_deferred_io *s,
    const struct kui_vmu_emulation_io *provider,const struct kui_vmu_deferred_guard *guard) {
    if(!span(s,sizeof(*s)) || !span(provider,sizeof(*provider)) ||
       !span(guard,sizeof(*guard)) || overlap(s,sizeof(*s),provider,sizeof(*provider)) ||
       overlap(s,sizeof(*s),guard,sizeof(*guard)) || !provider->read_block ||
       !provider->commit_block || provider->read_block==kui_vmu_deferred_io_read ||
       provider->commit_block==kui_vmu_deferred_io_commit ||
       !guard->enter || !guard->leave || !guard->worker_ready) return -1;
    const uint8_t *raw=(const uint8_t *)s;
    for(size_t i=0;i<sizeof(*s);i++) if(raw[i]) return -1;
    s->provider=*provider;s->guard=*guard;s->magic=DEFERRED_MAGIC;s->self=s;return 0;
}
static int request(struct kui_vmu_deferred_io *s,unsigned operation,uint16_t block,
    void *read_out,const void *write_in) {
    const void *data=operation==KUI_VMU_DEFERRED_READ?read_out:write_in;
    uint32_t saved;
    if(!valid(s) || block>=KUI_VMU_EMU_BLOCK_COUNT || !span(data,512u) ||
       overlap(s,sizeof(*s),data,512u)) return KUI_VMU_EMU_IO_ERROR;
    if(!s->guard.enter(s->guard.context,&saved)) return KUI_VMU_EMU_IO_ERROR;
    int result=KUI_VMU_EMU_IO_BUSY;
    if(s->state==KUI_VMU_DEFERRED_FAULT) result=KUI_VMU_EMU_IO_ERROR;
    else if(s->state>KUI_VMU_DEFERRED_FAULT) result=fault(s);
    else if(s->state==KUI_VMU_DEFERRED_IDLE) {
        if(s->ticket==UINT32_MAX) result=fault(s);
        else {
            s->operation=operation;s->block=block;++s->ticket;
            if(write_in) memcpy(s->bytes,write_in,512u);
            increment(&s->submissions);s->state=KUI_VMU_DEFERRED_PENDING;
        }
    } else if(s->operation==operation && s->block==block &&
              s->state==KUI_VMU_DEFERRED_DONE &&
              (!write_in || !memcmp(s->bytes,write_in,512u))) {
        if(read_out) memcpy(read_out,s->bytes,512u);
        s->state=KUI_VMU_DEFERRED_IDLE;result=KUI_VMU_EMU_IO_OK;
    }
    s->guard.leave(s->guard.context,saved);return result;
}
int kui_vmu_deferred_io_read(void *context,uint16_t block,uint8_t data[512]) {
    return request(context,KUI_VMU_DEFERRED_READ,block,data,NULL);
}
int kui_vmu_deferred_io_commit(void *context,uint16_t block,const uint8_t data[512]) {
    return request(context,KUI_VMU_DEFERRED_COMMIT,block,NULL,data);
}
int kui_vmu_deferred_io_poison(struct kui_vmu_deferred_io *s) {
    uint32_t saved;
    if(!valid(s) || !s->guard.enter(s->guard.context,&saved)) return -1;
    (void)fault(s);s->guard.leave(s->guard.context,saved);return 0;
}
int kui_vmu_deferred_io_work(struct kui_vmu_deferred_io *s) {
    uint32_t saved,ticket,operation,block;
    if(!valid(s) || !s->guard.worker_ready(s->guard.context) ||
       !s->guard.enter(s->guard.context,&saved)) return -2;
    if(s->state!=KUI_VMU_DEFERRED_PENDING) {
        int result=s->state==KUI_VMU_DEFERRED_FAULT?-1:0;
        if(s->state>KUI_VMU_DEFERRED_FAULT) result=fault(s);
        s->guard.leave(s->guard.context,saved);return result;
    }
    ticket=s->ticket;operation=s->operation;block=s->block;
    if(!ticket || block>=KUI_VMU_EMU_BLOCK_COUNT ||
       (operation!=KUI_VMU_DEFERRED_READ && operation!=KUI_VMU_DEFERRED_COMMIT)) {
        (void)fault(s);s->guard.leave(s->guard.context,saved);return -1;
    }
    s->state=KUI_VMU_DEFERRED_RUNNING;s->guard.leave(s->guard.context,saved);
    /* No lock, guest address, Maple fence or interrupt mask crosses I/O. */
    int io=operation==KUI_VMU_DEFERRED_READ?
        s->provider.read_block(s->provider.context,(uint16_t)block,s->bytes):
        s->provider.commit_block(s->provider.context,(uint16_t)block,s->bytes);
    if(!s->guard.enter(s->guard.context,&saved)) return -2; /* no ACK possible */
    int result;
    if(s->state!=KUI_VMU_DEFERRED_RUNNING || s->ticket!=ticket ||
       s->operation!=operation || s->block!=block) result=fault(s);
    else if(io==KUI_VMU_EMU_IO_BUSY) {
        increment(&s->provider_busy);s->state=KUI_VMU_DEFERRED_PENDING;result=0;
    } else if(io==KUI_VMU_EMU_IO_OK) {
        increment(&s->completions);s->state=KUI_VMU_DEFERRED_DONE;result=1;
    } else result=fault(s);
    s->guard.leave(s->guard.context,saved);return result;
}
