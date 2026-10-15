/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_NATIVE_DRIVE_H
#define KUI_RETAIL_NATIVE_DRIVE_H

/* Optional native image-drive condition reporting. A queued command's
 * progress is reported by CHECK; it does not change the mounted image's
 * logical drive condition. Keep the CE and Toy pilot policies separate. */
#ifndef KUI_RETAIL_NATIVE_DRIVE_STATE
#define KUI_RETAIL_NATIVE_DRIVE_STATE 0
#endif
#if KUI_RETAIL_NATIVE_DRIVE_STATE != 0 && KUI_RETAIL_NATIVE_DRIVE_STATE != 1
#error "KUI_RETAIL_NATIVE_DRIVE_STATE must be 0 or 1"
#endif
#if KUI_RETAIL_NATIVE_DRIVE_STATE && (defined(KUI_RETAIL_CE) || (KUI_RETAIL_TOY_PILOT && !KUI_RETAIL_MDK2_CDDA))
#error "Native drive state is restricted to native general readers"
#endif

#endif
