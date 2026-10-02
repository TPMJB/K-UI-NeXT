/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_STORAGE_SEM_H
#define KUI_TEST_STORAGE_SEM_H
typedef struct {unsigned placeholder;} semaphore_t;
int sem_wait_timed(semaphore_t *sem,unsigned timeout);
void sem_signal(semaphore_t *sem);
#endif
