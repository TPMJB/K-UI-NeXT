/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_identity.h"
#include "kui/hash.h"
#include "../dreamcast/platform.h"
#include <string.h>

/* Values from upstream KOS dc/syscalls.h at the dependencies.json pin. The
 * portable detector keeps these private; production compile-time checks below
 * ensure a changed header cannot silently alter the interpretation. */
enum { DRIVE_PAUSED=1, DRIVE_STANDBY=2, DRIVE_PLAYING=3,
       DRIVE_OPEN=6, DRIVE_EMPTY=7, DRIVE_RETRY=8, DRIVE_ERROR=9,
       DRIVE_FATAL=12, DISC_GD=0x80, DISC_FAIL=0xf0 };

static bool usable(const struct kui_disc_identity_ops *ops) {
    return ops && ops->status && ops->prepare && ops->read_one && ops->cancelled;
}
static void clear(struct kui_disc_identity *id,enum kui_disc_identity_state state) {
    id->state=state;id->title[0]=0;id->needs_identification=false;
}
void kui_disc_identity_init(struct kui_disc_identity *id) {
    if(!id) return;
    memset(id,0,sizeof(*id));id->disc_type=-1;id->armed=true;
}
void kui_disc_identity_invalidate(struct kui_disc_identity *id) {
    if(id && id->state!=KUI_DISC_IDENTITY_RESET_REQUIRED) kui_disc_identity_init(id);
}
static void accept_status(struct kui_disc_identity *id,int status,int type) {
    if(status==DRIVE_FATAL || type==DISC_FAIL) {
        clear(id,KUI_DISC_IDENTITY_RESET_REQUIRED);id->armed=false;return;
    }
    if(status==DRIVE_OPEN || status==DRIVE_EMPTY) {
        clear(id,status==DRIVE_OPEN?KUI_DISC_IDENTITY_OPEN:KUI_DISC_IDENTITY_EMPTY);
        id->disc_type=-1;id->armed=true;return;
    }
    if(status==DRIVE_RETRY && type==DISC_GD && id->armed) {
        /* A newly inserted GD may need the adapter's normal INIT before it
         * becomes ready. Permit that once; a failed attempt is not retried. */
        id->disc_type=type;id->state=KUI_DISC_IDENTITY_PENDING;
        id->needs_identification=true;return;
    }
    if(status<0 || status==DRIVE_ERROR || status==DRIVE_RETRY) {
        clear(id,KUI_DISC_IDENTITY_ERROR);return;
    }
    if(status!=DRIVE_PAUSED && status!=DRIVE_STANDBY && status!=DRIVE_PLAYING) {
        /* Busy/seek/scan is not proof of a media change. Retain the cached
         * title internally, but presentation says waiting until status settles. */
        id->state=KUI_DISC_IDENTITY_WAITING;id->needs_identification=false;return;
    }
    if(id->disc_type!=type) {
        id->title[0]=0;id->armed=true;id->disc_type=type;
    }
    if(type!=DISC_GD) {clear(id,KUI_DISC_IDENTITY_NON_GD);return;}
    if(id->armed) {
        id->state=KUI_DISC_IDENTITY_PENDING;id->needs_identification=true;
    } else id->state=id->title[0]?KUI_DISC_IDENTITY_READY:KUI_DISC_IDENTITY_ERROR;
}
bool kui_disc_identity_poll(struct kui_disc_identity *id,
    const struct kui_disc_identity_ops *ops,uint64_t now_ms,bool io_idle) {
    if(!id || !io_idle || id->state==KUI_DISC_IDENTITY_RESET_REQUIRED) return false;
    if(!usable(ops)) {clear(id,KUI_DISC_IDENTITY_ERROR);return false;}
    if(id->observed && now_ms<id->next_poll_ms) return id->needs_identification;
    id->observed=true;
    id->next_poll_ms=now_ms>UINT64_MAX-KUI_DISC_IDENTITY_POLL_MS?
        UINT64_MAX:now_ms+KUI_DISC_IDENTITY_POLL_MS;
    int status=-1,type=-1;
    if(ops->status(ops->ctx,&status,&type)!=0) clear(id,KUI_DISC_IDENTITY_ERROR);
    else accept_status(id,status,type);
    return id->needs_identification;
}
void kui_disc_identity_read(struct kui_disc_identity *id,
    const struct kui_disc_identity_ops *ops) {
    if(!id || !id->needs_identification || id->state!=KUI_DISC_IDENTITY_PENDING) return;
    id->armed=false;clear(id,KUI_DISC_IDENTITY_READING);
    uint8_t raw[KUI_RAW_BYTES];
    if(!usable(ops) || ops->cancelled(ops->ctx) || !ops->prepare(ops->ctx) ||
       ops->cancelled(ops->ctx) || !ops->read_one(ops->ctx,raw) ||
       ops->cancelled(ops->ctx)) {clear(id,KUI_DISC_IDENTITY_ERROR);return;}
    int offset=kui_data_offset(raw);
    if(offset<0 || !kui_sector_edc_valid(raw) ||
       memcmp(raw+offset,"SEGA SEGAKATANA",14)) {clear(id,KUI_DISC_IDENTITY_ERROR);return;}
    char title[KUI_DISC_IDENTITY_TITLE_CAP];
    for(unsigned i=0;i<128;i++) {
        unsigned ch=raw[(unsigned)offset+128+i];
        title[i]=ch>=32 && ch<=126?(char)ch:'?';
    }
    title[128]=0;
    size_t n=128;while(n && title[n-1]==' ') title[--n]=0;
    size_t first=0;while(first<n && title[first]==' ') ++first;
    if(first) {memmove(title,title+first,n-first+1);n-=first;}
    if(!n) strcpy(title,"Untitled GD-ROM");
    /* A lid opening during initialization/read must not publish an old title.
     * This is another cheap status observation, never another sector read. */
    int status=-1,type=-1;
    if(ops->status(ops->ctx,&status,&type)!=0) {clear(id,KUI_DISC_IDENTITY_ERROR);return;}
    accept_status(id,status,type);
    if(type!=DISC_GD || (status!=DRIVE_PAUSED && status!=DRIVE_STANDBY && status!=DRIVE_PLAYING)) return;
    strcpy(id->title,title);id->state=KUI_DISC_IDENTITY_READY;
    id->armed=false;id->needs_identification=false;
}
const char *kui_disc_identity_text(enum kui_disc_identity_state state) {
    switch(state) {
    case KUI_DISC_IDENTITY_OPEN:return "Disc lid open";
    case KUI_DISC_IDENTITY_EMPTY:return "No disc inserted";
    case KUI_DISC_IDENTITY_NON_GD:return "CD inserted - insert a retail GD-ROM";
    case KUI_DISC_IDENTITY_PENDING:case KUI_DISC_IDENTITY_READING:return "Identifying disc...";
    case KUI_DISC_IDENTITY_WAITING:return "Waiting for the drive...";
    case KUI_DISC_IDENTITY_READY:return "Retail GD-ROM";
    case KUI_DISC_IDENTITY_ERROR:return "Disc title unavailable";
    case KUI_DISC_IDENTITY_RESET_REQUIRED:return "Drive requires a console reset";
    default:return "Checking disc...";
    }
}

