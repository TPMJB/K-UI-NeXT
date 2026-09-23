/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_MUTEX_H
#define KUI_TEST_MUTEX_H
#include <assert.h>
typedef struct {unsigned locked;} mutex_t;
#define MUTEX_INITIALIZER {0}
static inline int mutex_lock(mutex_t *mutex) {assert(!mutex->locked);mutex->locked=1;return 0;}
static inline int mutex_unlock(mutex_t *mutex) {assert(mutex->locked);mutex->locked=0;return 0;}
#endif
