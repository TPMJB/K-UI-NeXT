# Reuse one boot disc for SD updates

The M1.2 test package contains a CD bootstrap with the existing diagnostics and
a separate diagnostic runtime loaded from SD. Both are independent KOS programs.
The earlier `cf8210bc5442` CD has no SD loader; it cannot gain that ability just
by copying new files to the card. Keep it as a known-working diagnostic disc.

The uploaded log from build `addd439aaea5` confirms the SD runtime launch and
passing disc/exFAT probes. The user also confirms that B selects CD bootstrap,
normal startup reaches SD runtime, and cold boots work (count unspecified).
The user has also confirmed the expected missing-file and bad-checksum messages
and successful normal operation after restoring the good file. The other four
malformed-package fixtures still lack hardware results; see the
[hardware evidence](hardware-evidence.md). Ordinary runtime updates should need only a new
`/KUI/runtime.kui` on the SD card. A bootstrap bug, new unsupported
storage hardware or a future incompatible package format could still require
a replacement CD. Do not burn each newly generated CDI for normal runtime tests.

## Install once

1. Download the combined `diagnostic` artifact. Burn its `kui-diagnostic.cdi`
   once; this image now contains the bootstrap and fallback diagnostics.
2. Copy `sd/KUI/runtime.kui` into the existing `KUI` directory on the card. Keep
   the rest of the card, including existing probe results. Safely eject it.
3. With the console off, attach the SD adapter/card. Boot the new CD without
   holding B. The bootstrap validates the complete package before starting it.
4. Confirm the final screen says **K-UI NeXT | SD runtime** and shows the build
   identifier from the download. `CD bootstrap` means the fallback is running.
5. Swap the CD for a retail GD-ROM after the runtime screen appears. A, X, Y,
   B and scrolling retain their diagnostic functions.

The new `sd-update` artifact adds [full capture and controlled resume](capture-test.md)
using this same bootstrap/package format. One complete capture and console reread
are now supported by user report and saved metadata; full PC/reference checks
and hardware Stop/Resume remain pending. See [hardware evidence](hardware-evidence.md).

Future updates: replace only `KUI/runtime.kui`, safely eject the card, and boot
the same CD. The runtime is entirely in RAM after launch, so disc probes can
still use the optical drive. The original runtime was a diagnostic; newer SD
updates supply capture/verification without replacing the burned CD.

## Distinguish startup from fallback

Every normal boot initially shows `K-UI NeXT | CD bootstrap`, including while
reading and validating the SD file. Successful handoff starts the separate
program and changes the final heading to `K-UI NeXT | SD runtime`.

If the heading stays on `CD bootstrap` after startup, the embedded diagnostics
are running. Look for `Using built-in CD diagnostics; SD runtime is not running.`
Earlier lines can show a missing file, SD initialization/mount problem, invalid
package, cancellation or a staging-address rejection. Holding B also selects
fallback. The disc and storage tests are available in either program, so those
tests passing alone does not distinguish the execution path.

Press Y once the screen is ready to save the current boot log. Wait for the save
to finish, note the exact path shown by `Report saved`, then power off and retrieve
that file. Its header and the on-screen heading use the same compile-time role;
a CD fallback log starts with `K-UI CD bootstrap`, and an SD program log starts
with `K-UI SD runtime`. A photo of the final heading and startup messages can
also resolve the distinction.

## Recovery and one-disc acceptance session

Hold **B** as the bootstrap appears to use the built-in diagnostics. Missing,
invalid or unreadable runtime files also fall back with a visible explanation.
This recovery is automatic; there is no recovery menu or test button. It keeps
the CD diagnostics available when the SD runtime cannot load. Restoring the
good runtime file is a manual step; the bootstrap does not repair SD files.
An executable with valid checksums can still contain a software bug; if it
hangs after launch, power off and boot again while holding B. This escape path
does not depend on the SD runtime working.

Use one burned bootstrap disc for all of these checks. Change files only with
the console powered off, retaining the original good package on the PC.

For the missing/corrupt-file checks, **do not hold B**: the loader must actually
attempt to read the file for these tests to exercise automatic fallback.

1. Rename `/KUI/runtime.kui` to `/KUI/runtime-good.kui` on the card and retain
   a backup on the PC. Safely eject, then boot without touching any buttons.
   Expect a missing-runtime explanation and the `CD bootstrap` diagnostics.
