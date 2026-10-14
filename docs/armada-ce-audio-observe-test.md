<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE audio request observer

This is the first live request-flow test for Armada's Windows CE CD audio
path. It follows the kernel and game analysis from the owner's startup bundle.
It records requests while retaining the installed CE SCI DMA ownership fix.
It does not play CD music or change the game's sound-effect implementation.

## Install and test

1. With the console off, back up `KUI/apps/games/ce-probe.kui`. Merge this ZIP's
   `KUI` folder onto the card. It replaces only that CE loader file.
2. Cold boot on SCI microSD with the existing exFAT card. Select **Original
   Armada** and launch with **A** through the normal launch confirmation.
   The CE route uses the background reader; leave CD audio off.
3. Stay at the main menu for about **30 seconds**, then start a game and play
   for about **two minutes**. Check that sound effects still work and whether
   FMV pacing is the same as before. CD music remains absent in this build.
4. Use **A+B+X+Y+Start** to return. Take **one photograph of the full return
   screen**, including `BUILD`, `GUARD` and all four audio rows below.
   Report whether effects remained intact and FMV pacing changed.

| Return-screen row | Meaning |
| --- | --- |
| `PLAY PLAY2 PAUSE RELEASE STOP` | Accepted command counts since this boot |
| `REJECTED STAT DRIVE PLAYCMD` | Rejected audio requests, accepted REQ_STAT requests, successful DRIVE calls, and the last accepted PLAY command |
| `START END REPEAT` | Three validated parameters from the last accepted PLAY or PLAY2 |
| `FN CMD RESULT ERROR PENDING` | Final GD service call and pending request state |

Values on the screen are hexadecimal. Static analysis found three game call
sites requesting track range **4–4**, repeat field **15**. If that request
reaches this observer, its parameter row should show `00000004 00000004
0000000F`. The photograph establishes the actual live request; the static
calls alone do not establish which path executes in the menu or gameplay.
The last accepted PLAY parameters survive STOP and GD initialization/reset,
so a later control request does not erase the evidence.

## Restore

Merge `restore-ce/KUI` onto the card and cold boot to restore the exact prior
DMA-corrected CE payload, build **fd3bde28d538**. That file is copied directly
from the previously supplied Armada reader-test ZIP; it is not a rebuilt
control. Its SHA-256 is
`42e378a7af567c751d9413ef03c6f8509762e90c0001f75a4648a7fba04c4198`.
Your own backup restores whichever CE file was installed immediately before
this test.

## Scope and admission

The separate candidate admits only the owner's original US Armada raw GDI on
the SCI background reader. The stage checks the five-track layout, product
`T40301N 00`, kernel name and length, IP checksum, prefix/body checksums and
the complete kernel SHA-256 before CE runs. The extracted CE interrupt
bookkeeping/table addresses must also match the audited kernel; this observer
does not fall back to the uninstrumented standard reader. A different image stops with
`ARMADA AUDIO TEST PROFILE MISMATCH`.

| Identity | Expected value |
| --- | --- |
| Kernel | `0WINCEOS.BIN`, 1,253,376 bytes |
| Kernel SHA-256 | `a7628c78090fce2c93012d5e4db6142096e138dceb7ebbf555142e902ca03c1f` |
| IP CRC32 | `684f8040` |
| Kernel prefix CRC32 | `6200729c` |
| Kernel body CRC32 | `b9f755bc` |
| Track starts | 0, 756, 45000, 149842, 434062 |
| Track layout | Raw 2352-byte sectors, data/audio/data/audio/data, zero file offsets |

The runtime, native general loader, MDK2 and Toy Commander loader files are
not supplied for installation by this package. Reader scheduling, CE timer
setup, interrupts, storage operations, GD responses and existing sound code
retain their previous behavior. The observer adds small bookkeeping overhead
after GD dispatch and replaces the normal CE return counters with a compact
audio report; fault screens keep their existing diagnostics. This is not an
FMV performance update.

## Why this test precedes playback

The verified game opens the CE CD device and requests track playback through
DeviceIoControl and WSEGACD. Its ordinary sound effects use a separate
DirectSound path. The current GD service can accept the music command without
producing audio, so acceptance is evidence of request handling, not audible
playback.

The owner's kernel also establishes that CE owns the periodic TMU0 timer,
sound memory heap, firmware command queue and PCM ports. The native MDK2 audio
backend cannot safely be transplanted by reusing its clock, fixed sound-memory
bank or channel pair. The static package does not prove the active firmware's
live addresses or ownership. This observer samples only the GD service's
validated parameter copy after accepted requests; it does not follow caller
pointers after rejection, reserve sound memory, write AICA registers or
reprogram a timer. It is the required request-flow checkpoint before the
separate CE audio ownership and playback implementation.

## Verification and source

The source includes admission tests for altered image identities and observer
tests for accepted/rejected requests, parameter capture, command controls,
counter saturation and preservation across later GD activity. The dedicated
build uses `KUI_CE_SCI_RESUME_FIX=1` and
`KUI_ARMADA_CE_AUDIO_OBSERVE=1` only for the separate CE SCI-background
resident. The other resident configurations retain their default code.

The package builder verifies the runtime envelope/CRC, the embedded CE stage,
resident placement and stack bounds, the exact committed source build stamp,
and the restoration payload's known SHA-256. It verifies every ZIP member's
bytes and CRC after closing the archive, and includes `SHA256SUMS`, validation
evidence, source archives and dependency licenses. `build.json` records the
actual candidate identity and build commands. Console acceptance is still
unverified when this package is produced.

The owner's subsequent return-screen photograph on 2026-10-14 confirms eight
accepted PLAY requests with parameters **4, 4, 15**, 316 accepted status
requests and eight successful DRIVE calls. Guard faults and rejected audio
requests were zero. The final call was a completed DMAREAD check with zero
error and no pending request. This confirms the live request path; it does
not prove audible playback, sound-effect behavior or FMV pacing. The exact
transcription and interpretation are in
`evidence/armada-ce-audio-console-2026-10-14.json`.

The distributed package contains no owner game executable, kernel, ARM driver,
music or startup-bundle bytes. Included audit JSON files contain metadata and
hashes from the private analysis, not those binaries.
