# K-UI Wi-Fi link protocol, version 1

How K-UI on the Dreamcast (the **host**, SPI master) talks to the Wi-Fi
board's firmware (the **bridge**, SPI slave) over the SH-4's SCI port. The
bridge keeps the TCP/IP stack; the host opens numbered socket slots on it,
much as it does with the W5500.

All multi-byte fields are little-endian (both CPUs are).

## Wires

| Line | Direction | Meaning |
| --- | --- | --- |
| SCLK, MOSI, MISO | | SPI mode 3 (clock rests high), MSB first, up to 12.5 MHz |
| CS | host → bridge | active low |
| READY | bridge → host | **toggles** each time the bridge has a transfer armed |
| RESET | host → bridge | optional; low for 5 ms or more resets the link, 2 s or more restarts the board |

The bridge can only take part in a transfer it prepared in advance, so it
flips READY every time it arms one. The host remembers the level it saw
before its last transfer and starts the next one only once READY has changed
from it. A missed pulse is therefore impossible: polling is enough. When the
host does not know the level (at start, or after a timeout) it takes the
current level as armed; a transfer that turns out to be unarmed returns no
valid frame and is simply repeated.

## Transfers

Every transfer is full duplex: the host clocks L bytes and each side sends
one frame while receiving the other's. L is a multiple of 4, from 16 to 4096,
and chosen by the host (see Sizing). Bytes after a frame are padding.

## Frame header (16 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | magic `K` `W` (0x4B 0x57) |
| 2 | 1 | flags |
| 3 | 1 | seq: this frame's number (DATA frames only) |
| 4 | 1 | ack: seq of the last in-order frame received (valid with ACK) |
| 5 | 1 | reserved, 0 |
| 6 | 2 | len: payload bytes after the header (at most 4080) |
| 8 | 2 | window: host = payload capacity promised for the bridge's next frame; bridge = bytes still waiting after this frame |
| 10 | 2 | session |
| 12 | 4 | CRC-32 (IEEE 802.3) of bytes 0–11 followed by the payload |

Flags: 0x01 DATA (a numbered payload follows), 0x02 ACK, 0x04 SYNC (host:
start a new session), 0x08 BRIDGE (set on every bridge frame), 0x10
NOSESSION (bridge: no session; the host must SYNC).

A frame whose magic, direction flag, length or CRC is wrong is ignored.

## Sessions

The host picks a random non-zero 16-bit session and sends header-only frames
with SYNC until a bridge frame carries that session. The bridge, on any SYNC,
closes every socket, clears its queues, adopts the session and numbers its
frames from 0 again; so does the host once the bridge has answered. A bridge
that has restarted answers with NOSESSION or another session, which tells
the host that every socket is gone.

## Reliable delivery

Only DATA frames are numbered. Each side keeps up to two frames it has sent
and not yet seen acknowledged, and accepts only the frame numbered
`expected`; anything else is dropped (the ack still says what arrived). An
answer to a frame can come back at the earliest one transfer later, so a
frame still unacknowledged two transfers after it went out is taken as lost:
the sender goes back and sends it and every frame after it again. With no
new data a side sends a header-only frame, which still carries ack and
window.

The bridge also knows how many bytes each transfer clocked. If the host
clocked fewer than the bridge's frame, that frame was cut short and is sent
again at once. The host sees such a frame's `len` in its (unchecked) header
and clocks enough for it next time.

## Sizing

- Each host frame's window promises a payload capacity for the bridge's
  next frame. The bridge builds each new frame after a transfer, to at most
  the capacity in the latest host header it received intact.
- The host announces the bridge's latest hint (its window), at least 112 and
  at most 4080.
- In each transfer the host clocks at least: its own frame; 16 plus the
  larger capacity it announced in its previous two frames (in case the last
  one did not arrive); and 16 plus the length of any bridge frame it saw cut
  short, until that frame arrives whole. Idle polling therefore costs 128
  bytes, about 80 µs at 12.5 MHz.

## Messages

A payload is a sequence of whole messages:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | type |
| 1 | 1 | sockets: slot (bits 0–2) and generation (bits 3–7); otherwise 0 |
| 2 | 2 | body length |
| 4 | n | body |

The host picks a new generation each time it opens a slot. Both sides drop
socket messages for a generation that is not the slot's current one, so
nothing from an earlier connection on a slot can be mistaken for the new one.

### Host to bridge

