/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored host filesystem admission followed by a closed-writer epoch. */
#define KUI_VMU_PERSIST_STAGE_MAIN rebind_unused_stage_fixture_main
#include "test_vmu_persist_stage.c"

static struct {
    struct kui_vmu_persist_allocation_transfer transfer;
    struct kui_vmu_persist_session reentrant;
    unsigned metadata_reads,wrong_identity_at,mutate_profile_identity,mutate_profile_read;
    unsigned mutate_transfer_read,reenter_identity,unhealthy,mutate_final_transfer,mutate_stage_profile;
    int reentry_result;
} rebound;
static struct kui_vmu_persist_session_ops rebind_ops(void);
static int rebind_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    int result=stage_identity(context,out);
    if(rebound.wrong_identity_at && card.identities>=rebound.wrong_identity_at)out->cid[0]^=1u;
    if(rebound.mutate_profile_identity==card.identities)card.expected.geometry.volume_serial^=1u;
    if(rebound.mutate_stage_profile==1u && card.work.phase==KUI_VMU_PERSIST_STAGE_ZERO_MARKER)
        card.expected.files[0].useful_count=1000u;
    if(rebound.mutate_stage_profile==2u && card.work.phase==KUI_VMU_PERSIST_STAGE_CHECKPOINT)
        card.expected.geometry.volume_serial^=1u;
    if(rebound.reenter_identity==card.identities) {
        struct kui_vmu_persist_session_ops ops=rebind_ops();
        rebound.reentry_result=kui_vmu_persist_session_rebind(&rebound.reentrant,&card.expected,
            &rebound.transfer,&ops,&card.work.image);
    }
    return result;
}
static int rebind_read(void *context,uint64_t sector,void *out) {
    if(!stage_sector(sector)) {++rebound.metadata_reads;assert(!"host metadata read inside retained epoch");}
    int result=stage_read(context,sector,out);
    if(rebound.mutate_profile_read==card.reads)card.expected.geometry.volume_serial^=1u;
    if(rebound.mutate_transfer_read==card.reads)rebound.transfer.domain_binding_sha256[0]^=1u;
    if(rebound.mutate_final_transfer && card.work.phase==KUI_VMU_PERSIST_STAGE_ZERO_MARKER)
        rebound.transfer.domain_binding_sha256[1]^=1u;
    return result;
}
static int rebind_write(void *context,uint64_t sector,const void *in) {
    assert(card.work.session.admitted && card.work.session.phase!=KUI_VMU_PERSIST_SESSION_REBIND);
    return stage_write(context,sector,in);
}
static bool rebind_health(void *context) {assert(context==&card);return !rebound.unhealthy;}
static struct kui_vmu_persist_session_ops rebind_ops(void) {
    return (struct kui_vmu_persist_session_ops){&card,rebind_identity,rebind_read,rebind_write,stage_sync,rebind_health};
}
static void rebind_mint(unsigned previous,unsigned next,uint32_t sequence) {
    struct kui_vmu_persist_allocation_transfer *t=&rebound.transfer;
    memset(t,0,sizeof(*t));t->magic=KUI_VMU_ALLOCATION_TRANSFER_MAGIC;
    t->version=KUI_VMU_ALLOCATION_TRANSFER_VERSION;t->bytes=sizeof(*t);
    t->state=KUI_VMU_ALLOCATION_TRANSFER_READY;t->card_sectors=card.expected.card_sectors;
    t->partition_first=card.expected.partition_first;t->partition_end=card.expected.partition_end;
    t->high_capacity=card.expected.high_capacity;t->generation=29u;t->sequence=sequence;
    t->previous_owner=previous;t->next_owner=next;memcpy(t->cid,card.expected.cid,16u);
    memcpy(t->image_id,card.expected.image_id,16u);
    assert(!kui_vmu_persist_profile_ownership_sha256(&card.expected,t->ownership_sha256));
    struct kui_sha256 hash;kui_sha256_init(&hash);static const char domain[]="authored live allocation epoch";
    kui_sha256_update(&hash,domain,sizeof(domain));kui_sha256_update(&hash,t->ownership_sha256,32u);
    kui_sha256_digest(&hash,t->domain_binding_sha256);
}
static void rebind_fresh(void) {
    stage_fresh();memset(&rebound,0,sizeof(rebound));
    /* Only the fixture lifecycle owner mints a transfer after full admission.
     * Subsequent callbacks deliberately reject every metadata-sector read. */
    assert(!kui_vmu_fs_admit(&disk.input,&card.expected,&disk.work));
    rebind_mint(1u,2u,2u);card.reads=card.writes=card.syncs=card.identities=0u;
}
static int rebind_session(void) {
    struct kui_vmu_persist_session_ops ops=rebind_ops();card.work.phase=KUI_VMU_PERSIST_STAGE_RECOVER;
    return kui_vmu_persist_session_rebind(&card.work.session,&card.expected,&rebound.transfer,&ops,&card.work.image);
}
static int rebind_stage(void) {
    struct kui_vmu_persist_session_ops ops=rebind_ops();
    return kui_vmu_persist_stage_rebind(&card.expected,&card.game,&rebound.transfer,&ops,&card.work,&card.handoff);
}
static void rebind_snapshot_policy(void) {
    for(unsigned recovery=0u;recovery<2u;recovery++) {
        struct kui_vmu_durable_handoff expected;
        rebind_fresh();if(recovery)stage_seed_real_marker();else card.base[123][7]^=0x71u;
        assert(!rebind_stage());expected=card.handoff;
        unsigned reads=card.reads,writes=card.writes,syncs=card.syncs;
        rebind_fresh();if(recovery)stage_seed_real_marker();else card.base[123][7]^=0x71u;
        struct kui_vmu_persist_session_ops ops=rebind_ops();
        assert(!kui_vmu_persist_stage_rebind_policy(&card.expected,&card.game,&rebound.transfer,&ops,
            &kui_vmu_persist_stage_exclusive_checkpoint,&card.work,&card.handoff));
        assert(!memcmp(&expected,&card.handoff,sizeof(expected)) && card.reads+526u==reads);
        assert(card.writes==writes && card.syncs==syncs && !rebound.metadata_reads);
        assert(rebound.transfer.state==KUI_VMU_ALLOCATION_TRANSFER_CONSUMED);
    }
    rebind_fresh();rebound.mutate_final_transfer=1u;
    struct kui_vmu_persist_session_ops ops=rebind_ops();
    assert(kui_vmu_persist_stage_rebind_policy(&card.expected,&card.game,&rebound.transfer,&ops,
        &kui_vmu_persist_stage_exclusive_checkpoint,&card.work,&card.handoff));
    stage_handoff_zero();assert(rebound.transfer.state==KUI_VMU_ALLOCATION_TRANSFER_POISONED);
}
static void rebind_current_image_recovery_and_reuse(void) {
    rebind_fresh();card.base[123][7]=0x71u;
    assert(!rebind_session() && card.work.session.ready && card.work.session.snapshot_valid);
    assert(rebound.transfer.state==KUI_VMU_ALLOCATION_TRANSFER_CONSUMED && !rebound.metadata_reads);
    assert(!card.writes && !card.syncs);
    assert(card.work.session.block_crc[123]==kui_retail_crc32(0,card.base[123],512u));
    assert(memcmp(card.work.session.base_sha256,card.expected.base_sha256,32u));
    /* The stack-local guard is gone; ordinary live operations retain only the
     * original caller callbacks, and still verify every current block. */
    uint8_t next[512];memset(next,0x5c,sizeof(next));
    assert(!kui_vmu_persist_session_commit(&card.work.session,123u,next));
    assert(!memcmp(card.base[123],next,512u));
    struct kui_vmu_persist_session_ops ops=rebind_ops();
    assert(kui_vmu_persist_session_rebind(&rebound.reentrant,&card.expected,&rebound.transfer,&ops,
        &card.work.image)==KUI_VMU_PERSIST_SESSION_ALLOCATION);
    assert(!rebound.reentrant.started);
    /* A different owner gets a NEW transfer and session, never stale hashes. */
    memset(&card.work,0,sizeof(card.work));rebind_mint(2u,1u,3u);
    assert(!rebind_stage() && !rebound.metadata_reads);
    assert(!kui_vmu_durable_handoff_validate(&card.handoff) && sizeof(card.handoff)==1272u);
    assert(card.handoff.block_crc[123]==kui_retail_crc32(0,next,512u));
    card.base[123][0]^=1u;uint8_t out[512];memset(out,0xa5,sizeof(out));
    assert(kui_vmu_persist_session_read(&card.work.session,123u,out));
    for(unsigned i=0u;i<512u;i++)assert(out[i]==0xa5u);
    rebind_fresh();stage_seed_real_marker();assert(!rebind_stage());
    assert(card.writes==2u && card.syncs==4u && !rebound.metadata_reads);
    for(unsigned i=0u;i<512u;i++)assert(card.base[123][i]==0x79u && !card.journal[1][i]);
    assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));
    for(unsigned next_owner=3u;next_owner<=4u;next_owner++) {
        rebind_fresh();rebind_mint(1u,next_owner,2u);assert(!rebind_stage());
    }
}
static void rebind_invalid_transfer_before_io(void) {
    for(unsigned which=0u;which<19u;which++) {
        rebind_fresh();stage_seed_real_marker();
        struct kui_vmu_persist_allocation_transfer *t=&rebound.transfer;
        if(which==0u)t->magic^=1u;
        if(which==1u)++t->version;
        if(which==2u)--t->bytes;
        if(which==3u)t->state=KUI_VMU_ALLOCATION_TRANSFER_CONSUMED;
        if(which==4u)++t->card_sectors;
        if(which==5u)++t->partition_first;
        if(which==6u)--t->partition_end;
        if(which==7u)t->high_capacity^=1u;
        if(which==8u)t->cid[0]^=1u;
        if(which==9u)t->image_id[0]^=1u;
        if(which==10u)t->ownership_sha256[0]^=1u;
        if(which==11u)memset(t->domain_binding_sha256,0,32u);
        if(which==12u)t->generation=0u;
        if(which==13u)t->sequence=1u;
        if(which==14u)t->previous_owner=3u;
        if(which==15u)t->next_owner=1u;
        if(which==16u)t->reserved=1u;
        if(which==17u)t->previous_owner=0u;
        if(which==18u)t->next_owner=5u;
        assert(rebind_stage());stage_handoff_zero();
        assert(!card.reads && !card.writes && !card.syncs && !card.identities);
    }
    for(unsigned role=0u;role<3u;role++) {
        rebind_fresh();card.game.slots[1].extent.card_lba=role==0u?lba(20u)+200u:role==1u?lba(60u)+2u:lba(61u)+1u;
        assert(rebind_stage()==KUI_VMU_PERSIST_STAGE_MAPPING && !card.reads && !card.writes);
        stage_handoff_zero();
    }
}
static void rebind_callback_fences_and_poison(void) {
    for(unsigned which=0u;which<7u;which++) {
        rebind_fresh();stage_seed_real_marker();
        if(which==0u)card.actual.cid[0]^=1u;
        if(which==1u)rebound.wrong_identity_at=2u;
        if(which==2u)rebound.mutate_profile_identity=1u;
        if(which==3u)rebound.mutate_profile_read=1u;
        if(which==4u)rebound.mutate_transfer_read=1u;
        if(which==5u)rebound.unhealthy=1u;
        if(which==6u)rebound.transfer.domain_binding_sha256[31]^=1u,rebound.mutate_transfer_read=1u;
        assert(rebind_stage());stage_handoff_zero();assert(!card.writes && !card.syncs);
        assert(rebound.transfer.state==KUI_VMU_ALLOCATION_TRANSFER_POISONED);
    }
    rebind_fresh();rebound.reenter_identity=1u;assert(!rebind_stage());
    assert(rebound.reentry_result==KUI_VMU_PERSIST_SESSION_ALLOCATION && !rebound.reentrant.started);
    rebind_fresh();card.base[255][0]=0u;assert(rebind_stage() && !card.writes);stage_handoff_zero();
    assert(rebound.transfer.state==KUI_VMU_ALLOCATION_TRANSFER_POISONED);
    rebind_fresh();card.damage_after_checkpoint=1u;assert(rebind_stage());stage_handoff_zero();
    assert(rebound.transfer.state==KUI_VMU_ALLOCATION_TRANSFER_POISONED);
    rebind_fresh();rebound.mutate_final_transfer=1u;assert(rebind_stage());stage_handoff_zero();
    assert(rebound.transfer.state==KUI_VMU_ALLOCATION_TRANSFER_POISONED);
    for(unsigned which=1u;which<=2u;which++) {
        rebind_fresh();rebound.mutate_stage_profile=which;assert(rebind_stage());stage_handoff_zero();
        assert(rebound.transfer.state==KUI_VMU_ALLOCATION_TRANSFER_POISONED);
        if(which==2u)assert(!card.writes && !card.syncs);
    }
}
static void rebind_partial_journal_and_power_loss(void) {
    for(unsigned which=0u;which<2u;which++) {
        rebind_fresh();stage_seed_real_marker();
        if(which) {card.journal[0][123]^=1u;assert(rebind_stage() && !card.writes);stage_handoff_zero();}
        else {card.journal[1][123]^=1u;assert(!rebind_stage() && card.writes==1u);assert(!card.base[123][0]);}
    }
    for(unsigned kind=0u;kind<2u;kind++)for(unsigned cut=1u;cut<=4u;cut++) {
        rebind_fresh();assert(!rebind_session());uint8_t before[512],next[512];
        memcpy(before,card.base[123],512u);memset(next,0xa7,sizeof(next));
        if(kind)card.fail_sync=cut;else card.fail_write=cut;
        assert(kui_vmu_persist_session_commit(&card.work.session,123u,next) && card.work.session.faulted);
        /* A fault ENDS this epoch. Reboot uses ordinary full admission and
         * recovery, not a fabricated continuation transfer from the fault. */
        memset(&card.work,0,sizeof(card.work));card.fail_sync=card.fail_write=0u;
        card.reads=card.writes=card.syncs=card.identities=0u;assert(!stage_prepare());
        assert(!memcmp(card.base[123],before,512u) || !memcmp(card.base[123],next,512u));
        assert(!kui_vmu_durable_handoff_validate(&card.handoff));
    }
}
static void rebind_argument_aliases(void) {
    rebind_fresh();struct kui_vmu_persist_session_ops ops=rebind_ops();
    assert(kui_vmu_persist_session_rebind(&card.work.session,&card.expected,
        (void *)&card.expected,&ops,&card.work.image)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    assert(kui_vmu_persist_session_rebind(&card.work.session,&card.expected,&rebound.transfer,&ops,
        (void *)&rebound.transfer)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    assert(kui_vmu_persist_stage_rebind(&card.expected,&card.game,&rebound.transfer,&ops,&card.work,
        (void *)&rebound.transfer)==KUI_VMU_PERSIST_STAGE_ARGUMENT);
    assert(!card.work.started && !card.work.session.started && !card.reads && !card.writes);
    ops.healthy=NULL;assert(kui_vmu_persist_session_rebind(&card.work.session,&card.expected,
        &rebound.transfer,&ops,&card.work.image)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
}
int main(void) {
    rebind_current_image_recovery_and_reuse();rebind_invalid_transfer_before_io();
    rebind_callback_fences_and_poison();rebind_partial_journal_and_power_loss();rebind_argument_aliases();
    rebind_snapshot_policy();
    puts("PASS allocation epoch rebind: zero host metadata IO, current image/per-block CRCs, real recovery, one-shot/alias/callback/role/CID fences, fresh1272 handoff; faulted epoch requires ordinary fresh admission");
    return 0;
}
