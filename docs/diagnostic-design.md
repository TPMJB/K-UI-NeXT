# Diagnostic implementation decisions

This implements the first M1.0/M1.1 test vehicle. Passing host tests and compiling
are prerequisites to a console test; neither completes hardware acceptance.

## Ownership and lifetime

One 64 KiB-stack worker owns optical and SD/filesystem I/O. The main thread polls
the controller, draws an in-memory log, and exchanges requests under a mutex.
It never checks the drive or writes a log concurrently with the worker. KOS's
optional CD-ROM/ISO9660, dcload and serial debug output are disabled. Code and the
KOS minifont are in the executable, so the boot CD can be removed.

The drive adapter uses direct documented KOS firmware calls and PIO only. The
small upstream BSD bus-activation helper retains its source attribution. No
firmware patch, DMA, streaming command or shared CD wrapper is used.

Submission and polling share one deadline: 10 seconds for INIT, 5 seconds for a
TOC/read, then up to 1 second to confirm abort. INIT permits at most two additional
attempts, only for disc-change sense. A failed abort permanently blocks further
disc commands until console reset. Firmware-visible parameters and buffers are
static, so an unfinished request cannot access freed memory. Polling deadlines
cannot interrupt a firmware call that itself never returns; that remains a
physical-console test limitation. The diagnostic does not claim proven drive
reset/recovery on damaged discs.

## What disc checks establish

Both density TOCs must parse before samples are read. Each session may contain
multiple tracks. Starts are FAD addresses; the checked conversion to displayed
GDI LBA subtracts 150 exactly once. TOC ends are **not** final dump boundaries.

The probe checks the start, midpoint and a late interior sector of every track,
deduplicating samples in short tracks. The late sample stays 150 sectors away
from a possible following pregap. No data is discarded from a dump: this build
does not dump tracks or generate GDI files.

Each raw sector is read twice into guarded buffers with different initial fills.
Equal results detect common underfill and instability; guards detect nearby
overwrites. Data sectors also require a supported Mode 1 or Mode 2 Form 1 layout,
two matching guarded 2048-byte reads, and equality with the raw-sector payload.
Audio gets repeat/guard checks, not offset correction. CRC32 and header bytes are
logged. EDC/ECC checking, independently verified reference hashes, subchannels,
full-track captures and disc fingerprints are later work. Matching samples do
not prove every byte on the disc can be read correctly.

## Storage and completion

The adapter accepts a 512-byte-sector FAT32/exFAT superfloppy or exactly one
supported MBR primary partition. It refuses GPT, extended/ambiguous layouts and
cards outside its 32-bit block range. Every FatFs request is range-checked and
translated within the selected volume. Any backend I/O/sync error latches the
device unavailable until the next explicit connection, preventing an automatic
remount from silently continuing a failed job. FatFs formatting APIs are absent.

The serial backend uses KOS SCIF with CRC enabled. Its `CTRL_SYNC` performs a
fresh SD read; the pinned KOS driver waits for the previous write's busy period
to end before issuing that command. This is deliberately different from the
upstream block-device no-op flush. The 500 ms ready wait does not guarantee
survival of sudden power loss, removal or opaque card-controller caching.

The storage test writes at least 2 MiB + 173 bytes and crosses two clusters on
larger-cluster volumes. Each byte is derived from its position. Successful
writes advance accounting only by their exact requested size. The test flushes,
closes, remounts, reopens, checks the file size, and rereads every byte against
the independent position-derived expectation. It also compares streaming CRC32.
A `storage.json` completion manifest is written only after that verification.

The PC tool recomputes the byte pattern and CRC32 and reports SHA-256 using
Python's standard library. A SHA-256 capture hash in the console dump format is
deferred to M1.4. The predictable storage fixture already allows byte-for-byte
validation without trusting a self-reported hash.

Each test/report uses an exclusively created `/KUI/probes/pNNNN/` directory and
new files. Failed or cancelled tests retain their partial data without a
completion manifest. B finishes the current filesystem call and performs close;
there is no resume support yet. Filesystem allocation scans and internal SD read
retry loops are not covered by the optical command deadline. This UI therefore
shows Stop requested rather than promising an immediate storage abort.

## Host validation

`make test` checks finite command behavior, abort failure, clock wrap,
partition bounds, media-error latching, malformed TOCs/raw layouts, CRC vectors,
chunking, and PC detection of corruption/truncation/incomplete results.

`make test-images` compiles actual FatFs and the same storage probe used by the
console. It creates FAT32/exFAT fixtures with independent distribution tools,
checks successful filesystems with `fsck`, verifies repeated runs, tests MBR
translation with outside-partition sentinels, and injects write/sync failures and
controlled cancellation. It checks that failures cannot produce a pass result.
The FAT32 output is also exported through mtools and checked by the PC verifier.

AddressSanitizer and UndefinedBehaviorSanitizer are enabled in host builds. On a
restricted execution host where LeakSanitizer cannot inspect `/proc`, use
`ASAN_OPTIONS=detect_leaks=0 make test test-images`; this leaves address/undefined
checks enabled. CI uses the full default sanitizer configuration.
