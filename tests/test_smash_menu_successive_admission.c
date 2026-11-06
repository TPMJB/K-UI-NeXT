/* SPDX-License-Identifier: GPL-3.0-only */
/* The real cold-stage semantic admission/recovery/checkpoint pipeline over
 * one synthetic filesystem/card, with unchanged old preparation provenance. */
#define KUI_VMU_PERSIST_STAGE_MAIN unused_stage_regression
#include "test_vmu_persist_stage.c"

static struct kui_vmu_persist_stage_workspace generations[4];
static struct kui_vmu_durable_handoff handoffs[4];
static uint8_t frozen_profile[4096];
static int current_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    assert(context==&card);++card.identities;*out=card.actual;return 0;
}
static void current_stage(unsigned generation) {
    struct kui_vmu_persist_session_ops ops=stage_ops();ops.identity=current_identity;
    struct kui_vmu_persist_profile decoded;uint8_t current[4096];
    assert(!kui_vmu_persist_profile_encode(&card.expected,current));
    assert(!memcmp(current,frozen_profile,sizeof(current)));
    assert(!kui_vmu_persist_profile_decode(&decoded,frozen_profile,sizeof(frozen_profile)));
    unsigned reads=card.reads,writes=card.writes;
    assert(!generations[generation].started);
    assert(!kui_vmu_persist_stage_prepare(&decoded,&card.game,&ops,
        &generations[generation],&handoffs[generation]));
    assert(generations[generation].phase==KUI_VMU_PERSIST_STAGE_SEAL);
    assert(generations[generation].filesystem.stats.reads && card.reads>reads+512u);
    assert(card.writes>writes && !kui_vmu_durable_handoff_validate(&handoffs[generation]));
    assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&handoffs[generation]));
    for(unsigned i=0;i<512u;i++)assert(!card.journal[1][i]);
}
int main(void) {
    stage_fresh();
    assert(!kui_vmu_persist_profile_encode(&card.expected,frozen_profile));
    struct kui_vmu_persist_profile original=card.expected;
    uint8_t sf[512],psii[512],settings[512],out[512];
    memset(sf,0x31,sizeof(sf));memset(psii,0x61,sizeof(psii));memset(settings,0x91,sizeof(settings));
    current_stage(0u); /* First Shining Force, fresh live ownership. */
    assert(!kui_vmu_persist_session_commit(&generations[0].session,123u,sf));
    assert(!kui_vmu_persist_session_commit(&generations[0].session,124u,settings));
    assert(!memcmp(&card.expected,&original,sizeof(original)));
    uint8_t current[4096];assert(!kui_vmu_persist_profile_encode(&card.expected,current));
    assert(!memcmp(current,frozen_profile,sizeof(current)));
    current_stage(1u); /* Original prepared hashes are now stale, by design. */
    assert(memcmp(handoffs[1].base_sha256,original.base_sha256,32u));
    assert(handoffs[1].block_crc[123u]==kui_retail_crc32(0,sf,512u));
    assert(!kui_vmu_persist_session_read(&generations[1].session,123u,out) && !memcmp(out,sf,512u));
    assert(!kui_vmu_persist_session_commit(&generations[1].session,125u,psii));
    current_stage(2u); /* Shining Force again, another complete live admission. */
    assert(memcmp(handoffs[2].base_sha256,handoffs[1].base_sha256,32u));
    assert(!kui_vmu_persist_session_read(&generations[2].session,123u,out) && !memcmp(out,sf,512u));
    assert(!kui_vmu_persist_session_read(&generations[2].session,124u,out) && !memcmp(out,settings,512u));
    assert(!kui_vmu_persist_session_read(&generations[2].session,125u,out) && !memcmp(out,psii,512u));
    assert(!memcmp(&card.expected,&original,sizeof(original)));
    assert(!kui_vmu_persist_profile_encode(&card.expected,current));
    assert(!memcmp(current,frozen_profile,sizeof(current)));
    /* A return must not turn the old profile into an ownership shortcut.
     * Relocate the actual journal file: fresh semantics must reject before
     * recovery/checkpoint writes despite matching old immutable profile data. */
    file_entry(disk.dirs[3]+96u,"card.journal",32u,62u,1024u,3u);allocated(62u,1);
    struct kui_vmu_persist_session_ops ops=stage_ops();ops.identity=current_identity;
    unsigned writes=card.writes,syncs=card.syncs;
    assert(kui_vmu_persist_stage_prepare(&card.expected,&card.game,&ops,&generations[3],&handoffs[3])==
        KUI_VMU_PERSIST_STAGE_SESSION);
    assert(card.writes==writes && card.syncs==syncs);
    assert(!memcmp(&handoffs[3],&(struct kui_vmu_durable_handoff){0},sizeof(handoffs[3])));
    puts("Successive cold admissions: SF -> PSII -> SF reread live ownership/current card data with stale preparation hashes; changed journal allocation refused before writes");
    return 0;
}
