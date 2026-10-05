/* SPDX-License-Identifier: MIT */
/* The transaction used by wifi.c, with driver calls observed/injected.
 * Failed driver changes must not publish or save a requested band, and
 * reconnect must run after every interrupted association. */
#include "wifi_band_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while(0)

struct mock {
    char calls[8];
    unsigned count;
    bool disconnect_ok, apply_ok, wanted, was_wanted, address, paused, changed;
    uint8_t actual, published, persisted;
};
static void called(struct mock *m, char c) { CHECK(m->count + 1u < sizeof m->calls); m->calls[m->count++] = c; }
static void pause_wifi(void *ctx) {
    struct mock *m = ctx;
    called(m, 'p');
    m->was_wanted = m->wanted;
    m->wanted = false;
    m->address = false;
    m->paused = true;
}
static bool disconnect_wifi(void *ctx) {
    struct mock *m = ctx;
    called(m, 'd');
    CHECK(m->paused && !m->wanted && !m->address);
    return m->disconnect_ok;
}
static bool apply(void *ctx, uint8_t band) {
    struct mock *m = ctx;
    called(m, 'a');
    CHECK(m->paused && !m->wanted);
    if(m->apply_ok) m->actual = band;
    return m->apply_ok;
}
static void commit(void *ctx, uint8_t band) {
    struct mock *m = ctx;
    called(m, 'c');
    CHECK(m->actual == band && m->paused);
    m->published = m->persisted = band;
}
static void resume(void *ctx, bool changed) {
    struct mock *m = ctx;
    called(m, 'r');
    m->changed = changed;
    m->paused = false;
    m->wanted = m->was_wanted;
}
static struct mock initial(void) {
    struct mock m = {0};
    m.disconnect_ok = m.apply_ok = m.wanted = m.address = true;
    m.actual = m.published = m.persisted = KWM_BAND_24;
    return m;
}
static bool change(struct mock *m, uint8_t band, bool dual) {
    const struct kwifi_band_ops ops = {m, pause_wifi, disconnect_wifi, apply, commit, resume};
    return kwifi_band_change(m->published, band, dual, &ops);
}
int main(void) {
    struct mock m = initial();
    CHECK(change(&m, KWM_BAND_5, true));
    CHECK(!strcmp(m.calls, "pdacr") && m.changed && m.wanted && !m.address && !m.paused);
    CHECK(m.actual == KWM_BAND_5 && m.published == KWM_BAND_5 && m.persisted == KWM_BAND_5);

    m = initial();
    m.apply_ok = false;
    CHECK(!change(&m, KWM_BAND_5, true));
    CHECK(!strcmp(m.calls, "pdar") && !m.changed && m.wanted && !m.paused);
    CHECK(m.actual == KWM_BAND_24 && m.published == KWM_BAND_24 && m.persisted == KWM_BAND_24);

    m = initial();
    m.disconnect_ok = false;
    CHECK(!change(&m, KWM_BAND_5, true));
    CHECK(!strcmp(m.calls, "pdr") && m.wanted && !m.changed);
    CHECK(m.published == KWM_BAND_24 && m.persisted == KWM_BAND_24);

    m = initial();
    CHECK(!change(&m, KWM_BAND_5, false) && !m.count && m.address && m.wanted);
    CHECK(!change(&m, 99, true) && !m.count);
    CHECK(change(&m, KWM_BAND_24, true) && !m.count && m.address);
    CHECK(change(&m, KWM_BAND_BOTH, false) && !m.count && m.published == KWM_BAND_24);

    m = initial();
    m.wanted = false;
    CHECK(change(&m, KWM_BAND_BOTH, true) && !m.wanted && m.changed);
    CHECK(m.published == KWM_BAND_BOTH && m.persisted == KWM_BAND_BOTH);
    puts("Wi-Fi bands: disconnect/reconnect ordering, rejected driver changes, saved-mode truth and C6 rejection passed");
    return 0;
}
