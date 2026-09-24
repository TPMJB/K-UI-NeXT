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
