/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_background_route.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ordinary[64],session[2u*KUI_NATIVE_SESSION_WIRE_BYTES+128u];
static void put(unsigned char *p,uint32_t n) {
    for(unsigned i=0;i<4u;++i)p[i]=(unsigned char)(n>>(8u*i));
}
static void dual(unsigned char *p) {
    const uint32_t words[]={KUI_NATIVE_DUAL_MAGIC0,KUI_NATIVE_DUAL_MAGIC1,1u,32u,
        KUI_RETAIL_READER_ASYNC,KUI_RETAIL_READER_ASYNC_EAGER,KUI_NATIVE_DUAL_COMPACT_LEASE_BYTES,0u};
    for(unsigned i=0;i<8u;++i)put(p+4u*i,words[i]);
}
static void descriptor(unsigned char *p) {
    memcpy(p,KUI_NATIVE_SESSION_MAGIC,8u);put(p+8u,1u);put(p+12u,KUI_NATIVE_SESSION_WIRE_BYTES);
}
int main(void) {
    dual(ordinary);dual(session);put(session+24u,KUI_NATIVE_DUAL_GENERAL_LEASE_BYTES);descriptor(session+64u);
    assert(!strcmp(kui_native_background_package(KUI_RETAIL_READER_ASYNC,false),KUI_GAMES_RETAIL_NATIVE_DUAL_PACKAGE));
    assert(!strcmp(kui_native_background_package(KUI_RETAIL_READER_ASYNC,true),KUI_GAMES_RETAIL_NATIVE_DUAL_A1_PACKAGE));
    assert(!strcmp(kui_native_background_package(KUI_RETAIL_READER_ASYNC_EAGER,false),KUI_GAMES_RETAIL_NATIVE_DUAL_PACKAGE));
    assert(!strcmp(kui_native_background_package(KUI_RETAIL_READER_ASYNC_EAGER,true),KUI_GAMES_RETAIL_NATIVE_DUAL_A1_PACKAGE));
    assert(kui_native_background_module_matches(1u,session,sizeof(session)));
    assert(kui_native_background_module_matches(2u,ordinary,sizeof(ordinary)));
    assert(kui_native_background_module_matches(1u,ordinary,sizeof(ordinary)));
    put(ordinary+24u,KUI_NATIVE_DUAL_GENERAL_LEASE_BYTES);
    assert(!kui_native_background_module_matches(1u,ordinary,sizeof(ordinary)));
    assert(!kui_native_background_module_matches(2u,ordinary,sizeof(ordinary)));
    put(ordinary+24u,KUI_NATIVE_DUAL_LEASE_BYTES);
    assert(!kui_native_background_module_matches(1u,ordinary,sizeof(ordinary)));
    assert(!kui_native_background_module_matches(2u,ordinary,sizeof(ordinary)));
    put(ordinary+24u,KUI_NATIVE_DUAL_COMPACT_LEASE_BYTES);
    put(session+24u,KUI_NATIVE_DUAL_COMPACT_LEASE_BYTES);
    assert(!kui_native_background_module_matches(1u,session,sizeof(session)));
    assert(!kui_native_background_module_matches(2u,session,sizeof(session)));
    put(session+24u,KUI_NATIVE_DUAL_GENERAL_LEASE_BYTES);
    assert(!kui_native_background_module_matches(2u,session,sizeof(session)));
    const uint32_t invalid[]={0u,3u,255u,256u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        assert(!kui_native_background_package(invalid[i],false));
        assert(!kui_native_background_package(invalid[i],true));
        assert(!kui_native_background_module_matches(invalid[i],session,sizeof(session)));
        assert(!kui_native_background_module_matches(invalid[i],ordinary,sizeof(ordinary)));
    }
    assert(!kui_native_background_module_matches(1u,NULL,sizeof(session)));
    assert(!kui_native_background_module_matches(2u,ordinary,31u));
    put(session+8u,2u);assert(!kui_native_background_module_matches(1u,session,sizeof(session)));put(session+8u,1u);
    dual(session+32u);assert(!kui_native_background_module_matches(1u,session,sizeof(session)));memset(session+32u,0,32u);
    descriptor(session+64u+KUI_NATIVE_SESSION_WIRE_BYTES);
    assert(!kui_native_background_module_matches(1u,session,sizeof(session)));
    memset(session+64u+KUI_NATIVE_SESSION_WIRE_BYTES,0,KUI_NATIVE_SESSION_WIRE_BYTES);
    session[64u+32u]=1u;assert(!kui_native_background_module_matches(1u,session,sizeof(session)));
    assert(!kui_native_background_module_matches(2u,session,sizeof(session)));session[64u+32u]=0u;
    assert(!kui_native_background_module_matches(1u,session,64u+KUI_NATIVE_SESSION_WIRE_BYTES-1u));
    puts("Background routing PASS: compact ordinary X/Y at dual A1/A2 paths; explicit optional session X only; malformed/duplicate/truncated capabilities and invalid readers refused");
}
