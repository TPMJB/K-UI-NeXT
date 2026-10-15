/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_VMU_MAPLE_H
#define KUI_TEST_VMU_MAPLE_H
#include <stdint.h>
#define MAPLE_PORT_COUNT 4
#define MAPLE_UNIT_COUNT 6
#define MAPLE_FUNC_LCD 0x04000000u
#define MAPLE_FUNC_MEMCARD 0x02000000u
typedef struct { uint32_t functions; } maple_devinfo_t;
typedef struct {uint8_t valid;int port,unit;maple_devinfo_t info;} maple_device_t;
maple_device_t *maple_enum_dev(int port,int unit);
maple_device_t *maple_enum_type(int index,uint32_t function);
#endif
