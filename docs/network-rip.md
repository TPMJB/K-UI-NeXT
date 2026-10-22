# Direct PC capture destination

The GD-Ripper can write its destination filesystem directly to a PC through a
native W5500 or Wi-Fi adapter. The PC stores a dedicated FAT32/exFAT **volume
image**. Captured track data does not pass through a temporary local-card dump.
The existing capture engine uses the remote volume for its files, journal,
manifests, Resume, and Verify operations.

This destination is a volume image rather than a shared PC folder. The protocol
does not implement SMB or NFS. The PC helper needs Python 3.10 or newer on a POSIX
host with `fcntl` locking and `pread`/`pwrite`; the example below uses Linux.

This backend has host loopback coverage with the actual capture engine and
passes Dreamcast cross-compilation. Real console adapter compatibility and
throughput still need hardware qualification.

## Prepare the PC volume

Create and format a new, dedicated image large enough for the intended dumps.
Use a direct FAT32/exFAT volume, with 512-byte sectors and no partition table.
For example, with Linux `fallocate` and `mkfs.exfat` installed:

```sh
fallocate -l 8G tyrfing-rips.img
mkfs.exfat tyrfing-rips.img
```

For FAT32 instead, use explicit geometry so the filesystem's declared sector
count matches the image file exactly:

```sh
mkfs.fat -F32 -g1/32 tyrfing-rips.img
```

The receiver refuses a filesystem whose declared capacity differs from the
image length; some default FAT32 formatting geometries round that count down.

These commands create the file and its filesystem. The K-UI helper itself never
formats, mounts, or resizes an image. An existing dedicated FAT32/exFAT image can
also be used. It must fit the protocol's 32-bit sector count.

Run the helper commands from the extracted package root or repository root,
where the `tools/` folder is present. The helper is included in the full
diagnostic, normal installation and SD update packages.

Bind that image to a new identity and secret. Replace the example address with
the PC's IPv4 address reachable from the Dreamcast:

```sh
python3 tools/network_rip_server.py init \
  --image tyrfing-rips.img \
  --identity tyrfing-rips.json \
  --key tyrfing-rips.key \
  --host 192.168.1.20 --port 7412 \
  --config network-rip.cfg
```

`init` validates the image and creates three new files without changing the
image's contents. It refuses to overwrite existing output files. Keep the image,
identity, and key together so later sessions can use the same destination.

Copy `network-rip.cfg` to `/KUI/network-rip.cfg` on the local card. The file has
four required fields and no implicit destination:

```ini
host=192.168.1.20
port=7412
key=<64 hexadecimal digits from init>
volume=<32 hexadecimal digits from init>
```

The generated configuration contains the shared secret. Store the key and
configuration privately. Each session and each read/write request is
authenticated with HMAC-SHA256; dump contents are not encrypted. Use a trusted
network interface and permit the chosen TCP port from the console's network.

## Capture, Resume, and Verify

Start the receiver with the explicit local interface address:

```sh
python3 tools/network_rip_server.py serve \
  --image tyrfing-rips.img \
  --identity tyrfing-rips.json \
  --key tyrfing-rips.key \
  --bind 192.168.1.20 --port 7412
```

The helper prints `READY 7412` once the volume is locked and the listener is
ready. Leave it running during the operation. One receiver owns the image and
serves one console session at a time. Do not mount or edit the image while the
receiver is running; the advisory lock excludes other copies of this helper,
but external mount tools do not necessarily honor it.

Open **Disc Ripper > Start > Rip destination: local / PC** and press A to
select PC before starting the operation. Local storage remains the default;
the PC selection is for the current session. K-UI reads
`/KUI/network-rip.cfg` from the local card before connecting the native network
adapter and uses the authenticated remote volume as its capture destination.

Use New dump, Resume, and Verify as for a local destination. The destination
path and new folder label are interpreted inside the PC volume; manifests,
checkpoints and raw tracks are written there. The folder and saved-job browsers
still enumerate the local card. Switching destination clears any selected local
job; PC Resume/Verify use automatic matching-job discovery. They need the same
PC image and identity. If capture
catalogue checks are wanted, put the optional `/KUI/redump.db` and
`/KUI/tosec.db` files inside the remote volume before starting the receiver.

A disconnect, timeout, authentication error, or changed image stops the session.
There is no automatic reconnect or write retry. An interrupted write may have
reached the PC before its acknowledgement was lost. Reconnect explicitly and use
Resume to reconcile the capture journal; use Verify to check completed output.
The helper flushes accepted writes when the session ends, and acknowledges a
successful sync only after the host has completed `fsync`.

The identity file binds the destination UUID to its capacity and boot-sector
fingerprint. Replacing, resizing, or reformatting the image causes refusal.
Ordinary exFAT dirty and usage fields may change without invalidating identity.
Identity checks do not replace capture verification or a filesystem check.

## Read the captured files on the PC

Stop the receiver with Ctrl-C before mounting the image or extracting its files.
For example, Linux can mount the completed volume read-only:

```sh
mkdir -p captured-rips
sudo mount -o loop,ro tyrfing-rips.img captured-rips
```

Copy the desired capture folders from `captured-rips`, then unmount it:

```sh
sudo umount captured-rips
```

An image-aware FAT/exFAT extraction tool is another option. Unmount the image
before restarting the receiver for more captures or a later Resume/Verify.

## Wire protocol and checks

Frames have a 48-byte little-endian header (`<4sBBHQQIIIIII`), a bounded payload,
and a 32-byte HMAC-SHA256 tag. The header contains `KNR1`, version 1, operation,
request/response flag, random session ID, sequence, block, count, payload length,
status, CRC32, and a reserved zero field. CRC32 covers the header with its CRC
field zeroed plus the payload. HMAC covers the transmitted header and payload.

The server begins with a challenge containing its UUID, capacity, and a fresh
32-byte nonce; this initial frame alone has a zero authentication tag. The client
returns that nonce in an authenticated AUTH request. The authenticated response
confirms the UUID and capacity before any block I/O. Later READ, WRITE, SYNC, and
CLOSE requests require the same session ID and strictly increasing sequences.
READ/WRITE transfers are bounded to 32 sectors (16 KiB) per request. A complete
WRITE frame must pass bounds, CRC, and HMAC checks before any image write begins.

The receiver handles short host I/O, refuses symlink images, takes an exclusive
image lock, and checks the named file's inode, size, and boot identity before
operations. Malformed frames close the connection without stream recovery.
Run its local checks with:

```sh
python3 -m unittest discover -s tests -p 'test_network_rip_server.py' -v
python3 -m unittest discover -s tests -p 'test_network_rip_integration.py' -v
```

The integration checks compile the portable client with ASan/UBSan. With FatFs
dependencies and `mkfs.fat`, `mkfs.exfat`, `fsck.fat`, and `fsck.exfat` installed,
they also run interrupted capture, Resume, Verify, extraction, independent dump
verification, and filesystem checks over actual loopback TCP on both formats.
