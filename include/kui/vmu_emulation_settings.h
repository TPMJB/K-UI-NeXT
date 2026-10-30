/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_EMULATION_SETTINGS_H
#define KUI_VMU_EMULATION_SETTINGS_H
#include "kui/probe.h"

#define KUI_VMU_EMULATION_SETTINGS_BYTES 64u
#define KUI_VMU_EMULATION_SETTINGS_PATH_A "0:/KUI/vmu/emulation-a.bin"
#define KUI_VMU_EMULATION_SETTINGS_PATH_B "0:/KUI/vmu/emulation-b.bin"
/* Global launch preference only. Missing both slots means enabled, preserving
 * existing launch behavior. The caller exclusively owns mounted FatFs; these
 * functions never mount, touch physical VMUs, or access card images/journals.
 * Damaged records are ignored only when another complete valid slot exists.
 * I/O errors, equal-sequence disagreements and two damaged slots refuse.
 * load preserves its output on failure. */
bool kui_vmu_emulation_settings_load(bool *enabled,kui_log_fn log);
/* Writes only the inactive slot, syncs, closes and rereads every byte. A
 * failed save may leave the previous or the complete requested preference,
 * never a partly decoded preference. Sequence exhaustion refuses. */
bool kui_vmu_emulation_settings_save(bool enabled,kui_log_fn log);
#endif
