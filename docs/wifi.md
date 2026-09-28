# Wi-Fi (the Wi-Fi board on the SCI port)

K-UI can use a small Wi-Fi board inside the console instead of a network
cable: a Seeed XIAO ESP32-C5 (dual-band, 2.4 and 5 GHz) running K-UI's own
firmware, wired to the SH-4's SCI port. The FTP server and the Network app
work over it as they do over a W5500. It is new and **has not yet been tried
on a console**: the board arrives, gets its firmware, is tested on the bench,
and only then goes in.

- Wiring: [the SCI connector plan](sci-connector.md).
- The board's firmware, and how to load it from a computer:
  [firmware/kui-wifi](../firmware/kui-wifi/README.md).
- The link between the two: [PROTOCOL.md](../firmware/kui-wifi/PROTOCOL.md).

The board runs Wi-Fi, DHCP and TCP/IP itself. K-UI talks to it over SPI, one
transfer at a time, and opens up to eight socket slots on it, much as it does
with the W5500's sockets. The board keeps the network it joined and rejoins
by itself after a power cut, so it is set up once.

## Setting it up

1. Load the firmware and try it from a computer first (its README says how).
2. Wire it to the SCI port and power it (see the connector plan).
3. On the console: **Network**, then **START (Wi-Fi)**. K-UI looks for the
   board and lists the networks in range, strongest first, with their band,
   channel, signal and security.
4. Choose your network with **A** and type its password with the on-screen
   keyboard: **Y** switches between lowercase, uppercase and symbols, **X**
   deletes, **START** or **DONE** joins. An open network joins at once.
   **Other network** at the bottom of the list is for a hidden network: type
   its name, then its password (none for an open one).
5. The page shows "Online on ... as 192.168.x.y" once the router has given
   the board an address. The board saves the network.

Also on the page:

- **Bands** (the first row): **LEFT/RIGHT** chooses 2.4 and 5 GHz, 5 GHz
  only or 2.4 GHz only; the board keeps the choice. The XIAO ESP32-C6 has no
  5 GHz radio and stays on 2.4 GHz.
- **X** scans again.
- **Y** forgets the saved network (after a confirmation): the board
  disconnects, drops the password and stays off until you join again.

The password goes to the board over the SCI wires and is stored only there
(in its flash, as ESP-IDF stores Wi-Fi settings). K-UI wipes its own copies
once the join has been sent and never writes it to the SD card or the log.

## Using it

- **Network, A (Inspect adapter)**: when no Broadband, LAN adapter or W5500
  is found, K-UI looks for the Wi-Fi board and reports its chip, firmware,
  the SPI speed and chip select, whether its READY line works, the Wi-Fi
  network, band, channel, signal, address, gateway, DNS and its MAC.
- **Network, X (Test network)**: waits (up to 20 seconds) for the board to
  be online, looks up `pool.ntp.org` through it, and reads the time the board
  set from the internet. It passes when the name lookup works.
- **Network, Y (FTP server)**: see [the FTP server](ftp.md). With the Wi-Fi
  board, the server waits for Wi-Fi instead of a cable, and the board keeps
  its own address; if Wi-Fi drops, the server keeps running and says so, and
  shows the new address if the router gives the board another one.

## How K-UI finds it

