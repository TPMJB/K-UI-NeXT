/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_metadata.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Entirely synthetic sectors; no retail IP.BIN or executable bytes. The sparse
 * callback represents an image whose data starts at absolute LBA 45000. */
#define SECTORS 160u
#define ROOT 20u
#define BOOT 21u
static struct fixture {
    uint8_t sectors[SECTORS][2048];
    uint32_t base, end, read_lba[160], reads, range_calls;
    uint32_t fail_read, cancel_read, denied_lba;
} fixture;

static void dual16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    p[2] = p[1]; p[3] = p[0];
}
static void dual32(uint8_t *p, uint32_t n) {
    for(unsigned i = 0; i < 4; ++i) p[i] = p[7 - i] = (uint8_t)(n >> (8 * i));
}
static unsigned entry(uint8_t *p, uint32_t lba, uint32_t bytes,
                      uint8_t flags, const uint8_t *name, unsigned name_length) {
    unsigned size = 33 + name_length + ((name_length & 1) ? 0 : 1);
    memset(p, 0, size);
    p[0] = (uint8_t)size;
    dual32(p + 2, lba); dual32(p + 10, bytes);
    p[25] = flags; dual16(p + 28, 1);
    p[32] = (uint8_t)name_length;
    memcpy(p + 33, name, name_length);
    return size;
}
static void setup(uint32_t base) {
    memset(&fixture, 0, sizeof(fixture));
    fixture.base = base; fixture.end = base + SECTORS;
    fixture.fail_read = fixture.cancel_read = fixture.denied_lba = UINT32_MAX;
    uint8_t *ip = fixture.sectors[0], *pvd = fixture.sectors[16];
    memset(ip, ' ', 256);
    memcpy(ip, "SEGA SEGAKATANA ", 16);
    memcpy(ip + 48, "JUE", 3);
    memcpy(ip + 64, "T-12345", 7);
    memcpy(ip + 74, "V1.000", 6);
    memcpy(ip + 96, "1ST_READ.BIN", 12);
    memcpy(ip + 128, " K-UI SYNTHETIC TEST", 20);
    pvd[0] = 1; memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    dual32(pvd + 80, fixture.end);
    dual16(pvd + 120, 1); dual16(pvd + 124, 1); dual16(pvd + 128, 2048);
    pvd[881] = 1;
    const uint8_t dot = 0, parent = 1;
    entry(pvd + 156, base + ROOT, 2048, 2, &dot, 1);
    uint8_t *dir = fixture.sectors[ROOT];
    unsigned pos = entry(dir, base + ROOT, 2048, 2, &dot, 1);
    pos += entry(dir + pos, base + ROOT, 2048, 2, &parent, 1);
    entry(dir + pos, base + BOOT, 3000, 0, (const uint8_t *)"1ST_READ.BIN;1", 14);
}
static enum kui_game_metadata_io_result read_sector(void *ctx, uint32_t lba,
                                                    uint8_t data[2048]) {
    struct fixture *f = ctx;
    assert(f->reads < 160);
    f->read_lba[f->reads] = lba;
    if(f->reads++ == f->cancel_read) return KUI_GAME_METADATA_IO_CANCELLED;
    if(f->reads - 1 == f->fail_read || lba < f->base || lba >= f->end)
        return KUI_GAME_METADATA_IO_ERROR;
    memcpy(data, f->sectors[lba - f->base], 2048);
    return KUI_GAME_METADATA_IO_OK;
}
static bool range(void *ctx, uint32_t lba, uint32_t count) {
    struct fixture *f = ctx;
    ++f->range_calls;
    assert(count);
    return lba >= f->base && (uint64_t)lba + count <= f->end &&
        !(lba <= f->denied_lba && (uint64_t)lba + count > f->denied_lba);
}
static const struct kui_game_metadata_ops ops = {&fixture, read_sector, range};
static struct kui_game_metadata meta;
static unsigned cases;
static void expect(enum kui_game_metadata_status want) {
    memset(&meta, 0xa5, sizeof(meta));
    assert(kui_game_metadata_read(&ops, fixture.base, &meta) == want);
    assert(meta.boot_valid == (want == KUI_GAME_METADATA_OK));
    assert(fixture.reads <= 145);
    ++cases;
}
static uint8_t *pvd(void) {return fixture.sectors[16];}
static uint8_t *root(void) {return pvd() + 156;}
static uint8_t *boot(void) {return fixture.sectors[ROOT] + 68;}

