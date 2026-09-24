/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_identity.h"
#include "kui/hash.h"
#include "../src/dreamcast/platform.h"
#include <dc/syscalls.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct {
    int status,type,status_error;
    unsigned polls,prepares,initializes,reads,phases;
    bool cancelled,prepare_fail,read_fail,poisoned,reading_phase,open_after_read;
    uint8_t raw[KUI_RAW_BYTES];
} fake;
static void seal(void) {
    uint32_t crc=kui_cd_edc(fake.raw,2064);
    for(unsigned i=0;i<4;i++) fake.raw[2064+i]=(uint8_t)(crc>>(i*8));
}
static void reset(const char *title) {
    memset(&fake,0,sizeof(fake));fake.status=CD_STATUS_PAUSED;fake.type=CD_GDROM;
    memset(fake.raw+1,255,10);fake.raw[15]=1;
    memcpy(fake.raw+16,"SEGA SEGAKATANA",14);
    memset(fake.raw+16+128,' ',128);
    size_t size=strlen(title);if(size>128) size=128;
    memcpy(fake.raw+16+128,title,size);seal();
}
int syscall_gdrom_check_drive(cd_check_drive_status_t *s) {
    ++fake.polls;s->status=(cd_stat_t)fake.status;s->disc_type=(cd_disc_types_t)fake.type;
    return fake.status_error;
}
bool kui_cancelled(void) {return fake.cancelled;}
bool kui_disc_prepare(struct kui_toc sessions[2]) {
    ++fake.prepares;
    if(fake.poisoned || fake.prepare_fail) return false;
    ++fake.initializes;
    memset(sessions,0,2*sizeof(*sessions));sessions[1].count=1;
    sessions[1].tracks[0].start=45150;sessions[1].tracks[0].control=4;
    fake.status=CD_STATUS_PAUSED;fake.type=CD_GDROM;
    if(fake.status_error<0) fake.status_error=0;
    return true;
}
void kui_disc_timing_phase(void *ctx,bool capturing) {
    assert(ctx==NULL);fake.reading_phase=capturing;++fake.phases;
}
enum kui_read_result kui_disc_read_raw(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    assert(!ctx && fad==45150 && sectors==1 && fake.reading_phase);
    ++fake.reads;
    if(fake.poisoned || fake.read_fail) return KUI_READ_FATAL;
    memcpy(out,fake.raw,sizeof(fake.raw));
    if(fake.open_after_read) fake.status=CD_STATUS_OPEN;
    return KUI_READ_OK;
}

