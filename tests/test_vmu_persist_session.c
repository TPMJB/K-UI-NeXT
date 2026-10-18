/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse only the independently constructed sparse filesystem fixture. Its
 * admission tests retain their own main; this suite supplies a separate raw
 * physical image/journal backend and exercises the session boundary. */
#define main vmu_session_unused_fs_fixture_main
#include "test_vmu_fs_admission.c"
#undef main
#include "kui/vmu_persist_session.h"
#include "kui/hash.h"
#include "kui/retail_image.h"

static struct {
    uint8_t base[256][512],journal[2][512];
    struct kui_vmu_persist_profile expected;
    struct kui_vmu_persist_session session,reopened;
    struct kui_vmu_image_workspace image_work;
    struct kui_vmu_persist_session_identity actual;
    unsigned reads,writes,syncs,identities,healths,fail_read,fail_write,fail_sync,fail_identity;
    unsigned bad_health,wrong_at_identity,corrupt_after_write,reenter;
    unsigned refuse_first_write_unless_admitted;
} media;
static int session_identity(void *ctx,struct kui_vmu_persist_session_identity *out) {
    assert(ctx==&media);++media.identities;
    if(media.fail_identity==media.identities) return -1;
    *out=media.actual;
    if(media.wrong_at_identity && media.identities>=media.wrong_at_identity) out->cid[0]^=1u;
    return 0;
}
static bool session_healthy(void *ctx) {
    assert(ctx==&media);++media.healths;
    if(media.reenter) {
        uint8_t out[512];memset(out,0x5a,sizeof(out));unsigned identities=media.identities;
        assert(kui_vmu_persist_session_read(&media.session,0u,out)==KUI_VMU_PERSIST_SESSION_BUSY);
        assert(media.identities==identities && media.session.busy);
        for(unsigned i=0;i<512u;i++) assert(out[i]==0x5a);
    }
    return !media.bad_health;
}
static uint8_t *session_sector(uint64_t sector) {
    uint64_t first=disk.partition+lba(20u);
    if(sector>=first && sector-first<256u) return media.base[sector-first];
    first=disk.partition+lba(60u);
    if(sector>=first && sector-first<2u) return media.journal[sector-first];
    return NULL;
}
static int session_raw_read(void *ctx,uint64_t sector,void *out) {
    assert(ctx==&media);++media.reads;
    if(media.fail_read==media.reads) return -1;
    uint8_t *p=session_sector(sector);
    if(p) memcpy(out,p,512u);
    else {assert(sector<=UINT32_MAX);return read_sector(&disk,(uint32_t)sector,out);}
    return 0;
}
static int session_raw_write(void *ctx,uint64_t sector,const void *data) {
    assert(ctx==&media);uint8_t *p=session_sector(sector);assert(p);
    if(media.refuse_first_write_unless_admitted) {
        assert(media.session.admitted && media.session.fs_result==0);
        assert(media.session.phase!=KUI_VMU_PERSIST_SESSION_ADMISSION);
    }
    ++media.writes;if(media.fail_write==media.writes) return -1;
    memcpy(p,data,512u);
    if(media.corrupt_after_write && sector>=lba(20u) && sector<lba(20u)+256u) p[0]^=1u;
    return 0;
}
static int session_raw_sync(void *ctx) {
    assert(ctx==&media);++media.syncs;
    return media.fail_sync==media.syncs?-1:0;
}
static struct kui_vmu_persist_session_ops session_ops(void) {
    return (struct kui_vmu_persist_session_ops){&media,session_identity,session_raw_read,
        session_raw_write,session_raw_sync,session_healthy};
}
static int session_role_read(void *ctx,unsigned role,uint32_t logical,uint8_t out[512]) {
    assert(ctx==&media);uint32_t sector;
    assert(kui_vmu_persist_profile_map_lba(&media.expected,role,logical,&sector)==0);
    return session_raw_read(ctx,sector,out);
}
static int session_open(struct kui_vmu_persist_session *s) {
    struct kui_vmu_persist_session_ops ops=session_ops();
    return kui_vmu_persist_session_init(s,&media.expected,&disk.input,&ops,&disk.work,&media.image_work);
}
static void session_fresh(void) {
    fresh();memset(&media,0,sizeof(media));
    for(unsigned b=0;b<256u;b++) assert(!kui_vmu_image_blank_block((uint16_t)b,media.base[b]));
    assert(!kui_vmu_fs_collect(&disk.input,&media.expected,&disk.work));
    memcpy(media.actual.cid,disk.input.cid,16u);media.actual.card_sectors=disk.input.card_sectors;
    media.actual.high_capacity=disk.input.high_capacity;
    assert(!kui_vmu_persist_profile_collect_snapshot(&media.expected,session_role_read,&media));
    media.reads=media.identities=media.healths=0u;
    media.refuse_first_write_unless_admitted=1u;
}
static void session_current_contents_and_clean_handoff(void) {
    session_fresh();assert(!session_open(&media.session));
    assert(media.session.ready && media.session.snapshot_valid && !media.writes && !media.syncs);
    assert(!memcmp(media.session.base_sha256,media.expected.base_sha256,32u));
    uint8_t block[512],out[512];memset(block,0x3c,sizeof(block));
    assert(!kui_vmu_persist_session_commit(&media.session,123u,block));
    assert(media.writes==4u && media.syncs==4u && media.session.store.sequence==1u);
    assert(!media.session.snapshot_valid && !memcmp(media.base[123],block,512u));
    assert(!kui_vmu_persist_session_read(&media.session,123u,out) && !memcmp(out,block,512u));
    assert(!kui_vmu_persist_session_refresh(&media.session,&media.image_work));
    assert(media.session.snapshot_valid && memcmp(media.session.base_sha256,media.expected.base_sha256,32u));
    /* Old preparation provenance is accepted after a legitimate saved write;
     * the actual physical redo is recovered, never rebound to synthetic LBAs. */
    media.refuse_first_write_unless_admitted=0u;unsigned writes=media.writes;
    assert(!session_open(&media.reopened));assert(media.writes==writes+1u);
    assert(media.reopened.store.sequence==1u && media.reopened.snapshot_valid);
    assert(!memcmp(media.reopened.base_sha256,media.session.base_sha256,32u));
    assert(!kui_vmu_persist_session_checkpoint(&media.reopened));
    for(unsigned i=0;i<512u;i++) assert(!media.journal[1][i]);
    assert(media.reopened.ready && !media.reopened.faulted);
    assert(session_open(&media.reopened)==KUI_VMU_PERSIST_SESSION_BLOCKED);
}
static void session_admission_before_mutation(void) {
    session_fresh();file_entry(disk.dirs[3]+96u,"card.journal",32u,62u,1024u,3u);allocated(62u,1);
    assert(session_open(&media.session)==KUI_VMU_PERSIST_SESSION_ALLOCATION);
    assert(media.session.fs_result==KUI_VMU_FS_CHANGED && !media.writes && !media.syncs && media.session.faulted);
    session_fresh();file_entry(disk.dirs[3],"card.vmd",33u,20u,131072u,3u);
    assert(session_open(&media.session)==KUI_VMU_PERSIST_SESSION_ALLOCATION && !media.writes);
    session_fresh();media.actual.cid[0]^=1u;
    assert(session_open(&media.session)==KUI_VMU_PERSIST_SESSION_IDENTITY && !media.reads && !media.writes);
    session_fresh();media.actual.card_sectors++;
    assert(session_open(&media.session)==KUI_VMU_PERSIST_SESSION_IDENTITY && !media.reads);
    session_fresh();media.actual.high_capacity=0u;
    assert(session_open(&media.session)==KUI_VMU_PERSIST_SESSION_IDENTITY && !media.reads);
    session_fresh();media.wrong_at_identity=2u;
    assert(session_open(&media.session)==KUI_VMU_PERSIST_SESSION_IDENTITY && !media.writes);
    session_fresh();media.base[255][0]=0u;
    assert(session_open(&media.session)==KUI_VMU_PERSIST_SESSION_IMAGE && !media.writes && !media.session.ready);
}
static void session_commit_guards_and_preserved_reads(void) {
    uint8_t block[512],out[512];memset(block,0x21,sizeof(block));
    session_fresh();assert(!session_open(&media.session));media.reenter=1u;
    assert(!kui_vmu_persist_session_commit(&media.session,5u,block));media.reenter=0u;
    assert(media.session.ready && !media.session.faulted && !media.session.busy);
    session_fresh();assert(!session_open(&media.session));media.actual.cid[0]^=1u;
    assert(kui_vmu_persist_session_commit(&media.session,5u,block)==KUI_VMU_PERSIST_SESSION_IDENTITY);
    assert(!media.writes && media.session.faulted);
    assert(kui_vmu_persist_session_commit(&media.session,5u,block)==KUI_VMU_PERSIST_SESSION_BLOCKED);
    session_fresh();assert(!session_open(&media.session));media.wrong_at_identity=media.identities+2u;
    assert(kui_vmu_persist_session_commit(&media.session,5u,block)==KUI_VMU_PERSIST_SESSION_IDENTITY);
    assert(!media.writes); /* The first write independently probes the card. */
    session_fresh();assert(!session_open(&media.session));media.corrupt_after_write=1u;
    assert(kui_vmu_persist_session_commit(&media.session,5u,block)==KUI_VMU_PERSIST_SESSION_VERIFY);
    assert(media.session.faulted && !media.session.snapshot_valid);
    session_fresh();assert(!session_open(&media.session));media.base[5][0]^=1u;memset(out,0xa5,sizeof(out));
    assert(kui_vmu_persist_session_read(&media.session,5u,out)==KUI_VMU_PERSIST_SESSION_VERIFY);
    for(unsigned i=0;i<512u;i++) assert(out[i]==0xa5u);
    session_fresh();assert(!session_open(&media.session));unsigned reads=media.reads;
    assert(kui_vmu_persist_session_read(&media.session,256u,out)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    assert(kui_vmu_persist_session_commit(&media.session,0u,media.session.scratch)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    assert(media.reads==reads && media.session.ready);
    struct kui_vmu_persist_session moved=media.session;
    assert(kui_vmu_persist_session_read(&moved,0u,out)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    kui_vmu_persist_session_poison(&media.session);
    assert(kui_vmu_persist_session_read(&media.session,0u,out)==KUI_VMU_PERSIST_SESSION_BLOCKED);
}
static void session_bound_callbacks_and_faults(void) {
    uint8_t block[512];memset(block,0x55,sizeof(block));
    /* Even accidental direct access to the embedded store cannot escape an
     * active public transaction or reach slack/identity/filesystem sectors. */
    session_fresh();assert(!session_open(&media.session));unsigned reads=media.reads;
    assert(media.session.store.io.read_sector(&media.session,lba(61u),block));
    assert(media.reads==reads && !media.writes && media.session.faulted);
    for(unsigned kind=0;kind<4u;kind++) for(unsigned cut=1u;cut<=(kind==0u?4u:kind==1u?4u:kind==2u?1u:10u);cut++) {
        session_fresh();assert(!session_open(&media.session));
        if(kind==0u) media.fail_write=cut;
        if(kind==1u) media.fail_sync=cut;
        if(kind==2u) media.fail_read=media.reads+cut;
        if(kind==3u) media.fail_identity=media.identities+cut;
        assert(kui_vmu_persist_session_commit(&media.session,12u,block));
        assert(media.session.faulted && !media.session.ready && !media.session.snapshot_valid);
        unsigned calls=media.reads+media.writes+media.syncs+media.identities;
        assert(kui_vmu_persist_session_checkpoint(&media.session)==KUI_VMU_PERSIST_SESSION_BLOCKED);
        assert(media.reads+media.writes+media.syncs+media.identities==calls);
    }
    session_fresh();assert(!session_open(&media.session));media.bad_health=1u;
    assert(kui_vmu_persist_session_commit(&media.session,0u,block)==KUI_VMU_PERSIST_SESSION_IO && !media.writes);
}
int main(void) {
    session_current_contents_and_clean_handoff();session_admission_before_mutation();
    session_commit_guards_and_preserved_reads();session_bound_callbacks_and_faults();
    puts("VMU persistent session: fresh physical ownership, recovery, current hashes, bounded I/O, identity, durability/readback and fault latching passed");
    return 0;
}