| Type | Name | Body |
| --- | --- | --- |
| 0x01 | HELLO | u16 protocol (1), u16 0 |
| 0x02 | WIFI_STATUS_GET | none |
| 0x03 | WIFI_SCAN | none |
| 0x04 | WIFI_JOIN | u8 band mode (0 keep, 1 2.4 GHz only, 2 5 GHz only, 3 both), u8 save, u8 ssid length, u8 password length, ssid, password |
| 0x05 | WIFI_LEAVE | u8 forget the saved network |
| 0x06 | WIFI_BAND | u8 band mode (1 2.4 GHz only, 2 5 GHz only, 3 both), kept across restarts; the WIFI_STATUS that follows shows it |
| 0x07 | ECHO | any bytes; answered by ECHO_R with the same bytes (the host checks a clock rate with large frames) |
| 0x10 | SOCK_OPEN | u8 kind (1 TCP listen, 2 TCP connect, 3 UDP), u8 flags (0x01 no delay, 0x02 keep-alive), u16 local port, u8 ip[4], u16 remote port, u16 keep-alive seconds, u32 receive credit |
| 0x11 | SOCK_CLOSE | u8 how (0 abort now, 1 send what is queued, then FIN) |
| 0x12 | SOCK_SEND | TCP: data. UDP: u8 ip[4], u16 port, data |
| 0x13 | SOCK_CREDIT | u32 more bytes the host can receive on this slot |
| 0x20 | DNS | u8 tag, u8 name length, name |
| 0x21 | TIME | none |
| 0x30 | OTA_BEGIN | u32 image size, u8 SHA-256[32] |
| 0x31 | OTA_DATA | u32 offset, data (in order) |
| 0x32 | OTA_END | none |
| 0x3f | REBOOT | none |

### Bridge to host

| Type | Name | Body |
| --- | --- | --- |
| 0x81 | HELLO_R | u16 protocol, u8 slots, u8 chip (5 = ESP32-C5, 6 = ESP32-C6), u16 max payload, u16 0, u8 MAC[6], u8 version length, version text |
| 0x82 | WIFI_STATUS | u8 state, u8 band (0, 2 or 5), u8 channel, i8 RSSI, u8 ip[4], mask[4], gateway[4], dns[4], bssid[6], u8 band mode, u8 last disconnect reason, u8 saved, u8 ssid length, ssid |
| 0x83 | WIFI_SCAN_R | u8 status, u8 count, then per network: u8 channel, i8 RSSI, u8 security (0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA3, 5 enterprise, 6 other), u8 ssid length, bssid[6], ssid |
| 0x84 | WIFI_JOIN_R | u8 status (0 accepted) |
| 0x85 | ECHO_R | the ECHO's bytes |
| 0x90 | SOCK_STATE | u8 state, u8 error, u16 local port, u8 ip[4], u16 remote port |
| 0x91 | SOCK_TXCREDIT | u32 more bytes the host may send on this slot |
| 0x92 | SOCK_DATA | TCP: data. UDP: u8 ip[4], u16 port, data (one datagram) |
| 0xa0 | DNS_R | u8 tag, u8 status (0 found), u8 ip[4] |
| 0xa1 | TIME_R | u8 status (0 synchronized), u8 0, u16 milliseconds, u32 seconds low, u32 seconds high (Unix time, UTC) |
| 0xb0 | OTA_R | u8 phase (1 begin, 2 data, 3 end), u8 status (0 ok, 1 out of order or too big, 2 flash error, 3 checksum mismatch, 4 not a valid image, 5 could not select it), u16 0, u32 bytes written |
| 0xbf | REBOOT_R | u8 status; the bridge restarts about 300 ms later |

Wi-Fi states: 0 no network saved, 1 connecting, 2 associated (waiting for an
address), 3 online, 4 wrong password, 5 network not found, 6 connection lost
(retrying).

Socket states use the W5500's numbers: 0x00 closed, 0x14 listening, 0x15
connecting, 0x17 established, 0x18 closing (FIN queued), 0x1c peer closed,
0x22 UDP. Errors: 0 none, 1 refused, 2 timed out, 3 reset, 4 unreachable,
5 out of memory, 6 port in use, 7 Wi-Fi offline, 8 invalid request, 9 other.

## Sockets

Eight slots. A listening slot takes the first connection to its port, as a
W5500 socket does: several slots may listen on one port, and a connection
that arrives when none is listening is refused. The bridge reports a peer's
close (0x1c) or a closed slot only after handing over all data received
before it.

Flow control is by credit in both directions. SOCK_OPEN carries the host's
initial receive credit and SOCK_CREDIT adds to it; the bridge never sends
more SOCK_DATA than that. Once a slot can send, the bridge grants transmit
credit with SOCK_TXCREDIT as its buffer empties; the host never sends more.
A UDP datagram costs its payload plus 8 bytes of credit either way. Credit
from an earlier connection on a slot does not carry over.
