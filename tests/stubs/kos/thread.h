/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_THREAD_H
#define KUI_TEST_THREAD_H
#include <stddef.h>
typedef struct {unsigned placeholder;} kthread_t;
typedef struct {size_t stack_size;const char *label;} kthread_attr_t;
kthread_t *thd_create_ex(const kthread_attr_t *attrs,void *(*routine)(void *),void *param);
int thd_join(kthread_t *thread,void **result);
void thd_sleep(unsigned ms);
void thd_pass(void);
#endif
