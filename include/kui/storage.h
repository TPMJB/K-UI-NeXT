/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_STORAGE_H
#define KUI_STORAGE_H

/* Stable on-wire IDs. Zero preserves existing SCIF launch manifests. AUTO
 * is a discovery policy only and must never reach a resident game reader. */
enum kui_storage_transport {
    KUI_STORAGE_SCIF = 0,
    KUI_STORAGE_SCI = 1,
    KUI_STORAGE_IDE = 2,
    KUI_STORAGE_AUTO = 3
};

#endif
