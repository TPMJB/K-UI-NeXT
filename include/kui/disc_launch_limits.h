/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_LAUNCH_LIMITS_H
#define KUI_DISC_LAUNCH_LIMITS_H

/* Shared by the C admission code and the assembler capability header. This
 * is the native physical-disc format ceiling, not a promise of contiguous
 * staging RAM while the GUI is running. Keep assembler-safe literals here. */
#define KUI_DISC_LAUNCH_EXEC_MAX_BYTES 0x00c00000
#define KUI_DISC_LAUNCH_LEGACY_EXEC_MAX_BYTES 0x00400000

#endif
