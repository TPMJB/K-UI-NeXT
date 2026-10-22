/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/ata.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fake {
    uint8_t selected, status, command;
    uint8_t count[2], lba[3][2];
    unsigned count_writes, lba_writes[3], commands, selections, prepares;
    unsigned word, remaining, reads, writes, bulk_reads, bulk_writes;
    uint16_t id[256];
    uint64_t clock;
    bool dma, absent, stuck, error, flush_error, protection_failure, final_error;
    unsigned activations, status_reads;
    unsigned master_busy_reads, busy_reads, command_delay, sector_delay;
    uint8_t after_busy;
    struct { uint8_t command, encoded_count[2]; unsigned count; uint64_t lba; } log[16];
};
static void status_after(struct fake *f, uint8_t status, unsigned delay) {
    /* ERR/DF deliberately coexist with BSY: they are invalid until idle. */
    f->status = delay ? 0xa1 : status;
    f->after_busy = status;
    f->busy_reads = delay;
}
static uint8_t r8(void *p, enum kui_ata_reg r) {
    struct fake *f = p;
    if(r == KUI_ATA_DEVICE) {
        /* A busy master aliases command-block register reads to Status. */
        if(!(f->selected & 0x10) && f->master_busy_reads) return 0x80;
        return f->selected;
    }
    if(r == KUI_ATA_STATUS || r == KUI_ATA_ALTSTATUS) {
        ++f->status_reads;
        if(!(f->selected & 0x10)) {
            if(f->master_busy_reads) { --f->master_busy_reads; return 0x80; }
            return 0x40;
        }
        if(f->absent) return 0xff;
        uint8_t status = f->status;
        if(f->busy_reads && !--f->busy_reads) f->status = f->after_busy;
        return status;
    }
    return 0;
}
static void w8(void *p, enum kui_ata_reg r, uint8_t v) {
    struct fake *f = p;
    if(r == KUI_ATA_DEVICE) {
        /* The transport must never switch away from a live PIO request. */
        assert(!(f->selected & 0x10) || f->absent || !(f->status & 0x88));
        assert((f->selected & 0x10) || !f->master_busy_reads);
        f->selected = v;
        ++f->selections;
    } else if(r == KUI_ATA_COUNT) {
        f->count[f->count_writes++ % 2] = v;
    } else if(r >= KUI_ATA_LBA0 && r <= KUI_ATA_LBA2) {
        unsigned i = (unsigned)(r - KUI_ATA_LBA0);
        f->lba[i][f->lba_writes[i]++ % 2] = v;
    } else if(r == KUI_ATA_COMMAND) {
        assert(f->selected & 0x10); /* no command is ever issued to master */
        f->command = v;
        assert(f->commands < sizeof(f->log) / sizeof(f->log[0]));
        unsigned entry = f->commands;
        ++f->commands;
        f->word = 0;
        bool ext = v == 0x24 || v == 0x34;
        f->remaining = v == 0xec ? 1 : (ext ?
            ((unsigned)f->count[0] << 8) | f->count[1] : f->count[0]);
        if(!f->remaining) f->remaining = ext ? 65536 : 256;
        f->log[entry].command = v;
        f->log[entry].count = f->remaining;
        memcpy(f->log[entry].encoded_count, f->count, sizeof(f->count));
        unsigned low = ext ? 1 : 0;
        f->log[entry].lba = (uint64_t)f->lba[0][low] |
            ((uint64_t)f->lba[1][low] << 8) | ((uint64_t)f->lba[2][low] << 16);
        if(ext) f->log[entry].lba |= (uint64_t)f->lba[0][0] << 24 |
            (uint64_t)f->lba[1][0] << 32 | (uint64_t)f->lba[2][0] << 40;
        else f->log[entry].lba |= (uint64_t)(f->selected & 0x0f) << 24;
        uint8_t status = (f->error || (f->flush_error && v == 0xe7)) ? 0x41 :
                         f->stuck ? 0x80 : v == 0xe7 ? 0x40 : 0x48;
        status_after(f, status, f->command_delay);
    }
}
static void advance(struct fake *f) {
    if(++f->word == 256) {
        f->word = 0;
        if(!--f->remaining) {
            status_after(f, f->final_error ? 0x41 : 0x40, f->sector_delay);
            f->count_writes = 0;
            memset(f->lba_writes, 0, sizeof(f->lba_writes));
        } else status_after(f, 0x48, f->sector_delay);
    }
}
static uint16_t r16(void *p) {
    struct fake *f = p;
    assert(f->status == 0x48);
    uint16_t v = f->command == 0xec ? f->id[f->word] : (uint16_t)(0xa500 | f->word);
    ++f->reads;
    advance(f);
    return v;
}
static void w16(void *p, uint16_t v) {
    struct fake *f = p;
    assert(f->status == 0x48 && v == 0x5a5a);
    ++f->writes;
    advance(f);
}
static bool busy(void *p) { return ((struct fake *)p)->dma; }
static void prepare(void *p) { ++((struct fake *)p)->prepares; }
static uint64_t now(void *p) { return ((struct fake *)p)->clock; }
static void pause_bus(void *p) { ((struct fake *)p)->clock += 1000000; }
static bool activate(void *p) {
    struct fake *f = p;
    /* Unlock must precede every taskfile write, including device selection. */
    assert(f->selections == 0 && f->commands == 0);
    ++f->activations;
    return !f->protection_failure;
}
static struct kui_ata_bus setup(struct fake *f) {
    memset(f, 0, sizeof(*f));
    f->selected = 0xa7;
    f->status = 0x40;
    f->id[0] = 0x848a; /* real CompactFlash signature */
    f->id[49] = 0x0200;
    f->id[60] = 4096;
    f->id[83] = 0x4000;
    return (struct kui_ata_bus){f, r8, w8, r16, w16, busy, prepare, now, pause_bus, activate, NULL, NULL};
}
static void capacity48(struct fake *f) {
    f->id[83] = 0x5400; /* valid command sets, LBA48, FLUSH */
    f->id[103] = 1; /* exactly 2^48 addressable sectors */
}
static void read_sector(void *p, void *data) {
    struct fake *f = p;
    uint8_t *out = data;
    ++f->bulk_reads;
    for(unsigned i = 0; i < 256; ++i) {
        uint16_t v = r16(p);
        *out++ = (uint8_t)v; *out++ = (uint8_t)(v >> 8);
    }
}
static void write_sector(void *p, const void *data) {
    struct fake *f = p;
    const uint8_t *in = data;
    ++f->bulk_writes;
    for(unsigned i = 0; i < 256; ++i, in += 2)
        w16(p, (uint16_t)(in[0] | ((uint16_t)in[1] << 8)));
}
static void test_command_sizes(void) {
    uint8_t guarded[257 * 512 + 2];
    for(unsigned n = 255; n <= 257; ++n) {
        struct fake f;
        struct kui_ata a;
        struct kui_ata_bus b = setup(&f);
        assert(kui_ata_init(&a, &b));
        memset(guarded, 0xcc, sizeof(guarded));
        assert(kui_ata_read(&a, 100, n, guarded + 1));
        assert(f.commands == (n > 256 ? 3u : 2u));
        assert(f.log[1].command == 0x20 && f.log[1].lba == 100);
        assert(f.log[1].count == (n > 256 ? 256 : n));
        assert(f.log[1].encoded_count[0] == (n >= 256 ? 0 : 255));
        if(n > 256) assert(f.log[2].count == 1 && f.log[2].lba == 356);
        assert(f.reads == 256 + n * 256 && guarded[0] == 0xcc && guarded[n * 512 + 1] == 0xcc);
        for(unsigned sector = 0; sector < n; ++sector) {
            assert(guarded[sector * 512 + 1] == 0 && guarded[sector * 512 + 2] == 0xa5);
            assert(guarded[sector * 512 + 511] == 0xff && guarded[sector * 512 + 512] == 0xa5);
        }
        unsigned commands = f.commands;
        memset(guarded + 1, 0x5a, n * 512);
        assert(kui_ata_write(&a, 100, n, guarded + 1));
        assert(f.commands == commands + (n > 256 ? 2u : 1u));
        assert(f.log[commands].command == 0x30 && f.log[commands].count == (n > 256 ? 256 : n));
        assert(f.writes == n * 256 && guarded[0] == 0xcc && guarded[n * 512 + 1] == 0xcc);
        assert(f.selected == 0xa7);
    }
}
static void test_addressing(void) {
    struct fake f;
    struct kui_ata a;
    struct kui_ata_bus b = setup(&f);
    uint8_t data[256 * 512];
    capacity48(&f);
    assert(kui_ata_init(&a, &b));
    assert(kui_ata_read(&a, UINT64_C(0x0ffffffe), 2, data));
    assert(f.command == 0x20 && f.log[1].lba == UINT64_C(0x0ffffffe));
    assert(kui_ata_read(&a, UINT64_C(0x0fffffff), 2, data));
    assert(f.command == 0x24 && f.log[2].lba == UINT64_C(0x0fffffff) && f.log[2].count == 2);
    assert(kui_ata_read(&a, UINT64_C(0x123456789abc), 256, data));
    assert(f.command == 0x24 && f.log[3].lba == UINT64_C(0x123456789abc));
    assert(f.log[3].count == 256 && f.log[3].encoded_count[0] == 1 && f.log[3].encoded_count[1] == 0);
    memset(data, 0x5a, sizeof(data));
    assert(kui_ata_write(&a, UINT64_C(0xffffffffffff), 1, data));
    assert(f.command == 0x34 && f.log[4].lba == UINT64_C(0xffffffffffff));
    unsigned commands = f.commands;
    assert(!kui_ata_read(&a, UINT64_C(0xffffffffffff), 2, data) && a.error == KUI_ATA_RANGE);
    assert(f.commands == commands && f.selected == 0xa7);
}
static void test_delayed_status_and_identify(void) {
    struct fake f;
    struct kui_ata a;
    struct kui_ata_bus b = setup(&f);
    uint8_t data[1024];
    f.master_busy_reads = 2;
    f.command_delay = f.sector_delay = 6;
    assert(kui_ata_init(&a, &b) && f.selected == 0xa7);
    f.master_busy_reads = 2;
    assert(kui_ata_read(&a, 0, 2, data) && f.selected == 0xa7);
    assert(f.clock > 0 && a.error == KUI_ATA_OK);
    f.final_error = true;
    assert(!kui_ata_read(&a, 0, 1, data) && a.error == KUI_ATA_DEVICE_ERROR && f.selected == 0xa7);
    for(unsigned invalid = 0; invalid < 3; ++invalid) {
        b = setup(&f);
        f.id[82] = 0xffff;
        f.id[83] = invalid == 0 ? 0 : invalid == 1 ? 0x8000 : 0xffff;
        assert(kui_ata_init(&a, &b) && !a.write_cache && !a.flush_supported && !a.lba48);
    }
    b = setup(&f); f.id[60] = 0;
    assert(!kui_ata_init(&a, &b) && a.error == KUI_ATA_UNSUPPORTED);
    b = setup(&f); f.id[61] = 0x1000; /* 2^28 plus 4096 */
    assert(!kui_ata_init(&a, &b) && a.error == KUI_ATA_UNSUPPORTED);
    b = setup(&f); capacity48(&f); f.id[100] = 1;
    assert(!kui_ata_init(&a, &b) && a.error == KUI_ATA_UNSUPPORTED);
    b = setup(&f); f.id[49] = 0;
    assert(!kui_ata_init(&a, &b) && a.error == KUI_ATA_UNSUPPORTED);
    b = setup(&f); f.id[106] = 0x5000; f.id[117] = 256;
    assert(kui_ata_init(&a, &b)); /* defined 512-byte logical sector */
    b = setup(&f); f.id[106] = 0xffff; f.id[117] = 2048;
    assert(kui_ata_init(&a, &b)); /* invalid sector-size word is ignored */
}
static void test_stream(void) {
    struct fake f;
    struct kui_ata a;
    struct kui_ata_bus b = setup(&f);
    uint8_t guarded[514];
    memset(guarded, 0xcc, sizeof(guarded));
    assert(kui_ata_init(&a, &b));
    for(unsigned i = 0; i < 8; ++i) {
        assert(kui_ata_read_run(&a, 100 + i, 16 - i, guarded + 1));
        assert(f.commands == 2 && f.reads == 256 + (i + 1) * 256);
        assert(a.read_active == (i < 7));
        assert(f.selected == (i < 7 ? 0xf0 : 0xa7));
        assert(guarded[0] == 0xcc && guarded[513] == 0xcc);
    }
    assert(f.log[1].count == 8 && f.log[1].lba == 100);
    assert(kui_ata_read_run(&a, 108, 8, guarded + 1) && a.read_active);
    unsigned commands = f.commands, reads = f.reads, selections = f.selections;
    assert(!kui_ata_read(&a, 0, 1, guarded + 1) && a.error == KUI_ATA_BUSY);
    assert(!kui_ata_write(&a, 0, 1, guarded + 1) && a.error == KUI_ATA_BUSY);
    assert(!kui_ata_sync(&a) && a.error == KUI_ATA_BUSY);
    kui_ata_shutdown(&a);
    assert(a.ready && a.read_active && a.error == KUI_ATA_BUSY);
    assert(!kui_ata_read_run(&a, 4096, 1, guarded + 1) && a.error == KUI_ATA_RANGE);
    assert(!kui_ata_read_run(&a, 109, 0, guarded + 1) && a.error == KUI_ATA_INVALID);
    assert(!kui_ata_read_run(&a, 109, 7, NULL) && a.error == KUI_ATA_INVALID);
    assert(a.read_active && f.commands == commands && f.reads == reads && f.selections == selections);
    f.dma = true;
    assert(!kui_ata_read_run(&a, 109, 7, guarded + 1) && a.error == KUI_ATA_BUSY);
    assert(!kui_ata_read_stop(&a) && a.error == KUI_ATA_BUSY && a.read_active);
    assert(f.reads == reads && f.selections == selections);
    f.dma = false;
    assert(kui_ata_read_stop(&a) && !a.read_active && f.selected == 0xa7);
    assert(f.reads == reads + 7 * 256 && f.commands == commands);
    assert(kui_ata_read_stop(&a) && f.reads == reads + 7 * 256);
    assert(kui_ata_read_run(&a, 200, 8, guarded + 1));
    reads = f.reads; commands = f.commands;
    assert(kui_ata_read_run(&a, 300, 2, guarded + 1));
    assert(f.reads == reads + 8 * 256 && f.commands == commands + 1);
    assert(f.log[commands].lba == 300 && f.log[commands].count == 2);
    assert(kui_ata_read_stop(&a));
    assert(kui_ata_read_run(&a, 400, 8, guarded + 1));
    commands = f.commands; reads = f.reads;
    assert(kui_ata_read_run(&a, 401, 1, guarded + 1));
    assert(f.commands == commands + 1 && f.reads == reads + 8 * 256);
    assert(!a.read_active && f.log[commands].count == 1 && f.selected == 0xa7);
    assert(kui_ata_read_run(&a, 4095, 1, guarded + 1) && !a.read_active);
    assert(!kui_ata_read_run(&a, 4095, 2, guarded + 1) && a.error == KUI_ATA_RANGE);
}
static void test_stream_failures_and_bulk(void) {
    struct fake f;
    struct kui_ata a;
    struct kui_ata_bus b = setup(&f);
    uint8_t guarded[1026];
    b.read_sector = read_sector; b.write_sector = write_sector;
    capacity48(&f);
    assert(kui_ata_init(&a, &b) && f.bulk_reads == 0);
    memset(guarded, 0xcc, sizeof(guarded));
    assert(kui_ata_read(&a, 0, 2, guarded + 1) && f.bulk_reads == 2);
    assert(guarded[0] == 0xcc && guarded[1025] == 0xcc);
    memset(guarded + 1, 0x5a, 1024);
    assert(kui_ata_write(&a, 0, 2, guarded + 1) && f.bulk_writes == 2);
    assert(kui_ata_read_run(&a, UINT64_C(0x0fffffff), 2, guarded + 1));
    assert(f.command == 0x24 && f.bulk_reads == 3);
    assert(kui_ata_read_stop(&a) && f.bulk_reads == 3); /* discard needs no sector buffer */
    f.error = true;
    assert(!kui_ata_read_run(&a, 100, 8, guarded + 1) && a.error == KUI_ATA_DEVICE_ERROR);
    assert(!a.read_active && a.ready && f.selected == 0xa7);
    b = setup(&f); assert(kui_ata_init(&a, &b));
    assert(kui_ata_read_run(&a, 100, 8, guarded + 1));
    f.status = 0x80;
    unsigned reads = f.reads;
    assert(!kui_ata_read_stop(&a) && a.error == KUI_ATA_TIMEOUT);
    assert(!a.ready && !a.read_active && f.selected == 0xf0 && f.reads == reads);
    b = setup(&f); assert(kui_ata_init(&a, &b));
    f.final_error = true;
    assert(!kui_ata_read_run(&a, 100, 1, guarded + 1) && a.error == KUI_ATA_DEVICE_ERROR);
    assert(!a.read_active && f.selected == 0xa7);
    b = setup(&f); assert(kui_ata_init(&a, &b));
    f.command_delay = f.sector_delay = 6;
    assert(kui_ata_read_run(&a, 100, 8, guarded + 1));
    assert(kui_ata_read_stop(&a) && f.selected == 0xa7);
    b = setup(&f); assert(kui_ata_init(&a, &b));
    assert(kui_ata_read_run(&a, 100, 8, guarded + 1));
    f.status = 0x41; /* a later sector reports an idle device error */
    reads = f.reads;
    assert(!kui_ata_read_run(&a, 101, 7, guarded + 1) && a.error == KUI_ATA_DEVICE_ERROR);
    assert(!a.read_active && a.ready && f.selected == 0xa7 && f.reads == reads);
    b = setup(&f); assert(kui_ata_init(&a, &b));
    assert(kui_ata_read_run(&a, 100, 8, guarded + 1));
    f.status = 0x49; /* even an error must not force a live DRQ off the bus */
    assert(!kui_ata_read_stop(&a) && a.error == KUI_ATA_TIMEOUT);
    assert(!a.ready && !a.read_active && f.selected == 0xf0);
}
static void test_finite_poll_cap(void) {
    for(unsigned clock_present = 0; clock_present < 2; ++clock_present) {
        struct fake f;
        struct kui_ata a;
        struct kui_ata_bus b = setup(&f);
        uint8_t data[512];
        b.pause = NULL;
        if(!clock_present) b.now_us = NULL;
        assert(kui_ata_init(&a, &b));
        f.stuck = true;
        unsigned reads = f.status_reads;
        assert(!kui_ata_read(&a, 0, 1, data) && a.error == KUI_ATA_TIMEOUT);
        assert(!a.ready && f.selected == 0xf0 && f.clock == 0);
        assert(f.status_reads - reads >= 10000000 && f.status_reads - reads < 10000032);
    }
}
int main(void) {
    test_command_sizes();
    test_addressing();
    test_delayed_status_and_identify();
    test_stream();
    test_stream_failures_and_bulk();
    test_finite_poll_cap();
    struct fake f;
    struct kui_ata a;
    struct kui_ata_bus b = setup(&f);
    uint8_t data[1024];
    assert(kui_ata_init(&a, &b));
    assert(f.activations == 1);
    assert(a.ready && a.sectors == 4096 && f.selected == 0xa7);
    assert(kui_ata_read(&a, 4094, 2, data));
    assert(data[0] == 0 && data[1] == 0xa5 && data[510] == 0xff && data[511] == 0xa5);
    assert(data[512] == 0 && data[513] == 0xa5 && f.selected == 0xa7);
    unsigned commands = f.commands;
    assert(!kui_ata_read(&a, UINT64_MAX, 1, data) && a.error == KUI_ATA_RANGE);
    assert(!kui_ata_read(&a, 4095, 2, data) && a.error == KUI_ATA_RANGE);
    assert(f.commands == commands);
    memset(data, 0x5a, sizeof(data));
    assert(kui_ata_write(&a, 0, 2, data) && f.writes == 512 && f.selected == 0xa7);
    commands = f.commands;
    assert(kui_ata_sync(&a) && f.commands == commands); /* cache-less CF */
    f.dma = true;
    unsigned selections = f.selections;
    assert(!kui_ata_read(&a, 0, 1, data) && a.error == KUI_ATA_BUSY);
    assert(f.selections == selections && f.commands == commands);
    f.dma = false;
    f.error = true;
    assert(!kui_ata_read(&a, 0, 1, data) && a.error == KUI_ATA_DEVICE_ERROR);
    assert(f.selected == 0xa7);

    b = setup(&f);
    f.absent = true;
    assert(!kui_ata_init(&a, &b) && a.error == KUI_ATA_ABSENT && f.selected == 0xa7);
    assert(f.commands == 0);
    b = setup(&f);
    f.protection_failure = true;
    assert(!kui_ata_init(&a, &b) && a.error == KUI_ATA_PROTECTION);
    assert(f.activations == 1 && f.commands == 0 && f.selections == 0);
    b = setup(&f);
    f.dma = true;
    assert(!kui_ata_init(&a, &b) && a.error == KUI_ATA_BUSY);
    assert(f.activations == 0 && f.selections == 0);
    b = setup(&f);
    f.id[106] = 0x5000;
    f.id[117] = 2048; /* unsupported 4096-byte logical sectors */
    assert(!kui_ata_init(&a, &b) && a.error == KUI_ATA_UNSUPPORTED && f.selected == 0xa7);
    b = setup(&f);
    f.id[82] = 0x20;
    assert(!kui_ata_init(&a, &b) && a.error == KUI_ATA_UNSUPPORTED);
    b = setup(&f);
    f.id[82] = 0x20;
    f.id[83] = 0x5400; /* valid + LBA48 + FLUSH */
    f.id[102] = 1; /* 2 TiB in sectors */
    assert(kui_ata_init(&a, &b) && a.lba48 && a.sectors == UINT64_C(0x100000000));
    assert(kui_ata_read(&a, UINT64_C(0x12345678), 1, data));
    assert(f.command == 0x24 && f.count[0] == 0 && f.count[1] == 1);
    assert(f.lba[0][0] == 0x12 && f.lba[1][0] == 0 && f.lba[2][0] == 0);
    assert(f.lba[0][1] == 0x78 && f.lba[1][1] == 0x56 && f.lba[2][1] == 0x34);
    assert(kui_ata_sync(&a) && f.command == 0xe7 && f.selected == 0xa7);
    f.flush_error = true;
    assert(!kui_ata_sync(&a) && a.error == KUI_ATA_DEVICE_ERROR && f.selected == 0xa7);
    f.flush_error = false;
    f.stuck = true;
    assert(!kui_ata_read(&a, 0, 1, data) && a.error == KUI_ATA_TIMEOUT && !a.ready);
    assert((f.selected & 0x10) && f.clock == 5000000); /* no illegal forced reset/deselect */
    kui_ata_shutdown(&a);
    assert(!a.ready && a.sectors == 0 && a.bus == NULL);
    puts("ATA: PIO chunks, bounded streams, IDENTIFY, CF, LBA48, bulk copies, DMA exclusion and failures passed");
    return 0;
}
