/* SPDX-License-Identifier: GPL-3.0-only */
/* The Wi-Fi board on the SCI port: finding it and checking the link at each
 * clock rate, waiting for Wi-Fi, its socket slots for the FTP server, the
 * Network app's inspection and test for it, and the Wi-Fi page's jobs. */
#include "kui/network_wifi.h"
#include "kui/clock.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HELLO_TRANSFERS 12u
#define ECHO_ROUNDS 4u
#define ECHO_BYTES 2048u
#define ECHO_TRANSFERS 16u
#define ONLINE_MS 20000u
#define MISSING_MS 10000u
#define ANSWER_MS 3000u

static uint64_t now(const struct kui_wifi_session *s) { return s->port->bus->now_ms(s->port->bus->ctx); }
static void pause_ms(const struct kui_wifi_session *s, unsigned ms) { s->port->bus->pause(s->port->bus->ctx, ms); }
static bool stopped(kui_cancel_fn cancel) { return cancel && cancel(); }
static const char *speed(const struct kui_wifi_session *s, unsigned level) {
    return s->port->speed ? s->port->speed(level) : "?";
}
static void problem(struct kui_wifi_session *s, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(s->problem, sizeof(s->problem), format, args);
    va_end(args);
}
static bool addressed(const struct kwh_wifi *w) { return w->ip[0] | w->ip[1] | w->ip[2] | w->ip[3]; }

/* One line for the state, as the FTP server and the pages show it. */
static void describe(char *out, size_t cap, const struct kwh_wifi *w) {
    switch(w->state) {
    case KWM_WIFI_IDLE: snprintf(out, cap, w->saved ? "Wi-Fi: off" : "Wi-Fi: no network set up"); break;
    case KWM_WIFI_CONNECTING: snprintf(out, cap, "Wi-Fi: connecting to %.32s", w->ssid); break;
    case KWM_WIFI_ASSOCIATED: snprintf(out, cap, "Wi-Fi: joined %.32s; waiting for an address", w->ssid); break;
    case KWM_WIFI_ONLINE: snprintf(out, cap, "Wi-Fi: online on %.32s", w->ssid); break;
    case KWM_WIFI_BAD_PASSWORD: snprintf(out, cap, "Wi-Fi: %.32s refused the password", w->ssid); break;
    case KWM_WIFI_NOT_FOUND: snprintf(out, cap, "Wi-Fi: %.32s not found; retrying", w->ssid); break;
    default: snprintf(out, cap, "Wi-Fi: connection to %.32s lost; retrying", w->ssid); break;
    }
}

/* The link's bus: the port's transfer, counting READY changes. */
static bool link_transfer(void *ctx, const uint8_t *out, uint8_t *in, size_t len) {
    struct kui_wifi_session *s = ctx;
    bool ready = false;
    if(!s->port->bus->transfer(s->port->bus->ctx, out, in, len, &ready)) return false;
    if(ready) ++s->ready_changes;
    else ++s->ready_timeouts;
    return true;
}
/* A new session number each start, so a board still holding an old one
 * cannot be mistaken for ours. */
static uint16_t session_number(const struct kui_wifi_session *s) {
    static uint16_t counter;
    uint16_t n = (uint16_t)(now(s) * 40503u + ++counter * 0x9e37u);
    return n ? n : 1u;
}

/* Every transfer, including discovery and echo checks, supplies checked
 * link feedback to platforms that adapt a missing READY wire's pacing. */
static void host_step(struct kui_wifi_session *s) {
    struct kwh *h = &s->host;
    uint32_t heard = h->heard, lost = h->counts.lost, received = h->link.stats.received;
    uint32_t duplicates = h->link.stats.duplicates, gaps = h->link.stats.gaps;
    unsigned head = h->link.out_head, count = h->link.out_count;
    bool active = !kwh_idle(h);
    (void)kwh_step(h);
    if(!s->port->bus->feedback) return;
    unsigned flags = 0;
    if(h->heard != heard && h->link.live) flags |= KUI_WIFI_PACE_VALID;
    if(h->link.stats.received != received || h->link.out_head != head || h->link.out_count < count)
        flags |= KUI_WIFI_PACE_PROGRESS;
    if(active || !kwh_idle(h)) flags |= KUI_WIFI_PACE_ACTIVE;
    if(h->link.stats.duplicates != duplicates || h->link.stats.gaps != gaps) flags |= KUI_WIFI_PACE_DUPLICATE;
    if(h->counts.lost != lost) flags |= KUI_WIFI_PACE_RESET;
    s->port->bus->feedback(s->port->bus->ctx, flags);
}

