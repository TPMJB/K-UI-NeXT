/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_LAUNCH_H
#define KUI_DISC_LAUNCH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Native physical-GD handoff. Flags 1 retain the validated physical-only
 * baseline; flags 3 require an independently admitted virtual-card profile.
 * Neither path replaces GD syscalls or uses an SD game-image map. The
 * executable and IP must have been read from the real drive.
 */
#define KUI_DISC_LAUNCH_PACKAGE "0:/KUI/apps/disc/native-disc.kui"
#define KUI_DISC_LAUNCH_VMU_PACKAGE "0:/KUI/apps/disc/native-disc-vmu.kui"
#define KUI_DISC_LAUNCH_VMU_A1_PACKAGE "0:/KUI/apps/disc/native-disc-vmu-a1.kui"
#define KUI_DISC_LAUNCH_LOAD_ADDRESS 0x8c010000u
#define KUI_DISC_LAUNCH_HEADER_OFFSET 0x100u
#define KUI_DISC_LAUNCH_HEADER_BYTES 64u
#define KUI_DISC_LAUNCH_CONFIG_OFFSET 0x1000u
#define KUI_DISC_LAUNCH_CONFIG_BYTES 512u
#define KUI_DISC_LAUNCH_STAGE_OFFSET 0x2000u
#define KUI_DISC_LAUNCH_STAGE_ADDRESS 0x8ce00000u
#define KUI_DISC_LAUNCH_STAGE_MAX_BYTES 0x10000u
#define KUI_DISC_LAUNCH_VMU_STAGE_MAX_BYTES 0x100000u
#define KUI_DISC_LAUNCH_STACK 0x8cff0000u
#define KUI_DISC_LAUNCH_IP_ADDRESS 0x8c008000u
#define KUI_DISC_LAUNCH_IP_BYTES 32768u
#define KUI_DISC_LAUNCH_EXEC_ADDRESS 0x8c010000u
#define KUI_DISC_LAUNCH_EXEC_MAX_BYTES 0x400000u
#define KUI_DISC_LAUNCH_FLAGS 1u /* controlled native-GD bootstrap, no region gate */
#define KUI_DISC_LAUNCH_VMU_FLAGS 3u /* independent virtual-card service */
#define KUI_DISC_LAUNCH_VMU_PROFILE_BYTES 4096u

struct kui_disc_launch_plan {
    uint32_t module_bytes,payload_bytes,ip_offset,exec_offset,exec_bytes;
    uint32_t ip_crc32,exec_crc32;
    uint8_t ip_sha256[32],exec_sha256[32];
    uint32_t flags,vmu_profile_offset,vmu_profile_bytes,vmu_transport;
    uint8_t vmu_profile_sha256[32];
};
/* Read-only admission of a freshly loaded template: exact capability shape,
 * linker geometry and an entirely zero configuration. No physical disc read.
 * Returns the complete appended allocation geometry for executable_bytes.
 * Output is cleared on refusal. Inputs and output must be disjoint. */
bool kui_disc_launch_layout(const void *module,size_t module_bytes,
    uint32_t executable_bytes,struct kui_disc_launch_plan *out);
/* Encode only the 512-byte configuration in an already allocated image.
 * Caller fills both exact IP/boot fingerprints from the physical-disc plan.
 * Does not copy or modify game bytes. Clears packet on invalid geometry.
 * Packet and plan must be disjoint. */
bool kui_disc_launch_encode(uint8_t packet[KUI_DISC_LAUNCH_CONFIG_BYTES],
    const struct kui_disc_launch_plan *plan);
/* Decode a bounded configuration before using its claimed payload length.
 * CRC, version, reserved bytes, offsets and complete RAM geometry are checked.
 * Output is cleared on refusal. Packet and output must be disjoint. */
bool kui_disc_launch_decode(const uint8_t packet[KUI_DISC_LAUNCH_CONFIG_BYTES],
    struct kui_disc_launch_plan *out);
/* Full handoff admission: package, packet, native GD IP, whole IP and exact
 * executable SHA-256+CRC. No destination write is performed. out is cleared
 * on refusal; it must be disjoint from the complete payload. */
bool kui_disc_launch_validate(const void *payload,size_t payload_bytes,
    struct kui_disc_launch_plan *out);
#endif
