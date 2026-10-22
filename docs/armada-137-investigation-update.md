<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada 137 investigation update

Build `137ca964caad` has now passed a first generated VMU block read and its
CE read-finalization exchange. The later runtime reached an ordinary A2 WRITE
request, where the deliberately read-only adapter stopped publishing. Severe
FMV slowdown and repeating audio remain unresolved.

The photo transcription and source correlation are in
`evidence/armada-137-read-and-runtime-console.json`. The hardware build ID is
retained as observed. The reconstructed source baseline has explicit gaps in
`source-recovery-armada-137.md`; it is not asserted to be the original Git tree.

## Confirmed hardware evidence

| Test | Evidence | Result |
| --- | --- | --- |
| First READ | AUTO reason 3, phase 2; `0x55c` PRE/POST/holds/restores | Original CE completion returned; fence inactive; no fence fault |
| First read-finalization | AUTO reason 4, phase 2; `0x5a1` PRE/POST/holds/restores | One INFO, MEDIA, READ and FINALIZE; no A2 or fence fault |
| Runtime | GAME RETURN; `0xc39` (3,129) paired PRE/POST/holds/restores | Fence inactive, enable restored, no traffic/fence fault |
| Runtime A2 | INFO 1, MEDIA 1, READ `0x36`, FINALIZE `0x36` | 54 generated read replies and 54 completion replies |
| Runtime WRITE | First fault `0x60` at `0x0cfd3010`; value `0x2200020c`, expected 2, detail `0x22` | Read-only request-size guard reached |

The reply and guest-memory-store counters agree exactly:

- `1 INFO + 1 MEDIA + 54 READ + 54 FINALIZE = 110 = 0x6e` target replies.
- `2,471 advertisements + 29 INFO words + 8 MEDIA words + 54 × 131 READ words +
  54 FINALIZE words = 9,636 = 0x25a4` confirmed RX-buffer stores.

These are reply-buffer stores, not SD writes. Aggregate traffic counters include
physical and virtual endpoints, and repeated requests; they do not prove unique
block counts or successful physical-card writes. The separate MAPPING/DRIVER
report pages are zero and provide no independent positive sample in this run.

## What the write fault means

`0x2200020c` decodes to command 12 (WRITE), destination 2 (A2), host source 0,
and 34 payload words. Its normal phase shape is a function word, location word,
and 128 data bytes: 136 payload bytes, or 140 bytes including the header.

In `src/loader/armada_ce_maple_traffic.c`, `a2_run` rejects payloads larger than
two words before copying into its 12-byte read-only request buffer. That is the
observed fault 96. The header is consistent with an ordinary VMU write phase;
it does not establish malformed guest traffic. The guard must stay until the
adapter can stage the full request and implement correct transaction semantics.
Removing it would risk an overflow; acknowledging the write without a durable
backend would falsely report a saved game. The latched fault subsequently stops
A2 publication; its exact application-level consequence was not captured.

The block-read and read-finalization transport is therefore working in this
admitted Armada build. Durable writes, save/reload and power-cycle recovery
remain unimplemented and untested by this experiment.

## Working theory and next discriminator

The leading performance theory remains excessive code and cache/ownership work
inside the interrupt-masked helper. All restored DMA gates show that the wrapper
returns normally; they do not measure masked duration or prove that CE's tick,
reader, video and audio services run promptly enough. The FMV report is a severe
performance failure. No VMU SD save I/O occurs in these blank-card tests, so SD
save-writing latency cannot explain that slowdown. Neither a specific clock/
scheduler failure nor the original b92 freeze mechanism is confirmed yet.

Compare isolated hook/gate controls against full A2 emulation with the same
movie, image, settings and skip policy. Count request/publication work and
measure the complete masked path where possible. Establish acceptable runtime
cost before adding durable write I/O. Then wire the CE packet adapter to the
existing journal/durable backend, with complete phase staging, bounded storage
ownership and acknowledgment only after successful commit/release. Validate a
real save, reload and power-cycle recovery.

## Broader compatibility goal

Reuse `vmu_emulation.h` and `vmu_persist_session.h` for one common packet/card/
journal backend. Automatically choose validated native SDK and CE adapters;
each still needs correct memory lifetime, producer/completion and cache rules.
The exact-owner profiles in `vmu_maple_observe_profile.h` and the Armada pre-NK
reservation do not yet establish compatibility with every driver revision or
custom Maple implementation.

Physical GD-ROM use also needs a retained disc bootstrap: `src/apps/gd_play.c`
currently exits to a BIOS reboot, which loses this runtime. The existing
`vmu_sd_lease.h` borrows SCI state and the image-reader lock. A physical-disc
launch needs independently owned save storage while normal GD firmware reads
continue. This is a feasible integration direction, with separate launch and
compatibility work after the immediate CE performance and persistence tests.
