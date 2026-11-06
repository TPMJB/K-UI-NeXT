/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_profile.h"
#include "kui/vc2_ce_vmu_persist.h"
#define SMASH_COLLECTION_FILES_TEST 1
#define main capsule_file_fixture_regressions
#include "test_smash_menu_capsule_prepare.c"
#undef main
#include "../src/apps/smash_menu_collection_files.inc"

static struct smash_collection_files files;
static uint8_t session[KUI_VC2_PERSIST_CAPSULE_BYTES];
static void collection_fixture(void) {
    fixture();
    for(unsigned i=0u;i<CAPSULES;i++) {
        capsules[i].old_table[2]=20u+i*30u;
        memcpy(capsules[i].new_table,capsules[i].old_table,sizeof(capsules[i].old_table));
        memcpy(capsules[i].readback_table,capsules[i].old_table,sizeof(capsules[i].old_table));
    }
    files=(struct smash_collection_files){.fs=&fs,.volume=&volume,.map=&game,.profile=&profile,.page=page};
    for(size_t i=0u;i<sizeof(session);i++)session[i]=(uint8_t)(17u*i+(i>>8));
}
static void no_writes(void) {
    for(unsigned i=0u;i<CAPSULES;i++)assert(!capsules[i].creates && !capsules[i].writes);
}
static void all_old_aliases(void) {
    for(unsigned i=0u;i<CAPSULES;i++)for(unsigned j=0u;j<i;j++) {
        collection_fixture();capsules[i].old_table[2]=capsules[j].old_table[2];
        assert(!smash_collection_files_inspect(&files,&problem,cancel));no_writes();
        collection_fixture();capsules[i].old_table[2]=capsules[j].old_table[2]+3u;
        capsules[j].old_table[1]=4u;
        assert(!smash_collection_files_inspect(&files,&problem,cancel));no_writes();
    }
}
static void all_new_aliases(void) {
    for(unsigned i=0u;i<SMASH_CAP_CARD_SESSION;i++)for(unsigned j=0u;j<SMASH_CAP_CARD_SESSION;j++)if(i!=j) {
        collection_fixture();assert(smash_collection_files_inspect(&files,&problem,cancel));
        unsigned fixture_i=0u,fixture_j=0u;
        while(strcmp(paths[fixture_i],smash_capsule_paths[i]))fixture_i++;
        while(strcmp(paths[fixture_j],smash_capsule_paths[j]))fixture_j++;
        capsules[fixture_i].present=false;capsules[fixture_i].new_table[2]=capsules[fixture_j].old_table[2];
        struct kui_smash_menu_capsule_prepared out;
        assert(!smash_collection_file_publish(&files,i,capsules[fixture_i].wanted,8704u,8704u,&out,
            &problem,log_message,cancel));
        assert(capsules[fixture_i].creates==1u && !capsules[fixture_i].writes);
        for(unsigned k=0u;k<CAPSULES;k++)if(k!=fixture_i)assert(!capsules[k].creates && !capsules[k].writes);
    }
}
static void session_reuse(void) {
    collection_fixture();
    struct fixture_capsule *s=&capsules[VC2_SESSION];
    s->disk_bytes=sizeof(session);s->old_table[1]=13u;
    memcpy(s->new_table,s->old_table,sizeof(s->old_table));
    memcpy(s->disk,session,sizeof(session));
    memset(s->disk+KUI_VC2_PERSIST_HELPER_BYTES,0x6a,KUI_VC2_PERSIST_CONFIG_BYTES);
    assert(smash_collection_files_inspect(&files,&problem,cancel));
    struct kui_vc2_vmu_late_seed seed={.helper_bytes=KUI_VC2_PERSIST_HELPER_BYTES};
    assert(smash_collection_session_publish(&files,KUI_SMASH_CE_VC2_SESSION,session,sizeof(session),
        KUI_VC2_PERSIST_HELPER_BYTES,&seed,&problem,log_message,cancel));
    assert(seed.first_blocks==KUI_VC2_PERSIST_CAPSULE_BLOCKS && !seed.second_lba && !s->creates && !s->writes);
    assert(s->disk[KUI_VC2_PERSIST_HELPER_BYTES]==0x6au); /* stage-owned tail survives reuse */
    s->disk[42000u]^=1u;
    assert(smash_collection_session_publish(&files,KUI_SMASH_CE_VC2_SESSION,session,sizeof(session),
        KUI_VC2_PERSIST_HELPER_BYTES,&seed,&problem,log_message,cancel));
    assert(s->creates==1u && s->writes==13u && !memcmp(s->disk,session,sizeof(session)));
    collection_fixture();s=&capsules[SWIRL_SESSION];s->present=false;s->new_table[1]=13u;s->fail_sync=true;
    assert(smash_collection_files_inspect(&files,&problem,cancel));
    assert(!smash_collection_session_publish(&files,KUI_SMASH_CE_SWIRL_SESSION,session,sizeof(session),
        KUI_VC2_PERSIST_HELPER_BYTES,&seed,&problem,log_message,cancel));
    assert(!memcmp(&files.allocated[SMASH_CAP_SWIRL_SESSION],&(struct kui_smash_menu_capsule_runs){0},
        sizeof(struct kui_smash_menu_capsule_runs)));
}
int main(void) {
    capsule_file_fixture_regressions();
    collection_fixture();assert(smash_collection_files_inspect(&files,&problem,cancel));no_writes();
    all_old_aliases();all_new_aliases();session_reuse();
    puts("Smash collection six-file allocation and helper reuse: OK");return 0;
}
