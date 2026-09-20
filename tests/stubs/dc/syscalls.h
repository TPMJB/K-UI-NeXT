/* SPDX-License-Identifier: GPL-3.0-only */
/* Host firmware test double. Production uses the pinned KOS headers. */
#ifndef KUI_TEST_SYSCALLS_H
#define KUI_TEST_SYSCALLS_H
#include <stdint.h>
typedef int cd_cmd_code_t;
typedef int cd_area_t;
enum { CD_CMD_PIOREAD=16, CD_CMD_DMAREAD=17, CD_CMD_INIT=24, CD_CMD_GETTOC2=19,
       CDROM_READ_WHOLE_SECTOR=0xff, CDROM_READ_DATA_AREA=0x20 };
typedef struct {uint32_t start_sec,num_sec;void *buffer;int is_test;} cd_read_params_t;
typedef struct {uint32_t entry[99],first,last,leadout_sector;} cd_toc_t;
typedef struct {cd_area_t area;cd_toc_t *toc;} cd_cmd_toc_params_t;
typedef struct {int32_t err1,err2;uint32_t size;int ata;} cd_cmd_chk_status_t;
typedef struct {int operation,part,track_type,size;} cd_sec_mode_params_t;
int syscall_gdrom_send_command(cd_cmd_code_t,void *);
void syscall_gdrom_exec_server(void);
int syscall_gdrom_check_command(int,cd_cmd_chk_status_t *);
void syscall_gdrom_abort_command(int);
int syscall_gdrom_sector_mode(cd_sec_mode_params_t *);
#endif
