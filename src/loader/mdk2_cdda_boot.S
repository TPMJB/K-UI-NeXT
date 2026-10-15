/* SPDX-License-Identifier: GPL-3.0-only */
#if !KUI_RETAIL_MDK2_CDDA
#error This bootstrap requires the isolated MDK2 CDDA profile
#endif
/* The authored low bootstrap is title independent once its scalar heap
 * contracts are selected. Keep its complete register/SR preservation and
 * persistent repeat-init refusal in one implementation. */
#include "toy_pilot_boot.S"
