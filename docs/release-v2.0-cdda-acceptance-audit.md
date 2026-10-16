<!-- SPDX-License-Identifier: GPL-3.0-only -->
# RC14 CDDA acceptance and binary audit

Audit date: 2026-10-15. Scope: native and WinCE CD music in the actual RC14
release ZIP, source `8c9190a9b6920b439523aed95e6bfa1ddfd8d57c`.
This is a source/package audit, not new console acceptance. Ordinary native
game-launch regressions are tracked separately.

## Accepted behavior versus shipped implementation

| Prior acceptance | RC14 evidence | Result |
| --- | --- | --- |
| MDK2 early sound-buffer reservation; runtime baseline `5d7502a23326` | `src/loader/toy_pilot_worker.c:482–518` reserves the oldest sound allocation immediately after the native constructor; `src/loader/mdk2_cdda_worker.S:151–196` supplies its hook; `Makefile.mdk2_cdda:11` enables the MDK2 branch. | Retained and enabled. The actual packaged worker has v8/92-byte exports and heap hook `0x8cfd4554`. The older exact MDK2 module identity is `89c4650e444f`; the runtime stamp is not its module stamp. |
| Toy Commander working music and later AP/AT DATA work | `tools/build_native_cdda_compat.py:16–18` selects private P2, schedule 2, budget 4, AD 2, tuning 1, IRQ 1 and pipeline 2. | Retained in the rebuilt raw/cooked adapters. Both embedded workers are 38,198 bytes and match their recorded reference hashes. FMV stalls remain a separate limitation. |
| Worms nearly imperceptible gameplay lag after RAM-copy build `1aa16821209b`, published as `d9f861ba87a2957234d0af276a4cfeb573367258` | `src/loader/armada_ce_stream_player.c:46,941–988,1067` retains the shared 32-pump allowance; `172–184,718` retains aligned four-byte copies and guarded 64-byte Worms chunks. | Performance changes retained. This does not establish hardware equivalence of a different binder or rebuilt module. |
| Armada, Bust-A-Move 4 and Worms runtime families | `Makefile.dc:62–67` builds all profiles with CE SCI resume enabled; `include/kui/wince_cdda_profiles.h:18–64` retains complete kernel-digest admission. | All three adapters shipped. Broader family discovery is intentional; unfamiliar applications remain experimental. |
| Final World Party replacement `b08802f8945f` | `src/loader/wwp_ce_stream_status.inc:1–9` and `src/loader/retail_resident.c:130–132`; packaged linked audit records state word `0x8cfb2200`. | Final status correction retained: aligned four-byte word, 12-byte leaf, one load, zero stack, calls or SR changes. Scoped prior acceptance does not certify this new binary. |

All seven actual package SHA-256 values match RC14 `build.json`, whose compile
records identify the same clean source commit. For native adapters the actual
embedded worker bytes also match `unchanged_reference_playback_sha256`.
Those reference builds use current source with exact admission; equality to
them is not equality to an independently recovered historical console binary.

## Proven Worms preservation discrepancy

The incremental World Party handoff required retaining the installed Worms
module byte-for-byte (`docs/cdda-2.0-handoff.md:25–30`). Its packager checks the
working module and deliberately omits a Worms installer member. The complete
RC14 packager instead rebuilds all CE modules (`tools/package.py:247–279`).

| Worms module | SHA-256 |
| --- | --- |
| Preserved diagnostic baseline required by the handoff | `3c39d5b4e149b4ed911f7d4cf1fd19ea750f94a0a783b149cf4c5357cd36651a` |
| Actually shipped RC14 `KUI/apps/games/ce-cdda-worms.kui` | `a998f4a5c730b5214f5d9404f6884f4c19b9209c82351d0f97c944eac36bea22` |

`tools/package_worms_tested_status.py:18–22,54–60` binds the preserved module
to diagnostic source `69140d55b8d702071db906ef7c8d0ef736672951` and diagnostic
ZIP SHA-256 `2ba06ce304cd8b6f9fbbf6b8b3a6cd764c6dffaefc2c69af2aa1ceb7a47bb54a`.
Its envelope build text was not independently recovered in this audit; do not
substitute the earlier RAM-copy runtime stamp for that missing observation.

This is a substantive selection difference as well as a changed build stamp.
`src/loader/armada_ce_excerpt_entry.c:3–6` enables
`KUI_ARMADA_CE_GENERIC_APPLICATION=1` for every full-stream adapter unless
explicitly overridden. The resulting binder discovers the checked native
sound singleton rather than relying on the earlier application holder.
Mainline commit `53dc10fbf6cf9f0b081e86bd850b44ab2fc4d8e3` integrated the
owner-approved broader family selection and accepted World Party work.

Broader discovery is an intentional feature. No omitted CDDA algorithm fix
was identified here. However, the byte-identical preservation statement in
the original integration record does not describe RC14, and the prior Worms
binary's hardware acceptance cannot establish this new binder's behavior.
RC14 package metadata correctly marks each new module hardware-untested.

## Minimal release gate

Keep a per-module acceptance record containing exact package SHA, source,
effective binder mode and console result. Package checks should reject a
claim of byte preservation when the complete module hash differs. Changes
to binder mode must require an explicit acceptance entry, even when worker
timing, source quotas and sound-copy code remain unchanged.

For Worms, either retain the exact accepted baseline on its established route
while keeping the broader adapter available explicitly, or test and record
the new binder before claiming equivalent behavior. Preserve the requested
broader-family feature and its Experimental status; do not describe its
rebuilt module as the unchanged accepted binary. Host and linked audits
remain useful evidence but cannot replace this console comparison.

## Final-promotion addendum — October 16, 2026 UTC

After the priority RC15 console retest, the owner reported “everything seems
to work now” on October 15, 2026 (America/Chicago) and authorized final 2.0.0
preparation. That report accepts the restored native baseline without listing
exact per-module CDDA hashes or a new four-adapter test matrix.

The final source retains the broader checked selection and the documented
worker-budget/RAM-copy corrections. It does not relabel rebuilt modules as
byte-identical to earlier accepted binaries. Prior per-feature observations
remain scoped to the binaries recorded above; unfamiliar matching runtimes
and World Party's broader support remain Experimental. Final package/source,
layout, inventory and checksum checks still apply to the assembled release.
