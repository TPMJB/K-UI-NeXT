/* SPDX-License-Identifier: GPL-3.0-only */
/* The W5500 on the SCI port: finding it, its cable link, DHCP through the
 * network probe, lease renewal, and the Network app's inspection and
 * connection test for it. */
#include "kui/network_w5500.h"
#include <stdio.h>
#include <string.h>

static uint64_t now(const struct kui_w5500_session *s) { return s->port->bus->now_ms(s->port->bus->ctx); }
static void pause_ms(const struct kui_w5500_session *s, unsigned ms) { s->port->bus->pause(s->port->bus->ctx, ms); }
static bool stopped(kui_cancel_fn cancel) { return cancel && cancel(); }
static const char *speed(const struct kui_w5500_session *s, unsigned level) {
    return s->port->speed ? s->port->speed(level) : "?";
}
static void problem(struct kui_w5500_session *s, const char *text) { snprintf(s->problem, sizeof(s->problem), "%s", text); }

bool kui_w5500_session_find(struct kui_w5500_session *s, const struct kui_w5500_port *port, kui_log_fn log) {
    if(!s) return false;
    memset(s, 0, sizeof(*s));
    s->port = port;
    if(!port || !port->bus || !port->bus->frame || !port->bus->now_ms || !port->bus->pause || !port->open ||
       !port->close || !port->mac || !port->levels) {
        problem(s, "No SCI port is available for a W5500");
        return false;
    }
    bool answered = false;
    uint8_t first = 0;
    for(unsigned level = 0; level < port->levels; ++level) {
        if(!port->open(level)) { problem(s, "The SCI port could not be started"); return false; }
        s->open = true;
        memset(&s->chip, 0, sizeof(s->chip));
        s->chip.bus = port->bus;
        uint8_t version = 0;
        bool reset = kui_w5500_reset(&s->chip, &version);
        if(!level) first = version;
        bool checked = reset && kui_w5500_bus_check(&s->chip, KUI_W5500_CHECK_ROUNDS);
        if(log) log("W5500 on SCI at %s: version %02x, %s", speed(s, level), version,
            !reset ? "no chip" : checked ? "wiring check passed" : "wiring check failed");
        answered = answered || reset;
        if(checked) {
            port->mac(s->mac);
            if(!kui_w5500_set_mac(&s->chip, s->mac)) break;
            s->level = level;
            s->version = version;
            s->found = true;
            return true;
        }
        port->close();
        s->open = false;
    }
    if(s->open) { port->close(); s->open = false; }
    if(answered) problem(s, "A W5500 answered but failed the wiring check at every speed");
    else snprintf(s->problem, sizeof(s->problem), "No W5500 answered on the SCI port (read %02X)", first);
    s->version = answered ? KUI_W5500_VERSION : first;
    return false;
}
bool kui_w5500_session_link(struct kui_w5500_session *s, kui_cancel_fn cancel) {
    if(!s || !s->found) return false;
    uint64_t start = now(s);
    for(;;) {
        if(!kui_w5500_link(&s->chip, &s->link)) { problem(s, "The W5500 stopped answering"); return false; }
        if(s->link.up) return true;
        if(stopped(cancel)) { problem(s, "Stopped"); return false; }
        if(now(s) - start > KUI_W5500_LINK_MS) {
            problem(s, "No network cable link: check the cable and the router");
            return false;
        }
        pause_ms(s, 50);
    }
}
static uint32_t transaction(const struct kui_w5500_session *s) {
    uint32_t xid = (uint32_t)now(s) ^ UINT32_C(0x4b555735);
    for(unsigned i = 0; i < 6; ++i) xid = xid * 31u + s->mac[i];
    return xid ? xid : 1u;
}
bool kui_w5500_session_dhcp(struct kui_w5500_session *s, bool lease_only, struct kui_network_probe *probe,
                            kui_log_fn log, kui_cancel_fn cancel, void (*stage)(enum kui_network_stage stage)) {
    struct kui_network_probe local;
    struct kui_network_probe *p = probe ? probe : &local;
    memset(p, 0, sizeof(*p));
    p->stage = KUI_NET_FAILED;
    if(!s || !s->found) return false;
    s->leased = false;
    static const uint8_t zero[4];
    if(!kui_w5500_set_ipv4(&s->chip, zero, zero, zero) ||
       !kui_w5500_open(&s->chip, 0, KUI_W5500_MACRAW | KUI_W5500_MR_MFEN, 0)) {
        problem(s, "The W5500 could not open its raw socket");
        return false;
    }
    kui_network_probe_begin(p, s->mac, transaction(s), NULL, now(s));
    p->lease_only = lease_only;
    enum kui_network_stage last = KUI_NET_FAILED;
    uint8_t frame[KUI_NETWORK_FRAME_MAX];
    const char *failure = NULL;
    while(p->stage < KUI_NET_DONE && !failure) {
        if(stopped(cancel)) { failure = "Stopped"; break; }
        for(unsigned i = 0; i < 8; ++i) {
            size_t got = 0;
            if(!kui_w5500_frame_receive(&s->chip, frame, sizeof(frame), &got)) { failure = "The W5500 stopped answering"; break; }
            if(!got) break;
            kui_network_probe_receive(p, frame, got, now(s));
        }
        if(failure) break;
        size_t bytes = kui_network_probe_step(p, frame, now(s));
        if(bytes && !kui_w5500_frame_send(&s->chip, frame, bytes)) { failure = "The W5500 could not send a frame"; break; }
        if(p->stage != last) {
            last = p->stage;
            if(stage) stage(last);
            if(log) log("W5500 network: %s", kui_network_stage_text(last));
        }
        pause_ms(s, 10);
    }
    (void)kui_w5500_close(&s->chip, 0);
    if(failure) { problem(s, failure); return false; }
    if(p->stage != KUI_NET_DONE) { problem(s, p->failure[0] ? p->failure : "DHCP did not finish"); return false; }
    s->config = p->config;
    memcpy(s->server, p->server, 4);
    s->lease_seconds = p->lease_seconds;
    s->leased_ms = now(s);
    s->leased = p->leased;
    if(!kui_w5500_set_ipv4(&s->chip, s->config.ip, s->config.mask, s->config.gateway)) {
        problem(s, "The W5500 stopped answering");
        return false;
    }
    return true;
}
/* A DHCP reply that arrived over UDP, rebuilt as the Ethernet frame the
 * probe reads (it checks the same fields a raw frame would carry). */
