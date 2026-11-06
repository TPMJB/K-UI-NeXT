/* SPDX-License-Identifier: GPL-3.0-only */
/* The diagnostic FatFs mapper needs this one additional libc leaf. Keep it
 * in the temporary stage, outside every existing resident/worker image. */
#include <stddef.h>
char *strchr(const char *s,int value) {
    unsigned char wanted=(unsigned char)value;
    for(;;s++) {
        if((unsigned char)*s==wanted)return (char *)s;
        if(!*s)return NULL;
    }
}
