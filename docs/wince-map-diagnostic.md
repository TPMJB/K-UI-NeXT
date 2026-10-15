<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Worms 2048-byte DATA: initial map failure

The owner launched the converted Worms Armageddon image with CD music On
using the experimental a7c85a8 update. The console stopped at INVALID RETAIL
MAP with detail 1 before SD initialization, executable loading or audio.

The canonical map format, decoder, validation policy and cooked control
flags are unchanged from the preceding working RAM-copy source. Linked SH
decoder/CRC/copy routines match the preceding implementation apart from
verified relocated literal addresses. The stage map pointer, BSS and decoded
allocation bounds agree. No confirmed root cause or playback fix is claimed.

A new regression test prepares the actual shipped Worms package with the
supplied private boot kernel, computes its real SHA, verifies only intended
map/seed bytes change, and executes the integer entry/BSS preamble against
bounded test RAM. Raw and converted DATA pass for both the working RAM-copy
and experimental packages. The target decoder's C implementation validates
the resulting map; target CPU caches and real shutdown are not emulated.
Retail game/kernel bytes are test inputs only and are not distributed.

The diagnostic update adds observations only when the primary decoder has
already failed. It preserves all acceptance checks and does not repair or
admit corrupt maps. The report identifies framing, checksum, reserved/unused
bytes, counts, track/header policy, text, bounds, flags, reader and session
conditions. It also compares the copied input against its original main-RAM
source when that source is in a safe readable range.

Install the one KUI file over the earlier experimental update with the
console off. Repeat the same 2048-byte image with CD music On. If it stops,
photograph the whole screen once. No additional game sample or rerip is
requested. If it launches, check the same match's music and responsiveness;
that outcome alone does not establish why the preceding handoff failed.

The display's MAP MODE value is 4 for the Worms WinCE/CDDA adapter. Its
expected primary-map pointer is 8C011000. CRC values and failed-check bits
are diagnostic evidence; the loader continues to refuse an invalid map.
