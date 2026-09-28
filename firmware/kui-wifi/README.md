# K-UI Wi-Fi firmware

Firmware for a **Seeed XIAO ESP32-C5** (dual-band Wi-Fi 6) wired to the
Dreamcast's SCI port. It gives K-UI a network connection the way the W5500
does, over Wi-Fi instead of a cable. It also builds for the XIAO ESP32-C6
(2.4 GHz only).

**Status: first version, not yet tried on hardware.** The board side is
complete and host-tested, and so is K-UI's side: its driver, the FTP server
over the board and a Wi-Fi page to choose a network (see
[Wi-Fi](../../docs/wifi.md)). The board can be set up and tested on its own
from a computer first, which is worth doing before any soldering.

## What it does

- Joins your Wi-Fi network (WPA2/WPA3, 2.4 or 5 GHz), remembers it, and
  rejoins by itself after a power cut or a dropped connection.
- Runs the TCP/IP stack itself. The Dreamcast opens up to eight numbered
  sockets on it, much as it does on the W5500, so the FTP server can run
  over either.
- Also offers the Dreamcast a network list, joining, name lookups, the time
  of day (set from the internet), and firmware updates, so once it is
  installed the board never needs to come out again.
- A USB console for setting it up and checking it from a computer.

The link to the Dreamcast is described in [PROTOCOL.md](PROTOCOL.md); the
wiring is in [the SCI connector plan](../../docs/sci-connector.md).

## Loading it the first time

You need the board, a USB-C data cable and `esptool` (`pip install esptool`).

1. Download the `kui-wifi-esp32c5` artifact from the latest successful
   **Wi-Fi firmware** run in the repository's Actions tab, and unzip it.
2. Plug the board into the computer. On Linux it appears as `/dev/ttyACM0`.
3. Write the whole image:

   ```
   esptool --chip esp32c5 --port /dev/ttyACM0 write-flash 0x0 kui-wifi-esp32c5.bin
   ```

   Older esptool versions spell it `esptool.py ... write_flash`. If esptool
   cannot connect, hold the board's **B** (boot) button while plugging it
   in, then try again.

`kui-wifi-esp32c5-update.bin` in the same download is the app on its own,
for updates sent from the Dreamcast later. `SHA256SUMS` lists checksums.

## Testing it on the bench

Attach the antenna first: the board has no antenna of its own, and the one
in the box is reportedly 2.4 GHz only, so use a dual-band one for 5 GHz.

Open the board's console, for example with
`python -m serial.tools.miniterm /dev/ttyACM0` or `tio /dev/ttyACM0`, and
press Enter for the `kui-wifi>` prompt.

```
scan                            networks in range, with band and signal
join "My network" password      join and save (quote names with spaces)
status                          state, band, channel, signal, addresses, clock
band 5                          5 GHz only; `band auto` for both, `band 2.4`
forget                          disconnect and forget the saved network
reboot                          restart the board
```

`status` should show **online**, a 5 GHz channel (36 or above) and an
address. From the computer, `ping` that address (or `kui-wifi-xxxx`, the
name shown to your router). After a `reboot` it should come back online by
itself. The console works the same with the board installed, as long as its
USB-C port can be reached.

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

## Licence

MIT (see [LICENSE](LICENSE)), unlike the rest of K-UI (GPL-3.0). The
firmware is linked with Espressif's closed-source Wi-Fi libraries, which GPL
code cannot be combined with. The link protocol code in
`components/kwlink` is shared with K-UI, which can include MIT code. No
DreamShell code is used.
