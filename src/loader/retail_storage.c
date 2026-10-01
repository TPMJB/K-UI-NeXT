/* SPDX-License-Identifier: GPL-3.0-only */
#include "retail_storage.h"
#include "sci_sd_bus.h"
#include <string.h>

const char *kui_retail_storage_name(uint32_t transport) {
    switch(transport) {
        case KUI_STORAGE_SCIF: return "SCIF MICROSD";
        case KUI_STORAGE_SCI: return "SCI MICROSD";
        case KUI_STORAGE_IDE: return "IDE / CF";
        default: return "INVALID STORAGE";
    }
}
uint64_t kui_retail_storage_blocks(const struct kui_retail_storage *s) {
    return s->transport == KUI_STORAGE_IDE ? s->device.ata.sectors : s->device.sd.blocks;
}
enum kui_loader_sd_result kui_retail_storage_init(struct kui_retail_storage *s, uint32_t transport) {
    if(!s || transport > KUI_STORAGE_IDE) return KUI_LOADER_SD_ARGUMENT;
    memset(s, 0, sizeof(*s)); s->transport = transport;
    if(transport == KUI_STORAGE_IDE)
        return kui_ata_init(&s->device.ata, kui_ata_native_bus()) ? KUI_LOADER_SD_OK : KUI_LOADER_SD_NOT_READY;
    if(transport == KUI_STORAGE_SCIF) return kui_retail_sd_init(&s->device.sd);
    enum kui_loader_sd_result r = kui_sci_sd_acquire();
    if(r != KUI_LOADER_SD_OK) return r;
    r = kui_loader_sd_init_bus(&s->device.sd, kui_sci_sd_bus());
    if(r == KUI_LOADER_SD_OK && !kui_sci_sd_healthy()) {
        s->device.sd.ready = false; r = KUI_LOADER_SD_TIMEOUT;
    }
    kui_sci_sd_release();
    return r;
}
enum kui_loader_sd_result kui_retail_storage_adopt(struct kui_retail_storage *s,
    const struct kui_retail_storage *prepared) {
    if(!s || !prepared || s == prepared || prepared->transport > KUI_STORAGE_IDE)
        return KUI_LOADER_SD_ARGUMENT;
    if(prepared->stream.active) return KUI_LOADER_SD_NOT_READY;
    memset(s, 0, sizeof(*s)); s->transport = prepared->transport;
    if(s->transport == KUI_STORAGE_IDE) {
        if(!prepared->device.ata.ready || !prepared->device.ata.sectors)
            return KUI_LOADER_SD_NOT_READY;
        s->device.ata = prepared->device.ata;
        s->device.ata.bus = kui_ata_native_bus();
        return s->device.ata.bus ? KUI_LOADER_SD_OK : KUI_LOADER_SD_UNSUPPORTED;
    }
    if(s->transport == KUI_STORAGE_SCIF)
        return kui_retail_sd_adopt(&s->device.sd, &prepared->device.sd);
    if(!prepared->device.sd.ready || prepared->device.sd.slow || !prepared->device.sd.blocks)
        return KUI_LOADER_SD_NOT_READY;
    const struct kui_loader_sd_bus *bus = kui_sci_sd_bus();
    if(!bus) return KUI_LOADER_SD_UNSUPPORTED;
    s->device.sd = prepared->device.sd;
    s->device.sd.bus = *bus;
    return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_retail_storage_acquire(struct kui_retail_storage *s) {
    if(!s) return KUI_LOADER_SD_ARGUMENT;
    switch(s->transport) {
        case KUI_STORAGE_SCIF: return kui_retail_sd_acquire();
        case KUI_STORAGE_SCI: return kui_sci_sd_acquire();
        case KUI_STORAGE_IDE: return s->device.ata.ready ? KUI_LOADER_SD_OK : KUI_LOADER_SD_NOT_READY;
        default: return KUI_LOADER_SD_ARGUMENT;
    }
}
enum kui_loader_sd_result kui_retail_storage_read_run(struct kui_retail_storage *s,
    uint32_t lba, uint32_t available, uint8_t out[512]) {
    if(!s || !out || !available || s->transport > KUI_STORAGE_IDE)
        return KUI_LOADER_SD_ARGUMENT;
    if(s->transport == KUI_STORAGE_IDE)
        return kui_ata_read(&s->device.ata, lba, 1, out) ? KUI_LOADER_SD_OK : KUI_LOADER_SD_COMMAND;
    return kui_retail_sd_read_run(&s->device.sd, &s->stream, lba, available, out);
}
enum kui_loader_sd_result kui_retail_storage_stop(struct kui_retail_storage *s) {
    if(!s || s->transport > KUI_STORAGE_IDE) return KUI_LOADER_SD_ARGUMENT;
    if(s->transport == KUI_STORAGE_IDE) return KUI_LOADER_SD_OK;
    enum kui_loader_sd_result r = kui_loader_sd_stream_stop(&s->device.sd, &s->stream);
    if(s->transport == KUI_STORAGE_SCI && !kui_sci_sd_healthy()) {
        s->device.sd.ready = false;
        return KUI_LOADER_SD_TIMEOUT;
    }
    return r;
}
void kui_retail_storage_release(struct kui_retail_storage *s) {
    if(s->transport == KUI_STORAGE_SCIF) kui_retail_sd_release();
    else if(s->transport == KUI_STORAGE_SCI) kui_sci_sd_release();
}
