/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_HARNESS_HOST_KOS_H
#define KUI_VMU_HARNESS_HOST_KOS_H
#include <stdint.h>
#define INIT_IRQ 1
#define INIT_CONTROLLER 2
#define INIT_NO_DCLOAD 4
#define INIT_QUIET 8
#define KOS_INIT_FLAGS(flags) _Static_assert((flags)!=0,"initialization flags")
#define DM_640x480 1
#define DM_MULTIBUFFER 2
#define PM_RGB565 1
#define MAPLE_FUNC_CONTROLLER 1
#define CONT_A 1
#define CONT_X 2
#define CONT_DPAD_LEFT 4
#define CONT_DPAD_RIGHT 8
typedef struct maple_device {int unused;} maple_device_t;
extern uint16_t *vram_s;
void vid_waitvbl(void);
void vid_flip(int page);
void vid_set_mode(int mode,int format);
void thd_sleep(unsigned milliseconds);
maple_device_t *maple_enum_type(int n,uint32_t functions);
void *maple_dev_status(maple_device_t *device);
#endif
