# Wi-Fi performance test: current K-UI 1.7 and C5 0.1.2

The first physical C5 test succeeded on 5 GHz channel 161 with signal
-56 dBm, SCI at 12.5 MHz with DMA, and no READY wire. FTP received one
16 MB file at 66 KiB/s; the screen reported card 930 KiB/s and network
71 KiB/s. This establishes a working connection and upload, not a completed
integrity or sustained-load test.

This experimental build targets that six-wire installation. Keep the existing
SCIF storage, card filesystem, boot disc and SCI network wiring. Install the
matching runtime and Apps together. The separate 2048-sector runtime does not
contain these Wi-Fi changes.

## Changes

- A validated no-READY session uses a shorter transfer gap with error/stall
  backoff. The wired READY path keeps its original bounded wait. Initial
  discovery and reset recovery stay conservative.
- C5 firmware 0.1.2 uses 8 KB instead of 4 KB receive/transmit rings per open
  socket. A production-protocol TCP test moved the same 256 KB stream in
  68 transfers instead of 129, verifying every byte and its CRC. Physical
  performance depends on the bus, card, radio and scheduling.
- Firmware disables modem sleep for lower receive latency. This uses more
  power than the previous radio setting.
- Band changes check the Wi-Fi driver's result before saving the setting,
  clear the old connection status, and reconnect the saved network. A rejected
  change retains the previous allowed band rather than reporting success.
- FTP says "no overlap (Wi-Fi)" instead of "no DMA (Wi-Fi)". The Wi-Fi bus
  can use SCI DMA; the FTP path processes network and storage in turn.

## Update the existing board

Stop FTP, shut down and disconnect the board's Dreamcast power and signal
connections, then attach USB. The XIAO's 5V pin is USB VBUS: do not connect
USB and Dreamcast 5V together.

The existing 0.1.1 image you just installed uses app slot ota_0 at **0x20000**.
For that installation, writing the app-only image to the same slot preserves
the NVS partition containing the saved network and band:

```sh
export KUI_WIFI_PORT=/dev/ttyACM0
sha256sum -c SHA256SUMS
python -m esptool --chip esp32c5 --port "$KUI_WIFI_PORT" --baud 460800 \
  write-flash 0x20000 kui-wifi-esp32c5-update.bin
python -m serial.tools.miniterm --raw --eol CR "$KUI_WIFI_PORT" 115200
```

Run `version` and `status` inside `kui-wifi>`. Expect firmware **0.1.2**
and the saved network to reconnect. Exit miniterm with Ctrl+] and unplug
USB before restoring Dreamcast connections.

For a fresh board, use the complete `kui-wifi-esp32c5.bin` at **0x0** as in
[the Arch guide](wifi-flash-arch.md), then join the network. The app-only
method above is for the known ota_0 installation; it is not a general method
for boards whose active OTA slot or partition table has changed.

Back up the card's KUI folder. Copy the performance pack's supplied KUI files
and all matching Apps together, retaining your Games, dumps and preferences.
The build ID distinguishes the test from the current 1.7 build.

## Switch bands on the Dreamcast

1. Stop FTP with B.
2. Open Network and press START for Wi-Fi.
3. Highlight the first row, **Bands**, then use LEFT/RIGHT to select
   2.4 GHz only, 5 GHz only, or both. A successfully applied change is saved
   on the board and reconnects the saved network.
4. If the router uses different network names on its two bands, select the
   matching network from the list and join it. Restricting a saved 5 GHz
   network to 2.4 GHz does not change its name to your 2.4 GHz network.
5. Return to Network, inspect the adapter with A, then restart FTP with Y.

Compare the actual band/channel in the inspection result, not just the
network's name. Auto allows either band and does not force the stronger one.
The USB console equivalents are `band 2.4`, `band 5`, and `band auto`, followed
by `status`. A band restriction and an actual association are separate: an
offline board has no current radio channel to confirm.

## First performance and integrity check

Use the same source file, client and location as the first transfer.
Upload one reasonably sized file (16-64 MB), then download it to a different
computer path and compare SHA-256 hashes. On Arch:

```sh
sha256sum /path/to/original.bin /path/to/downloaded-copy.bin
```

The two digests must match. Record the Dreamcast's completed-transfer timing
line, the adapter inspection (bus speed and READY/pacing), and any errors.
Repeat once on each band if desired. Both upload and download matter because
they exercise opposite link directions. If the adapter fails its echo check,
stops answering, or produces a mismatched file, stop the test and restore the
previous runtime/Apps and firmware.

READY remains an optional next step: D1 goes to GPIO5, VA1 **RA101 pin 2**.
The manual/schematic mapping is documented in [the wiring guide](sci-connector.md).
Check actual board orientation before soldering; no extra wire is required
to try this pack.

## Sources

- [Espressif C5 power-saving documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c5/api-guides/wifi.html#esp32-c5-wi-fi-power-saving-mode)
- [Espressif C5 band-mode API](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c5/api-reference/network/esp_wifi.html#_CPPv422esp_wifi_set_band_mode16wifi_band_mode_t)
- [Espressif flash write commands](https://docs.espressif.com/projects/esptool/en/latest/esp32c5/esptool/basic-commands.html)
- Firmware partition table: `firmware/kui-wifi/partitions.csv`.
