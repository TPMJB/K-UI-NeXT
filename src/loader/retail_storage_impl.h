/* SPDX-License-Identifier: GPL-3.0-only */
#include "sci_sd_bus.h"
#include <string.h>

/* The high stage supports all devices. Each low-memory resident is compiled
 * for exactly one transport; unreachable backends and their state/code are
 * discarded without expanding the original protected IP-memory footprint. */
#ifdef KUI_RETAIL_TRANSPORT
_Static_assert(KUI_RETAIL_TRANSPORT >= KUI_STORAGE_SCIF &&
               KUI_RETAIL_TRANSPORT <= KUI_STORAGE_IDE, "resident transport");
#endif
static inline uint32_t selected(const struct kui_retail_storage *s) {
#ifdef KUI_RETAIL_TRANSPORT
    (void)s;
    return KUI_RETAIL_TRANSPORT;
#else
    return s->transport;
#endif
}
static inline bool supported(uint32_t transport) {
#ifdef KUI_RETAIL_TRANSPORT
    return transport == KUI_RETAIL_TRANSPORT;
#else
    return transport <= KUI_STORAGE_IDE;
#endif
}

KUI_RETAIL_STORAGE_API const char *kui_retail_storage_name(uint32_t transport) {
#ifdef KUI_RETAIL_TRANSPORT
    (void)transport;
#if KUI_RETAIL_TRANSPORT == 0
    return "SCIF MICROSD";
#elif KUI_RETAIL_TRANSPORT == 1
    return "SCI MICROSD";
#else
    return "IDE / CF";
#endif
#else
    switch(transport) {
        case KUI_STORAGE_SCIF: return "SCIF MICROSD";
        case KUI_STORAGE_SCI: return "SCI MICROSD";
        case KUI_STORAGE_IDE: return "IDE / CF";
        default: return "INVALID STORAGE";
    }
#endif
}
KUI_RETAIL_STORAGE_API uint64_t kui_retail_storage_blocks(const struct kui_retail_storage *s) {
    return selected(s) == KUI_STORAGE_IDE ? s->device.ata.sectors : s->device.sd.blocks;
}
KUI_RETAIL_STORAGE_API enum kui_loader_sd_result kui_retail_storage_init(struct kui_retail_storage *s, uint32_t transport) {
    if(!s || !supported(transport)) return KUI_LOADER_SD_ARGUMENT;
    memset(s, 0, sizeof(*s)); s->transport = transport;
    if(selected(s) == KUI_STORAGE_IDE)
        return kui_ata_init(&s->device.ata, kui_ata_native_bus()) ? KUI_LOADER_SD_OK : KUI_LOADER_SD_NOT_READY;
    if(selected(s) == KUI_STORAGE_SCIF) return kui_retail_sd_init(&s->device.sd);
    enum kui_loader_sd_result r = kui_sci_sd_acquire();
    if(r != KUI_LOADER_SD_OK) return r;
    r = kui_loader_sd_init_bus(&s->device.sd, kui_sci_sd_bus());
    if(r == KUI_LOADER_SD_OK && !kui_sci_sd_healthy()) {
        s->device.sd.ready = false; r = KUI_LOADER_SD_TIMEOUT;
    }
    kui_sci_sd_release();
    return r;
}
KUI_RETAIL_STORAGE_API enum kui_loader_sd_result kui_retail_storage_adopt(struct kui_retail_storage *s,
    const struct kui_retail_storage *prepared) {
    if(!s || !prepared || s == prepared || !supported(prepared->transport))
        return KUI_LOADER_SD_ARGUMENT;
    if(prepared->stream.active ||
       (selected(prepared) == KUI_STORAGE_IDE && prepared->device.ata.read_active))
        return KUI_LOADER_SD_NOT_READY;
    memset(s, 0, sizeof(*s)); s->transport = prepared->transport;
    if(selected(s) == KUI_STORAGE_IDE) {
        if(!prepared->device.ata.ready || !prepared->device.ata.sectors)
            return KUI_LOADER_SD_NOT_READY;
        s->device.ata = prepared->device.ata;
        s->device.ata.bus = kui_ata_native_bus();
        return s->device.ata.bus ? KUI_LOADER_SD_OK : KUI_LOADER_SD_UNSUPPORTED;
    }
    if(selected(s) == KUI_STORAGE_SCIF)
        return kui_retail_sd_adopt(&s->device.sd, &prepared->device.sd);
    if(!prepared->device.sd.ready || prepared->device.sd.slow || !prepared->device.sd.blocks)
        return KUI_LOADER_SD_NOT_READY;
    const struct kui_loader_sd_bus *bus = kui_sci_sd_bus();
    if(!bus) return KUI_LOADER_SD_UNSUPPORTED;
    s->device.sd = prepared->device.sd;
    s->device.sd.bus = *bus;
    return KUI_LOADER_SD_OK;
}
KUI_RETAIL_STORAGE_API enum kui_loader_sd_result kui_retail_storage_acquire(struct kui_retail_storage *s) {
    if(!s || !supported(s->transport)) return KUI_LOADER_SD_ARGUMENT;
    switch(selected(s)) {
        case KUI_STORAGE_SCIF: return kui_retail_sd_acquire();
        case KUI_STORAGE_SCI: return kui_sci_sd_acquire();
        case KUI_STORAGE_IDE: return s->device.ata.ready ? KUI_LOADER_SD_OK : KUI_LOADER_SD_NOT_READY;
        default: return KUI_LOADER_SD_ARGUMENT;
    }
}
KUI_RETAIL_STORAGE_API enum kui_loader_sd_result kui_retail_storage_read_run(struct kui_retail_storage *s,
    uint32_t lba, uint32_t available, uint8_t out[512]) {
    if(!s || !out || !available || !supported(s->transport))
        return KUI_LOADER_SD_ARGUMENT;
    if(selected(s) == KUI_STORAGE_IDE)
        return kui_ata_read_run(&s->device.ata, lba, available, out) ? KUI_LOADER_SD_OK : KUI_LOADER_SD_COMMAND;
    return kui_retail_sd_read_run(&s->device.sd, &s->stream, lba, available, out);
}
KUI_RETAIL_STORAGE_API enum kui_loader_sd_result kui_retail_storage_stop(struct kui_retail_storage *s) {
    if(!s || !supported(s->transport)) return KUI_LOADER_SD_ARGUMENT;
    if(selected(s) == KUI_STORAGE_IDE)
        return kui_ata_read_stop(&s->device.ata) ? KUI_LOADER_SD_OK : KUI_LOADER_SD_COMMAND;
    enum kui_loader_sd_result r = kui_loader_sd_stream_stop(&s->device.sd, &s->stream);
    if(selected(s) == KUI_STORAGE_SCI && !kui_sci_sd_healthy()) {
        s->device.sd.ready = false;
        return KUI_LOADER_SD_TIMEOUT;
    }
    return r;
}
KUI_RETAIL_STORAGE_API void kui_retail_storage_release(struct kui_retail_storage *s) {
    if(selected(s) == KUI_STORAGE_SCIF) kui_retail_sd_release();
    else if(selected(s) == KUI_STORAGE_SCI) kui_sci_sd_release();
}
