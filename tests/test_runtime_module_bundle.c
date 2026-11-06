/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/runtime_module_bundle.h"
#include "kui/update_boot.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char path[]="0:/KUI/apps/games/test-module.kui";
static bool pinned,cancelled,force_bundle_error;
static unsigned opens,allocation,fail_allocation;
static uint8_t *wire;static size_t wire_bytes;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n) {return ++allocation==fail_allocation?NULL:__real_malloc(n);}
enum kui_runtime_result __real_kui_runtime_module_bundle_read(const char *,struct kui_runtime_image *,kui_cancel_fn,bool *);
enum kui_runtime_result __wrap_kui_runtime_module_bundle_read(const char *name,struct kui_runtime_image *out,kui_cancel_fn cancel,bool *handled) {
    if(force_bundle_error) {*out=(struct kui_runtime_image){0};*handled=true;return KUI_RUNTIME_CHECKSUM;}
    return __real_kui_runtime_module_bundle_read(name,out,cancel,handled);
}
bool kui_update_runtime_resolve(const char *name,char *out,size_t n,struct kui_update_runtime_stream *stream) {
    if(stream) {memset(stream,0,sizeof(*stream));stream->pinned=pinned;}
    int size=snprintf(out,n,"%s",pinned?"0:/KUI/system/123456789abc/apps/games/test-module.kui":name);
    return size>0 && (size_t)size<n;
}
FRESULT f_open(FIL *f,const TCHAR *name,BYTE mode) {(void)f;(void)name;(void)mode;++opens;return FR_NO_FILE;}
FRESULT f_read(FIL *f,void *p,UINT n,UINT *done) {(void)f;(void)p;(void)n;*done=0u;return FR_DISK_ERR;}
FRESULT f_close(FIL *f) {(void)f;return FR_OK;}
static void log_line(const char *format,...) {(void)format;}
static bool cancel(void) {return cancelled;}
static void reset(void) {pinned=cancelled=force_bundle_error=false;opens=allocation=fail_allocation=0u;}
static void damaged(size_t at) {
    uint8_t *copy=malloc(wire_bytes);assert(copy);memcpy(copy,wire,wire_bytes);copy[at]^=1u;
    struct kui_runtime_image image;bool handled=false;
    assert(kui_runtime_module_bundle_claims_view(copy,wire_bytes,path));
    assert(kui_runtime_module_bundle_read_view(copy,wire_bytes,path,&image,cancel,&handled)!=KUI_RUNTIME_OK);
    assert(handled && !image.data && !image.info.payload_bytes);free(copy);
}
static void forged_basename(uint8_t first) {
    uint8_t *copy=malloc(wire_bytes);assert(copy);memcpy(copy,wire,wire_bytes);
    copy[96u+11u]=first; /* First basename character after apps/games/. */
    struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,copy+96u,(size_t)wire[16u]*128u);
    kui_sha256_digest(&hash,copy+64u); /* Integrity-valid forged manifest. */
    struct kui_runtime_image image;bool handled=false;
    assert(kui_runtime_module_bundle_claims_view(copy,wire_bytes,path));
    assert(kui_runtime_module_bundle_read_view(copy,wire_bytes,path,&image,cancel,&handled)==KUI_RUNTIME_CHECKSUM);
    assert(handled && !image.data);free(copy);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *source=fopen(argv[1],"rb");assert(source);
    assert(!fseek(source,0,SEEK_END));long n=ftell(source);assert(n>0);wire_bytes=(size_t)n;
    rewind(source);wire=malloc(wire_bytes);assert(wire && fread(wire,1u,wire_bytes,source)==wire_bytes);assert(!fclose(source));
    reset();struct kui_runtime_image image;bool handled=false;
    assert(kui_runtime_module_bundle_read_view(wire,wire_bytes,path,&image,cancel,&handled)==KUI_RUNTIME_OK);
    assert(handled && image.data && image.info.payload_bytes==4096u && !strcmp(image.info.build,"123456789abc"));
    for(unsigned i=0u;i<4096u;i++)assert(((const uint8_t *)image.data)[i]==(uint8_t)(i*17u));
    kui_runtime_free(&image);
    reset();assert(kui_runtime_bundle_contains(path));
    assert(kui_runtime_read(path,&image,log_line,cancel)==KUI_RUNTIME_OK && !opens);
    kui_runtime_free(&image);
    reset();pinned=true;assert(!kui_runtime_bundle_contains(path));
    assert(kui_runtime_read(path,&image,log_line,cancel)==KUI_RUNTIME_IO && opens==1u && !image.data);
    reset();force_bundle_error=true;
    assert(kui_runtime_read(path,&image,log_line,cancel)==KUI_RUNTIME_CHECKSUM && !opens && !image.data);
    reset();cancelled=true;assert(kui_runtime_read(path,&image,log_line,cancel)==KUI_RUNTIME_CANCELLED && !opens);
    reset();handled=true;
    assert(!kui_runtime_module_bundle_claims_view(wire,wire_bytes,"0:/KUI/apps/games/absent.kui"));
    assert(kui_runtime_module_bundle_read_view(wire,wire_bytes,"0:/KUI/apps/games/absent.kui",&image,cancel,&handled)==KUI_RUNTIME_IO && !handled);
    for(unsigned fail=1u;fail<=2u;fail++) {
        reset();fail_allocation=fail;
        assert(kui_runtime_module_bundle_read_view(wire,wire_bytes,path,&image,cancel,&handled)==KUI_RUNTIME_MEMORY && handled && !image.data);
    }
    reset();
    const size_t offsets[]={0u,8u,12u,16u,20u,24u,28u,32u,44u,64u,96u,176u,180u,184u,188u,192u};
    for(unsigned i=0u;i<sizeof(offsets)/sizeof(offsets[0]);i++)damaged(offsets[i]);
    forged_basename('.');forged_basename('_');forged_basename('-');
    damaged(wire_bytes-1u);
    for(size_t size=0u;size<224u;size++) {
        handled=false;
        assert(kui_runtime_module_bundle_read_view(wire,size,path,&image,cancel,&handled)!=KUI_RUNTIME_OK && handled && !image.data);
    }
    free(wire);puts("PASS runtime module bundle: actual codec/decompression, CRC/SHA, truncation/corruption/OOM/cancel, explicit-bank priority and no filesystem fallback");return 0;
}
