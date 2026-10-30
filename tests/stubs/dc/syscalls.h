/* SPDX-License-Identifier: GPL-3.0-only */
/* Host firmware test double. Production uses the pinned KOS headers. */
#ifndef KUI_TEST_SYSCALLS_H
#define KUI_TEST_SYSCALLS_H
#include <stdint.h>
typedef int cd_cmd_code_t;
typedef int cd_area_t;
/* Native media admission uses the pinned KOS check-drive ABI. In particular,
 * disc type zero means audio CD OR no disc, so readiness is a separate field. */
typedef enum cd_stat {
    CD_STATUS_READ_FAIL=-1, CD_STATUS_BUSY=0, CD_STATUS_PAUSED=1,
    CD_STATUS_STANDBY=2, CD_STATUS_PLAYING=3, CD_STATUS_SEEKING=4,
    CD_STATUS_SCANNING=5, CD_STATUS_OPEN=6, CD_STATUS_NO_DISC=7,
    CD_STATUS_RETRY=8, CD_STATUS_ERROR=9, CD_STATUS_FATAL=12
} cd_stat_t;
typedef enum cd_disc_types {
    CD_CDDA=0x00, CD_CDROM=0x10, CD_CDROM_XA=0x20, CD_CDI=0x30,
    CD_GDROM=0x80, CD_FAIL=0xf0
} cd_disc_types_t;
typedef struct cd_check_drive_status {
    cd_stat_t status;
    cd_disc_types_t disc_type;
} cd_check_drive_status_t;
enum { CD_CMD_PIOREAD=16, CD_CMD_DMAREAD=17, CD_CMD_INIT=24, CD_CMD_GETTOC2=19,
       CD_CMD_REQ_MODE=30, CD_CMD_SET_MODE=31, CD_CMD_STOP=33,
       CDROM_READ_WHOLE_SECTOR=0xff, CDROM_READ_DATA_AREA=0x20 };
typedef struct {uint32_t start_sec,num_sec;void *buffer;int is_test;} cd_read_params_t;
typedef struct {uint32_t entry[99],first,last,leadout_sector;} cd_toc_t;
typedef struct {cd_area_t area;cd_toc_t *toc;} cd_cmd_toc_params_t;
typedef struct {int32_t err1,err2;uint32_t size;int ata;} cd_cmd_chk_status_t;
typedef struct {int operation,part,track_type,size;} cd_sec_mode_params_t;
int syscall_gdrom_send_command(cd_cmd_code_t,void *);
int syscall_gdrom_check_drive(cd_check_drive_status_t *);
void syscall_gdrom_exec_server(void);
int syscall_gdrom_check_command(int,cd_cmd_chk_status_t *);
void syscall_gdrom_abort_command(int);
int syscall_gdrom_sector_mode(cd_sec_mode_params_t *);
#endif
