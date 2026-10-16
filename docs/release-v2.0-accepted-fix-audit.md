<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Accepted-fix audit: RC14 omissions and RC15 restoration

Audit date: October 16, 2026. Examined RC14 source/build
`8c9190a9b6920b439523aed95e6bfa1ddfd8d57c`, its actual release artifact,
earlier delivered source/binary records and recovered owner acceptance reports.
This inventory distinguishes accepted feature builds from the newly assembled
candidate. It does not certify another console run or every game revision.

**Current status:** the [final-promotion addendum](#final-promotion-addendum--october-16-2026-utc)
records the subsequent owner acceptance of RC15 and 2.0.0 preparation. Pending
statements below describe the original audit-time state.

## Confirmed omissions

The October 13 working native reference is
`72e3bf049b26c892142365a643d5fed7e774dea5`, delivered as
`K-UI-Skies-Drive-State-Test.zip`. Its `evidence/layout-config` records
`LOW=1 TRACE=0 SONIC=0 SCI_RESUME_FIX=1 RETURN_TRACE=3
SKIES_READ_DIAGNOSTIC=1 NATIVE_DISC_CHECK=1 NATIVE_DRIVE_STATE=1`.
The owner confirmed Skies raw/2048 and Time Stalkers working; the same thread
reported no more FMV slowdown in Evolution and Evolution II. The drive-state
delivery retained the earlier accepted Evolution resume improvement.

| Behavior | RC14 finding | RC15 action |
| --- | --- | --- |
| Native DRIVE status independent of pending commands | Correction exists in `src/core/retail_gd.c`, but `Makefile.dc` defaults `KUI_RETAIL_NATIVE_DRIVE_STATE=0`. The old pending-command-to-BUSY path was compiled. | Enable the accepted native correction in ordinary native builds. |
| Native SCI resume/cancellation ownership used by the Evolution improvement | Correction remains opt-in; `KUI_GENERAL_SCI_RESUME_FIX=0` in ordinary native builds. | Enable the accepted native SCI path while retaining CE's separate correction. |
| Mounted-image BIOS disc check | Working reference enabled `KUI_RETAIL_NATIVE_DISC_CHECK=1`; RC14 defaulted to 0. It answers BIOS function 2 for the unchanged mounted image instead of forwarding to the physical boot disc. | Retain this functional part of the accepted native configuration. |
| Historical failure-only Skies instrumentation | Working test also enabled return trace 3 and its RAM-only failure capture. | Leave diagnostics off; they are not required release behavior. |

This was a configuration promotion failure. The working local commit is not
present in the remote repository. Pre-integration source `3599188`, Tyrfing
integration `3e3f86c`, PR #16 head `1b6f809` and RC14 all retain drive-state
default 0. PR #14/#16 did not delete the corrected code. The earlier merge
audit checked source integration without reconciling this accepted build
configuration, and the release notes repeated stale compatibility claims.

## Saved preset migration omission

The accepted saved-options runtime `5686c3b825b0` used
`/KUI/game-launch.json` and its `.bak`. RC14 reads only the newer
`/KUI/apps/games/launch-settings-{a,b}.bin` records. Its binary v1/v2 handling
does not migrate the older JSON, so those reader/CDDA choices were ignored.
RC15 imports valid legacy presets only when both new binary slots are absent,
preserving the original JSON and existing new-format profiles. The retired
release-loader comparison has no current equivalent and is reported/skipped
rather than silently mapped to another loader. Decode and filesystem checks
cover fallback, refusal, readback and preservation of the source data. The
new table holds 128 profiles; larger legacy collections are explicitly refused
rather than truncated. A failed initial binary write can require manual recovery;
the original JSON remains available and is never deleted.

## Other accepted behavior checked

| Accepted behavior/reference | Evidence in RC14 | Assessment |
| --- | --- | --- |
| Native low placement; Sonic/Grandia/Skies `5973741a6077` | Native reader remains below the IP image with exact layout/stack audits. Owner's RC14 Sonic, Grandia II and Power Stone 1/2 launches passed. | Retained; separate from the omitted later fixes. |
| CE token allowance `6f14bc529472`; rejected 512 experiment | `src/loader/sci_stream.h` retains 256-byte token slices. | Retained; rejected experiment stays out. |
| Fast SCIF bootstrap `18dd87d457fc` | `bootstrap.c` has the identical Git blob; unchanged-frame redraw suppression remains in `main.c`. | Retained. |
| SCI/exFAT runtime I/O | `sci_sd_storage.c/.h`, `diskio.c`, and `sd_reader.c` are byte-identical to stable 1.8.5; ordinary bus DMA remains enabled. | Retained source; no new throughput claim. |
| Stable 2048 converter | `tools/gdi_optimize.py` is byte-identical to stable 1.8.5 and still packaged. | Retained. |
| W5500 transfer/close handling | FTP retains the 250 ms closing cleanup. | Retained. |
| C5 transfer pacing/listener readiness | Adaptive pacing and bounded bursts remain; firmware changes require the separate firmware build/install. | Retained source; newer radio/band/update acceptance remains pending. |
| Games caching/grouping and current saved settings | Catalogue/art caches, Original/2048 paths and binary per-image options remain; sorting, reports and restored selection are present. | Retained implementation; older JSON migration was omitted and is corrected above. |
| Scanline geometry/wrap batching `ce7006087f20` | `src/core/retail_pace.c:5–57` uses the SPG_LOAD counter period independently of vblank position, invalidates changed geometry and counts wraps. The resident calls its budget/measurement path at `src/loader/retail_resident.c:1052,1072`. | Retained and reachable; fresh UBSan pacing test passed all 246 checks, including interlaced geometry and wrap cases. No new console timing claim. |
| ATA bounded PIO/busy-selection/older CF support `d584316ac1c2` | `src/core/ata.c:50–73,101–115,212–231` retains idle-before-select, CF signature/capability handling and up to eight sectors per streamed command. `src/loader/retail_storage_impl.h:104,110` uses the streamed read/stop APIs. | Retained in the IDE build and runtime; fresh UBSan ATA tests passed batching, CF/LBA48, DMA exclusion and failure handling. Physical IDE/CF acceptance remains pending. |
| Newer direct ZSO readers | Separate Standard LZ4 and Historical LZO1X reader targets remain in `Makefile.dc`; actual RC14 ZIP contains `zso-boot.kui` and `lzo-boot.kui`, with their codec identities recorded in `build.json`. | Retained and packaged; fresh UBSan container and both detached-reader tests passed bounds, fragmented reads, poisoned-cache recovery and explicit codec handling. Hardware compatibility remains a separate gate. |
| Durable launch reports | `src/apps/games_retail.c:790–812` saves the prepared reader/storage/music/package identity through `game_launch_report.c`; the runtime build includes that implementation. | Retained; fresh UBSan report tests passed independent CRC fixtures, torn/conflicting slots, malformed records, write/readback faults and cancellation. A report failure remains nonfatal to launch. |
| GDI/CUE duplicate collapse, preferring GDI (PR #13) | `src/apps/games.c:692–750` compares complete track maps before hiding aliases and explicitly prefers GDI. `tests/games_image.c:817–835` checks selector choice, different layouts, transient failure/cancellation and bounded reads. | Retained; existing FAT32/exFAT cases are registered in `tests/test_games_images.py:46–48`. Equal titles alone do not hide an image. |
| Ripper exact Saved job and New dump label (PR #13) | `shell.c` retains picker/label controls; `src/dreamcast/main.c:2436–2441` sends the exact job only to Resume/Verify and the folder label only to New. `tests/test_shell.c:535–686,1121–1158` covers selection, sanitization, cancellation and rendered controls. | Retained and wired into the packaged runtime; new console-path acceptance is still pending. Existing jobs are not renamed by a new-dump label. |
| MDK2 early sound-buffer reservation | Actual shipped worker contains the heap constructor hook and early reservation, not just dormant source. | Retained and enabled. |
| Toy raw/cooked CD music | Actual embedded workers match their reference hashes and selected AP/AT pipeline settings. | Retained rebuilt adapters; no new FMV fix claimed. |
| WinCE shared worker budget and aligned RAM copies | Shared 32-pump allowance and aligned/guarded Worms copies remain enabled; CE resume remains separately enabled. | Performance changes retained; rebuilt-module acceptance remains scoped. |
| Final World Party status correction `b08802f8945f` | Actual linked status reader remains a 12-byte, one-load, zero-stack leaf without calls or SR writes. | Retained and enabled. |
| New ripper tools and RC12/13 VMU functions | Implemented and packaged, with host/fault/filesystem checks. Earlier reports did not supply console acceptance for these new paths. | Pending console checks; not lost earlier passes. |

## Additional provenance correction

The handoff/integration text claimed the accepted Worms music binary was kept
byte-for-byte. The full RC14 packager rebuilt it, enabling broader application
discovery. Its worker-budget/RAM-copy fixes are retained, but the earlier exact
binary acceptance does not establish the rebuilt binder's behavior. See the
[CDDA artifact audit](release-v2.0-cdda-acceptance-audit.md) for the exact hashes
and distinction between intentional broader support and inaccurate preservation
claims. Broader untested combinations remain Experimental.

## Release checks and immediate retest

The packager now refuses a normal 2.0 candidate whose native layout record
omits/disables an accepted flag or enables the old owner-only return diagnostic.
The exact native configuration is included in `build.json`. Host checks exercise
both behavior and disabled/missing-flag refusals; Dreamcast linked size, stack
and instruction audits still apply. Future accepted experiments must update this
inventory and release defaults/guards together, with source and payload identity.

The native GD source is byte-identical to the accepted reference; native
disc-check assembly preprocesses identically. SCI resume differences add
the separate CE condition while retaining the accepted native path.

First RC15 tests: Skies raw and 2048 through the intro using Standard and
Background; Time Stalkers into gameplay; Evolution and Evolution II opening
FMVs. Then repeat the already-passing Sonic/Grandia/Power Stone baseline and
Worms music/input checks. The [complete plan](release-v2.0-rc15-test-plan.md)
covers the remaining features. Final 2.0 remains blocked on console acceptance.

## Boundaries and final publication gates

The restored layout/configuration guard covers ordinary raw/2048 retail
readers. Separate detached ZSO/LZO readers retain their existing settings and
remain without a console-accepted compressed-loader baseline. They require
their separate tests; the native restoration is not a compressed-game pass.

Final 2.0 publication also remains unfinished: publisher/workflow version gates
still admit only 1.7/1.8.5, the final announcements file is absent, and final
asset validation must expand beyond the legacy payload set to cover the full
2.0 codec/CDDA inventory and accepted native flags. RC rejection, exact-source
CI identity and checksum checks remain. These are final-promotion tasks, not
lost console fixes or requirements to install this candidate. Complete and
test them before final packaging/publication; do not enable 2.0 by an allowlist
change alone.

## Final-promotion addendum — October 16, 2026 UTC

On October 15, 2026 (America/Chicago), after receiving RC15 and the prioritized
Skies/Time Stalkers/Evolution test instructions, the owner reported “everything
seems to work now” and requested final 2.0.0. This accepts the restored native
baseline. The report does not enumerate every image, reader, revision, optional
maintenance action or music module; no complete matrix is inferred.

RC15 was merged to main at `9ded185e7219cb2db55791198ecba7d6ea8870b5`. Final
promotion retains its loader behavior and accepted defaults, with the final
version, documentation and release tooling updated together. The earlier
“blocked on console acceptance” statement is superseded for that baseline.

The final pipeline now handles version 2.0.0, supplies final announcements,
and validates the complete codec/CDDA payload inventory and accepted native
configuration. Final build/CI, exact-source, checksum and publication checks
still apply to the final assembled commit; this addendum does not claim those
future artifacts already passed or were published.

Separate ZSO readers, unfamiliar CD music combinations, IDE/CF hardware,
new maintenance/recovery/PC/CD paths and newer wireless behavior keep their
individual experimental/acceptance labels. The final
[release notes](release-v2.0-notes.md) replace stale public compatibility wording
without converting the broad owner report into unrecorded feature passes.
