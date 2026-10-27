/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_WRITE_MAPLE_H
#define KUI_TEST_WRITE_MAPLE_H
#include <stdint.h>
#define MAPLE_FUNC_MEMCARD UINT32_C(0x02000000)
#define MAPLE_FUNC_CONTROLLER UINT32_C(0x01000000)
#define MAPLE_COMMAND_BWRITE 12
#define MAPLE_COMMAND_BSYNC 13
#define MAPLE_RESPONSE_OK 7
#define MAPLE_RESPONSE_AGAIN (-4)
#define MAPLE_FRAME_VACANT 0
#define MAPLE_FRAME_UNSENT 1
#define MAPLE_FRAME_SENT 2
#define MAPLE_FRAME_RESPONDED 3
#define MAPLE_PORT_COUNT 4
#define MAPLE_UNIT_COUNT 6
#define MAPLE_EOK 0
#define MAPLE_EFAIL (-1)
#define MAPLE_EINVALID (-3)
#define MAPLE_ETIMEOUT (-5)
typedef struct maple_state_str {int unused;} maple_state_t;
typedef struct maple_frame {
    int cmd,dst_port,dst_unit,length;
    volatile int state,queued;
    uint32_t *send_buf;
    uint8_t *recv_buf;
    void (*callback)(maple_state_t *,struct maple_frame *);
} maple_frame_t;
typedef struct maple_device {
    uint8_t valid;
    int port,unit;
    struct {uint32_t functions;} info;
    maple_frame_t frame;
} maple_device_t;
typedef struct maple_response {
    int8_t response;
    uint8_t dst_addr,src_addr,data_len;
    uint8_t data[];
} maple_response_t;
maple_device_t *maple_enum_dev(int,int);
uint8_t maple_addr(int,int);
int maple_frame_trylock(maple_frame_t *);
void maple_frame_init(maple_frame_t *);
void maple_frame_unlock(maple_frame_t *);
int maple_queue_frame(maple_frame_t *);
int maple_queue_remove(maple_frame_t *);
#endif
