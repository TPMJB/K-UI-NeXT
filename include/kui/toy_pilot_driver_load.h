/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_DRIVER_LOAD_H
#define KUI_TOY_PILOT_DRIVER_LOAD_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_TOY_PILOT_DRIVER_BYTES 20740u

/* amInit receives an empty allocated destination. Its original file loader
 * fills that destination later. This one-use ticket joins those two exact
 * boundaries without reading uninitialized memory or trusting another load.
 * The epoch changes on lifecycle revocation; the driver generation changes
 * on each prepared initialization. Neither is an SDK success result. */
struct kui_toy_pilot_driver_load {
    uint32_t image, bytes, driver_generation, epoch;
};

static inline void kui_toy_pilot_driver_load_clear(
        struct kui_toy_pilot_driver_load *load) {
    if(load) *load=(struct kui_toy_pilot_driver_load){0};
}

static inline bool kui_toy_pilot_driver_load_prepare(
        struct kui_toy_pilot_driver_load *load,uint32_t image,uint32_t bytes,
        uint32_t driver_generation,uint32_t epoch) {
    if(!load) return false;
    kui_toy_pilot_driver_load_clear(load);
    if(bytes!=KUI_TOY_PILOT_DRIVER_BYTES || image<UINT32_C(0x8c010000) ||
       image>=UINT32_C(0x8cfd0000) || bytes>UINT32_C(0x8cfd0000)-image ||
       !driver_generation || !epoch || epoch>=UINT32_C(0x7fffffff)) return false;
    *load=(struct kui_toy_pilot_driver_load){image,bytes,driver_generation,epoch};
    return true;
}

static inline bool kui_toy_pilot_driver_load_current(
        const struct kui_toy_pilot_driver_load *load,
        uint32_t driver_generation,uint32_t epoch) {
    return load && load->image && load->bytes==KUI_TOY_PILOT_DRIVER_BYTES &&
        load->driver_generation==driver_generation && load->epoch==epoch &&
        driver_generation && epoch && epoch<UINT32_C(0x7fffffff);
}

static inline bool kui_toy_pilot_driver_load_take(
        struct kui_toy_pilot_driver_load *pending,uint32_t result,uint32_t image,
        uint32_t driver_generation,uint32_t epoch,
        struct kui_toy_pilot_driver_load *ticket) {
    if(!pending || !ticket) return false;
    struct kui_toy_pilot_driver_load saved=*pending;
    kui_toy_pilot_driver_load_clear(pending);
    kui_toy_pilot_driver_load_clear(ticket);
    if(result!=1u || saved.image!=image ||
       !kui_toy_pilot_driver_load_current(&saved,driver_generation,epoch)) return false;
    *ticket=saved;
    return true;
}
#endif
