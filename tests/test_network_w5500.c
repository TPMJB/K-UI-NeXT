/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "kui/network_w5500.h"
#include "w5500_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The W5500 bring-up and the Network app's use of it, on the model. The
 * port's pauses move the model's clock instead of sleeping, so ten-second
 * link and fifteen-second DHCP limits run at once. */
static unsigned noisy_levels, opens, closes;
static bool is_open;
static bool frame(void *ctx, const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes) {
    assert(is_open);
    return w5500_model_bus.frame(ctx, header, out, in, bytes);
}
static uint64_t now_ms(void *ctx) { return w5500_model_bus.now_ms(ctx); }
static void pause_ms(void *ctx, unsigned ms) { w5500_model_advance(ms); w5500_model_bus.pause(ctx, 0); }
static const struct kui_w5500_bus bus = {NULL, frame, now_ms, pause_ms};
static bool open_level(unsigned level) {
    assert(!is_open && level < 4);
    is_open = true;
    ++opens;
    w5500_model_live()->corrupt_reads = level < noisy_levels;
    return true;
}
static void close_port(void) { assert(is_open); is_open = false; ++closes; }
static const char *speed(unsigned level) {
    static const char *const names[] = {"12.5 MHz", "6.25 MHz", "3.125 MHz", "1.5625 MHz"};
    return names[level];
}
static const uint8_t test_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static void mac(uint8_t out[6]) { memcpy(out, test_mac, 6); }
static const struct kui_w5500_port port = {&bus, 4, open_level, close_port, speed, mac};
const struct kui_w5500_port *kui_w5500_console_port(void) { return &port; }

static void begin(const struct w5500_model_options *options, unsigned noise) {
    w5500_model_start(options);
    noisy_levels = noise;
    opens = closes = 0;
}
static bool line(const struct kui_app_status *s, const char *text) {
    for(unsigned i = 0; i < s->line_count; ++i) if(strstr(s->lines[i], text)) return true;
    return false;
}
static unsigned progress_calls;
static void progress(const struct kui_app_status *s) { assert(s->message[0]); ++progress_calls; }
static bool never(void) { return false; }

static void inspection(void) {
    struct kui_app_status out;
    begin(NULL, 0);
    assert(kui_w5500_network_inspect(&out, NULL, never));
    assert(out.complete && out.passed && !out.errors && !is_open && opens == 1 && closes == 1);
    assert(line(&out, "WIZnet W5500 on the SCI port (version 4)") && line(&out, "SPI: 12.5 MHz"));
    assert(line(&out, "Link: 100 Mbit/s, full duplex") && line(&out, "MAC: 02:11:22:33:44:55"));
    assert(line(&out, "SD card stays on SCIF"));
    w5500_model_stop();

    /* Bit errors at the two fastest speeds: the third is used. */
    begin(NULL, 2);
    assert(kui_w5500_network_inspect(&out, NULL, never) && out.passed && line(&out, "SPI: 3.125 MHz"));
    assert(opens == 3 && closes == 3 && !is_open);
    w5500_model_stop();

    /* At every speed: reported as a wiring fault, not as no adapter. */
    begin(NULL, 4);
    memset(&out, 0, sizeof(out));
    assert(kui_w5500_network_inspect(&out, NULL, never));
    assert(out.complete && !out.passed && out.errors && strstr(out.message, "wiring check failed"));
    assert(opens == 4 && closes == 4 && !is_open);
    w5500_model_stop();

    /* Nothing there: false, and the result is left for the caller. */
    struct w5500_model_options absent = {.absent = true};
    begin(&absent, 0);
    strcpy(out.message, "untouched");
    assert(!kui_w5500_network_inspect(&out, NULL, never) && !strcmp(out.message, "untouched"));
    assert(opens == 4 && closes == 4 && !is_open);
    w5500_model_stop();

    struct w5500_model_options down = {.link_down = true};
    begin(&down, 0);
    assert(kui_w5500_network_inspect(&out, NULL, never) && !out.passed && line(&out, "no cable link after 3 seconds"));
    w5500_model_stop();
    puts("PASS W5500 inspection: found, slower speeds on a noisy bus, wiring fault, absent, no cable");
}