bool kui_wifi_session_step(struct kui_wifi_session *s) {
    if(!s || !s->open) return false;
    host_step(s);
    uint64_t t = now(s);
    if(s->host.heard != s->heard) {
        s->heard = s->host.heard;
        s->heard_ms = t;
        return true;
    }
    if(t - s->heard_ms <= KUI_WIFI_SILENT_MS) return true;
    problem(s, "The Wi-Fi board stopped answering");
    return false;
}
bool kui_wifi_session_busy(const struct kui_wifi_session *s) { return s && s->open && !kwh_idle(&s->host); }
/* Steps until *counter moves past `before`. */
static bool wait_for(struct kui_wifi_session *s, const uint32_t *counter, uint32_t before, unsigned limit_ms,
                     kui_cancel_fn cancel) {
    uint64_t start = now(s);
    while(*counter == before) {
        if(stopped(cancel)) { problem(s, "Stopped"); return false; }
        if(!kui_wifi_session_step(s)) return false;
        if(*counter != before) break;
        if(now(s) - start > limit_ms) { problem(s, "The Wi-Fi board did not answer in time"); return false; }
        if(!kui_wifi_session_busy(s)) pause_ms(s, 2);
    }
    return true;
}

/* ---- Finding the board ---- */
struct trial {
    bool hello, echoed, sign;
    uint32_t bad;
};
/* At the rate just opened: a new session and HELLO, then large frames
 * echoed both ways. sign: anything at all suggests a board is there. */
static struct trial attempt(struct kui_wifi_session *s) {
    struct trial t = {false, false, false, 0};
    uint32_t changes = s->ready_changes;
    kwh_init(&s->host, &s->link);
    kwh_start(&s->host, session_number(s));
    s->heard = 0;
    s->heard_ms = now(s);
    for(unsigned i = 0; i < HELLO_TRANSFERS && !s->host.ready; ++i) host_step(s);
    t.hello = s->host.ready && s->host.hello.protocol == KWL_PROTOCOL;
    for(unsigned round = 0; t.hello && round < ECHO_ROUNDS; ++round) {
        uint8_t sample[ECHO_BYTES];
        for(unsigned i = 0; i < ECHO_BYTES; ++i) sample[i] = (uint8_t)(i * 7u + round * 61u + (i >> 8));
        uint32_t before = s->host.counts.echo;
        if(!kwh_echo(&s->host, sample, sizeof(sample))) break;
        for(unsigned i = 0; i < ECHO_TRANSFERS && s->host.counts.echo == before; ++i) host_step(s);
        if(s->host.counts.echo == before || s->host.echo_len != sizeof(sample) || memcmp(s->host.echo, sample, sizeof(sample)))
            break;
        t.echoed = round + 1u == ECHO_ROUNDS;
    }
    t.bad = s->host.link.stats.bad;
    if(t.hello && t.echoed && !t.bad && s->port->bus->feedback)
        s->port->bus->feedback(s->port->bus->ctx, KUI_WIFI_PACE_TRAIN);
    t.sign = s->host.ready || s->host.heard || t.bad || s->host.link.stats.truncated || s->ready_changes != changes;
    s->heard = s->host.heard;
    s->heard_ms = now(s);
    return t;
}
bool kui_wifi_session_find(struct kui_wifi_session *s, const struct kui_wifi_port *port, kui_log_fn log) {
    if(!s) return false;
    memset(s, 0, sizeof(*s));
    s->port = port;
    s->link = (struct kwh_bus){s, link_transfer};
    if(!port || !port->bus || !port->bus->transfer || !port->bus->now_ms || !port->bus->pause || !port->open ||
       !port->close || !port->levels) {
        problem(s, "No SCI port is available for the Wi-Fi board");
        return false;
    }
    unsigned speeds = port->speeds && port->speeds <= port->levels ? port->speeds : port->levels;
    int best = -1;
    uint32_t best_bad = UINT32_MAX;
    for(unsigned level = 0; level < port->levels; ++level) {
        if(!port->open(level)) { problem(s, "The SCI port could not be started"); return false; }
        s->open = true;
        s->ready_changes = s->ready_timeouts = 0;
        struct trial t = attempt(s);
        s->answered = s->answered || t.sign;
        if(s->host.ready && s->host.hello.protocol != KWL_PROTOCOL) {
            problem(s, "The Wi-Fi board's firmware speaks protocol %u; this K-UI needs %u: update it",
                s->host.hello.protocol, KWL_PROTOCOL);
            if(log) log("Wi-Fi board on SCI at %s: %s", speed(s, level), s->problem);
            port->close();
            s->open = false;
            return false;
        }
        if(log) {
            if(t.hello && t.echoed) log("Wi-Fi board on SCI at %s: firmware %s; link check: %u bad frames",
                speed(s, level), s->host.hello.version, (unsigned)t.bad);
            else if(t.hello) log("Wi-Fi board on SCI at %s: answered, but large frames did not cross intact (%u bad)",
                speed(s, level), (unsigned)t.bad);
            else if(t.sign) log("Wi-Fi board on SCI at %s: signs of a board but no answer (%u bad frames, READY %s)",
                speed(s, level), (unsigned)t.bad, s->ready_changes ? "changing" : "still");
            else log("Wi-Fi board on SCI at %s: no answer", speed(s, level));
        }
        if(t.hello && t.echoed && !t.bad) {
            s->level = level;
            s->found = true;
            if(log && port->bus->gap_us) {
                unsigned gap = port->bus->gap_us(port->bus->ctx);
                if(gap) log("Wi-Fi SCI pacing: READY absent; transfer gap %u ms (adaptive)", gap / 1000u);
                else log("Wi-Fi SCI pacing: READY handshake working; 20 ms timeout");
            }
            return true;
        }
        port->close();
        s->open = false;
        if(t.hello && t.echoed && t.bad < best_bad) {
            best = (int)level;
            best_bad = t.bad;
        }
        /* Nothing at all at the fastest rate: the board is not on this
         * chip select. Seen, but no clean rate: it is on this one. */
        if(level % speeds == 0 && !t.sign) level += speeds - 1u;
        else if(level % speeds == speeds - 1u && s->answered) break;
    }
    if(best >= 0 && port->open((unsigned)best)) {
        s->open = true;
        struct trial t = attempt(s);
        if(t.hello) {
            s->level = (unsigned)best;
            s->found = true;
            s->bad = t.bad;
            if(log) log("Wi-Fi board: using %s despite %u bad frames", speed(s, s->level), (unsigned)t.bad);
            return true;
        }
        port->close();
        s->open = false;
    }
    if(s->answered) problem(s, "The Wi-Fi board answered, but not reliably at any speed: check the SCI wires");
    else problem(s, "No Wi-Fi board answered on the SCI port");
    return false;
}

