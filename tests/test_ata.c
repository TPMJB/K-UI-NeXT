/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/ata.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fake {
    uint8_t selected, status, command;
    uint8_t count[2], lba[3][2];
    unsigned count_writes, lba_writes[3], commands, selections, prepares;
    unsigned word, remaining, reads, writes;
    uint16_t id[256];
    uint64_t clock;
    bool dma, absent, stuck, error, flush_error, protection_failure;
    unsigned activations;
};
static uint8_t r8(void *p, enum kui_ata_reg r) {
    struct fake *f = p;
    if(r == KUI_ATA_DEVICE) return f->selected;
    if(r == KUI_ATA_STATUS || r == KUI_ATA_ALTSTATUS) {
        if(!(f->selected & 0x10)) return 0x40;
        if(f->absent) return 0xff;
        return f->status;
    }
    return 0;
}
static void w8(void *p, enum kui_ata_reg r, uint8_t v) {
    struct fake *f = p;
    if(r == KUI_ATA_DEVICE) {
        /* The transport must never switch away from a live PIO request. */
        assert(!(f->selected & 0x10) || f->absent || !(f->status & 0x88));
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
        ++f->commands;
        f->word = 0;
        f->remaining = v == 0xec ? 1 : (f->count_writes == 2 ?
            ((unsigned)f->count[0] << 8) | f->count[1] : f->count[0]);
        if(!f->remaining) f->remaining = 256;
        f->status = (f->error || (f->flush_error && v == 0xe7)) ? 0x41 :
                    f->stuck ? 0x80 : v == 0xe7 ? 0x40 : 0x48;
    }
}
static void advance(struct fake *f) {
    if(++f->word == 256) {
        f->word = 0;
        if(!--f->remaining) {
            f->status = 0x40;
            f->count_writes = 0;
            memset(f->lba_writes, 0, sizeof(f->lba_writes));
        }
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
    return (struct kui_ata_bus){f, r8, w8, r16, w16, busy, prepare, now, pause_bus, activate};
}
int main(void) {
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
    puts("ATA: PIO, IDENTIFY, CF, LBA48, range, DMA exclusion and bounded failures passed");
    return 0;
}
