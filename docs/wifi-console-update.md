# Update the Wi-Fi adapter from K-UI

K-UI can send a firmware image from the card to the adapter through the
existing SCI wiring. The installed K-UI Wi-Fi 0.1.1 firmware already supports
this; no USB connection or extra wire is needed. The radio can be offline.

## Install the console updater

Back up the card's KUI folder and merge the supplied pack's KUI folder onto
the card root, including the matching runtime and Apps. Keep the existing
boot disc and card filesystem. The pack includes the C5 update image and its
checksum in KUI/firmware.

Stop FTP before opening Network. In Network, START opens Wi-Fi; START on the
Wi-Fi page checks the image on the card. The confirmation shows the adapter,
new firmware version, file size and path. A installs that checked image;
B cancels. Saved Wi-Fi settings remain on the adapter.

K-UI checks the full file again after confirmation, writes the adapter's
other app slot, validates it, restarts only the adapter and waits for the
expected firmware to answer. The console stays in K-UI. Progress appears
while transferring. B can stop before activation; cancellation is deferred
once firmware activation begins. Keep the console powered during activation
and restart.

After success, inspect the adapter in Network and try FTP again. Measure
the same upload as before, download it to a different computer path, then
compare the original and downloaded SHA-256 digests.

## Future firmware images

For a C5 adapter, put these two files in /KUI/firmware:

- kui-wifi-esp32c5-update.bin
- kui-wifi-esp32c5-update.bin.sha256

C6 uses the corresponding esp32c6 filenames. Use the app-only update image;
the complete USB image is refused. The sidecar is exactly the image's
64-character SHA-256 digest, optionally ending in LF or CRLF. Firmware
downloads include it; it detects file corruption, not authorship.

The updater checks the chip, K-UI app identity, image size, checksum, and
adapter identity. A changed image or adapter requires a new check and
confirmation. Transfer errors leave the currently running firmware in use;
K-UI does not activate an incomplete upload. If activation cannot be confirmed,
follow the on-screen instruction to power-cycle the console and inspect the
adapter. Trial firmware rolls back on a later restart if it never reaches
the Dreamcast link. Initial programming and recovery from an adapter that
cannot answer still use [USB](wifi-flash-arch.md).

The firmware-update protocol is documented in
[PROTOCOL.md](../firmware/kui-wifi/PROTOCOL.md). The card and adapter must use
separate available buses, as they do with SCIF storage and SCI Wi-Fi.