/* ---- Wi-Fi ---- */
bool kui_wifi_session_status(struct kui_wifi_session *s, kui_cancel_fn cancel) {
    if(!s || !s->found) return false;
    uint32_t before = s->host.counts.wifi;
    if(!kwh_wifi_query(&s->host)) { problem(s, "The link to the Wi-Fi board is full"); return false; }
    return wait_for(s, &s->host.counts.wifi, before, ANSWER_MS, cancel);
}
bool kui_wifi_session_online(struct kui_wifi_session *s, unsigned limit_ms, kui_cancel_fn cancel,
                             void (*stage)(void *ctx, const char *text), void *ctx) {
    if(!s || !s->found) return false;
    uint64_t start = now(s), asked = 0;
    uint32_t seen = s->host.counts.wifi;
    bool have = false;
    uint8_t shown = 0xff;
    for(;;) {
        if(stopped(cancel)) { problem(s, "Stopped"); return false; }
        uint64_t t = now(s);
        if(!asked || t - asked >= 1000u) {
            (void)kwh_wifi_query(&s->host);
            asked = t ? t : 1u;
        }
        if(!kui_wifi_session_step(s)) return false;
        const struct kwh_wifi *w = &s->host.wifi;
        if(s->host.counts.wifi != seen) {
            seen = s->host.counts.wifi;
            have = true;
            if(w->state != shown) {
                shown = w->state;
                char text[96];
                describe(text, sizeof(text), w);
                if(stage) stage(ctx, text);
            }
            if(w->state == KWM_WIFI_ONLINE && addressed(w)) return true;
            if(w->state == KWM_WIFI_IDLE) {
                problem(s, w->saved ? "Wi-Fi is switched off: join a network on the Wi-Fi page (Network, START)" :
                    "No Wi-Fi network is set up: choose one on the Wi-Fi page (Network, START)");
                return false;
            }
            if(w->state == KWM_WIFI_BAD_PASSWORD) {
                problem(s, "%.32s refused the Wi-Fi password: enter it again on the Wi-Fi page", w->ssid);
                return false;
            }
        }
        if(now(s) - start > limit_ms) {
            if(!have) problem(s, "The Wi-Fi board did not report its Wi-Fi status");
            else if(w->state == KWM_WIFI_NOT_FOUND) problem(s, "Wi-Fi network %.32s not found", w->ssid);
            else problem(s, "Not connected to %.32s after %u seconds", w->ssid, limit_ms / 1000u);
            return false;
        }
        if(!kui_wifi_session_busy(s)) pause_ms(s, 5);
    }
}
bool kui_wifi_session_scan(struct kui_wifi_session *s, kui_cancel_fn cancel) {
    if(!s || !s->found) return false;
    uint32_t before = s->host.counts.scan;
    if(!kwh_wifi_scan(&s->host)) { problem(s, "The link to the Wi-Fi board is full"); return false; }
    if(!wait_for(s, &s->host.counts.scan, before, KUI_WIFI_SCAN_MS, cancel)) return false;
    if(s->host.scan_status) { problem(s, "The Wi-Fi board could not scan"); return false; }
    return true;
}
bool kui_wifi_session_join(struct kui_wifi_session *s, const char *ssid, const char *password, uint8_t band,
                           kui_cancel_fn cancel, void (*stage)(void *ctx, const char *text), void *ctx) {
    if(!s || !s->found) return false;
    uint32_t join_before = s->host.counts.join, mark = 0;
    if(!kwh_wifi_join(&s->host, ssid, password, true, band)) {
        problem(s, "A network name has 1 to 32 characters and a password at most 64");
        return false;
    }
    uint64_t start = now(s), asked = 0, missing_since = 0;
    bool joined = false;
    uint8_t shown = 0xff;
    for(;;) {
        if(stopped(cancel)) { problem(s, "Stopped; the board keeps trying to join %.32s", ssid); return false; }
        uint32_t wifi_before = s->host.counts.wifi;
        if(!kui_wifi_session_step(s)) return false;
        uint64_t t = now(s);
        if(!joined && s->host.counts.join != join_before) {
            joined = true;
            if(s->host.join_status) {
                problem(s, s->host.join_status == KWM_E_INVALID ?
                    "The board refused that password: a Wi-Fi password has 8 to 63 characters" :
                    "The Wi-Fi board could not start joining");
                return false;
            }
            /* A status in the same answer comes after the join. */
            mark = wifi_before;
        }
        if(joined && s->host.counts.wifi != mark) {
            mark = s->host.counts.wifi;
            const struct kwh_wifi *w = &s->host.wifi;
            if(w->state != shown) {
                shown = w->state;
                char text[96];
                describe(text, sizeof(text), w);
                if(stage) stage(ctx, text);
            }
            if(w->state == KWM_WIFI_ONLINE && addressed(w)) return true;
            if(w->state == KWM_WIFI_BAD_PASSWORD) { problem(s, "%.32s refused the password", ssid); return false; }
            if(w->state == KWM_WIFI_IDLE) { problem(s, "The Wi-Fi board stopped joining %.32s", ssid); return false; }
            if(w->state != KWM_WIFI_NOT_FOUND) missing_since = 0;
            else if(!missing_since) missing_since = t;
        }
        if(missing_since && t - missing_since > MISSING_MS) {
            problem(s, "%.32s not found; the board keeps looking for it", ssid);
            return false;
        }
        if(t - start > KUI_WIFI_JOIN_MS) {
            problem(s, "Not online on %.32s after %u seconds; the board keeps trying", ssid, KUI_WIFI_JOIN_MS / 1000u);
            return false;
        }
        if(joined && (!asked || t - asked >= 2000u)) {
            (void)kwh_wifi_query(&s->host);
            asked = t ? t : 1u;
        }
        if(!kui_wifi_session_busy(s)) pause_ms(s, 5);
    }
}
/* Steps until the status shows what was asked for. */
static bool settle(struct kui_wifi_session *s, bool (*done)(const struct kwh_wifi *w, uint8_t value), uint8_t value,
                   kui_cancel_fn cancel) {
    uint64_t start = now(s), asked = now(s);
    while(!done(&s->host.wifi, value)) {
        if(stopped(cancel)) { problem(s, "Stopped"); return false; }
        if(!kui_wifi_session_step(s)) return false;
        uint64_t t = now(s);
        if(t - start > ANSWER_MS) return false;
        if(t - asked >= 500u) {
            (void)kwh_wifi_query(&s->host);
            asked = t;
        }
        if(!kui_wifi_session_busy(s)) pause_ms(s, 2);
    }
    return true;
}
static bool left(const struct kwh_wifi *w, uint8_t forget) { return w->state == KWM_WIFI_IDLE && (!forget || !w->saved); }
static bool banded(const struct kwh_wifi *w, uint8_t band) { return w->band_mode == band; }
bool kui_wifi_session_leave(struct kui_wifi_session *s, bool forget, kui_cancel_fn cancel) {
    if(!s || !s->found) return false;
    if(!kwh_wifi_leave(&s->host, forget)) { problem(s, "The link to the Wi-Fi board is full"); return false; }
    if(settle(s, left, forget, cancel)) return true;
    if(!s->problem[0]) problem(s, "The Wi-Fi board did not confirm the disconnection");
    return false;
}
bool kui_wifi_session_band(struct kui_wifi_session *s, uint8_t band, kui_cancel_fn cancel) {
    if(!s || !s->found) return false;
    if(!kwh_wifi_band(&s->host, band)) { problem(s, "The link to the Wi-Fi board is full"); return false; }
    if(settle(s, banded, band, cancel)) return true;
    if(!s->problem[0]) problem(s, "The Wi-Fi board did not apply the requested bands");
    return false;
}

