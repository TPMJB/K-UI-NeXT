/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_READ_PROBE_HOST_KOS_H
#define KUI_VMU_READ_PROBE_HOST_KOS_H
#include <stdint.h>
#define INIT_IRQ 1
#define INIT_CONTROLLER 2
#define INIT_NO_DCLOAD 4
#define INIT_QUIET 8
#define INIT_VMU 16
#define KOS_INIT_FLAGS(flags) _Static_assert((flags)!=0,"initialization flags")
#define DM_640x480 1
#define DM_MULTIBUFFER 2
#define PM_RGB565 1
#define MAPLE_FUNC_CONTROLLER 1u
#define MAPLE_FUNC_MEMCARD 0x02000000u
#define MAPLE_EOK 0
#define MAPLE_EFAIL -1
#define MAPLE_EINVALID -2
#define MAPLE_ETIMEOUT -5
#define CONT_A 1u
#define CONT_X 2u
#define CONT_DPAD_LEFT 4u
#define CONT_DPAD_RIGHT 8u
#define CONT_DPAD_UP 16u
#define CONT_DPAD_DOWN 32u
#define CONT_B 64u
#define CONT_START 128u
typedef struct maple_device {
    int valid;
    struct {uint32_t functions;char product_name[30];} info;
} maple_device_t;
extern uint16_t *vram_s;
void vid_waitvbl(void);
void vid_flip(int);
void vid_set_mode(int,int);
void thd_sleep(unsigned);
uint64_t timer_us_gettime64(void);
uint64_t timer_ms_gettime64(void);
maple_device_t *maple_enum_type(int,uint32_t);
maple_device_t *maple_enum_dev(int,int);
void *maple_dev_status(maple_device_t *);
#endif
