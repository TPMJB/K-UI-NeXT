/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_WRITE_THREAD_H
#define KUI_TEST_WRITE_THREAD_H
typedef int (*thd_cb_t)(void *);
int thd_poll(thd_cb_t,void *,unsigned long);
#endif
