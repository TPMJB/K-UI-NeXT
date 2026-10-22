/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_IDENTITY_TEST_SYSCALLS_H
#define KUI_IDENTITY_TEST_SYSCALLS_H
/* Minimal status-only test double for pinned KOS. There are intentionally no
 * reset/init/command functions here: identity must use the guarded adapter. */
typedef enum {CD_STATUS_READ_FAIL=-1,CD_STATUS_BUSY=0,CD_STATUS_PAUSED=1,
    CD_STATUS_STANDBY=2,CD_STATUS_PLAYING=3,CD_STATUS_SEEKING=4,
    CD_STATUS_SCANNING=5,CD_STATUS_OPEN=6,CD_STATUS_NO_DISC=7,
    CD_STATUS_RETRY=8,CD_STATUS_ERROR=9,CD_STATUS_FATAL=12} cd_stat_t;
typedef enum {CD_CDDA=0,CD_CDROM=0x10,CD_CDROM_XA=0x20,CD_CDI=0x30,
    CD_GDROM=0x80,CD_FAIL=0xf0} cd_disc_types_t;
typedef struct {cd_stat_t status;cd_disc_types_t disc_type;} cd_check_drive_status_t;
int syscall_gdrom_check_drive(cd_check_drive_status_t *status);
#endif
