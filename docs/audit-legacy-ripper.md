# Legacy GD Ripper comparison for 2.0 Tyrfing

Source audit, 2026-10-15. Legacy: `TPMJB/DreamShell_NeXT`, `master`
at `2a530929` (GD Ripper app 2.2.5). Independent implementation:
`TPMJB/K-UI-NeXT`, `release/2.0-tyrfing`, starting at `3599188`.
Line references below refer to those inspected sources; the release integration
may move them. This is a functionality comparison, not a code port or a new
hardware acceptance result. G1 SD adapter hardware experiments are excluded.

The independent ripper already has the central workflows from the adapted
project. The useful differences are mostly destination and job selection,
optional diagnostics, and the scope of recovery and supported disc profiles.
There is no missing catalogue verification or missing resume engine to import.

## Existing functionality, independently implemented

| User workflow | Legacy evidence | Independent K-UI evidence and result |
| --- | --- | --- |
| Read disc title and make a named game folder/GDI | `applications/gd_ripper/modules/module.c:522` reads IP.BIN; `:1410` builds the name | `src/core/capture.c:206` identifies the disc; `:301` selects a named job; `src/core/destination.c:86` sanitizes titles. Present, with numbered collision avoidance. |
| Choose destination folders | Legacy `module.c:2555–2637`, `modules/folders.h` | `src/core/shell.c:1621–1663`, `src/core/destination.c`, `src/core/destination_file.c`. Present, including saved destination and path entry. Device differences are below. |
| Stop and resume a partial capture | Legacy `module.c:983–1016`, `:2003`, `:2147` | `src/core/capture.c:372–430`, `:844–879`; `src/core/shell.c:1604–1620`. Present, with alternating checkpoints and identity checks. Also offers explicit Quick Resume; SHA jobs retain full prefix reads. |
| Retry bad reads, stop on fatal drive or storage errors | Legacy `module.c:2339–2408` | `src/core/capture.c:181–200` falls back to one sector with bounded retries; `:142–152` checks write/sync errors. Present. Normal retry-count configurability differs below. |
| Stream CRC and Redump/TOSEC matching, with partial/full distinctions | Legacy `module.c:1557–1574`, `modules/verify.c:708–749` | `src/core/capture.c:786–820`, `src/core/known_dumps.c`. Present. Capture-reference matching is separate from saved-file reread verification. |
| Reread saved bytes; scan raw sectors with EDC/P/Q/address checks | Legacy `module.c:1905–1937`, `modules/verify.c:196–300` | `src/core/capture.c:876–879,892–915`; `src/core/recovery_scan.c:239–304`. Present. Advanced CRC can browse a completed job or supported imported raw GDI without an inserted disc. |
| Recover unreadable sectors with tracked placeholders and later optical rereads | Legacy `modules/recovery.c`, `module.c:1535–1543` | `src/core/salvage.c`, `include/kui/salvage.h:20–36`; reachable through `src/core/shell.c:1613` and the Salvage page. Present as a separate job format, with explicit zero-fill consent and 1/5/10/20/50 recovery sweeps. Console acceptance remains pending per `docs/salvage-worker.md`. |
| Progress, speed/time information, retry/FAD state and saved diagnostics | Legacy `module.c:1656–1744`, `:229–281` | `src/core/capture_display.c`, `src/dreamcast/disc.c:55–99`, `src/dreamcast/main.c` automatic capture/scan report paths. Present. Binary diagnostic sampling differs below. |
| Output formats | Legacy `module.c:1019–1031` offers raw BIN or cooked ISO data tracks in GDI, plus raw audio | `include/kui/capture_format.h:4–25` offers GDI, BIN/CUE, CSO, ZSO and CHD; `src/core/capture_export.c` verifies exports while retaining raw capture. New formats are broader, but direct cooked GDI output differs below. |

## Actual gaps and differences

