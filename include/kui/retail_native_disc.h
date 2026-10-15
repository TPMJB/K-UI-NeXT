/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_NATIVE_DISC_H
#define KUI_RETAIL_NATIVE_DISC_H

/* BIOS system E0 function 2 checks whether the boot disc is unchanged.
 * Native games must check the selected image instead of the physical drive,
 * which may hold K-UI's boot disc. The mounted image is immutable throughout
 * a launch, so this optional native control returns zero (unchanged).
 * Keep it separate from CE's existing entry and the Toy pilot experiments. */
#ifndef KUI_RETAIL_NATIVE_DISC_CHECK
#define KUI_RETAIL_NATIVE_DISC_CHECK 0
#endif
#if KUI_RETAIL_NATIVE_DISC_CHECK != 0 && KUI_RETAIL_NATIVE_DISC_CHECK != 1
#error "KUI_RETAIL_NATIVE_DISC_CHECK must be 0 or 1"
#endif
#if KUI_RETAIL_NATIVE_DISC_CHECK && (defined(KUI_RETAIL_CE) || (KUI_RETAIL_TOY_PILOT && !KUI_RETAIL_MDK2_CDDA))
#error "Native disc check is restricted to native general readers"
#endif

#endif
