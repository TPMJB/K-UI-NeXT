/* SPDX-License-Identifier: GPL-3.0-only */
/* Installer admission uses the actual immutable Evolution executable. Each
 * immutable live vector byte and chained-prologue byte is independently
 * corrupted; no replacement template implementation supplies the witness. */
#include "vmu_native_fault_capture.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VECTOR UINT32_C(0x8c00f500)
#define LOAD UINT32_C(0x8c010000)
static uint8_t *game,original_vector[60],vector[60];
static size_t game_bytes;
static uint32_t sr,vbr,gateway;
static bool owned;
static unsigned reads,writes,publishes,inject_readback;
static struct kui_vmu_native_discovery driver;
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8u*i));
}
uint32_t kui_fault_test_sr(void) {++reads;return sr;}
uint32_t kui_fault_test_vbr(void) {++reads;return vbr;}
uint32_t kui_fault_test_gateway(void) {return gateway;}
bool kui_fault_test_owned(void) {return owned;}
uint32_t kui_fault_test_read(uint32_t at) {
    ++reads;
    if(at>=VECTOR && at<=VECTOR+56u && !(at&3u)) {
        if(inject_readback && writes==3u && at==VECTOR) {inject_readback=0;return 0;}
        return get32(vector+at-VECTOR);
    }
    assert(at>=LOAD && at<=LOAD+game_bytes-4u && !(at&3u));
    return get32(game+at-LOAD);
}
void kui_fault_test_write(uint32_t at,uint32_t value) {
    assert(at==VECTOR || at==VECTOR+4u || at==VECTOR+56u);
    ++writes;put32(vector+at-VECTOR,value);
}
void kui_fault_test_publish(uint32_t begin,uint32_t end) {
    assert(begin==VECTOR && end==VECTOR+60u);++publishes;
}
static void reset(void) {
    memcpy(vector,original_vector,sizeof(vector));
    memset((void *)kui_vmu_native_fault_capture_state,0,sizeof(uint32_t)*8u);
    sr=0x600000f1u;vbr=0x8c00f400u;gateway=0x8c100100u;owned=true;
    reads=writes=publishes=inject_readback=0u;
}
static void refused(void) {
    assert(kui_vmu_native_fault_capture_install(true,&driver)==-1);
    assert(!writes && !publishes && !kui_vmu_native_fault_capture_state[7]);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n==2143652);
    game_bytes=(size_t)n;rewind(f);game=malloc(game_bytes);assert(game);
    assert(fread(game,1,game_bytes,f)==game_bytes);assert(!fclose(f));
    memcpy(original_vector,game+0x8c1da048u-LOAD,sizeof(original_vector));
    memset(&driver,0,sizeof(driver));driver.version=KUI_VMU_NATIVE_DISCOVERY_VERSION;
    driver.bytes=sizeof(driver);driver.family=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1;
    driver.requirements=KUI_VMU_NATIVE_REQUIREMENTS;
    driver.load_address=LOAD;driver.image_bytes=(uint32_t)game_bytes;
    struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,game,game_bytes);
    kui_sha256_digest(&sha,driver.image_sha256);
    reset();assert(!kui_vmu_native_fault_capture_install(false,NULL));assert(!reads && !writes);
    assert(kui_vmu_native_fault_capture_install(true,NULL)==-1);assert(!reads && !writes);
    driver.image_sha256[0]^=1;assert(!kui_vmu_native_fault_capture_install(true,&driver));
    assert(!reads && !writes);driver.image_sha256[0]^=1;
    unsigned corruptions=0;
    for(unsigned i=0;i<sizeof(vector);i++) {
        if(i>=44u && i<48u) continue;
        reset();vector[i]^=0x80u;refused();++corruptions;
    }
    for(unsigned i=0;i<12u;i++) {
        reset();game[0x8c1da10cu-LOAD+i]^=0x80u;refused();
        game[0x8c1da10cu-LOAD+i]^=0x80u;++corruptions;
    }
    for(unsigned i=0;i<4u;i++) {
        reset();game[0x8c1da270u-LOAD+i]^=0x80u;refused();
        game[0x8c1da270u-LOAD+i]^=0x80u;++corruptions;
    }
    reset();sr=0x600000e0u;refused();reset();vbr+=0x100u;refused();
    reset();owned=false;refused();
    const uint32_t bad_gateway[]={0x8c100101u,0xac100100u,0x80000100u,0u};
    for(unsigned i=0;i<sizeof(bad_gateway)/sizeof(*bad_gateway);i++) {
        reset();gateway=bad_gateway[i];refused();
    }
    reset();driver.requirements^=1u;refused();driver.requirements^=1u;
    reset();driver.image_bytes-=4u;refused();driver.image_bytes+=4u;
    reset();driver.load_address+=4u;refused();driver.load_address-=4u;
    reset();driver.version+=1u;refused();driver.version-=1u;
    reset();driver.bytes-=4u;refused();driver.bytes+=4u;
    reset();driver.family=0u;refused();driver.family=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1;
    for(uint32_t saved=0;saved<32u;saved++) {
        reset();put32(vector+44u,0xdead0000u+saved);
        assert(kui_vmu_native_fault_capture_install(true,&driver)==1);
        assert(writes==3u && publishes==1u && kui_vmu_native_fault_capture_state[7]==1u);
        assert(get32(vector)==0xd00d6f03u && get32(vector+4)==0x0009402bu);
        assert(get32(vector+56)==gateway && get32(vector+44)==0xdead0000u+saved);
        assert(!memcmp(vector+8u,original_vector+8u,36u));
        assert(!memcmp(vector+48u,original_vector+48u,8u));
        assert(kui_vmu_native_fault_capture_state[4]==VECTOR+8u);
        assert(kui_vmu_native_fault_capture_state[5]==0x8c1d9f50u);
        reads=writes=publishes=0;
        assert(kui_vmu_native_fault_capture_install(true,&driver)==-1);
        assert(!writes && !publishes && kui_vmu_native_fault_capture_state[7]==1u);
    }
    reset();inject_readback=1u;
    assert(kui_vmu_native_fault_capture_install(true,&driver)==-1);
    assert(writes==6u && publishes==2u && !memcmp(vector,original_vector,sizeof(vector)));
    assert(!kui_vmu_native_fault_capture_state[7]);
    free(game);printf("PASS optional fault installer: %u original-code corruption refusals; 32 mutable-R0 installs; disabled isolation; rollback\n",corruptions);
    return 0;
}