#ifdef KUI_ON_CONSOLE
#include <dc/syscalls.h>
_Static_assert((int)CD_STATUS_PAUSED==DRIVE_PAUSED && (int)CD_STATUS_STANDBY==DRIVE_STANDBY &&
    (int)CD_STATUS_PLAYING==DRIVE_PLAYING && (int)CD_STATUS_OPEN==DRIVE_OPEN &&
    (int)CD_STATUS_NO_DISC==DRIVE_EMPTY && (int)CD_STATUS_RETRY==DRIVE_RETRY &&
    (int)CD_STATUS_ERROR==DRIVE_ERROR && (int)CD_STATUS_FATAL==DRIVE_FATAL &&
    (int)CD_GDROM==DISC_GD && (int)CD_FAIL==DISC_FAIL,"Pinned KOS status values changed");
static int console_status(void *ctx,int *status,int *type) {
    (void)ctx;
    cd_check_drive_status_t result;
    int rv=syscall_gdrom_check_drive(&result);
    if(rv==0) {*status=result.status;*type=result.disc_type;}
    return rv;
}
static bool console_prepare(void *ctx) {
    (void)ctx;
    struct kui_toc sessions[2];
    /* This checks the existing poisoned latch BEFORE any initialization. Do
     * not call syscall_gdrom_init/reset here or replace this with KOS reinit. */
    return kui_disc_prepare(sessions) && sessions[1].count &&
        sessions[1].tracks[0].start==45150 && (sessions[1].tracks[0].control&4);
}
static bool console_read(void *ctx,uint8_t *raw) {
    (void)ctx;
    /* Exactly one guarded sector request, no repeated identity samples. The
     * adapter owns static firmware buffers even if abort recovery fails; raw
     * receives bytes only after successful completion. Capture resets its own
     * profiling before it starts, so this adds no work to its acquisition loop. */
    kui_disc_timing_phase(NULL,true);
    enum kui_read_result result=kui_disc_read_raw(NULL,45150,1,raw);
    kui_disc_timing_phase(NULL,false);
    return result==KUI_READ_OK;
}
static bool console_cancelled(void *ctx) {(void)ctx;return kui_cancelled();}
static const struct kui_disc_identity_ops console={NULL,console_status,console_prepare,console_read,console_cancelled};
const struct kui_disc_identity_ops *kui_disc_identity_console_ops(void) {return &console;}
#else
const struct kui_disc_identity_ops *kui_disc_identity_console_ops(void) {return NULL;}
#endif
