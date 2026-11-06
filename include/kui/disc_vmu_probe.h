/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_VMU_PROBE_H
#define KUI_DISC_VMU_PROBE_H
#include "kui/vmu_native_layout.h"
#include "kui/vmu_native_heap.h"
#include <stddef.h>
#include <stdint.h>

#define KUI_DISC_VMU_PROBE_VERSION 1u
enum kui_disc_vmu_probe_stage {
    KUI_DISC_VMU_PROBE_ARGUMENT=0,
    KUI_DISC_VMU_PROBE_EXTENT,
    KUI_DISC_VMU_PROBE_MAPLE_DRIVER,
    KUI_DISC_VMU_PROBE_MAPLE_LAYOUT,
    KUI_DISC_VMU_PROBE_HEAP_DISCOVERY,
    KUI_DISC_VMU_PROBE_HEAP_SLOT,
    KUI_DISC_VMU_PROBE_HEAP_CALL,
    KUI_DISC_VMU_PROBE_STARTUP_CLEAR,
    KUI_DISC_VMU_PROBE_READY
};
/* Diagnostic metadata only, never a resident-launch capability. The caller
 * supplies the physical disc's product separately. A failure retains only
 * metadata independently proved before its named gate; missing candidate
 * objects stay zero. driver_result/heap_result are the existing core enums,
 * initialized INVALID until their corresponding finder has run.
 *
 * READY says only that these immutable recognizers passed. It does not prove
 * allocator placement/lifetime, live SDK state, exit windows, card ownership,
 * transport support or safe installation. The stage must still run those
 * independent checks. A recognized pre-allocator clear that can erase the
 * high installer is refused: physical-disc boot has no surviving low-memory
 * continuation for deferring that clear. Nothing here touches a device, card
 * or game RAM. */
struct kui_disc_vmu_probe {
    uint32_t version,bytes,stage,file_bytes,program_bytes,file_crc32;
    int32_t driver_result,heap_result;
    struct kui_vmu_native_discovery driver;
    struct kui_vmu_native_layout layout;
    struct kui_vmu_native_heap heap;
    struct kui_vmu_native_heap_call call;
};
/* Read a bounded immutable native executable using the same extent, fresh
 * Maple/layout and allocator/caller recognizers as physical disc boot.
 * Ordinary failures initialize out and return false; overlapping/aliased or
 * invalid output storage refuses without writing either object. No input
 * byte is modified. Only READY returns true. */
bool kui_disc_vmu_probe_run(const void *image,size_t file_bytes,uint32_t load,
    struct kui_disc_vmu_probe *out);
const char *kui_disc_vmu_probe_stage_text(uint32_t stage);
#endif
