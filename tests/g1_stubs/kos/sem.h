/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_G1_SEM_H
#define KUI_TEST_G1_SEM_H
typedef struct {unsigned count;} semaphore_t;
int sem_wait_timed(semaphore_t *,unsigned);
void sem_signal(semaphore_t *);
#endif
