# FTP server (W5500 on the SCI port)

K-UI can share the SD card over your home network with FTP, so games,
music and pictures can be copied to and from a computer without taking the
card out. It needs a WIZnet W5500 Ethernet module wired to the console's
SCI port (a modification). It is new since K-UI 1.5.1 and has not yet been
tried on a console.

It is independent K-UI code. The W5500 driver is written from WIZnet's
W5500 datasheet and uses KallistiOS's SCI driver (`dc/sci.h`) only to move
bytes; it does not use KallistiOS's own W5500 network driver, which also
probes the serial port the SD adapter uses. No DreamShell code is involved.

## The hardware

The SD adapter stays where it is, on the serial port (SCIF). The W5500 goes
on the SH-4's other serial interface, SCI, run as SPI:

| W5500 | Console |
| --- | --- |
| MOSI | SCI TXD |
| MISO | SCI RXD |
| SCLK | SCI SCK |
| SCSn (chip select) | SH-4 port A, pin 7 (PA7) |
| 3.3 V and GND | 3.3 V and ground |
| RSTn | held high (or to a reset line) |

Chip select on PA7 is how KallistiOS's SCI driver drives it on a retail
console; K-UI uses that driver unchanged. The W5500's interrupt pin is not
used.

K-UI looks for the W5500 only when asked (Network, or the FTP server),
never at start-up. It resets the chip, checks its version, and writes and
reads back 64 test patterns before using it. It starts at 12.5 MHz and, if a
pattern comes back wrong, tries 6.25, 3.125 and 1.5625 MHz. If every speed
fails, it says the W5500 was found but its wiring check failed.

The W5500 has no MAC address of its own. K-UI makes a locally administered
one from the console's unique ID, so it is the same every time (the router
sees one device).

## Using it

1. Connect the W5500 to your router with a network cable.
2. Open **Network** on Home and press **Y (FTP server)**.
3. K-UI finds the W5500, opens the SD card, waits for the cable link and asks
   the router for an address (DHCP). Then it shows:
   - the address, such as `ftp://192.168.1.50`;
   - the user (`kui`; any name works) and the password.
4. Connect an FTP client to that address on port 21 with that password.
5. **B** stops the server. Anything still being uploaded is discarded and
   connected clients are told the server is stopping. **A** starts it again;
   **B** again returns to Network.

The screen shows each connected client and what it is sending or receiving,
with progress, speed, the totals so far and the latest events. While the
server runs, the rest of K-UI waits (music already playing keeps playing).

### The password

The first start saves a random 8-digit password in `KUI/ftp-password.txt`;
later starts use the same one, so an FTP client can remember it. To choose
your own, edit that file on a computer: 1 to 32 letters, digits or symbols
on one line. Delete the file (with the File Manager, say) to get a new
random one. A wrong password is answered only after two seconds, and three
in a row close the connection.

This is plain FTP: the password and the files cross the network
unencrypted. Use it on your home network only. There is no FTPS or SFTP.

### Clients

- **FileZilla**: Host `192.168.1.50` (as shown), User `kui`, the password,
  Port 21. In the Site Manager, set Encryption to "Only use plain FTP
  (insecure)" to avoid the TLS question. FileZilla uses up to three
  connections (one to browse, two to transfer), which is exactly what K-UI
  serves; if you raised its limit, set "Limit number of simultaneous
  connections" to 3.
- **WinSCP**: File protocol FTP, Encryption "No encryption".
- **Windows File Explorer**: type `ftp://192.168.1.50` in the address bar.
- **macOS Finder** (Go, Connect to Server) can read but not write.
- Command-line clients (`ftp`, `lftp`, `curl`) work too. Passive mode is
  the usual setting and works through any home router; active mode (PORT)
  works too, but only back to the client's own address.

## What it does

- **Browse**: every folder and file on the card, with sizes and dates
  (`LIST`, `NLST`, `MLSD`, `MLST`, `SIZE`, `MDTM`). `LIST` and `NLST`
  accept `*` and `?` in the last name (`mget *.bin` in a command-line
  client), matching capitals either way as FAT does.
- **Download**, including resuming a partial download (`REST`).
- **Upload**: the file is written beside its target as
  `KUI-ftp-<n>.kui-part` and takes its name only when the client has sent
  all of it. If the upload stops (cancelled, connection lost, card full, or
  B on the console), the part file is removed and nothing else changes. An
  existing file of that name is replaced only once the new one is complete.
- **Rename and move** (`RNFR`/`RNTO`, also between folders), **delete**
  (read-only files included, as in the File Manager), **new folder** and
  **remove folder** (empty folders; clients delete the contents first).
