/* SPDX-License-Identifier: GPL-3.0-only */
/* Integer-only primitives for the separate read-only preflight image. */
#include <stddef.h>
int strcmp(const char *a,const char *b) {
    while(*a && *a==*b) { ++a; ++b; }
    return (unsigned char)*a-(unsigned char)*b;
}
int strncmp(const char *a,const char *b,size_t n) {
    while(n && *a && *a==*b) { ++a; ++b; --n; }
    return n?(unsigned char)*a-(unsigned char)*b:0;
}
