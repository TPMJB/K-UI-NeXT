/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/ata.h"
#include <stddef.h>

#define ATA_BSY 0x80u
#define ATA_DRDY 0x40u
#define ATA_DF 0x20u
#define ATA_DRQ 0x08u
#define ATA_ERR 0x01u
#define ATA_TIMEOUT_US 5000000u
#define ATA_POLL_LIMIT 10000000u
#define ATA_LBA28_SECTORS UINT64_C(0x10000000)
#define ATA_LBA48_SECTORS UINT64_C(0x1000000000000)

static uint8_t rd(struct kui_ata *a, enum kui_ata_reg r) {
    return a->bus->read8(a->bus->ctx, r);
}
static void wr(struct kui_ata *a, enum kui_ata_reg r, uint8_t v) {
    a->bus->write8(a->bus->ctx, r, v);
}
static bool fail(struct kui_ata *a, enum kui_ata_error e) {
    a->error = e;
    return false;
}
/* Four alternate-status reads provide the ATA device-select/command settling
 * interval without owning a timer or clearing the interrupt status. */
static void settle(struct kui_ata *a) {
    for(unsigned i = 0; i < 4; ++i) (void)rd(a, KUI_ATA_ALTSTATUS);
}
static bool wait_status(struct kui_ata *a, uint8_t set, uint8_t clear,
                        bool check_error, bool check_present) {
    const struct kui_ata_bus *b = a->bus;
    uint64_t start = b->now_us ? b->now_us(b->ctx) : 0;
    for(unsigned poll = 0; poll < ATA_POLL_LIMIT; ++poll) {
        uint8_t status = rd(a, KUI_ATA_ALTSTATUS);
        if(check_present && (status == 0 || status == 0xff))
            return fail(a, KUI_ATA_ABSENT);
        /* ERR/DF are not valid until BSY clears. */
        if(!(status & ATA_BSY)) {
            if(check_error && (status & (ATA_ERR | ATA_DF)))
                return fail(a, KUI_ATA_DEVICE_ERROR);
            if((status & set) == set && !(status & clear)) return true;
        }
        if(b->now_us && b->now_us(b->ctx) - start >= ATA_TIMEOUT_US) break;
        if(b->pause) b->pause(b->ctx);
    }
    return fail(a, KUI_ATA_TIMEOUT);
}

/* Never change selection during another device's DMA or PIO transfer. */
static bool begin(struct kui_ata *a, uint8_t *previous) {
    if(a->bus->dma_busy(a->bus->ctx)) return fail(a, KUI_ATA_BUSY);
    *previous = rd(a, KUI_ATA_DEVICE);
    if(!wait_status(a, 0, ATA_BSY | ATA_DRQ, false, false)) return false;
    if(a->bus->dma_busy(a->bus->ctx)) return fail(a, KUI_ATA_BUSY);
    if(a->bus->prepare) a->bus->prepare(a->bus->ctx);
    wr(a, KUI_ATA_DEVICE, 0xf0);
    settle(a);
    return true;
}
static bool end(struct kui_ata *a, uint8_t previous, bool ok) {
    enum kui_ata_error saved = a->error;
    uint8_t s = rd(a, KUI_ATA_ALTSTATUS);
    /* An absent slave may float all bits high. It is safe to switch away;
     * a present device actually stuck busy must not be forcibly deselected. */
    if(s != 0xff && (s & (ATA_BSY | ATA_DRQ))) {
        a->ready = false;
        return fail(a, KUI_ATA_TIMEOUT);
    }
    (void)rd(a, KUI_ATA_STATUS); /* acknowledge this slave's final IRQ */
    wr(a, KUI_ATA_DEVICE, previous);
    settle(a);
    a->error = ok ? KUI_ATA_OK : saved;
    return ok;
}

bool kui_ata_init(struct kui_ata *a, const struct kui_ata_bus *bus) {
    if(!a) return false;
    *a = (struct kui_ata){.bus = bus};
    if(!bus || !bus->read8 || !bus->write8 || !bus->read16 ||
       !bus->write16 || !bus->dma_busy) return fail(a, KUI_ATA_INVALID);
    if(bus->dma_busy(bus->ctx)) return fail(a, KUI_ATA_BUSY);
    if(bus->activate && !bus->activate(bus->ctx)) return fail(a, KUI_ATA_PROTECTION);
    uint8_t previous;
    if(!begin(a, &previous)) return false;
    bool ok = wait_status(a, 0, ATA_BSY | ATA_DRQ, false, true);
    if(!ok) return end(a, previous, false);
    wr(a, KUI_ATA_COUNT, 0);
    wr(a, KUI_ATA_LBA0, 0);
    wr(a, KUI_ATA_LBA1, 0);
    wr(a, KUI_ATA_LBA2, 0);
    wr(a, KUI_ATA_COMMAND, 0xec); /* IDENTIFY DEVICE; never packet/reset */
    settle(a);
    if(!wait_status(a, ATA_DRQ, ATA_BSY, true, true)) return end(a, previous, false);
    uint16_t id[256];
    for(unsigned i = 0; i < 256; ++i) id[i] = bus->read16(bus->ctx);
    settle(a);
    ok = wait_status(a, 0, ATA_BSY | ATA_DRQ, true, true);
    if(ok) {
        /* Word 106 only has defined sector-size bits with validity 01b. */
        uint32_t words = (uint32_t)id[117] | ((uint32_t)id[118] << 16);
        /* CF's valid word 0 signature 0x848a also has bit 15 set; do not
         * misclassify it as ATAPI. IDENTIFY DEVICE already rejects ATAPI. */
        if(!(id[49] & 0x0200u) ||
           ((id[106] & 0xc000u) == 0x4000u && (id[106] & 0x1000u) && words != 256u))
            ok = fail(a, KUI_ATA_UNSUPPORTED);
    }
    if(ok) {
        bool sets_valid = (id[83] & 0xc000u) == 0x4000u;
        a->lba48 = sets_valid && (id[83] & 0x0400u);
        a->sectors = a->lba48 ? ((uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                         ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48)) :
                         ((uint64_t)id[60] | ((uint64_t)id[61] << 16));
        a->write_cache = (id[82] & 0x0020u) != 0;
        a->flush_supported = sets_valid && (id[83] & 0x1000u);
        if(!a->sectors || a->sectors > (a->lba48 ? ATA_LBA48_SECTORS : ATA_LBA28_SECTORS))
            ok = fail(a, KUI_ATA_UNSUPPORTED);
        /* Do not expose writable media whose advertised cache cannot be made
         * durable. Older cache-less CF needs no FLUSH command. */
        if(a->write_cache && !a->flush_supported) ok = fail(a, KUI_ATA_UNSUPPORTED);
    }
    ok = end(a, previous, ok);
    a->ready = ok;
    return ok;
}