/* ---- Sockets for the FTP server ---- */
static struct kwh *host_of(void *ctx) { return &((struct kui_wifi_session *)ctx)->host; }
/* Keep-alive is asked for when a slot opens (after 60 seconds' silence). */
#define SLOT_FLAGS (KWM_NODELAY | KWM_KEEPALIVE)
static bool slot_listen(void *ctx, unsigned s, uint16_t port) { return kwh_listen(host_of(ctx), s, port, SLOT_FLAGS); }
static bool slot_connect(void *ctx, unsigned s, uint16_t local, const uint8_t ip[4], uint16_t port) {
    (void)local; /* the board picks its own local port */
    return kwh_connect(host_of(ctx), s, ip, port, SLOT_FLAGS);
}
static bool slot_disconnect(void *ctx, unsigned s) { return kwh_close(host_of(ctx), s, true); }
static bool slot_close(void *ctx, unsigned s) { return kwh_close(host_of(ctx), s, false); }
static bool slot_state(void *ctx, unsigned s, uint8_t *state) {
    *state = kwh_state(host_of(ctx), s);
    return s < KWM_SLOTS;
}
static bool slot_peer(void *ctx, unsigned s, uint8_t ip[4], uint16_t *port) { return kwh_peer(host_of(ctx), s, ip, port); }
static bool slot_keepalive(void *ctx, unsigned s, unsigned seconds) {
    (void)ctx;
    (void)seconds;
    return s < KWM_SLOTS;
}
static bool slot_received(void *ctx, unsigned s, size_t *bytes) {
    *bytes = kwh_available(host_of(ctx), s);
    return s < KWM_SLOTS;
}
static bool slot_receive(void *ctx, unsigned s, void *data, size_t bytes) {
    return kwh_recv(host_of(ctx), s, data, bytes) == bytes;
}
static bool slot_room(void *ctx, unsigned s, size_t *bytes) {
    *bytes = kwh_room(host_of(ctx), s);
    return s < KWM_SLOTS;
}
static bool slot_send(void *ctx, unsigned s, const void *data, size_t bytes) {
    return kwh_send(host_of(ctx), s, data, bytes) == bytes;
}
static bool slot_sent(void *ctx, unsigned s, bool *all) {
    *all = kwh_flushed(host_of(ctx), s);
    return s < KWM_SLOTS;
}
void kui_wifi_session_sockets(struct kui_wifi_session *s, struct kui_net_sockets *out) {
    *out = (struct kui_net_sockets){s, KWM_SLOTS, slot_listen, slot_connect, slot_disconnect, slot_close, slot_state,
        slot_peer, slot_keepalive, slot_received, slot_receive, slot_room, slot_send, slot_sent};
}
void kui_wifi_session_end(struct kui_wifi_session *s) {
    if(!s || !s->port || !s->open) return;
    if(s->found) {
        /* Nothing may stay listening on the board once K-UI stops using it. */
        unsigned pending = 0, graceful = 0;
        for(unsigned i = 0; i < KWM_SLOTS; ++i) {
            uint8_t state = kwh_state(&s->host, i);
            if(state == KWM_CLOSED || state == KWM_CLOSING) continue;
            pending |= 1u << i;
            if(state == KWM_ESTABLISHED || state == KWM_PEER_CLOSED) graceful |= 1u << i;
        }
        uint64_t start = now(s);
        for(;;) {
            /* A full outgoing queue can refuse a close. Keep its intent
             * until it is queued, then drain the accepted requests too. */
            for(unsigned i = 0; i < KWM_SLOTS; ++i) {
                unsigned bit = 1u << i;
                if((pending & bit) && kwh_close(&s->host, i, (graceful & bit) != 0)) pending &= ~bit;
            }
            if(!pending && !kui_wifi_session_busy(s)) break;
            if(now(s) - start >= 500u || !kui_wifi_session_step(s)) break;
        }
    }
    s->port->close();
    s->open = false;
}

