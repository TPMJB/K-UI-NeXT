# SD card in your browser

Open **Network → SD card in browser**, then start sharing. K-UI displays a LAN
address such as `http://192.168.1.20:8080/`. Open that address on a phone or PC
on the same network. Leave the sharing screen open and press **B** to stop.
Sharing is off when K-UI starts and stops before another storage job or game
launch can use the worker. It does not stay resident in a game.

The first version lists and downloads these exports:

| Browser address | SD directory |
| --- | --- |
| `/screenshots/` | `/KUI/screenshots` |
| `/reports/` | `/KUI/reports/support` |
| `/saves/` | `/KUI/exports/saves` |

Only BMP/PNG/PPM screenshots, TXT/JSON/ZIP/LOG/MD reports (plus their optional
BMP screenshot) and VMS/VMI/DCI/VMD/ZIP
save exports are offered. Live virtual cards and their journals are outside
these roots. Incomplete `.part`, `.tmp` and `.kui-part` paths are refused,
including unpublished report directories. Exporting a card into the dedicated
save directory is an explicit operation separate from downloading it.

This is a read-only LAN service with no login. Anyone who can reach the shown
address while it is enabled can download these exports. The page includes no
remote assets or scripts. HTTP requests cannot upload, delete, rename, change
settings, browse the rest of the card, or install updates.

## Runtime contract

`kui_browser_run()` in `kui/browser.h` runs on the same sole storage worker as
FTP. Start/stop, status publication and cancellation follow FTP's existing
worker contract. The status includes `state`, `message`, `adapter`, `url`,
`ip`, `port`, `wifi`, `link`, and cumulative request/download/output counts.
The URL is valid while the status is READY. The browser and FTP are exclusive;
both reject a nested start before touching the adapter or mounted media.

The existing gateway discovers a W5500 or the Wi-Fi board, acquires/maintains
its address, mounts the boot-selected card and clocks network work. The
existing SCI pin reservation remains enforced. A storage card occupying SCI
prevents use of the SCI network adapter; SCIF storage can be used with it.
There is no new network thread or filesystem owner. Starting browser sharing
does not start FTP listeners or create/read an FTP password file.

Two bounded HTTP connection slots reuse adapter sockets 3 and 4. The gateway
services each once per pass; Wi-Fi still uses the existing bounded SPI pump.
Each connection has a 2 KiB request limit, 16 KiB listing buffer and 4 KiB file
buffer. Sends are at most 1 KiB per pass; directory generation reads at most
eight entries per pass and displays six entries per page. A request scans at
most 1,024 directory entries, allows eight descendant levels and page indices
0–127. These limits intentionally avoid allocating a complete card catalogue.
Very large export folders should be split into subdirectories.

GET and HEAD requests use explicit Content-Length and close the connection
after each response. Request bodies, chunked uploads, request pipelining,
encoded separators, traversal and ambiguous malformed requests are refused.
Partial requests have a 15-second absolute deadline; stalled transfers stop
after 60 seconds, and TCP drain/close has a 10-second bound. Cancel or a lost
connection releases every open file/directory before the gateway unmounts.

## Validation

- `tests/test_browser.c`: the real HTTP parser and service with deterministic
  bounded socket/card operations. Partial input/output; traversal and double
  encoding; request/header bounds; GET/HEAD lengths; escaped, paginated
  listings; binary/empty downloads; cancellation; peer disconnect; input
  timeout; delayed acknowledgement; read errors; cleanup.
- `tests/browser_image.c` and `tests/test_browser_images.py`: real TCP clients
  through the existing W5500 model and Wi-Fi firmware bridge model, with real
  FatFs on a sparse FAT32 image. Ready follows acknowledged Wi-Fi listeners;
  every open handle is accounted for; nested FTP/browser starts are refused;
  the entire card SHA-256 must remain unchanged after sharing stops. Then the
  existing FTP gateway is started on each adapter and its normal login,
  upload and download behavior is checked.
- Existing `test-ftp` and `test-ftp-cleanup` retain FTP protocol and socket
  cleanup coverage. Physical console/Wi-Fi/W5500 acceptance remains necessary.

Run host tests with `ASAN_OPTIONS=detect_leaks=0` as for the other runtime
network tests.