Only when asked (never at start-up), and after a W5500: a W5500's probe is
harmless to the board, while the board's frames are not meant for a W5500.
The board's chip select may be on **GPIO6** (the connector's network select)
or **GPIO7** (RA101, where a W5500's usually goes); K-UI tries GPIO6 first.
At each, it starts at 12.5 MHz: it starts a session, asks for the board's
HELLO, then sends four 2 KB echoes and checks every byte and checksum that
comes back. On errors it tries 6.25, 3.125 and 1.5625 MHz. With no sign of a
board at 12.5 MHz (no answer, no READY change) it moves on to the other chip
select at once, so a missing board costs about half a second.

Each transfer waits for the board's READY line (GPIO5) to change, which it
does each time it is ready for another transfer; without it (a missing wire)
transfers still work after a 20 ms wait each, very slowly, and the
inspection says "READY never changed (GPIO5)".

Every frame carries a CRC-32; damaged or missed frames are sent again, so a
marginal wire makes things slower, not wrong. If the board stops answering
for three seconds, whatever is using it stops and says so.

## Troubleshooting

| The page says | Likely cause |
| --- | --- |
| No Wi-Fi board answered on the SCI port | Not powered (5 V), no firmware, the chip select on neither GPIO6 nor GPIO7, or MISO/MOSI swapped |
| The Wi-Fi board answered, but not reliably at any speed | A bad clock, MOSI or MISO connection, or long wires; check the solder points |
| The Wi-Fi board's firmware speaks protocol N | Update the board's firmware (from a computer, for now) |
| ... refused the password | Wrong password: choose the network again |
| ... not found; the board keeps looking for it | Out of range, a 5 GHz network with "2.4 GHz only" set, or a typo in a typed name |
| Not online on ... after 30 seconds | The router did not answer: check it, then X to scan again |

The board's USB console (see its README) shows the same status from a
computer, with more detail, if its USB-C port can be reached.

## How it is built

- `firmware/kui-wifi/components/kwlink`: the link library shared with the
  firmware (MIT): frames, sessions, retransmission (`kwlink.c`) and the
  Dreamcast's side of the messages and socket slots (`kwhost.c`).
- `src/apps/network_wifi.c`: finding the board and checking the link at each
  rate, waiting for Wi-Fi, scan, join, bands, forget, the socket slots for the
  FTP server (`kui/net.h`), the Network app's inspection and test, and the
  Wi-Fi page's jobs. `src/core/wifi_text.c` has its words.
- `src/dreamcast/sci_port.c`: the SCI port as an SPI bus with chip selects
  on GPIO7 (through KallistiOS's SCI driver) and GPIO6, and READY on GPIO5.
  `src/dreamcast/wifi_sci.c`: the board's transfers through
  `sci_spi_rw_data`, paced by READY.
- `src/apps/ftp_server.c`: the FTP server over `kui/net.h`, which the W5500
  (`src/apps/network_w5500.c`) and the Wi-Fi board both provide.
- `src/core/shell.c`, `src/dreamcast/shell_draw.c`, `src/dreamcast/main.c`:
  the Wi-Fi page, START on the Network page, the keyboard's symbols, and
  worker actions 65 to 68.

## Validation

- `test-network-wifi`: the driver against `tests/wifi_model.c`, which runs
  the firmware's own bridge core (`firmware/kui-wifi/main/bridge.c`) behind a
  simulated SPI bus with its READY line and a stand-in for the board's Wi-Fi,
  with real sockets on the computer: finding the board on either chip select,
  a slower rate when the fast one damages frames, damaged at every rate, no
  board, no READY wire; status, scan, refused and accepted joins, a too-short
  password, bands, forget; the socket slots with a real TCP client; a board
  that stops answering; the Network app's inspection and test; the Wi-Fi
  page's jobs.
- `test_ftp_images.py`: the whole FTP suite again over the Wi-Fi board model
  on a FAT32 image, then Wi-Fi dropping and coming back on another address
  and the board restarting mid-session; and the server's messages with no
  adapter and with the board but no network set up.
- `firmware/kui-wifi/test`: the link and bridge on their own (see the
  firmware's README).
- `test-shell`: the Wi-Fi page's controls and every state it draws.

None of this can check the wiring, the ESP32-C5's SPI timing or its radio.

## Console test

When the board is in:

1. **Network, START**: the page should find the board ("XIAO ESP32-C5,
   firmware ...") and list networks. Photograph it.
2. Join your network; it should say "Online on ... as ...".
3. **B**, then **A (Inspect)**: note the SPI speed and chip select, and that
   READY is "working".
4. **X (Test network)**: the name lookup and the network time should appear.
5. **Y (FTP server)**: connect from a computer and copy a large file both
   ways; note the speed on the console screen.
6. Turn the router's Wi-Fi off for a minute: the FTP page should say the
   connection was lost, then that it is back.
