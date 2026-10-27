/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_BOUNDED_GENWAIT_H
#define KUI_TEST_BOUNDED_GENWAIT_H
int genwait_wait(void *,const char *,unsigned int);
void genwait_wake_all(const void *);
#endif