2. Power off. From the downloaded `diagnostic` package, copy
   `loader-tests/bad-checksum.kui` onto the card as `/KUI/runtime.kui`.
   Safely eject and boot without holding B. Expect a checksum rejection and
   usable `CD bootstrap` diagnostics. A photo or Y-saved log can record it.
3. Repeat with the other supplied fixtures below, powering off before each
   file change and using the exact destination name `/KUI/runtime.kui`.
4. Power off, remove only the invalid test copy of `/KUI/runtime.kui`, and
   rename `/KUI/runtime-good.kui` back to `/KUI/runtime.kui`. Safely eject and
   confirm a normal boot reaches `SD runtime` again.

| Check | Expected result |
| --- | --- |
| Valid `sd/KUI/runtime.kui` | Final heading is `SD runtime`; build ID is correct |
| Hold B at startup with a valid package present | `CD bootstrap` fallback; controller and diagnostics work |
| Rename runtime.kui temporarily, or boot without the SD card | Explanation and usable built-in diagnostics |
| Copy `loader-tests/truncated.kui` as `KUI/runtime.kui` | Rejected; no runtime launch |
| Repeat with bad-magic, bad-checksum, oversized and wrong-version fixtures | Each is rejected; fallback remains usable |
| Restore the original good runtime.kui | Normal SD runtime launch succeeds again |
| Disc probe, SD write/reread, scrolling and display stability in the SD runtime | Same checks as the hardware guide pass |
| Five power-off/power-on boots with the valid package | Runtime appears and accepts controller input every time |

The supplied rejection fixtures are deliberately invalid test inputs. Do not
leave one installed as the normal runtime. They are small copies/variants of
the compiled runtime package; the oversized case has only a header.

Use the accessible exFAT card for this session. The 32 GB FAT32 console-reported
pass is recorded; lack of a reader need not block bootstrap testing. Repeat the
SD launch/storage checks on FAT32 when files can be copied to that card.
Photos of the final heading/build ID and rejection messages are useful if logs
cannot be extracted. Saved logs distinguish `SD runtime` from `CD bootstrap`.

## Stable version-1 package contract

The bootstrap reads `0:/KUI/runtime.kui` using the existing FatFs/serial adapter.
It opens the file read-only, validates the header before allocating, checks the
exact file length, reads the whole payload with exact-length checks, and verifies
its CRC32. Close/unmount, SD shutdown and cancellation checks precede execution.
The handoff happens in main before an I/O worker exists, via upstream KOS
`arch_exec`, not through a DreamShell loader.

All integer fields are little-endian unsigned 32-bit values. The 64-byte header
is followed by an unscrambled raw binary padded to a multiple of four bytes.

| Offset | Field |
| --- | --- |
| 0 | Eight-byte magic `KUIRUN1` followed by NUL |
| 8 | Format version, 1 |
| 12 | Header length, 64 |
| 16 | Payload length, 4 bytes through 4 MiB, multiple of four |
| 20 | P1 load address, `0x8c010000` |
| 24 | Entry address, `0x8c010000` |
| 28 | Static memory span including BSS, at least payload length, at most 8 MiB, multiple of four |
| 32 | CRC32 of the complete padded payload |
| 36 | Flags, zero |
| 40 | Twelve lowercase hexadecimal build-ID characters followed by four NUL bytes |
| 56 | Reserved, zero |
| 60 | CRC32 of header bytes 0 through 59 |

CRC32 provides corruption detection, not executable authentication. Only run
trusted project packages. The packager independently checks the ELF class,
endianness, machine, entry point, load segments and memory bounds before forming
the raw image. It rejects dynamic/interpreted executables and overlapping or
out-of-range segments. It does not load arbitrary ELF files on the console.

The bootstrap stages the image in heap memory and checks its location/alignment
against the destination and current stack. Upstream `arch_exec` copies forward
in four-byte units to the standard P2 alias `0xac010000`, so source must not lie
below the destination. The 4 MiB payload cap leaves the main stack/trampoline at
the top of RAM outside the copied region. Runtime initialization clears its own
BSS. The SD runtime does not attempt to load itself again.

Host checks exercise the same C/FatFs reader against FAT32 and exFAT images,
including the 4 MiB boundary, malformed headers, corruption, missing/truncated
files, read errors and cancellation. Image hashes confirm loading does not
write to the card. Host checks cannot establish physical execution handoff;
that is the first console acceptance test above.
