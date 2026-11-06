/* SPDX-License-Identifier: GPL-3.0-only */
/* Production owner-contract parser, hint selector, NVR codec and fresh-stage
 * replay. The authored source deliberately does NOT have the named MDE SHA;
 * selecting its configuration must never claim that these source bytes pass. */
#define KUI_GAMES_NATIVE_CACHE_TEST_MAIN ordinary_native_cache_regressions
#include "test_games_native_cache.c"
#undef KUI_GAMES_NATIVE_CACHE_TEST_MAIN
#define malloc fixture_malloc
#include "../src/apps/games_native_owner_cache.inc"
#undef malloc

static const uint8_t engine_sha[32]={4,244,157,88,59,57,189,199,16,206,198,208,164,52,181,215,
    16,185,188,206,221,83,155,237,218,41,117,81,53,240,254,97};
static uint8_t *engine;
static void owner_word(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void owner_record(uint8_t wire[64]) {
    memset(wire,0,64u);memcpy(wire,"KUINOWN1",8u);owner_word(wire+8u,1u);owner_word(wire+12u,64u);
    owner_word(wire+16u,KUI_SMASH_NATIVE_ENGINE_BYTES);owner_word(wire+20u,load);
    memcpy(wire+24u,engine_sha,32u);owner_word(wire+56u,1u);owner_word(wire+60u,kui_crc32(0u,wire,60u));
}
static unsigned owner_fixture(void) {
    setup(engine,KUI_SMASH_NATIVE_ENGINE_BYTES);owner_record(module+2048u);memcpy(before,module,sizeof(module));
    char path[120];CHECK(snprintf(path,sizeof(path),"0:/KUI/VMU/native-cache/v%u/"
        "04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61.nvr",
        KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH)<(int)sizeof(path));
    unsigned node=node_add(path,false);struct kui_vmu_native_resolution r;
    mock(engine,KUI_SMASH_NATIVE_ENGINE_BYTES,&r);memcpy(r.driver.image_sha256,engine_sha,32u);
    CHECK(kui_vmu_native_resolution_cache_encode(&r,disk[node].data,sizeof(disk[node].data)));
    disk[node].size=1024u;return node;
}
static enum native_cache_result owner_prepare(bool *deferred) {
    return smash_native_mapping_prepare(&game,&map,&package,"/Games/SMASH_PACK",logger,cancelled,
        &source_proof,deferred);
}
static void expect_fresh(void) {
    bool deferred=true;CHECK(owner_prepare(&deferred)==NATIVE_CACHE_SKIPPED);
    CHECK(!deferred && io.reads==1u && source_proof.complete);
    CHECK(!memcmp(module,before,sizeof(module)));
}
static void parser_tests(void) {
    uint8_t record[64],image[256];owner_record(record);memset(image,0x5a,sizeof(image));
    memcpy(image+64u,record,sizeof(record));
    /* Same magic in compiled literal pools lacks the full typed prefix. */
    memcpy(image,"KUINOWN1",8u);memcpy(image+192u,"KUINOWN1",8u);
    struct kui_native_source_owner owner,before_owner;memset(&before_owner,0xa5,sizeof(before_owner));
    CHECK(kui_native_source_owner_find(image,sizeof(image),&owner)==1);
    CHECK(owner.image_bytes==KUI_SMASH_NATIVE_ENGINE_BYTES && owner.load==load && !memcmp(owner.sha256,engine_sha,32u));
    for(unsigned i=0;i<64u;i++) {
        image[64u+i]^=1u;owner=before_owner;
        CHECK(kui_native_source_owner_find(image,sizeof(image),&owner)!=1);
        CHECK(!memcmp(&owner,&before_owner,sizeof(owner)));image[64u+i]^=1u;
    }
    memcpy(image+128u,record,sizeof(record));owner=before_owner;
    CHECK(kui_native_source_owner_find(image,sizeof(image),&owner)==-1 && !memcmp(&owner,&before_owner,sizeof(owner)));
    CHECK(kui_native_source_owner_find(record,sizeof(record),(void *)record)==-1);
}
static void actual_module(const char *path) {
    FILE *file=fopen(path,"rb");CHECK(file && !fseek(file,0,SEEK_END));long bytes=ftell(file);CHECK(bytes>64);
    rewind(file);uint8_t *data=malloc((size_t)bytes);CHECK(data);
    CHECK(fread(data,1u,(size_t)bytes,file)==(size_t)bytes && !fclose(file));
    struct kui_runtime_info info;CHECK(kui_runtime_header(data,(size_t)bytes,&info)==KUI_RUNTIME_OK);
    CHECK(kui_crc32(0u,data+64u,info.payload_bytes)==info.crc32);
    struct kui_native_source_owner owner;
    CHECK(kui_native_source_owner_find(data+64u,info.payload_bytes,&owner)==1);
    CHECK(owner.load==load && owner.image_bytes==KUI_SMASH_NATIVE_ENGINE_BYTES && !memcmp(owner.sha256,engine_sha,32u));
    free(data);
}
int main(int argc,char **argv) {
    CHECK(argc>=1 && argc<=2);
    engine=malloc(KUI_SMASH_NATIVE_ENGINE_BYTES);CHECK(engine);memset(engine,0xa5,KUI_SMASH_NATIVE_ENGINE_BYTES);
    parser_tests();unsigned node=owner_fixture();bool deferred=false;
    CHECK(owner_prepare(&deferred)==NATIVE_CACHE_READY && deferred);
    CHECK(!source_proof.complete && !io.reads && !io.source_sha_calls && !io.extent_calls && !io.writes);
    const uint8_t *wire,*pin;struct kui_vmu_native_resolution r;
    CHECK(kui_vmu_native_cache_seed_read(module+128u,1120u,&wire,&pin));
    CHECK(!kui_vmu_native_resolution_cache_replay(engine,KUI_SMASH_NATIVE_ENGINE_BYTES,load,wire,1024u,pin,&r));
    /* An old stage has no contract, so it keeps full fresh GUI preflight. */
    owner_fixture();memset(module+2048u,0x59,64u);memcpy(before,module,sizeof(module));expect_fresh();
    owner_fixture();module[2108u]^=1u;memcpy(before,module,sizeof(module));expect_fresh();
    owner_fixture();memcpy(module+2304u,module+2048u,64u);memcpy(before,module,sizeof(module));expect_fresh();
    node=owner_fixture();disk[node].data[300u]^=1u;expect_fresh();
    node=owner_fixture();disk[node].exists=false;expect_fresh();
    node=owner_fixture();CHECK(kui_vmu_native_resolution_cache_inspect_config(disk[node].data,1024u,&r));
    r.driver.image_sha256[0]^=1u;CHECK(kui_vmu_native_resolution_cache_encode(&r,disk[node].data,1024u));expect_fresh();
    node=owner_fixture();CHECK(kui_vmu_native_resolution_cache_inspect_config(disk[node].data,1024u,&r));
    r.driver.image_bytes-=32u;r.heap.image_bytes-=32u;r.exits.image_bytes-=32u;
    CHECK(kui_vmu_native_resolution_cache_encode(&r,disk[node].data,1024u));expect_fresh();
    owner_fixture();io.allocation_at=1u;expect_fresh();
    owner_fixture();io.cancel_at=1u;deferred=true;
    CHECK(owner_prepare(&deferred)==NATIVE_CACHE_CANCELLED && !deferred && !io.reads);
    CHECK(!memcmp(module,before,sizeof(module)));
    if(argc==2)actual_module(argv[1]);
    free(engine);printf("Native exact-owner mapping configuration: %u checks passed; no cached digest became fresh source proof%s\n",checks,
        argc==2?"; actual linked module capability unique":"");
    return 0;
}