static void connection_test(void) {
    struct kui_app_status out;
    begin(NULL, 0);
    progress_calls = 0;
    assert(kui_w5500_network_test(&out, NULL, never, progress));
    assert(out.complete && out.passed && !out.errors && progress_calls >= 4 && !is_open);
    assert(strstr(out.message, "gateway ping replied") && line(&out, "Address: 10.0.0.2 (DHCP ACK)"));
    assert(line(&out, "Gateway: 10.0.0.1") && line(&out, "DNS advertised: 10.0.0.53"));
    assert(line(&out, "Gateway ARP: replied; ICMP: replied") && line(&out, "W5500 on SCI at 12.5 MHz"));
    assert(w5500_model_counts.pings == 1);
    w5500_model_stop();

    struct w5500_model_options silent = {.dhcp_silent = true};
    begin(&silent, 0);
    assert(kui_w5500_network_test(&out, NULL, never, NULL) && !out.passed && out.errors);
    assert(strstr(out.message, "No DHCP offer within 15 seconds") && !is_open);
    w5500_model_stop();

    struct w5500_model_options down = {.link_down = true};
    begin(&down, 0);
    assert(kui_w5500_network_test(&out, NULL, never, NULL) && !out.passed && strstr(out.message, "No network cable link"));
    w5500_model_stop();

    struct w5500_model_options conflict = {.conflict = true};
    begin(&conflict, 0);
    assert(kui_w5500_network_test(&out, NULL, never, NULL) && !out.passed && strstr(out.message, "conflict"));
    w5500_model_stop();

    struct w5500_model_options absent = {.absent = true};
    begin(&absent, 0);
    assert(!kui_w5500_network_test(&out, NULL, never, NULL) && !is_open);
    w5500_model_stop();
    puts("PASS W5500 connection test: DHCP, gateway ARP and ping, no offer, no cable, conflict, absent");
}

static void lease(void) {
    begin(NULL, 0);
    struct kui_w5500_session s;
    assert(kui_w5500_session_find(&s, &port, NULL) && s.found && s.level == 0 && !memcmp(s.mac, test_mac, 6));
    assert(kui_w5500_session_link(&s, never));
    struct kui_network_probe p;
    assert(kui_w5500_session_dhcp(&s, true, &p, NULL, never, NULL));
    assert(s.leased && s.lease_seconds == 3600 && !memcmp(s.config.ip, w5500_model_lease, 4));
    assert(!p.arp_reply && !w5500_model_counts.pings && w5500_model_counts.arp_probes >= 1);
    /* The chip now answers for the leased address. */
    uint8_t ip[4];
    assert(kui_w5500_read(&s.chip, KUI_W5500_COMMON, KUI_W5500_SIPR, ip, 4) && !memcmp(ip, w5500_model_lease, 4));
    uint64_t t = now_ms(NULL);
    assert(kui_w5500_session_renew_in(&s, t) == 1800 && kui_w5500_session_expires_in(&s, t) == 3600);
    assert(kui_w5500_session_renew_in(&s, t + 1801000u) == 0 && kui_w5500_session_expires_in(&s, t + 3601000u) == 0);

    /* Renewal over UDP while TCP sockets could stay open. */
    w5500_model_advance(1900000u);
    assert(kui_w5500_session_renew_in(&s, now_ms(NULL)) == 0);
    bool refused = true;
    assert(kui_w5500_session_renew(&s, 3, 3000, &refused, never) && !refused);
    assert(kui_w5500_session_renew_in(&s, now_ms(NULL)) == 1800 && w5500_model_counts.udp_requests >= 1);
    uint8_t state;
    assert(kui_w5500_status(&s.chip, 3, &state) && state == KUI_W5500_CLOSED);

    w5500_model_live()->dhcp_nak = true;
    assert(!kui_w5500_session_renew(&s, 3, 3000, &refused, never) && refused && strstr(s.problem, "rejected"));
    w5500_model_live()->dhcp_nak = false;
    w5500_model_live()->dhcp_silent = true;
    assert(!kui_w5500_session_renew(&s, 3, 3000, &refused, never) && !refused);
    kui_w5500_session_end(&s);
    assert(!is_open && closes == 1);

    /* An infinite lease is never renewed. */
    s.leased = true;
    s.lease_seconds = UINT32_MAX;
    assert(kui_w5500_session_renew_in(&s, 0) == UINT32_MAX && kui_w5500_session_expires_in(&s, 0) == UINT32_MAX);
    w5500_model_stop();
    puts("PASS W5500 lease: DHCP for the server, renewal over UDP, refused and silent renewals");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    inspection();
    connection_test();
    lease();
    puts("PASS W5500 network");
    return 0;
}
