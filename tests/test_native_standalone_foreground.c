/* SPDX-License-Identifier: GPL-3.0-only */
/* Foreground control admission is independent of New's SDK callback proof.
 * All accepted manifests retain their decoded STANDARD reader and every
 * rejected manifest remains byte-for-byte unchanged. No retail bytes used. */
#include "kui/native_standalone_admission.h"
#include "kui/native_standalone_capability.h"
#include "kui/native_standalone_foreground.h"
#include "kui/retail_loader_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !KUI_NATIVE_STANDALONE_FOREGROUND
#error "This test requires the explicit foreground control build"
#endif
static unsigned checks,accepted,refused_count;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"foreground admission check failed line %u: %s\n",__LINE__,#x); \
    exit(1); } } while (0)
static struct kui_retail_manifest valid(void) {
    struct kui_retail_manifest m;
    memset(&m,0x5a,sizeof(m));
    m.storage_transport=KUI_STORAGE_SCI;
    m.reader=KUI_RETAIL_READER_STANDARD;
    m.boot_bytes=354724u;
    m.track_count=3u;
    m.extent_count=3u;
    m.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    return m;
}
static void admit(struct kui_retail_manifest m) {
    const struct kui_retail_manifest before=m;
    CHECK(kui_native_standalone_launch_prepare(&m));
    CHECK(!memcmp(&m,&before,sizeof(m)));
    CHECK(m.reader==KUI_RETAIL_READER_STANDARD);
    ++accepted;
}
static void refuse(struct kui_retail_manifest m) {
    const struct kui_retail_manifest before=m;
    CHECK(!kui_native_standalone_launch_prepare(&m));
    CHECK(!memcmp(&m,&before,sizeof(m)));
    ++refused_count;
}
static void launch_checks(void) {
    CHECK(!kui_native_standalone_launch_prepare(NULL));
    /* Exercise both sides of every admitted combined track/extent boundary. */
    for (uint32_t tracks=1u;tracks<64u;++tracks) {
        for (uint32_t extents=1u;extents<=64u-tracks;++extents) {
            struct kui_retail_manifest m=valid();
            m.track_count=tracks;m.extent_count=extents;
            m.flags=(tracks&1u)?KUI_RETAIL_IMAGE_BOOT_CRC:0u;
            admit(m);
        }
        struct kui_retail_manifest m=valid();
        m.track_count=tracks;m.extent_count=65u-tracks;refuse(m);
    }
    /* This manifest helper does not pin one title's executable size.
     * The stage independently retains its original SDK-family proof. */
    const uint32_t sizes[]={128u,98692u,354724u,1316164u,
        KUI_NATIVE_STANDALONE_DOA2_BYTES,KUI_RETAIL_EXEC_MAX_BYTES};
    for (unsigned i=0u;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        struct kui_retail_manifest m=valid();m.boot_bytes=sizes[i];admit(m);
    }
    for (uint32_t transport=0u;transport<16u;++transport)
        if (transport!=KUI_STORAGE_SCI) {
            struct kui_retail_manifest m=valid();m.storage_transport=transport;refuse(m);
        }
    for (uint32_t reader=0u;reader<16u;++reader)
        if (reader!=KUI_RETAIL_READER_STANDARD) {
            struct kui_retail_manifest m=valid();m.reader=reader;refuse(m);
        }
    for (unsigned bit=0u;bit<32u;++bit) {
        const uint32_t flag=UINT32_C(1)<<bit;
        if (flag==KUI_RETAIL_IMAGE_BOOT_CRC) continue;
        struct kui_retail_manifest m=valid();m.flags=flag;refuse(m);
        m=valid();m.flags=flag|KUI_RETAIL_IMAGE_BOOT_CRC;refuse(m);
    }
    const uint32_t invalid_counts[]={0u,64u,65u,UINT32_MAX};
    for (unsigned i=0u;i<sizeof(invalid_counts)/sizeof(invalid_counts[0]);++i) {
        struct kui_retail_manifest m=valid();m.track_count=invalid_counts[i];refuse(m);
        m=valid();m.extent_count=invalid_counts[i];refuse(m);
        m=valid();m.track_count=invalid_counts[i];m.extent_count=invalid_counts[i];refuse(m);
    }
    struct kui_retail_manifest m=valid();m.track_count=UINT32_MAX;m.extent_count=1u;refuse(m);
    m=valid();m.track_count=1u;m.extent_count=UINT32_MAX;refuse(m);
    m=valid();m.storage_transport=UINT32_MAX;refuse(m);
    m=valid();m.reader=UINT32_MAX;refuse(m);
}
static void put(uint8_t *p,uint32_t value) {
    for(unsigned i=0u;i<4u;++i)p[i]=(uint8_t)(value>>(8u*i));
}
static void foreground_record(uint8_t *p) {
    /* Authored wire values independently pin the separate control ABI. */
    const uint32_t words[8]={UINT32_C(0x3146474b),UINT32_C(0x574f4c46),
        1u,32u,KUI_RETAIL_READER_STANDARD,64u,0u,0u};
    for(unsigned i=0u;i<8u;++i)put(p+4u*i,words[i]);
}
static void foreground_capability_checks(void) {
    CHECK(sizeof(struct kui_native_foreground_capability)==32u);
    uint8_t package[256],before[sizeof(package)];size_t offset;
    CHECK(!kui_native_foreground_descriptor_valid(NULL,32u));
    CHECK(kui_native_foreground_descriptor_find(NULL,sizeof(package),NULL)==0);
    for(size_t at=0u;at<=sizeof(package)-32u;at+=7u) {
        memset(package,0xa5,sizeof(package));foreground_record(package+at);
        memcpy(before,package,sizeof(package));offset=SIZE_MAX;
        CHECK(kui_native_foreground_descriptor_valid(package+at,32u));
        CHECK(kui_native_foreground_descriptor_find(package,sizeof(package),&offset)==1);
        CHECK(offset==at);
        CHECK(!memcmp(package,before,sizeof(package)));
        offset=SIZE_MAX;
        CHECK(!kui_native_foreground_descriptor_valid(package+at,31u));
        CHECK(kui_native_foreground_descriptor_find(package,at+31u,&offset)==0);
        CHECK(offset==SIZE_MAX);
        CHECK(kui_native_standalone_descriptor_find(package,sizeof(package),NULL)==0);
    }
    /* Any changed field, including a claimed heap lease, new flags, or
     * a slot count other than64, loses the explicit control capability. */
    for(unsigned word=0u;word<8u;++word)for(unsigned bit=0u;bit<32u;++bit) {
        memset(package,0,sizeof(package));foreground_record(package+17u);
        package[17u+4u*word+bit/8u]^=(uint8_t)(1u<<(bit%8u));
        memcpy(before,package,sizeof(package));offset=SIZE_MAX;
        CHECK(!kui_native_foreground_descriptor_valid(package+17u,32u));
        CHECK(kui_native_foreground_descriptor_find(package,sizeof(package),&offset)==0);
        CHECK(offset==SIZE_MAX);
        CHECK(!memcmp(package,before,sizeof(package)));
    }
    memset(package,0,sizeof(package));foreground_record(package+1u);
    foreground_record(package+100u);offset=SIZE_MAX;
    CHECK(kui_native_foreground_descriptor_find(package,sizeof(package),&offset)==-1);
    CHECK(offset==SIZE_MAX);
}
static void capability_checks(void) {
    uint8_t package[128];memset(package,0,sizeof(package));
    const uint32_t words[8]={KUI_NATIVE_STANDALONE_CAP_MAGIC0,
        KUI_NATIVE_STANDALONE_CAP_MAGIC1,1u,KUI_NATIVE_STANDALONE_CAP_BYTES,
        KUI_RETAIL_READER_STANDARD,0u,0u,0u};
    for(unsigned i=0u;i<8u;++i)put(package+17u+4u*i,words[i]);
    size_t offset=SIZE_MAX;
    CHECK(!kui_native_standalone_descriptor_valid(package+17u,32u));
    CHECK(kui_native_standalone_descriptor_find(package,sizeof(package),&offset)==0);
    CHECK(offset==SIZE_MAX);
    /* Positive control: the existing New descriptor still means ASYNC.
     * A STANDARD foreground control must not satisfy that unchanged ABI. */
    put(package+17u+16u,KUI_RETAIL_READER_ASYNC);
    CHECK(kui_native_standalone_descriptor_valid(package+17u,32u));
    CHECK(kui_native_standalone_descriptor_find(package,sizeof(package),&offset)==1);
    CHECK(offset==17u);
}
int main(void) {
    launch_checks();foreground_capability_checks();capability_checks();
    printf("Standalone foreground control admission: %u checks PASS; %u accepted, %u refused; STANDARD preserved, no New capability\n",
        checks,accepted,refused_count);
    printf("FOREGROUND_ADMISSION_JSON {\"checks\":%u,\"accepted\":%u,"
        "\"refused\":%u,\"hardware_executed\":false}\n",
        checks,accepted,refused_count);
    return 0;
}