int main(void) {
    setup(45000); expect(KUI_GAME_METADATA_OK);
    assert(meta.ip_valid && meta.boot_valid);
    assert(!strcmp(meta.title, "K-UI SYNTHETIC TEST"));
    assert(!strcmp(meta.product, "T-12345"));
    assert(!strcmp(meta.version, "V1.000"));
    assert(!strcmp(meta.region, "JUE"));
    assert(!strcmp(meta.bootfile, "1ST_READ.BIN"));
    assert(meta.root_lba == 45020 && meta.boot_lba == 45021 && meta.boot_bytes == 3000);
    assert(meta.sectors_read == 3 && fixture.reads == 3 && fixture.range_calls == 2);
    assert(fixture.read_lba[0] == 45000 && fixture.read_lba[1] == 45016 &&
           fixture.read_lba[2] == 45020); /* never add session to ISO extents */
    setup(0); expect(KUI_GAME_METADATA_OK);
    assert(meta.boot_lba == 21);
    setup(45000); dual32(pvd() + 80, SECTORS); expect(KUI_GAME_METADATA_OK);
    assert(meta.volume_blocks == SECTORS && meta.boot_lba == 45021);

    setup(45000); memset(fixture.sectors[0], 0, 16);
    expect(KUI_GAME_METADATA_IP_HEADER); assert(!meta.ip_valid && fixture.reads == 1);
    setup(45000); fixture.sectors[0][15] = 'X'; expect(KUI_GAME_METADATA_IP_HEADER);
    setup(45000); memset(fixture.sectors[0] + 128, 'A', 128);
    fixture.sectors[0][129] = 0xff; expect(KUI_GAME_METADATA_OK);
    assert(strlen(meta.title) == 128 && meta.title[1] == '?');
    setup(45000); memcpy(fixture.sectors[0] + 96, "../BAD.BIN       ", 16);
    expect(KUI_GAME_METADATA_UNSUPPORTED); assert(meta.ip_valid && fixture.reads == 1);
    setup(45000); memset(fixture.sectors[0] + 96, ' ', 16);
    expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); memcpy(fixture.sectors[0] + 96, "ABCDEFGHIJKLMNOP", 16);
    entry(boot(), 45021, 3000, 0, (const uint8_t *)"ABCDEFGHIJKLMNOP;1", 18);
    expect(KUI_GAME_METADATA_OK); assert(strlen(meta.bootfile) == 16);
    setup(45000); memcpy(boot() + 33, "1st_read.bin;1", 14); expect(KUI_GAME_METADATA_OK);

    setup(45000); fixture.fail_read = 0; expect(KUI_GAME_METADATA_IO);
    assert(!meta.ip_valid && meta.sectors_read == 0);
    setup(45000); fixture.cancel_read = 0; expect(KUI_GAME_METADATA_CANCELLED);
    setup(45000); fixture.fail_read = 1; expect(KUI_GAME_METADATA_IO); assert(meta.ip_valid);
    setup(45000); fixture.fail_read = 2; expect(KUI_GAME_METADATA_IO); assert(meta.ip_valid);
    setup(45000); fixture.cancel_read = 2; expect(KUI_GAME_METADATA_CANCELLED);
    assert(meta.ip_valid && meta.sectors_read == 2);

    setup(45000); pvd()[1] = 'x'; expect(KUI_GAME_METADATA_ISO);
    setup(45000); pvd()[6] = 2; expect(KUI_GAME_METADATA_ISO);
    setup(45000); pvd()[7] = 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); pvd()[881] = 2; expect(KUI_GAME_METADATA_ISO);
    setup(45000); pvd()[84] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); dual32(pvd() + 80, 0); expect(KUI_GAME_METADATA_ISO);
    setup(45000); dual32(pvd() + 80, 16); expect(KUI_GAME_METADATA_ISO);
    setup(45000); pvd()[122] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); pvd()[126] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); pvd()[130] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); dual16(pvd() + 128, 512); expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); dual16(pvd() + 120, 2); expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); dual16(pvd() + 124, 2); expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); pvd()[0] = 255; expect(KUI_GAME_METADATA_ISO);
    setup(45000); pvd()[0] = 4; expect(KUI_GAME_METADATA_ISO);
    setup(45000); memcpy(fixture.sectors[17], pvd(), 2048); pvd()[0] = 0;
    expect(KUI_GAME_METADATA_OK); assert(meta.sectors_read == 4);
    setup(45000); memcpy(fixture.sectors[17], pvd(), 2048); pvd()[0] = 0;
    dual32(fixture.sectors[17] + 80, 17); expect(KUI_GAME_METADATA_ISO);
    setup(45000);
    for(unsigned i = 16; i < 32; ++i) {
        fixture.sectors[i][0] = 2;
        memcpy(fixture.sectors[i] + 1, "CD001", 5); fixture.sectors[i][6] = 1;
    }
    expect(KUI_GAME_METADATA_LIMIT); assert(fixture.reads == 17);

    setup(45000); root()[0] = 33; expect(KUI_GAME_METADATA_ISO);
    setup(45000); root()[25] = 0; expect(KUI_GAME_METADATA_ISO);
    setup(45000); root()[33] = 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); root()[6] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); root()[14] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); root()[30] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); root()[1] = 1; expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); root()[26] = 1; expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); dual32(root() + 10, 0); expect(KUI_GAME_METADATA_ISO);
    setup(45000); dual32(root() + 10, KUI_GAME_METADATA_MAX_DIRECTORY_BYTES + 1);
    expect(KUI_GAME_METADATA_LIMIT); assert(fixture.reads == 2);
    setup(45000); dual32(root() + 2, UINT32_MAX); expect(KUI_GAME_METADATA_ISO);
    setup(45000); dual32(root() + 2, 20); expect(KUI_GAME_METADATA_ISO); /* no relative fallback */
    setup(45000); fixture.denied_lba = 45020; expect(KUI_GAME_METADATA_ISO);

    setup(45000); boot()[33] = 'X'; expect(KUI_GAME_METADATA_BOOT_NOT_FOUND);
    setup(45000); boot()[46] = '2'; expect(KUI_GAME_METADATA_BOOT_NOT_FOUND);
    setup(45000); boot()[0] = 32; expect(KUI_GAME_METADATA_ISO);
    setup(45000); boot()[0] = 255; expect(KUI_GAME_METADATA_ISO);
    setup(45000); boot()[32] = 200; expect(KUI_GAME_METADATA_ISO);
    setup(45000); boot()[6] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); boot()[14] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); boot()[30] ^= 1; expect(KUI_GAME_METADATA_ISO);
    setup(45000); boot()[25] = 2; expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); boot()[25] = 128; expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); boot()[1] = 1; expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); boot()[26] = 1; expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); boot()[27] = 1; expect(KUI_GAME_METADATA_UNSUPPORTED);
    setup(45000); dual32(boot() + 10, 0); expect(KUI_GAME_METADATA_ISO);
    setup(45000); dual32(boot() + 10, KUI_GAME_METADATA_MAX_BOOT_BYTES + 1);
    expect(KUI_GAME_METADATA_LIMIT);
    setup(45000); fixture.end = 45021 + KUI_GAME_METADATA_MAX_BOOT_BYTES / 2048;
    dual32(pvd() + 80, fixture.end - fixture.base);
    dual32(boot() + 10, KUI_GAME_METADATA_MAX_BOOT_BYTES);
    expect(KUI_GAME_METADATA_OK); assert(fixture.reads == 3);
    setup(45000); dual32(boot() + 2, UINT32_MAX - 1); expect(KUI_GAME_METADATA_ISO);
    setup(45000); fixture.end = 45022; expect(KUI_GAME_METADATA_ISO); /* short file */
    setup(45000); fixture.denied_lba = 45022; expect(KUI_GAME_METADATA_ISO); /* gap/audio */
    setup(45000); dual32(pvd() + 80, 20);
    dual32(boot() + 10, 21 * 2048); expect(KUI_GAME_METADATA_ISO);
    setup(45000); fixture.sectors[ROOT][68 + 48 + 1] = 7;
    expect(KUI_GAME_METADATA_ISO); /* nonzero sector padding */
    setup(45000); memcpy(boot() + boot()[0], boot(), boot()[0]); expect(KUI_GAME_METADATA_ISO);

    setup(45000);
    memcpy(fixture.sectors[ROOT + 1], boot(), boot()[0]);
    memset(boot(), 0, 2048 - 68);
    dual32(root() + 10, 4096);
    dual32(fixture.sectors[ROOT + 1] + 2, 45022);
    expect(KUI_GAME_METADATA_OK);
    assert(meta.boot_lba == 45022 && fixture.reads == 4);
    setup(45000); dual32(root() + 10, 4096); fixture.cancel_read = 3;
    expect(KUI_GAME_METADATA_CANCELLED); /* found file cannot publish before cancellation */
    assert(meta.boot_lba == 0);
    setup(45000); dual32(root() + 10, 2048 - 1);
    expect(KUI_GAME_METADATA_OK); /* directory's last block may be partial */
    setup(45000); dual32(root() + 10, 70); expect(KUI_GAME_METADATA_ISO);
    setup(45000); dual32(root() + 10, KUI_GAME_METADATA_MAX_DIRECTORY_BYTES);
    expect(KUI_GAME_METADATA_OK); assert(fixture.reads == 130);

    assert(kui_game_metadata_read(NULL, 45000, &meta) == KUI_GAME_METADATA_ARGUMENT);
    assert(!meta.ip_valid && !meta.boot_valid);
    assert(kui_game_metadata_read(&ops, UINT32_MAX, &meta) == KUI_GAME_METADATA_ARGUMENT);
    assert(kui_game_metadata_read(&ops, 45000, NULL) == KUI_GAME_METADATA_ARGUMENT);
    for(int result = KUI_GAME_METADATA_OK; result <= KUI_GAME_METADATA_UNSUPPORTED; ++result)
        assert(kui_game_metadata_status_text((enum kui_game_metadata_status)result)[0]);
    printf("Games IP.BIN and ISO9660 metadata tests passed (%u scenarios plus argument/status checks)\n", cases);
    return 0;
}
