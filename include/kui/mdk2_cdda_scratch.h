/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MDK2_CDDA_SCRATCH_H
#define KUI_MDK2_CDDA_SCRATCH_H
#include "kui/retail_gd.h"
#include "kui/toy_pilot.h"
#include "kui/toy_pilot_scratch.h"

/* A native IRQ can inherit the suspended worker's P2 stack. The exact MDK2
 * veneers reserve these local parameter/response arrays below their caller's
 * live frame. This per-dispatch capability covers only that one array; it
 * never admits a payload buffer, worker state, stack guard or bridge anchor.
 * Stage admission proves top is the linked 8192-byte private stack end and
 * bridge_active is a protected word in that worker's P2 BSS. */
struct kui_mdk2_cdda_scratch_proof {
    uint32_t sp, pr, top, bridge_active, locked, masked_sr;
    uint32_t token, command, executing;
};
struct kui_mdk2_cdda_scratch {
    uint32_t address, access, token, command;
};
struct kui_mdk2_cdda_scratch_veneer {
    uint32_t pr;
    uint8_t command, bytes, offset, reserved;
};

static inline void kui_mdk2_cdda_scratch_clear(struct kui_mdk2_cdda_scratch *c) {
    c->address=0u;
}
static inline void kui_mdk2_cdda_scratch_begin(struct kui_mdk2_cdda_scratch *c,
    const struct kui_mdk2_cdda_scratch_proof *p,uint32_t function,
    uint32_t argument,uint32_t param) {
    kui_mdk2_cdda_scratch_clear(c);
    if(p->bridge_active!=1u || p->locked!=1u || p->executing ||
       (p->masked_sr&UINT32_C(0x100000f0))!=UINT32_C(0x100000f0) ||
       (p->sp&3u) || p->sp<UINT32_C(0xacfd0000) || p->sp>=UINT32_C(0xacfe0000) ||
       (p->top&31u) || p->top<UINT32_C(0x8cfd2000) ||
       p->top>UINT32_C(0x8cfd8000)) return;
    uint32_t bytes=0u,offset=0u,writing=0u;
    if(function==KUI_GD_CHECK) {
        if(p->pr!=UINT32_C(0x8c0b1f88) || !argument || argument!=p->token) return;
        bytes=16u;writing=1u;
    } else if(function==KUI_GD_REQUEST) {
        if(p->command) return;
        static const struct kui_mdk2_cdda_scratch_veneer veneers[]={
            {UINT32_C(0x8c0b1eca),KUI_GD_PIOREAD,16u,4u,0u},
            {UINT32_C(0x8c0b1eca),KUI_GD_DMAREAD,16u,4u,0u},
            {UINT32_C(0x8c0b2050),KUI_RETAIL_GD_SEEK,4u,0u,0u},
            {UINT32_C(0x8c0b20d0),KUI_RETAIL_GD_PLAY,12u,0u,0u},
            {UINT32_C(0x8c0b20f2),KUI_RETAIL_GD_PLAY2,12u,0u,0u},
            {UINT32_C(0x8c0b213c),KUI_GD_GETTOC2,8u,0u,0u},
            {UINT32_C(0x8c0b215e),KUI_RETAIL_GD_GETSCD,12u,0u,0u},
            {UINT32_C(0x8c0b2192),KUI_RETAIL_GD_REQ_STAT,16u,0u,0u},
            {UINT32_C(0x8c0b21b0),KUI_RETAIL_GD_GET_VERS,4u,0u,0u}
        };
        for(unsigned i=0;i<sizeof(veneers)/sizeof(veneers[0]);i++)
            if(p->pr==veneers[i].pr && argument==veneers[i].command) {
                bytes=veneers[i].bytes;offset=veneers[i].offset;break;
            }
        if(!bytes) return;
    } else return;
    if(param!=p->sp+offset) return;
    uint32_t canonical=p->sp-UINT32_C(0x20000000);
    uint32_t bottom=p->top-KUI_TOY_PILOT_STACK_BYTES;
    if(canonical<bottom+KUI_TOY_SCRATCH_GUARD_BYTES+KUI_TOY_SCRATCH_CALLER_BYTES ||
       canonical+offset+bytes>p->top-KUI_TOY_SCRATCH_ANCHOR_BYTES) return;
    c->address=canonical+offset;c->access=bytes|(writing<<16);
    c->token=p->token;c->command=p->command;
}
static inline bool kui_mdk2_cdda_scratch_maps(const struct kui_mdk2_cdda_scratch *c,
    uint32_t address,uint32_t bytes,int writing,uint32_t token,
    uint32_t command,uint32_t executing) {
    return c->address && !executing && c->token==token && c->command==command &&
        address==c->address && bytes==(c->access&UINT32_C(0xffff)) &&
        writing==(int)(c->access>>16);
}
#endif