static void identify(struct kui_disc_identity *id,const struct kui_disc_identity_ops *ops,uint64_t at) {
    assert(kui_disc_identity_poll(id,ops,at,true));
    assert(id->state==KUI_DISC_IDENTITY_PENDING);
    kui_disc_identity_read(id,ops);
}
int main(void) {
    const struct kui_disc_identity_ops *ops=kui_disc_identity_console_ops();assert(ops);
    struct kui_disc_identity id;
    reset("  OMIKRON THE NOMAD SOUL  ");kui_disc_identity_init(&id);
    for(unsigned i=0;i<30;i++) assert(!kui_disc_identity_poll(&id,ops,i*1000,false));
    assert(fake.polls==0 && fake.prepares==0 && fake.reads==0);
    identify(&id,ops,30000);
    assert(id.state==KUI_DISC_IDENTITY_READY && !strcmp(id.title,"OMIKRON THE NOMAD SOUL"));
    assert(fake.prepares==1 && fake.reads==1 && fake.phases==2 && !fake.reading_phase);
    unsigned polls=fake.polls;
    for(unsigned i=30001;i<30500;i++) assert(!kui_disc_identity_poll(&id,ops,i,true));
    assert(fake.polls==polls);
    for(unsigned i=30500;i<60000;i+=500) assert(!kui_disc_identity_poll(&id,ops,i,true));
    assert(fake.prepares==1 && fake.reads==1); /* settled media never keeps seeking */
    fake.status=CD_STATUS_SEEKING;assert(!kui_disc_identity_poll(&id,ops,60000,true));
    assert(id.state==KUI_DISC_IDENTITY_WAITING);
    fake.status=CD_STATUS_PAUSED;assert(!kui_disc_identity_poll(&id,ops,60500,true));
    assert(id.state==KUI_DISC_IDENTITY_READY && fake.reads==1);
    fake.status=CD_STATUS_OPEN;assert(!kui_disc_identity_poll(&id,ops,61000,true));
    assert(id.state==KUI_DISC_IDENTITY_OPEN && !id.title[0]);
    fake.status=CD_STATUS_NO_DISC;assert(!kui_disc_identity_poll(&id,ops,61500,true));
    assert(id.state==KUI_DISC_IDENTITY_EMPTY);
    fake.status=CD_STATUS_STANDBY;fake.type=CD_CDROM;
    assert(kui_disc_identity_poll(&id,ops,62000,true));
    fake.prepare_fail=true;kui_disc_identity_read(&id,ops);fake.prepare_fail=false;
    assert(id.state==KUI_DISC_IDENTITY_NON_GD && fake.reads==1);
    fake.type=CD_GDROM;identify(&id,ops,62500);assert(fake.reads==2);
    fake.status_error=-1;assert(!kui_disc_identity_poll(&id,ops,63000,true));
    assert(id.state==KUI_DISC_IDENTITY_ERROR && !id.title[0]);
    fake.status_error=0;assert(!kui_disc_identity_poll(&id,ops,63500,true));
    assert(fake.reads==2); /* status failure cannot cause an optical retry loop */

    reset("MDK2");kui_disc_identity_init(&id);fake.poisoned=true;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_ERROR && fake.prepares==1 && !fake.reads);
    for(unsigned i=500;i<5000;i+=500) assert(!kui_disc_identity_poll(&id,ops,i,true));
    assert(fake.prepares==1 && fake.poisoned);
    kui_disc_identity_invalidate(&id);identify(&id,ops,5000);
    assert(fake.poisoned && !fake.reads); /* even explicit refresh cannot reset the adapter */

    /* Startup has no INIT_CDROM: one guarded preparation must precede
     * treating an uninitialized BIOS status as a permanent reset condition. */
    reset("MDK2");kui_disc_identity_init(&id);fake.status=CD_STATUS_FATAL;
    identify(&id,ops,0);
    assert(id.state==KUI_DISC_IDENTITY_READY && fake.initializes==1);
    reset("MDK2");kui_disc_identity_init(&id);fake.status_error=-1;
    identify(&id,ops,0);
    assert(id.state==KUI_DISC_IDENTITY_READY && fake.initializes==1);
    reset("MDK2");kui_disc_identity_init(&id);
    fake.status=CD_STATUS_FATAL;fake.prepare_fail=true;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_RESET_REQUIRED);
    for(unsigned i=500;i<5000;i+=500) assert(!kui_disc_identity_poll(&id,ops,i,true));
    assert(fake.prepares==1 && !fake.initializes && !fake.reads);
    /* A status-only error is not the protected adapter's poison latch. */
    fake.status=CD_STATUS_OPEN;
    assert(!kui_disc_identity_poll(&id,ops,5000,true) && id.state==KUI_DISC_IDENTITY_OPEN);
    fake.status=CD_STATUS_RETRY;fake.type=CD_CDDA;fake.prepare_fail=false;
    identify(&id,ops,5500);assert(id.state==KUI_DISC_IDENTITY_READY && fake.reads==1);
    /* Fatal status after a successful read never triggers another prepare. */
    fake.status=CD_STATUS_FATAL;
    assert(!kui_disc_identity_poll(&id,ops,6000,true));
    assert(id.state==KUI_DISC_IDENTITY_RESET_REQUIRED && !id.title[0] && fake.prepares==2);
    /* Actual poisoned guard prevents init/read, including startup recovery.
     * Main stops invoking polling once its existing RESET REQUIRED latch fires. */
    reset("MDK2");kui_disc_identity_init(&id);
    fake.poisoned=true;fake.status=CD_STATUS_FATAL;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_RESET_REQUIRED);
    assert(fake.prepares==1 && !fake.initializes && !fake.reads && fake.poisoned);
    polls=fake.polls;
    for(unsigned i=500;i<5000;i+=500) assert(!kui_disc_identity_poll(&id,ops,i,false));
    assert(fake.polls==polls && fake.prepares==1);

    /* The pinned KOS wrapper accepts nonnegative syscall success. */
    reset("MDK2");kui_disc_identity_init(&id);fake.status_error=1;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_READY);
    assert(id.last_status_result==1 && id.last_status==CD_STATUS_PAUSED &&
           id.last_disc_type==CD_GDROM && fake.reads==1);
    reset("MDK2");kui_disc_identity_init(&id);fake.status=CD_STATUS_READ_FAIL;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_READY && fake.initializes==1);
    reset("MDK2");kui_disc_identity_init(&id);fake.type=CD_FAIL;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_READY && fake.initializes==1);
    reset("MDK2");kui_disc_identity_init(&id);fake.type=-1;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_READY && fake.initializes==1);
    /* No disc at boot: no optical setup, despite stale failure-type word. */
    reset("MDK2");kui_disc_identity_init(&id);
    fake.status=CD_STATUS_NO_DISC;fake.type=CD_FAIL;
    for(unsigned i=0;i<5000;i+=500) assert(!kui_disc_identity_poll(&id,ops,i,true));
    assert(id.state==KUI_DISC_IDENTITY_EMPTY && !fake.prepares && !fake.reads);
    /* New media can report RETRY while its type is still old or unknown. */
    fake.status=CD_STATUS_RETRY;fake.type=CD_CDDA;
    identify(&id,ops,5000);assert(id.state==KUI_DISC_IDENTITY_READY && fake.reads==1);
    fake.status=CD_STATUS_OPEN;fake.type=CD_FAIL;
    assert(!kui_disc_identity_poll(&id,ops,5500,true) && id.state==KUI_DISC_IDENTITY_OPEN);
    reset("SWORD OF THE BERSERK");fake.status=CD_STATUS_RETRY;fake.type=CD_FAIL;
    identify(&id,ops,6000);
    assert(!strcmp(id.title,"SWORD OF THE BERSERK") && fake.reads==1);
    /* Boot CD -> lid open -> GD insertion with stale, settled CD type:
     * require exactly one guarded INIT before trusting that type. */
    reset("BOOT CD");kui_disc_identity_init(&id);fake.type=CD_CDROM;
    assert(!kui_disc_identity_poll(&id,ops,0,true) && !fake.prepares);
    fake.status=CD_STATUS_OPEN;
    assert(!kui_disc_identity_poll(&id,ops,500,true));
    reset("MDK2");fake.type=CD_CDROM;
    identify(&id,ops,1000);
    assert(id.state==KUI_DISC_IDENTITY_READY && !strcmp(id.title,"MDK2") &&
           fake.prepares==1 && fake.reads==1);
    for(unsigned i=1500;i<5000;i+=500) assert(!kui_disc_identity_poll(&id,ops,i,true));
    assert(fake.prepares==1 && fake.reads==1);
    /* A title failure after insertion does not keep reinitializing the drive. */
    fake.status=CD_STATUS_OPEN;assert(!kui_disc_identity_poll(&id,ops,5000,true));
    fake.status=CD_STATUS_PAUSED;fake.type=CD_CDROM;fake.prepare_fail=true;
    identify(&id,ops,5500);
    assert(id.state==KUI_DISC_IDENTITY_NON_GD && fake.prepares==2 && fake.reads==1);
    for(unsigned i=6000;i<10000;i+=500) assert(!kui_disc_identity_poll(&id,ops,i,true));
    assert(fake.prepares==2 && fake.reads==1);
    /* A steady non-GD CD is displayed without a GD-ROM title read. */
    reset("MDK2");kui_disc_identity_init(&id);fake.type=CD_CDROM;
    assert(!kui_disc_identity_poll(&id,ops,0,true));
    assert(id.state==KUI_DISC_IDENTITY_NON_GD && !fake.prepares && !fake.reads);
    /* If unknown media is really a CD, a rejected GD prepare is classified
     * using the fresh status; polling does not keep issuing GETTOC/read. */
    reset("MDK2");kui_disc_identity_init(&id);fake.status=CD_STATUS_RETRY;
    fake.type=CD_CDROM;fake.prepare_fail=true;
    assert(kui_disc_identity_poll(&id,ops,0,true));
    fake.status=CD_STATUS_PAUSED;
    kui_disc_identity_read(&id,ops);
    assert(id.state==KUI_DISC_IDENTITY_NON_GD && fake.prepares==1 && !fake.reads);
    assert(!kui_disc_identity_poll(&id,ops,500,true));
    /* Invalidation does not renew the startup-error attempt. */
    reset("MDK2");kui_disc_identity_init(&id);fake.status_error=-1;fake.prepare_fail=true;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_ERROR);
    kui_disc_identity_invalidate(&id);
    assert(!kui_disc_identity_poll(&id,ops,500,true) && fake.prepares==1);
    fake.status_error=0;fake.status=CD_STATUS_RETRY;fake.type=-1;
    assert(kui_disc_identity_poll(&id,ops,1000,true));
    kui_disc_identity_read(&id,ops);
    for(unsigned i=1500;i<5000;i+=500) assert(!kui_disc_identity_poll(&id,ops,i,true));
    assert(fake.prepares==2 && !fake.reads);

    reset("MDK2");kui_disc_identity_init(&id);fake.status=CD_STATUS_RETRY;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_READY && fake.prepares==1);
    reset("MDK2");kui_disc_identity_init(&id);fake.status=CD_STATUS_RETRY;fake.prepare_fail=true;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_ERROR);
    assert(!kui_disc_identity_poll(&id,ops,500,true) && fake.prepares==1);

    reset("MDK2");kui_disc_identity_init(&id);fake.read_fail=true;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_ERROR && !fake.reading_phase);
    assert(!kui_disc_identity_poll(&id,ops,500,true) && fake.reads==1);
    reset("MDK2");kui_disc_identity_init(&id);fake.cancelled=true;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_ERROR && !fake.prepares && !fake.reads);
    reset("MDK2");kui_disc_identity_init(&id);fake.open_after_read=true;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_OPEN && !id.title[0]);

    reset("MDK2");kui_disc_identity_init(&id);fake.raw[160]^=1;
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_ERROR && !id.title[0]);
    reset("MDK2");kui_disc_identity_init(&id);fake.raw[16]='X';seal();
    identify(&id,ops,0);assert(id.state==KUI_DISC_IDENTITY_ERROR && !id.title[0]);
    reset("");kui_disc_identity_init(&id);identify(&id,ops,0);
    assert(!strcmp(id.title,"Untitled GD-ROM"));
    reset("TITLE\nWITH\001CONTROL");kui_disc_identity_init(&id);identify(&id,ops,0);
    assert(!strcmp(id.title,"TITLE?WITH?CONTROL"));
    assert(kui_disc_identity_text(KUI_DISC_IDENTITY_NON_GD)[0]);
    puts("PASS idle disc status: nonnegative BIOS success, unknown-type insertion, guarded startup, open/empty/replacement, bounded retries, title EDC/signature, CD exclusion, cancellation, poisoned-adapter preservation");
    return 0;
}