/* ---- The Network app ---- */
static void address(char *out, const char *name, const uint8_t ip[4]) {
    snprintf(out, KUI_APP_LINE_CAP, "%s: %u.%u.%u.%u", name, ip[0], ip[1], ip[2], ip[3]);
}
static void board_line(char *out, size_t cap, const struct kui_wifi_session *s) {
    snprintf(out, cap, "XIAO %s, firmware %.24s (SPI %.24s)", kui_wifi_chip_text(s->host.hello.chip),
        s->host.hello.version, speed(s, s->level));
}
static void radio_line(char *out, const struct kwh_wifi *w) {
    if(w->channel) snprintf(out, KUI_APP_LINE_CAP, "Radio: %s GHz channel %u, signal %d dBm; allowed: %s",
        w->band == 5 ? "5" : "2.4", w->channel, w->rssi, kui_wifi_band_text(w->band_mode));
    else snprintf(out, KUI_APP_LINE_CAP, "Radio: not connected; allowed: %s", kui_wifi_band_text(w->band_mode));
}
/* A board that answers badly is reported, not hidden: the wiring needs
 * attention. */
static bool unusable(struct kui_app_status *out, const struct kui_wifi_session *s) {
    if(!s->answered) return false;
    memset(out, 0, sizeof(*out));
    out->complete = true;
    out->done = out->total = 1;
    out->errors = 1;
    snprintf(out->message, sizeof(out->message), "%s", s->problem);
    out->line_count = 3;
    snprintf(out->lines[0], KUI_APP_LINE_CAP, "Adapter: K-UI Wi-Fi board on the SCI port");
    snprintf(out->lines[1], KUI_APP_LINE_CAP, "Check the SCI wires (MISO, MOSI, clock, select, READY) and 5 V");
    snprintf(out->lines[2], KUI_APP_LINE_CAP, "The SD card is unaffected: it stays on SCIF");
    return true;
}
static struct kui_wifi_session *start_session(kui_log_fn log, struct kui_app_status *out, bool *reported) {
    *reported = false;
    struct kui_wifi_session *s = calloc(1, sizeof(*s));
    if(!s) return NULL;
    if(kui_wifi_session_find(s, kui_wifi_console_port(), log)) return s;
    *reported = unusable(out, s);
    free(s);
    return NULL;
}
static void finish(struct kui_wifi_session *s) {
    kui_wifi_session_end(s);
    /* The link's buffers held the Wi-Fi password if one was sent. */
    memset(s, 0, sizeof(*s));
    free(s);
}
bool kui_wifi_network_inspect(struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel) {
    if(!out) return false;
    bool reported;
    struct kui_wifi_session *s = start_session(log, out, &reported);
    if(!s) return reported;
    bool answered = kui_wifi_session_status(s, cancel);
    const struct kwh_wifi *w = &s->host.wifi;
    memset(out, 0, sizeof(*out));
    out->complete = true;
    out->done = out->total = 1;
    out->passed = answered && w->state == KWM_WIFI_ONLINE;
    if(!answered) out->errors = 1;
    snprintf(out->message, sizeof(out->message), "%s", answered ? "Adapter inspection complete" : s->problem);
    out->line_count = 8;
    snprintf(out->lines[0], KUI_APP_LINE_CAP, "Adapter: K-UI Wi-Fi board (XIAO %s), firmware %.24s",
        kui_wifi_chip_text(s->host.hello.chip), s->host.hello.version);
    snprintf(out->lines[1], KUI_APP_LINE_CAP, "SPI %.24s; link checked; READY %s", speed(s, s->level),
        s->ready_changes ? "working" : "never changed (GPIO5)");
    if(answered) {
        describe(out->lines[2], KUI_APP_LINE_CAP, w);
        radio_line(out->lines[3], w);
        snprintf(out->lines[4], KUI_APP_LINE_CAP, "Address: %u.%u.%u.%u   Gateway: %u.%u.%u.%u", w->ip[0], w->ip[1],
            w->ip[2], w->ip[3], w->gateway[0], w->gateway[1], w->gateway[2], w->gateway[3]);
    } else {
        snprintf(out->lines[2], KUI_APP_LINE_CAP, "Wi-Fi status: not reported");
        snprintf(out->lines[3], KUI_APP_LINE_CAP, "Radio: unknown");
        snprintf(out->lines[4], KUI_APP_LINE_CAP, "Address: unknown");
    }
    const uint8_t *mac = s->host.hello.mac;
    snprintf(out->lines[5], KUI_APP_LINE_CAP, "DNS: %u.%u.%u.%u   MAC: %02X:%02X:%02X:%02X:%02X:%02X", w->dns[0], w->dns[1],
        w->dns[2], w->dns[3], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(out->lines[6], KUI_APP_LINE_CAP, "%s", out->passed ? "The FTP server (Y) can use this connection" :
        "Choose a network on the Wi-Fi page (START)");
    snprintf(out->lines[7], KUI_APP_LINE_CAP, "No settings were changed");
    if(s->port->bus->gap_us) {
        unsigned gap = s->port->bus->gap_us(s->port->bus->ctx);
        if(gap) snprintf(out->lines[7], KUI_APP_LINE_CAP, "READY absent; transfer gap %u ms (adaptive)", gap / 1000u);
        else snprintf(out->lines[7], KUI_APP_LINE_CAP, "READY handshake working; 20 ms timeout");
    }
    finish(s);
    if(log) {
        log("Network inspection: %s", out->message);
        for(unsigned i = 0; i < out->line_count; ++i) log("%s", out->lines[i]);
    }
    return true;
}
struct test_stage {
    struct kui_app_status *out;
    kui_app_progress_fn progress;
    kui_log_fn log;
};
static void test_stage(void *ctx, const char *text) {
    struct test_stage *t = ctx;
    snprintf(t->out->message, sizeof(t->out->message), "%s", text);
    if(t->log) t->log("%s", text);
    if(t->progress) t->progress(t->out);
}
bool kui_wifi_network_test(struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel,
                           kui_app_progress_fn progress) {
    if(!out) return false;
    bool reported;
    struct kui_wifi_session *s = start_session(log, out, &reported);
    if(!s) return reported;
    memset(out, 0, sizeof(*out));
    struct test_stage stage = {out, progress, log};
    test_stage(&stage, "Waiting for the Wi-Fi network");
    bool online = kui_wifi_session_online(s, ONLINE_MS, cancel, test_stage, &stage);
    bool named = false, timed = false;
    uint64_t lookup_ms = 0;
    if(online) {
        test_stage(&stage, "Looking up pool.ntp.org");
        uint64_t start = now(s);
        uint32_t before = s->host.counts.dns;
        named = kwh_dns(&s->host, 1, "pool.ntp.org") && wait_for(s, &s->host.counts.dns, before, 10000u, cancel) &&
            !s->host.dns_status;
        lookup_ms = now(s) - start;
        before = s->host.counts.time;
        timed = kwh_time(&s->host) && wait_for(s, &s->host.counts.time, before, ANSWER_MS, cancel) &&
            !s->host.time_status;
    }
    bool halted = stopped(cancel);
    const struct kwh_wifi *w = &s->host.wifi;
    out->complete = !halted;
    out->stopped = halted;
    out->passed = online && named;
    if(!out->passed) ++out->errors;
    snprintf(out->message, sizeof(out->message), "%s", halted ? "Network connection test stopped" :
        !online ? s->problem : named ? "Online; a name was looked up on the Internet" :
        "Online, but the name lookup failed: check the router's Internet connection");
    out->line_count = 8;
    snprintf(out->lines[0], KUI_APP_LINE_CAP, "Address: %u.%u.%u.%u (%s)", w->ip[0], w->ip[1], w->ip[2], w->ip[3],
        online ? "DHCP by the Wi-Fi board" : "none");
    address(out->lines[1], "Gateway", w->gateway);
    address(out->lines[2], "DNS", w->dns);
    if(named) snprintf(out->lines[3], KUI_APP_LINE_CAP, "pool.ntp.org: %u.%u.%u.%u (%lu ms)", s->host.dns_ip[0],
        s->host.dns_ip[1], s->host.dns_ip[2], s->host.dns_ip[3], (unsigned long)lookup_ms);
    else snprintf(out->lines[3], KUI_APP_LINE_CAP, "Name lookup: %s", online ? "failed" : "not tried");
    struct kui_datetime utc;
    if(timed && kui_clock_from_seconds((int64_t)(s->host.time_ms / 1000u), &utc))
        snprintf(out->lines[4], KUI_APP_LINE_CAP, "Network time: %04u-%02u-%02u %02u:%02u:%02u UTC", utc.year, utc.month,
            utc.day, utc.hour, utc.minute, utc.second);
    else snprintf(out->lines[4], KUI_APP_LINE_CAP, "Network time: %s", online ? "not set yet" : "not tried");
    describe(out->lines[5], KUI_APP_LINE_CAP, w);
    radio_line(out->lines[6], w);
    snprintf(out->lines[7], KUI_APP_LINE_CAP, "Saved settings and console flash are unchanged");
    finish(s);
    if(log) {
        log("Network connection test: %s", out->message);
        for(unsigned i = 0; i < out->line_count; ++i) log("%s", out->lines[i]);
    }
    if(progress) progress(out);
    return true;
}

/* ---- The Wi-Fi page ---- */
struct page {
    struct kui_wifi_view *out;
    kui_wifi_publish_fn publish;
    kui_log_fn log;
};
static void say(struct page *p, bool failed, const char *text) {
    snprintf(p->out->message, sizeof(p->out->message), "%s", text);
    p->out->failed = failed;
    if(p->log) p->log("Wi-Fi: %s", text);
    if(p->publish) p->publish(p->out);
}
static void page_stage(void *ctx, const char *text) { say(ctx, false, text); }
/* Each network name once, strongest first; hidden networks are left out. */
static void list_networks(struct kui_wifi_view *out, const struct kwh *h) {
    out->count = 0;
    for(unsigned i = 0; i < h->scan_count; ++i) {
        const struct kwh_ap *ap = &h->scan[i];
        if(!ap->ssid[0]) continue;
        unsigned at = 0;
        while(at < out->count && strcmp(out->networks[at].ssid, ap->ssid)) ++at;
        if(at < out->count && out->networks[at].rssi >= ap->rssi) continue;
        if(at == out->count) {
            if(out->count == KUI_WIFI_NETWORKS) continue;
            ++out->count;
        }
        struct kui_wifi_network *n = &out->networks[at];
        snprintf(n->ssid, sizeof(n->ssid), "%s", ap->ssid);
        n->channel = ap->channel;
        n->security = ap->auth;
        n->rssi = ap->rssi;
        n->five = ap->channel > 14;
    }
    for(unsigned i = 1; i < out->count; ++i)
        for(unsigned j = i; j && out->networks[j].rssi > out->networks[j - 1].rssi; --j) {
            struct kui_wifi_network swap = out->networks[j];
            out->networks[j] = out->networks[j - 1];
            out->networks[j - 1] = swap;
        }
    out->scanned = true;
}
void kui_wifi_run(const struct kui_wifi_port *port, const struct kui_wifi_request *request, struct kui_wifi_view *out,
                  kui_log_fn log, kui_cancel_fn cancel, kui_wifi_publish_fn publish) {
    if(!out || !request) return;
    memset(out, 0, sizeof(*out));
    out->working = true;
    struct page p = {out, publish, log};
    say(&p, false, "Looking for the Wi-Fi board");
    struct kui_wifi_session *s = calloc(1, sizeof(*s));
    if(!s) {
        out->working = false;
        say(&p, true, "Insufficient memory for the Wi-Fi board");
        return;
    }
    if(!kui_wifi_session_find(s, port, log)) {
        out->working = false;
        say(&p, true, s->problem);
        free(s);
        return;
    }
    out->found = true;
    board_line(out->board, sizeof(out->board), s);
    bool ok = kui_wifi_session_status(s, cancel);
    out->wifi = s->host.wifi;
    char text[128] = "";
    if(ok) switch(request->action) {
    case KUI_WIFI_REFRESH:
        say(&p, false, "Looking for networks in range");
        ok = kui_wifi_session_scan(s, cancel);
        if(ok) {
            list_networks(out, &s->host);
            snprintf(text, sizeof(text), "%u network%s in range", out->count, out->count == 1 ? "" : "s");
        }
        break;
    case KUI_WIFI_JOIN: {
        snprintf(text, sizeof(text), "Joining %.32s", request->ssid);
        say(&p, false, text);
        ok = kui_wifi_session_join(s, request->ssid, request->password, request->band, cancel, page_stage, &p);
        const uint8_t *ip = s->host.wifi.ip;
        if(ok) snprintf(text, sizeof(text), "Online on %.32s as %u.%u.%u.%u; saved on the board", request->ssid, ip[0],
            ip[1], ip[2], ip[3]);
        break;
    }
    case KUI_WIFI_FORGET:
        ok = kui_wifi_session_leave(s, true, cancel);
        if(ok) snprintf(text, sizeof(text), "Forgot the network; Wi-Fi is off until you join one");
        break;
    case KUI_WIFI_BAND:
        ok = kui_wifi_session_band(s, request->band, cancel);
        if(ok) snprintf(text, sizeof(text), "Bands: %s (kept on the board)", kui_wifi_band_text(request->band));
        break;
    case KUI_WIFI_UPDATE_CHECK: case KUI_WIFI_UPDATE:
        ok = false;
        problem(s, "Use the SD firmware updater for this request");
        break;
    }
    if(!ok) snprintf(text, sizeof(text), "%s", s->problem);
    /* The latest status after a change, successful or not. */
    if(request->action != KUI_WIFI_REFRESH) (void)kui_wifi_session_status(s, NULL);
    out->wifi = s->host.wifi;
    finish(s);
    out->working = false;
    say(&p, !ok, text);
}