- **One transfer per file**: a file being sent to one client cannot be
  replaced, renamed or deleted by another until it is done ("450 ... being
  transferred by another client").

### What is protected

As in the File Manager, K-UI's start-up files `KUI/runtime.kui` and
`KUI/apps/games/retail-boot.kui`, and the folders that hold them, cannot be
replaced, renamed or deleted over FTP. They can be downloaded, and other
files can be added to those folders. Update K-UI itself on a computer.

## Limits

- Names follow the File Manager's rules: FAT-safe UTF-8 names under 128
  bytes, whole paths under 384 bytes. Names outside them are listed but
  cannot be opened over FTP.
- Uploads cannot be resumed (`REST` before `STOR`, `APPE`): send the whole
  file again.
- Up to three clients at once; a fourth is told so ("421").
- IPv4 and DHCP only; no static address yet.
- File times are the console clock's local time; clients that read `MLSD`
  as UTC may show them shifted by your time zone.
- The SD card on the serial port sets the pace: it reads at about 0.7 MB/s
  and writes at about 1.1 MB/s, and the W5500's link adds its own time, so
  expect well under 1 MB/s (not yet measured on a console). A computer with
  a card reader is much faster for whole game libraries.
- A client that goes quiet for ten minutes, or does not log in within a
  minute, is disconnected. A transfer with no progress for a minute is
  stopped.
- The address lease is renewed at half its time. If the router refuses, or
  the lease runs out, the server stops and says why.

## Network app

**A (Inspect adapter)** and **X (Test network)** also look for a W5500 on the
SCI port when no Broadband or LAN adapter is found. Inspection reports the
chip, the SPI speed that passed the wiring check, the cable link and the MAC
address. The network test runs the same DHCP, address-conflict, gateway ARP
and ping checks as with a BBA (see [the connection test](network-connection-test.md)),
through the W5500's raw Ethernet socket.

## How it is built

- `src/core/w5500.c`: the W5500 driver: SPI frames, registers, sockets,
  TCP, UDP and MACRAW, over any SPI link (`struct kui_w5500_bus`).
- `src/dreamcast/w5500_sci.c`: that link on the console, through KOS's SCI
  driver; the four speeds and the MAC address from the console ID.
- `src/apps/network_w5500.c`: finding the chip, the cable link, DHCP through
  the existing network probe (raw frames on socket 0), lease renewal over
  UDP, and the Network app's inspection and test.
- `src/core/ftp_protocol.c`: commands, paths, `PORT`/`EPRT` and listing
  lines.
- `src/apps/ftp_server.c`: the server loop on the storage worker: sessions,
  data connections, the card through FatFs, and the status the screen draws.
  It uses the W5500's own TCP sockets: 0-2 carry data (socket 0 is used for
  DHCP first), 3-6 listen for control connections.
- `src/core/shell.c`, `src/dreamcast/shell_draw.c`, `src/dreamcast/main.c`:
  the FTP Server page, Y on the Network page, and worker action 64.

## Validation

- `test-w5500`: the driver against `tests/w5500_model.c`, a W5500 for host
  tests whose TCP sockets are real localhost sockets and whose raw socket
  reaches a small network with a DHCP server: reset and version, the wiring
  check (including a noisy bus and no chip), buffer sizes, TCP accept,
  receive and send through wrapping buffers, one SEND at a time, both ways of
  closing, resets, refused connections, active connections, UDP, and DHCP,
  address conflict, gateway ARP and ping over MACRAW.
- `test-network-w5500`: finding the chip, slower speeds on a noisy bus, the
  wiring fault and no-chip reports, no cable, the connection test with good,
  silent, conflicting and refusing DHCP servers, and lease renewal.
- `test-ftp`: command parsing, path resolution (`.`, `..`, limits, unsafe
  names), `PORT`/`EPRT`, timestamps, listing lines and wildcards.
- `test_ftp_images.py`: the whole server on the W5500 model with real FatFs
  on FAT32 and exFAT images, driven by Python's `ftplib`: login and wrong
  passwords, folders, uploads and downloads of up to 20 MB compared by
  SHA-256, resume, replace, listings and wildcards, active mode, `EPSV` and `EPRT`,
  renames and moves, deletes, protected files, unsafe names, a closed data
  connection, `ABOR`, an upload cut off by a reset, files in use, the
  three-client limit, a full card, stopping with a client connected, no
  W5500 and an unusable password file. `fsck` checks every image, and on
  FAT32 mtools reads the uploads back independently. Every run must leave no
  file or folder open.
- `test-shell`: Y on Network, Stop, restart and back, and every state of the
  FTP page.

These run on the host. They check the protocol, the driver's register and
socket handling, and the card; they cannot check the SCI wiring or the
W5500 itself.

## Console test

Use a card whose contents are backed up.

1. Open **Network**, press **A**: it should report the W5500, an SPI speed
   and "Link: 100 Mbit/s" with the cable connected. Photograph it.
2. Press **X**: the connection test should get an address and ping the
   router.
3. Press **Y**: note the address and password. From a computer, connect
   with FileZilla, list the root and `/Games`, and download a small file.
4. Upload a folder with a few files, then a large file (a game track), and
   note the speed on the console screen.
5. Rename and delete the uploaded files, and check that deleting
   `KUI/runtime.kui` is refused.
6. Stop the server with **B** during an upload: the client should be told,
   and no part file should remain.

Photograph anything that looks wrong, and save a report from Diagnostics
afterwards (the FTP server's events are in the log).