| Capability in the adapted app | Current independent behavior | Release recommendation |
| --- | --- | --- |
| **Choose an exact existing dump for Resume/Verify.** Legacy `module.c:2555–2557,2589–2592,2623–2634` recognizes `rip.state`, sets that dump's parent and exact name. | Normal Resume/Verify selects the highest matching numbered folder under the configured parent (`src/core/capture.c:301–337`), with legacy-job fallback. Advanced CRC already lets the user scan an exact older completed raw dump (`src/core/shell.c:1621–1644`). There is no corresponding exact selection for an older unfinished normal job. | Best convenience candidate after integration: add an explicit job picker without weakening identity/checkpoint checks. For Tyrfing, document the existing Advanced CRC route for older completed dumps. This is not absence of manual file verification. |
| **Edit the output game/folder name.** Legacy editable `gname-text` (`applications/gd_ripper/app.xml:168`) calls `module.c:512–520`. | New job names always derive from the identified title (`src/core/capture.c:307,354`). The keyboard edits only the destination parent (`src/core/shell.c:1637–1641`), not the new title. | Optional later convenience. Add an output-label override with existing sanitization and collision rules; keep optical identity separate from the label. Avoid changing capture/checkpoint formats just for naming. |
| **Choose PC share as a direct rip destination.** Legacy browser offers SD, IDE and PC (`app.xml:199–206`; `modules/folders.h:13`), using DreamShell's mounted `/pc` service. | Current capture uses one selected local storage backend through FatFs (`src/dreamcast/capture.c:46–66`, `src/dreamcast/sd.c:282`). SCIF, SCI and IDE/CF exist; IDE already serializes GD-ROM/ATA access (`src/dreamcast/capture.c:6–19`). There is no `/pc` mount capture destination. FTP transfer is a separate workflow. | Later independent network-filesystem/streaming project. Do not describe IDE ripping as missing, and do not merge G1 SD board experiments to close this gap. A within-ripper device picker is UI convenience, since storage is already selected globally. |
| **Cooked 2048-byte data tracks directly in the console's GDI output.** Legacy `module.c:1019–1026,2483–2507`; UI explicitly says cooked output has no catalogue CRC match. | Normal raw GDI is 2352-byte tracks; CSO/ZSO export cooked data but are not cooked GDI (`include/kui/capture_format.h`). `tools/gdi_optimize.py:223` already produces a separate 2048-byte GDI copy on a PC, checking framing/address structure without establishing EDC/ECC or catalogue correctness. | Later optional on-console post-capture export using the existing raw job as the source. Keep raw capture/hash/resume conventions intact. Existing PC conversion covers the practical workflow for Tyrfing. |
| **Rip single-session CD/CD-R/audio media as tracks.** Legacy chooses one TOC area for non-GD discs (`module.c:1429`), with CD-specific track-tail treatment (`:1060–1065`); this path is reachable, not merely an unused declaration. | `src/core/capture_plan.c:29–36` requires both GD sessions and the 150/45150 starts. The current ripper supports its declared retail-GD profile, despite the broader Disc Ripper name. Audio-CD playback is a different app. | Later separate CD capture profile with correct CUE/session/index semantics and media tests. Source support in the legacy app is not proof of archival accuracy for every CD layout. |
| **Optional P/Q checks during an ordinary rip.** Legacy Advanced CRC changes `checked_sector_read` from EDC to `gd_check_sector` (`module.c:2085–2094`). | Normal capture checks data layout/EDC (`src/core/capture.c:159–179`). The saved-file Advanced CRC scanner and separate salvage perform full Mode 1 parity/address checks. | Keep the accepted normal DMA acquisition path for Tyrfing. An optional strict acquisition mode can be evaluated later if needed; saved-file Advanced CRC already covers the validation goal. |
| **Repair suspect sectors in an existing ordinary saved dump.** A saved-file scan creates `.suspect`; enabling Advanced CRC then Start/Resume invokes optical replacement with original-byte backups (`module.c:2177–2180,2950–3015`). | Advanced CRC is explicitly read-only (`include/kui/recovery_scan.h:7–11`). Salvage repairs only its own journaled zero-hole targets, not an arbitrary normal/imported capture (`include/kui/salvage.h:24–36`). | Later explicit repair-to-copy workflow with durable evidence, identity and interrupted-write handling. This is a real workflow gap; do not solve it by making Scan or ordinary Resume silently patch data. Neither implementation performs generic ECC reconstruction. |
| **Storage-instability forensic samples.** Legacy reopens/rereads up to a bounded number of suspect sectors twice, records byte differences and `readback.bin`, detects buffer changes and blocks optical repair after disagreement (`modules/readback.c:109–174`, `:14–31`; `module.c:1422–1426`). | Current Advanced CRC reads each track once, reports suspect FADs and hash mismatches (`src/core/recovery_scan.c:255–303`), and preserves originals. Normal capture has guarded read buffers, optional sample readback and hash checks, but no equivalent saved-sector reopen/diff bundle. | Useful later diagnostic enhancement if unexplained readback corruption returns. The missing artifact is forensic discrimination, not a missing corruption check or a demonstrated reliability regression. |
| **Configurable normal-capture retry limit.** Legacy UI cycles 1/5/10/20/50 (`module.c:503–509`), applying it to ordinary reads and recovery. | Normal capture uses the accepted fixed ten additional retries (`include/kui/capture.h:12`, `src/core/capture.c:181–200`). Salvage already exposes the same five sweep limits. | Low priority. Keep normal retry behavior for Tyrfing and expose tuning only if real media needs it. Counting recovery sweeps as normal retries would misrepresent the two policies. |

## Tyrfing scope

The immediate safe work is documentation and discoverability: describe exact-folder
Advanced CRC scanning, the PC 2048 conversion tool, the retail-GD capture scope,
and the distinction between normal capture and Salvage. These do not need a
new disc burn or a change to the accepted reader.

An exact-job selector is the most useful next app enhancement, followed by an
editable output label. Both need normal UI/filesystem validation and should be
reviewed independently of release integration. Network destinations, a CD profile,
on-console cooked exports, in-place normal-dump repairs and extra forensic reads
are separate projects, not prerequisites for merging the accepted 2.0 features.

No legacy implementation code was copied. The comparison does not imply that
DreamShell's storage, repair or CD paths should replace K-UI's independent ones.
