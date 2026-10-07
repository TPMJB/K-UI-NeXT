/* SPDX-License-Identifier: GPL-3.0-only */
/* The existing bounded GDI parser needs this integer libc primitive in the
 * detached image. No allocator or retired kernel state is used. */
#include <stddef.h>
void *memchr(const void *memory,int wanted,size_t bytes) {
    const unsigned char *p=memory;
    while(bytes--) {if(*p==(unsigned char)wanted) return (void *)p;p++;}
    return NULL;
}
