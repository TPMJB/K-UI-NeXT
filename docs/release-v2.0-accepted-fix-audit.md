<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Accepted-fix audit: RC14 omissions and RC15 restoration

Audit date: October 15, 2026. Examined RC14 source/build
`8c9190a9b6920b439523aed95e6bfa1ddfd8d57c`, its actual release artifact,
earlier delivered source/binary records and recovered owner acceptance reports.
This inventory distinguishes accepted feature builds from the newly assembled
candidate. It does not certify another console run or every game revision.

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
| Games caching/grouping and saved settings | Catalogue/art caches, Original/2048 paths and per-image options remain; sorting, reports and restored selection are present. | Retained implementation; new options still need their scoped console checks. |
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

First RC15 tests: Skies raw and 2048 through the intro using Standard and
Background; Time Stalkers into gameplay; Evolution and Evolution II opening
FMVs. Then repeat the already-passing Sonic/Grandia/Power Stone baseline and
Worms music/input checks. The [complete plan](release-v2.0-rc15-test-plan.md)
covers the remaining features. Final 2.0 remains blocked on console acceptance.
