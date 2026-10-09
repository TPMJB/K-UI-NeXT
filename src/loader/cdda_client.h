/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_CLIENT_H
#define KUI_CDDA_CLIENT_H
#include <stdint.h>
#include "kui/cdda_handoff.h"
#include "kui/cdda_bridge.h"
#define CDDA_CLIENT_MAGIC 0x43444337u
enum cdda_client_op { CDDA_CLIENT_SERVICE,CDDA_CLIENT_PAUSE,CDDA_CLIENT_RESUME,
    CDDA_CLIENT_SEEK,CDDA_CLIENT_STATUS,CDDA_CLIENT_STOP,CDDA_CLIENT_RESTART,
    CDDA_CLIENT_ARM_GAP,CDDA_CLIENT_REENTRY,CDDA_CLIENT_MARK,CDDA_CLIENT_REPORT };
struct cdda_client_request {
    uint32_t epoch,op,value,aux,pc,sp,result,state,frame;
};
/* Native pointers are separate from the fixed160-byte handoff descriptor. */
struct cdda_client_exports {
    uint32_t magic,revision,bytes,epoch;
    void *context;
    uint32_t (*call)(void *,struct cdda_client_request *);
    uint32_t (*clock)(void *);
    uint32_t (*probe)(uint32_t (*)(void *),void *);
};
uint32_t cdda_client_entry(const void *);
#endif
