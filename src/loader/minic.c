/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>
#include <stdint.h>

/* Each freestanding image receives its own tiny routines. No implementation
 * or global state from the retired KOS/libc image survives the handoff. */
void *memset(void *out, int value, size_t bytes) {
    uint8_t *p = out;
    while(bytes--) *p++ = (uint8_t)value;
    return out;
}
void *memcpy(void *out, const void *in, size_t bytes) {
    uint8_t *p = out;
    const uint8_t *q = in;
#ifdef KUI_RETAIL_FAST_IO
    /* SH-4 requires aligned word accesses. Only the retail build uses this
     * path; keep byte tails and every unaligned combination exact. may_alias
     * allows copying arbitrary object representations under GCC's alias rules.
     * Integer stores also preserve the caller's FPU and store-queue state. */
    typedef uint32_t copy_word __attribute__((__may_alias__));
    if(!(((uintptr_t)p | (uintptr_t)q) & 3u)) {
        while(bytes >= 4) {
            *(copy_word *)p = *(const copy_word *)q;
            p += 4; q += 4; bytes -= 4;
        }
    }
#endif
    while(bytes--) *p++ = *q++;
    return out;
}
void *memmove(void *out, const void *in, size_t bytes) {
    uint8_t *p = out;
    const uint8_t *q = in;
    if((uintptr_t)p <= (uintptr_t)q) {
        while(bytes--) *p++ = *q++;
    } else {
        p += bytes; q += bytes;
        while(bytes--) *--p = *--q;
    }
    return out;
}
int memcmp(const void *left, const void *right, size_t bytes) {
    const uint8_t *a = left, *b = right;
    while(bytes--) {
        if(*a != *b) return *a < *b ? -1 : 1;
        a++; b++;
    }
    return 0;
}
size_t strlen(const char *text) {
    const char *p = text;
    while(*p) p++;
    return (size_t)(p - text);
}
