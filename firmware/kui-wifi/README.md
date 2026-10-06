# K-UI Wi-Fi firmware

Firmware for a **Seeed XIAO ESP32-C5** (dual-band Wi-Fi 6) wired to the
Dreamcast's SCI port. It gives K-UI a network connection the way the W5500
does, over Wi-Fi instead of a cable. It also builds for the XIAO ESP32-C6
(2.4 GHz only).

**Status: an ESP32-C5 has joined 5 GHz Wi-Fi and transferred files over
SCI on a Dreamcast.** The first console upload measured about 65 KiB/s with
READY omitted. The first performance update reached 226 KiB/s on a console
upload, with 291 KiB/s measured in the network path. Firmware 0.1.3 keeps
the 8 KiB socket rings and disabled modem sleep, and arms the next SPI
frame before polling established sockets. Socket work can then run while
DMA waits for the next transfer, shortening the gap during which a host
without READY would find the adapter unarmed. The new timing still needs a
console measurement. K-UI includes the driver, FTP server and a Wi-Fi page
to choose a network (see [Wi-Fi](../../docs/wifi.md)). The board can be set
up and tested on its own from a computer before installation.

Changing bands now disconnects and rejoins the configured network, clears
the previous connection's displayed address/channel while reconnecting,
and saves the preference only if the Wi-Fi driver accepts it. This keeps
the same network name and password: if your router gives 5 GHz a different
name, select that name with `join "SSID" "password"` after `band 5`.

## What it does

- Joins your Wi-Fi network (WPA2/WPA3, 2.4 or 5 GHz), remembers it, and
  rejoins by itself after a power cut or a dropped connection.
- Runs the TCP/IP stack itself. The Dreamcast opens up to eight numbered
  sockets on it, much as it does on the W5500, so the FTP server can run
  over either.
- Also offers the Dreamcast a network list, joining, name lookups, and the
  time of day (set from the internet). K-UI's
  [console updater](../../docs/wifi-console-update.md) installs checked
  firmware from the card through the existing SCI link.
- A USB console for setting it up and checking it from a computer.

The link to the Dreamcast is described in [PROTOCOL.md](PROTOCOL.md); the
wiring is in [the SCI connector plan](../../docs/sci-connector.md).

## Loading it the first time

For Arch Linux, follow the [C5 flashing and bench-test guide](../../docs/wifi-flash-arch.md).

You need the board, a USB-C data cable and a current `esptool` with ESP32-C5
support. On Arch use its packages as described in the guide above; other
systems can use [Espressif's installation instructions](https://docs.espressif.com/projects/esptool/en/latest/esp32c5/installation.html).
Keep the board disconnected from the Dreamcast during this first USB test.
If already installed, disconnect its Dreamcast power and signal connections
before attaching powered USB: the XIAO's 5V pin is also USB VBUS.

1. Download the `kui-wifi-esp32c5` artifact from the latest successful
   **Wi-Fi firmware** run in the repository's Actions tab, and unzip it.
2. Plug the board into the computer. On Linux it appears as `/dev/ttyACM0`.
3. Write the whole image:

   ```
   esptool --chip esp32c5 --port /dev/ttyACM0 write-flash 0x0 kui-wifi-esp32c5.bin
   ```

   If esptool cannot connect, hold the board's **B** (boot) button while
   plugging it in, release it, then try again.

`kui-wifi-esp32c5-update.bin` in the same download is the app on its own,
for the console updater, not initial USB flashing. Its `.bin.sha256` sidecar
supplies the expected digest. `SHA256SUMS` lists checksums. The
protocol's rollback mechanism keeps an update only once the new firmware
has started and the Dreamcast has reached it over the link; otherwise the
board goes back to the firmware it had (see [PROTOCOL.md](PROTOCOL.md)).

## Testing it on the bench

Attach the included antenna to the board's U.FL connector while unpowered.
Start with a 2.4 GHz network. If a 5 GHz
network cannot be reached, check its channel and test with a known dual-band antenna.

Open the board's console, for example with
`python -m serial.tools.miniterm --raw --eol CR /dev/ttyACM0 115200`, and
press Enter for the `kui-wifi>` prompt.

```
band 2.4                        start with 2.4 GHz only
scan                            networks in range, with band and signal
join "My network" password      join and save (quote names with spaces)
status                          state, band, channel, signal, addresses, clock
band auto                       both bands; `band 5` for a separate 5 GHz test
forget                          disconnect and forget the saved network
reboot                          restart the board
```

`status` should show **online**, a channel and an address. From the computer,
`ping` that address. After a `reboot` it should come back online by itself;
reopen the terminal if USB disconnects. This tests the board's Wi-Fi, not
its SCI connection to the Dreamcast. Exit miniterm with **Ctrl+]** and unplug
USB before connecting the Dreamcast wiring and its 5V supply. For later USB
access to an installed board, disconnect those Dreamcast connections first.

## Building

With [ESP-IDF v5.5.5](https://docs.espressif.com/projects/esp-idf/):

```
cd firmware/kui-wifi
idf.py set-target esp32c5
idf.py build
idf.py merge-bin -o kui-wifi-esp32c5.bin    # in build/
```

The link and bridge code also builds on a normal computer. `make -C
firmware/kui-wifi/test` runs the tests: exactly-once, in-order delivery
through bit errors and cut-short transfers; and a simulated Dreamcast
driving real TCP and UDP sockets on localhost, with listening, connecting,
closes both ways, bands, echoes, a board reset and a firmware update. K-UI's
own tests also run this bridge core, behind K-UI's real driver and FTP
server (`tests/wifi_model.c` at the top of the repository).

The throughput regression moves 256 KiB through real localhost TCP sockets
and the production link protocol. It compares transfer counts with 4 KiB
and 8 KiB bridge rings, verifies every byte and the stream's CRC32, then
repeats with damaged frames and retransmission. This checks payload per
transfer, not radio, storage or physical SPI speed. The rings are allocated
when a socket slot is used; 8 KiB in each direction costs 16 KiB per used
slot, at most 128 KiB for all eight slots (64 KiB more than before).
The band-control transaction also has driver-failure tests: unsuccessful
disconnect/apply operations cannot publish or save the requested mode,
and retries resume after the operation; C6 rejects 5 GHz before disconnecting.

## Licence

MIT (see [LICENSE](LICENSE)), unlike the rest of K-UI (GPL-3.0). The
firmware is linked with Espressif's closed-source Wi-Fi libraries, which GPL
code cannot be combined with. The link protocol code in
`components/kwlink` is shared with K-UI, which can include MIT code. No
DreamShell code is used.
