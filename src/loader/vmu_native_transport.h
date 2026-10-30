/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_TRANSPORT_H
#define KUI_VMU_NATIVE_TRANSPORT_H
#include "retail_storage.h"
#include "sci_sd_bus.h"

#if KUI_NATIVE_VMU_ONLY
/* Both variants borrow the existing reader's card under its GD lock. The
 * SPI write/status protocol is bus-independent; each backend must still
 * prove its own ownership and successful restoration. */
static inline bool kui_native_vmu_transport_supported(uint32_t transport) {
#ifdef KUI_RETAIL_TRANSPORT
    return transport==KUI_RETAIL_TRANSPORT;
#else
    return transport==KUI_STORAGE_SCI || transport==KUI_STORAGE_SCIF;
#endif
}
static inline bool kui_native_vmu_transport_healthy(uint32_t transport) {
#if defined(KUI_RETAIL_TRANSPORT) && KUI_RETAIL_TRANSPORT == 1
    return transport==KUI_STORAGE_SCI && kui_sci_sd_healthy();
#elif defined(KUI_RETAIL_TRANSPORT) && KUI_RETAIL_TRANSPORT == 0
    return transport==KUI_STORAGE_SCIF && kui_retail_sd_healthy();
#else
    return transport==KUI_STORAGE_SCI?kui_sci_sd_healthy():
        transport==KUI_STORAGE_SCIF?kui_retail_sd_healthy():false;
#endif
}
static inline bool kui_native_vmu_transport_faulted(uint32_t transport) {
#if defined(KUI_RETAIL_TRANSPORT) && KUI_RETAIL_TRANSPORT == 1
    return transport!=KUI_STORAGE_SCI || kui_sci_sd_faulted();
#elif defined(KUI_RETAIL_TRANSPORT) && KUI_RETAIL_TRANSPORT == 0
    return transport!=KUI_STORAGE_SCIF || kui_retail_sd_faulted();
#else
    return transport==KUI_STORAGE_SCI?kui_sci_sd_faulted():
        transport==KUI_STORAGE_SCIF?kui_retail_sd_faulted():true;
#endif
}
#endif
#endif
