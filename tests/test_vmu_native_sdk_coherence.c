/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/loader/vmu_native_sdk_coherence.h"
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"coherence preparation:%u: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint32_t u32(const unsigned char *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void fixture(const char *path,struct kui_vmu_native_passive_config *c) {
    unsigned char wire[1024];FILE *f=fopen(path,"rb");CHECK(f);size_t n=fread(wire,1,sizeof(wire),f);CHECK(!fclose(f));CHECK(n>=608u);
    memset(c,0,sizeof(*c));c->version=1;c->bytes=sizeof(*c);
    for(unsigned i=0;i<18u;i++) ((uint32_t *)&c->driver)[i]=u32(wire+64u+4u*i);
    memcpy(c->driver.image_sha256,wire+136u,32u);
    for(unsigned i=0;i<sizeof(c->layout)/4u;i++) ((uint32_t *)&c->layout)[i]=u32(wire+168u+4u*i);
}
static void reset(void) {for(unsigned i=0;i<12u;i++) kui_vmu_native_sdk_coherence_state[i]=0;}
static void refused(const struct kui_vmu_native_passive_config *c) {
    uint32_t before[12];memcpy(before,(const void *)kui_vmu_native_sdk_coherence_state,sizeof(before));
    CHECK(!kui_vmu_native_sdk_coherence_prepare(c));
    CHECK(!memcmp(before,(const void *)kui_vmu_native_sdk_coherence_state,sizeof(before)));
}
int main(int argc,char **argv) {
    CHECK(argc==6);
    for(int file=1;file<argc;file++) {
        struct kui_vmu_native_passive_config c,bad;fixture(argv[file],&c);reset();refused(NULL);
        CHECK(kui_vmu_native_sdk_coherence_prepare(&c)==1);
        uint32_t lock=(c.driver.lock_cell&0x1fffffffu)|0x80000000u;
        uint32_t expected[12]={lock,c.driver.manager_cell,c.driver.manager_initial,c.layout.initializer+12u,
            0,0,0,KUI_VMU_NATIVE_SDK_COHERENCE_READY,(lock+4u)|0xa0000000u,c.layout.rx_layout,c.layout.initializer,0};
        CHECK(!memcmp(expected,(const void *)kui_vmu_native_sdk_coherence_state,sizeof(expected)));refused(&c);
#define BAD(field,value) do {reset();bad=c;bad.field=(value);refused(&bad);} while(0)
        BAD(version,0);BAD(bytes,sizeof(c)-4);BAD(driver.version,0);BAD(driver.bytes,0);
        BAD(driver.family,0);BAD(driver.requirements,0);BAD(layout.version,0);BAD(layout.bytes,0);
        BAD(layout.rx_layout,2);BAD(driver.lock_cell,0x8d000000u);BAD(driver.lock_cell,c.driver.lock_cell+1u);
        BAD(driver.manager_cell,0x8d000000u);BAD(driver.manager_initial,0x8cfff000u);
        BAD(driver.initializer,0xfffffffeu);BAD(driver.initializer,c.driver.initializer+2u);
        BAD(layout.initializer,c.layout.initializer+2u);BAD(layout.initializer,c.layout.initializer|0xa0000000u);
        BAD(driver.image_bytes,8u);
#undef BAD
        /* Both a shared pointer/flag line and a split pair are supported;
         * SA2's actual cell is at offset28 and needs both lines retired. */
        for(unsigned offset=24u;offset<=28u;offset+=4u) {
            reset();bad=c;bad.driver.lock_cell=(c.driver.lock_cell&~31u)+offset;
            CHECK(kui_vmu_native_sdk_coherence_prepare(&bad)==1);
        }
    }
    printf("PASS native SDK coherence preparation: %u checks\n",checks);return 0;
}
