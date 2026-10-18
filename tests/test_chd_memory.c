/* SPDX-License-Identifier: GPL-3.0-only
 * Host-only allocation accounting and fault injection for optical CHD reads.
 * Link this harness and the real decoder with --wrap for malloc/calloc/realloc/free.
 * Run FILE [FAIL_AT]. Every allocation failure must close without retained heap;
 * a failed read is retried after memory becomes available to check partial init.
 */
#define _POSIX_C_SOURCE 200809L
#include "kui/chd_lib.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void *__real_malloc(size_t);
void __real_free(void *);
void *__real_realloc(void *, size_t);
static size_t live, peak, largest, allocations, attempts, fail_at;
static int fault_fired;
union allocation {
    max_align_t alignment;
    struct {size_t bytes;} meta;
};
static int fail_allocation(void) {
    if(++attempts == fail_at) {fault_fired = 1; return 1;}
    return 0;
}
static void account(size_t bytes) {
    live += bytes;
    if(live > peak) peak = live;
    if(bytes > largest) largest = bytes;
    ++allocations;
}
void *__wrap_malloc(size_t bytes) {
    if(fail_allocation() || bytes > SIZE_MAX - sizeof(union allocation)) return NULL;
    union allocation *p = __real_malloc(bytes + sizeof(*p));
    if(!p) return NULL;
    p->meta.bytes = bytes;
    account(bytes);
    return p + 1;
}
void __wrap_free(void *pointer) {
    if(!pointer) return;
    union allocation *p = (union allocation *)pointer - 1;
    assert(live >= p->meta.bytes);
    live -= p->meta.bytes;
    __real_free(p);
}
void *__wrap_calloc(size_t count, size_t bytes) {
    if(count && bytes > SIZE_MAX / count) return NULL;
    size_t size = count * bytes;
    void *p = __wrap_malloc(size);
    if(p) memset(p, 0, size);
    return p;
}
void *__wrap_realloc(void *pointer, size_t bytes) {
    if(!pointer) return __wrap_malloc(bytes);
    if(!bytes) {__wrap_free(pointer); return NULL;}
    if(fail_allocation() || bytes > SIZE_MAX - sizeof(union allocation)) return NULL;
    union allocation *old = (union allocation *)pointer - 1;
    size_t old_bytes = old->meta.bytes;
    union allocation *p = __real_realloc(old, bytes + sizeof(*p));
    if(!p) return NULL;
    p->meta.bytes = bytes;
    assert(live >= old_bytes);
    live -= old_bytes;
    account(bytes);
    return p + 1;
}
struct source {FILE *file; uint64_t bytes;};
static enum kui_chdl_result read_source(void *arg, uint64_t offset, void *out, size_t bytes) {
    struct source *s = arg;
    if(offset > s->bytes || bytes > s->bytes - offset ||
       fseeko(s->file, (off_t)offset, SEEK_SET) || fread(out, 1, bytes, s->file) != bytes)
        return KUI_CHDL_IO;
    return KUI_CHDL_OK;
}
static enum kui_chdl_result decode(struct kui_chdl *reader) {
    const struct kui_chdl_info *info = kui_chdl_get_info(reader);
    assert(info && info->logical_bytes / KUI_CHDL_FRAME_BYTES <= UINT32_MAX);
    uint8_t output[16u * KUI_CHDL_FRAME_BYTES];
    uint32_t frames = (uint32_t)(info->logical_bytes / KUI_CHDL_FRAME_BYTES);
    for(uint32_t first = 0; first < frames;) {
        uint32_t count = frames - first;
        if(count > 16u) count = 16u;
        enum kui_chdl_result result = kui_chdl_read_frames(reader, first, count, output, sizeof(output));
        if(result != KUI_CHDL_OK) return result;
        first += count;
    }
    return KUI_CHDL_OK;
}
int main(int argc, char **argv) {
    if(argc < 2 || argc > 3) return 2;
    size_t requested_failure = 0;
    if(argc == 3) {
        char *end = NULL;
        requested_failure = (size_t)strtoull(argv[2], &end, 10);
        if(!end || *end) return 2;
    }
    fail_at = requested_failure;
    struct source source = {.file = fopen(argv[1], "rb")};
    if(!source.file || fseeko(source.file, 0, SEEK_END)) return 2;
    off_t length = ftello(source.file);
    if(length < 0) return 2;
    source.bytes = (uint64_t)length;
    struct kui_chdl_io io = {.context = &source, .read = read_source};
    struct kui_chdl *reader = NULL;
    enum kui_chdl_result result = kui_chdl_open(&io, source.bytes, &reader);
    size_t opened = live;
    enum kui_chdl_result initial = result;
    if(result != KUI_CHDL_OK) {
        assert(reader == NULL && fault_fired);
        assert(live == 0);
        fail_at = 0;
        result = kui_chdl_open(&io, source.bytes, &reader);
    }
    assert(result == KUI_CHDL_OK && reader);
    result = decode(reader);
    if(result != KUI_CHDL_OK) {
        assert(fault_fired);
        initial = result;
        fail_at = 0;
        result = decode(reader);
    }
    assert(result == KUI_CHDL_OK);
    kui_chdl_close(reader);
    assert(!fclose(source.file));
    printf("memory fail=%zu fired=%d initial=%u opened=%zu peak=%zu largest=%zu "
           "allocations=%zu attempts=%zu retained=%zu\n", requested_failure,
           fault_fired, initial, opened, peak, largest, allocations, attempts, live);
    fflush(stdout);
    assert(live == 0);
    return 0;
}
