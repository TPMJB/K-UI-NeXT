/* SPDX-License-Identifier: GPL-3.0-only */
/* This separate experimental pilot uses the existing isolated diagnostic
 * SCI bus without adding the large generic observer to the low reader. The
 * scoped header admission changes no default target or bus implementation. */
#if !KUI_RETAIL_TOY_PILOT || !KUI_RETAIL_SCI_DIAGNOSTIC || !KUI_SCI_DMA_PACED
#error This admission is restricted to the separate paced Toy pilot
#endif
#undef KUI_RETAIL_OBSERVE
#define KUI_RETAIL_OBSERVE 1
#include "sci_sd_bus.h"
#undef KUI_RETAIL_OBSERVE
#define KUI_RETAIL_OBSERVE 0
