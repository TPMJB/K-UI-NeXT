/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_standalone_admission.h"
#include "kui/hash.h"
#include <string.h>
#if defined(KUI_NATIVE_STANDALONE_STRUCTURAL) && KUI_NATIVE_STANDALONE_STRUCTURAL
/* The separately installed structural module is reached only by the
 * launcher's New route; its stage proves the SDK family on the loaded image. */
bool __attribute__((noipa)) kui_native_standalone_launch_prepare(struct kui_retail_manifest *m) {
    return m && m->storage_transport==KUI_STORAGE_SCI &&
       m->reader==KUI_RETAIL_READER_ASYNC &&
       m->track_count && m->track_count<=KUI_RETAIL_ASYNC_SLOTS &&
       m->extent_count && m->extent_count<=KUI_RETAIL_ASYNC_SLOTS-m->track_count &&
       !(m->flags&~KUI_RETAIL_IMAGE_BOOT_CRC);
}
#else
bool __attribute__((noipa)) kui_native_standalone_launch_prepare(struct kui_retail_manifest *m) {
    if(!m || m->storage_transport!=KUI_STORAGE_SCI ||
       m->reader!=KUI_RETAIL_READER_STANDARD ||
       m->boot_bytes!=KUI_NATIVE_STANDALONE_DOA2_BYTES ||
       !m->track_count || m->track_count>KUI_RETAIL_ASYNC_SLOTS ||
       !m->extent_count || m->extent_count>KUI_RETAIL_ASYNC_SLOTS-m->track_count ||
       (m->flags&~KUI_RETAIL_IMAGE_BOOT_CRC))return false;
    m->reader=KUI_RETAIL_READER_ASYNC;return true;
}
#endif
bool __attribute__((noipa)) kui_native_standalone_image_admit(const void *image,size_t bytes) {
    static const uint8_t expected[32]={
        0x10,0x50,0x56,0xd9,0xff,0xef,0x86,0xbf,0x5e,0x47,0x0f,0xa2,0x8d,0xc5,0x90,0x74,
        0x43,0x15,0x6f,0x9a,0xb1,0xc6,0xbc,0x00,0x9f,0x73,0x66,0x1d,0x9c,0x04,0x7a,0x85};
    if(!image || bytes!=KUI_NATIVE_STANDALONE_DOA2_BYTES)return false;
    struct kui_sha256 h;uint8_t digest[32];kui_sha256_init(&h);
    kui_sha256_update(&h,image,bytes);kui_sha256_digest(&h,digest);
    return !memcmp(digest,expected,sizeof(expected));
}
