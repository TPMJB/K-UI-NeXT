/* SPDX-License-Identifier: MIT */
#include "wifi_band_control.h"

bool kwifi_band_change(uint8_t current, uint8_t requested, bool supports_5g, const struct kwifi_band_ops *ops) {
    if(requested != KWM_BAND_24 && requested != KWM_BAND_5 && requested != KWM_BAND_BOTH) return false;
    if(!supports_5g) {
        if(requested == KWM_BAND_5) return false;
        requested = KWM_BAND_24;
    }
    if(requested == current) return true;
    ops->pause(ops->ctx);
    bool changed = ops->disconnect(ops->ctx) && ops->apply(ops->ctx, requested);
    if(changed) ops->commit(ops->ctx, requested);
    ops->resume(ops->ctx, changed);
    return changed;
}