static size_t rebuild(uint8_t *f, const uint8_t mac[6], const uint8_t from[4], const uint8_t *data, size_t bytes) {
    if(bytes + 42u > KUI_NETWORK_FRAME_MAX) return 0;
    memset(f, 0, 42);
    memcpy(f, mac, 6);
    f[12] = 0x08;
    uint8_t *ip = f + 14;
    size_t total = bytes + 28u;
    ip[0] = 0x45; ip[2] = (uint8_t)(total >> 8); ip[3] = (uint8_t)total; ip[8] = 64; ip[9] = 17;
    memcpy(ip + 12, from, 4);
    memset(ip + 16, 0xff, 4);
    uint32_t sum = 0;
    for(unsigned i = 0; i < 20; i += 2) sum += (uint32_t)ip[i] << 8 | ip[i + 1];
    while(sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    ip[10] = (uint8_t)(~sum >> 8); ip[11] = (uint8_t)~sum;
    uint8_t *udp = f + 34;
    udp[1] = 67; udp[3] = 68; udp[4] = (uint8_t)((bytes + 8u) >> 8); udp[5] = (uint8_t)(bytes + 8u);
    memcpy(f + 42, data, bytes);
    return bytes + 42u;
}
bool kui_w5500_session_renew(struct kui_w5500_session *s, unsigned udp, unsigned limit_ms, bool *refused,
                             kui_cancel_fn cancel) {
    if(refused) *refused = false;
    if(!s || !s->found || !s->leased) return false;
    if(!kui_w5500_open(&s->chip, udp, KUI_W5500_UDP, 68)) { problem(s, "The W5500 could not open a UDP socket"); return false; }
    struct kui_network_probe p;
    kui_network_probe_renew(&p, s->mac, transaction(s), &s->config, s->server, now(s));
    static const uint8_t all[4] = {255, 255, 255, 255};
    uint8_t frame[KUI_NETWORK_FRAME_MAX], data[KUI_NETWORK_FRAME_MAX - 42u];
    const char *failure = NULL;
    uint64_t start = now(s);
    while(p.stage < KUI_NET_DONE && !failure) {
        if(stopped(cancel)) { failure = "Stopped"; break; }
        if(now(s) - start > limit_ms) { failure = "No answer from the DHCP server"; break; }
        for(unsigned i = 0; i < 4; ++i) {
            uint8_t from[4];
            uint16_t port = 0;
            size_t got = 0;
            if(!kui_w5500_datagram_receive(&s->chip, udp, from, &port, data, sizeof(data), &got)) {
                failure = "The W5500 stopped answering";
                break;
            }
            if(!got) break;
            size_t bytes = port == 67 ? rebuild(frame, s->mac, from, data, got) : 0;
            if(bytes) kui_network_probe_receive(&p, frame, bytes, now(s));
        }
        if(failure) break;
        size_t bytes = kui_network_probe_step(&p, frame, now(s));
        if(bytes > 42u && !kui_w5500_datagram_send(&s->chip, udp, all, 67, frame + 42, (uint16_t)(bytes - 42u)))
            failure = "The W5500 could not send the renewal";
        pause_ms(s, 10);
    }
    (void)kui_w5500_close(&s->chip, udp);
    if(failure) { problem(s, failure); return false; }
    if(p.stage != KUI_NET_DONE) {
        problem(s, p.failure[0] ? p.failure : "The lease was not renewed");
        if(refused) *refused = strstr(p.failure, "rejected") != NULL;
        return false;
    }
    s->lease_seconds = p.lease_seconds;
    s->leased_ms = now(s);
    if(memcmp(&s->config, &p.config, sizeof(s->config))) {
        s->config = p.config;
        if(!kui_w5500_set_ipv4(&s->chip, s->config.ip, s->config.mask, s->config.gateway)) {
            problem(s, "The W5500 stopped answering");
            return false;
        }
    }
    return true;
}
void kui_w5500_session_end(struct kui_w5500_session *s) {
    if(!s || !s->port || !s->open) return;
    /* A reset closes every socket and stops the chip answering the network. */
    if(s->found) (void)kui_w5500_reset(&s->chip, NULL);
    s->port->close();
    s->open = false;
}
static uint32_t elapsed(const struct kui_w5500_session *s, uint64_t now_ms) {
    uint64_t ms = now_ms > s->leased_ms ? now_ms - s->leased_ms : 0;
    return ms / 1000u > UINT32_MAX ? UINT32_MAX : (uint32_t)(ms / 1000u);
}
uint32_t kui_w5500_session_renew_in(const struct kui_w5500_session *s, uint64_t now_ms) {
    /* An infinite lease (all ones) is never renewed. */
    if(!s || !s->leased || s->lease_seconds == UINT32_MAX) return UINT32_MAX;
    uint32_t half = s->lease_seconds / 2u, gone = elapsed(s, now_ms);
    return gone >= half ? 0 : half - gone;
}
uint32_t kui_w5500_session_expires_in(const struct kui_w5500_session *s, uint64_t now_ms) {
    if(!s || !s->leased || s->lease_seconds == UINT32_MAX) return UINT32_MAX;
    uint32_t gone = elapsed(s, now_ms);
    return gone >= s->lease_seconds ? 0 : s->lease_seconds - gone;
}

static void address(char *out, const char *name, const uint8_t ip[4]) {
    snprintf(out, KUI_APP_LINE_CAP, "%s: %u.%u.%u.%u", name, ip[0], ip[1], ip[2], ip[3]);
}
static void link_text(char *out, const struct kui_w5500_link *link) {
    if(!link->up) snprintf(out, KUI_APP_LINE_CAP, "Link: no cable link");
    else snprintf(out, KUI_APP_LINE_CAP, "Link: %s Mbit/s, %s duplex", link->fast ? "100" : "10", link->full ? "full" : "half");
}
static void mac_text(char *out, const uint8_t mac[6]) {
    snprintf(out, KUI_APP_LINE_CAP, "MAC: %02X:%02X:%02X:%02X:%02X:%02X (from this console's ID)",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
/* A chip that answers but fails the wiring check is reported, not hidden:
 * the modification needs attention. */
static bool unusable(struct kui_app_status *out, const struct kui_w5500_session *s) {
    if(s->version != KUI_W5500_VERSION) return false;
    memset(out, 0, sizeof(*out));
    out->complete = true;
    out->done = out->total = 1;
    out->errors = 1;
    snprintf(out->message, sizeof(out->message), "W5500 found, but its wiring check failed");
    out->line_count = 4;
    snprintf(out->lines[0], KUI_APP_LINE_CAP, "Adapter: WIZnet W5500 on the SCI port");
    snprintf(out->lines[1], KUI_APP_LINE_CAP, "Data read back from it differed at every SPI speed");
    snprintf(out->lines[2], KUI_APP_LINE_CAP, "Check the SCI wires (MISO, MOSI, clock, select) and 3.3 V");
    snprintf(out->lines[3], KUI_APP_LINE_CAP, "The SD card is unaffected: it stays on SCIF");
    return true;
}
bool kui_w5500_network_inspect(struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel) {
    if(!out) return false;
    struct kui_w5500_session s;
    if(!kui_w5500_session_find(&s, kui_w5500_console_port(), log)) {
        kui_w5500_session_end(&s);
        return unusable(out, &s);
    }
    /* Auto-negotiation after the reset takes a few seconds. */
    uint64_t start = now(&s);
    bool answering = true;
    while(answering && (answering = kui_w5500_link(&s.chip, &s.link)) && !s.link.up && now(&s) - start < 3000u &&
          !stopped(cancel)) pause_ms(&s, 50);
    memset(out, 0, sizeof(*out));
    out->complete = true;
    out->done = out->total = 1;
    out->passed = answering && s.link.up;
    snprintf(out->message, sizeof(out->message), "%s", !answering ? "The W5500 stopped answering" :
        "Adapter inspection complete");
    if(!answering) out->errors = 1;
    out->line_count = 8;
    snprintf(out->lines[0], KUI_APP_LINE_CAP, "Adapter: WIZnet W5500 on the SCI port (version %u)", s.version);
    snprintf(out->lines[1], KUI_APP_LINE_CAP, "SPI: %s; wiring check passed (%u patterns)", speed(&s, s.level),
        KUI_W5500_CHECK_ROUNDS);
    if(answering) link_text(out->lines[2], &s.link);
    else snprintf(out->lines[2], KUI_APP_LINE_CAP, "Link: not checked");
    if(answering && !s.link.up) snprintf(out->lines[2], KUI_APP_LINE_CAP, "Link: no cable link after 3 seconds");
    mac_text(out->lines[3], s.mac);
    snprintf(out->lines[4], KUI_APP_LINE_CAP, "The SD card stays on SCIF; the W5500 uses SCI only");
    snprintf(out->lines[5], KUI_APP_LINE_CAP, "No configuration files or flash settings were changed");
    snprintf(out->lines[6], KUI_APP_LINE_CAP, "DHCP was not requested by this test");
    snprintf(out->lines[7], KUI_APP_LINE_CAP, "Internet reachability was not tested");
    kui_w5500_session_end(&s);
    if(log) {
        log("Network inspection: %s", out->message);
        for(unsigned i = 0; i < out->line_count; ++i) log("%s", out->lines[i]);
    }
    return true;
}
static struct kui_app_status *test_out;
static kui_app_progress_fn test_progress;
static void test_stage(enum kui_network_stage stage) {
    snprintf(test_out->message, sizeof(test_out->message), "%s", kui_network_stage_text(stage));
    if(test_progress) test_progress(test_out);
}
bool kui_w5500_network_test(struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel,
                            kui_app_progress_fn progress) {
    if(!out) return false;
    struct kui_w5500_session s;
    if(!kui_w5500_session_find(&s, kui_w5500_console_port(), log)) {
        kui_w5500_session_end(&s);
        return unusable(out, &s);
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->message, sizeof(out->message), "Waiting for the cable link (up to %u seconds)", KUI_W5500_LINK_MS / 1000u);
    if(progress) progress(out);
    struct kui_network_probe p;
    memset(&p, 0, sizeof(p));
    bool linked = kui_w5500_session_link(&s, cancel);
    test_out = out;
    test_progress = progress;
    bool passed = linked && kui_w5500_session_dhcp(&s, false, &p, log, cancel, test_stage);
    test_out = NULL;
    test_progress = NULL;
    bool halted = stopped(cancel) && !passed;
    out->complete = !halted;
    out->stopped = halted;
    out->passed = passed;
    if(!passed) ++out->errors;
    snprintf(out->message, sizeof(out->message), "%s", halted ? "Network connection test stopped" :
        passed ? (p.echo_reply ? "Address acquired; gateway ping replied" : "Address acquired; no gateway advertised") :
        s.problem);
    out->line_count = 8;
    snprintf(out->lines[0], KUI_APP_LINE_CAP, "Address: %u.%u.%u.%u (%s)", p.config.ip[0], p.config.ip[1], p.config.ip[2],
        p.config.ip[3], p.leased ? "DHCP ACK" : "no lease");
    address(out->lines[1], "Gateway", p.config.gateway);
    address(out->lines[2], "DNS advertised", p.config.dns);
    snprintf(out->lines[3], KUI_APP_LINE_CAP, "Gateway ARP: %s; ICMP: %s", p.arp_reply ? "replied" : "not proven",
        p.echo_reply ? "replied" : "not proven");
    snprintf(out->lines[4], KUI_APP_LINE_CAP, "Ping reply: %lu ms; received %u; ignored %u", (unsigned long)p.echo_ms,
        p.received, p.ignored);
    char link[KUI_APP_LINE_CAP];
    link_text(link, &s.link);
    snprintf(out->lines[5], KUI_APP_LINE_CAP, "W5500 on SCI at %s; %.48s", speed(&s, s.level), link);
    snprintf(out->lines[6], KUI_APP_LINE_CAP, "DNS lookup and Internet access were not tested");
    snprintf(out->lines[7], KUI_APP_LINE_CAP, "Saved settings and console flash are unchanged");
    kui_w5500_session_end(&s);
    if(log) {
        log("Network connection test: %s", out->message);
        for(unsigned i = 0; i < out->line_count; ++i) log("%s", out->lines[i]);
    }
    if(progress) progress(out);
    return true;
}
