/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_LAUNCH_PACKAGE_H
#define KUI_VMU_LAUNCH_PACKAGE_H
#define KUI_VMU_LAUNCH_CAP_OFFSET 0x180
#define KUI_VMU_LAUNCH_CAP_BYTES 64
#define KUI_VMU_LAUNCH_CAP_MAGIC "KUIVMU04"
#define KUI_VMU_LAUNCH_CAP_VERSION 1
#define KUI_VMU_LAUNCH_CAP_PROFILE 2
#define KUI_VMU_LAUNCH_CAP_MODE 4
#define KUI_VMU_LAUNCH_CAP_FLAGS 1
#define KUI_VMU_LAUNCH_GDI_CRC 0x5e1b611a
#define KUI_VMU_LAUNCH_IP_CRC 0x770d2fed
#define KUI_VMU_LAUNCH_EXEC_CRC 0x7d83cf24
#define KUI_VMU_LAUNCH_EXEC_BYTES 2956128
#define KUI_VMU_LAUNCH_SESSION_LBA 45000
#define KUI_VMU_LAUNCH_BOOT_LBA 547556
/* Legacy unqualified constants above remain the Silver assembly contract. */
#define KUI_VMU_LAUNCH_TOY_CAP_PROFILE 1
#define KUI_VMU_LAUNCH_TOY_GDI_CRC 0xe25531d1
#define KUI_VMU_LAUNCH_TOY_IP_CRC 0x38ba2868
#define KUI_VMU_LAUNCH_TOY_EXEC_CRC 0xcdc493b3
#define KUI_VMU_LAUNCH_TOY_EXEC_BYTES 748444
#define KUI_VMU_LAUNCH_TOY_SESSION_LBA 45000
#define KUI_VMU_LAUNCH_TOY_BOOT_LBA 548634
/* Canonical LE capability: magic8; version,size,profile,mode,flags at8..24;
 * exact GDI/IP/EXEC CRCs at28/32/36; executable bytes/session/boot at40/44/48;
 * three zero reserved words at52/56/60. This describes code capability, not
 * an SD allocation or a selected card. Native packaging binds it to the
 * actual worker/stage; runtime filesystem admission remains mandatory. */
#ifndef __ASSEMBLER__
#include "kui/retail_image.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum kui_vmu_launch_route {
    KUI_VMU_LAUNCH_ORDINARY=0, KUI_VMU_LAUNCH_SILVER=1, KUI_VMU_LAUNCH_TOY=2,
    KUI_VMU_LAUNCH_SETTINGS=-1, KUI_VMU_LAUNCH_MANIFEST=-2,
    KUI_VMU_LAUNCH_CAPABILITY=-3
};
bool kui_vmu_launch_silver_candidate(const struct kui_retail_manifest *);
bool kui_vmu_launch_toy_candidate(const struct kui_retail_manifest *);
/* Candidate profile identifiers match capability records, not route values:
 * 1 Toy, 2 Silver, 0 ordinary. This grants no launch authority. */
unsigned kui_vmu_launch_profile_candidate(const struct kui_retail_manifest *);
bool kui_vmu_launch_capability_valid(const void *,size_t);
/* Presence is opt-in only for exact Silver/Toy candidates. Nonzero requested
 * options (including background requests which later fell back), transformed
 * containers and RAM patches refuse. Other games never inspect this module.
 * No I/O, mutation, fallback or write authority is performed by this helper. */
enum kui_vmu_launch_route kui_vmu_launch_package_route(
    const struct kui_retail_manifest *,uint32_t requested_options,
    bool original_raw_gdi,bool patches,bool optional_present,
    const void *capability,size_t bytes);
#endif
#endif