static bool transfer(struct kui_ata *a, uint64_t lba, uint32_t count,
                     void *buffer, bool write) {
    if(!a) return false;
    if(!a->ready || !buffer || !count) return fail(a, KUI_ATA_INVALID);
    if(lba >= a->sectors || (uint64_t)count > a->sectors - lba)
        return fail(a, KUI_ATA_RANGE);
#if SIZE_MAX < UINT64_MAX
    if(count > SIZE_MAX / 512u) return fail(a, KUI_ATA_RANGE);
#endif
    uint8_t previous;
    if(!begin(a, &previous)) return false;
    uint8_t *p = buffer;
    bool ok = true;
    while(count && ok) {
        unsigned n = count > 256u ? 256u : count;
        bool ext = lba + n > ATA_LBA28_SECTORS;
        if(ext && !a->lba48) { ok = fail(a, KUI_ATA_RANGE); break; }
        ok = wait_status(a, ATA_DRDY, ATA_BSY | ATA_DRQ, false, true);
        if(!ok) break;
        wr(a, KUI_ATA_DEVICE, ext ? 0xf0 : (uint8_t)(0xf0u | (lba >> 24)));
        settle(a);
        if(ext) {
            wr(a, KUI_ATA_COUNT, (uint8_t)(n >> 8));
            wr(a, KUI_ATA_LBA0, (uint8_t)(lba >> 24));
            wr(a, KUI_ATA_LBA1, (uint8_t)(lba >> 32));
            wr(a, KUI_ATA_LBA2, (uint8_t)(lba >> 40));
        }
        wr(a, KUI_ATA_COUNT, (uint8_t)n);
        wr(a, KUI_ATA_LBA0, (uint8_t)lba);
        wr(a, KUI_ATA_LBA1, (uint8_t)(lba >> 8));
        wr(a, KUI_ATA_LBA2, (uint8_t)(lba >> 16));
        wr(a, KUI_ATA_COMMAND, write ? (ext ? 0x34 : 0x30) : (ext ? 0x24 : 0x20));
        settle(a);
        for(unsigned sector = 0; sector < n && ok; ++sector) {
            ok = wait_status(a, ATA_DRQ, ATA_BSY, true, true);
            if(!ok) break;
            for(unsigned word = 0; word < 256; ++word, p += 2) {
                if(write) a->bus->write16(a->bus->ctx, (uint16_t)(p[0] | ((uint16_t)p[1] << 8)));
                else {
                    uint16_t v = a->bus->read16(a->bus->ctx);
                    p[0] = (uint8_t)v;
                    p[1] = (uint8_t)(v >> 8);
                }
            }
            settle(a);
        }
        if(ok) ok = wait_status(a, 0, ATA_BSY | ATA_DRQ, true, true);
        lba += n;
        count -= n;
    }
    return end(a, previous, ok);
}
bool kui_ata_read(struct kui_ata *a, uint64_t lba, uint32_t n, void *p) {
    return transfer(a, lba, n, p, false);
}
bool kui_ata_write(struct kui_ata *a, uint64_t lba, uint32_t n, const void *p) {
    return transfer(a, lba, n, (void *)p, true);
}
bool kui_ata_sync(struct kui_ata *a) {
    if(!a) return false;
    if(!a->ready) return fail(a, KUI_ATA_INVALID);
    if(!a->flush_supported) { a->error = KUI_ATA_OK; return true; }
    uint8_t previous;
    if(!begin(a, &previous)) return false;
    bool ok = wait_status(a, ATA_DRDY, ATA_BSY | ATA_DRQ, false, true);
    if(ok) {
        wr(a, KUI_ATA_COMMAND, 0xe7); /* FLUSH CACHE also valid for LBA48 */
        settle(a);
        ok = wait_status(a, 0, ATA_BSY | ATA_DRQ, true, true);
    }
    return end(a, previous, ok);
}
void kui_ata_shutdown(struct kui_ata *a) {
    if(a) *a = (struct kui_ata){0};
}
