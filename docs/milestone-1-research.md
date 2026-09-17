# K-UI — Milestone 1 Research and Scope

Research date: 16 September 2026
Status: research snapshot before implementation; see README for current progress
Project goal: an independent Dreamcast environment built directly on KallistiOS

## 1. Decision and milestone definition

Proceed with a small, independent K-UI application. The first milestone is:

**Boot without DreamShell, use the existing serial SD adapter with FAT32 or exFAT, dump every supported track of a known-good retail GD-ROM, and verify the saved files on the console and PC.**

A usable milestone includes basic retries, controlled stop/resume, clear failure reporting and a minimal controller interface. It does not depend on replacing DreamShell's retail-game loader.

The first implementation should be a small diagnostic build that proves disc and storage access. Research found enough evidence to justify that build, but cannot establish raw-read correctness or recovery behavior on this particular Dreamcast. A successful compilation or emulator boot will not close that gap.

This is a proposed scope for new work. No existing DreamShell NeXT branch, release, license, artwork or source file was changed during this research.

## 2. Findings that determine the design

| Finding | Evidence and confidence | Consequence |
| --- | --- | --- |
| Upstream KOS is a suitable independent foundation. | Its own license is BSD-style; its documented hardware support includes optical drive, serial SD, controllers and graphics. Confirmed in upstream sources. | Use the official KOS repository and retain its required notices. |
| Raw high-density reads need an early proof. | KOS's CD header retains a warning about high-density access, while the same API exposes high-density TOCs and raw-sector modes. The older BSD-licensed `httpd-ack` dumper uses closely related KOS interfaces for GD-ROM dumping. | Treat this as an unresolved hardware capability question, not as proof that a new low-level driver is either mandatory or unnecessary. |
| Upstream KOS's bundled FAT filesystem is not exFAT. | Its `fs_fat.h` explicitly lists FAT12/16/32. | Integrate upstream FatFs through a new SD adapter layer. Preserve exFAT in the milestone. |
| Default disc-read wrappers are unsuitable for a reliable ripper without review. | In the examined implementation, ordinary PIO/DMA reads include waits without a finite deadline. A timed-command error path also appears capable of reacquiring its own semaphore. | Prove timeout, abort and reset behavior before long dumps. |
| Booting an independently built application is supported. | KOS supplies bootstrap tooling and an `arch_exec` interface for replacing the running program. | Build a small bootstrap and runtime, both independent of DreamShell. |

Sources: [KOS license](https://github.com/KallistiOS/KallistiOS/blob/master/doc/license/LICENSE.KOS), [KOS capabilities](https://github.com/KallistiOS/KallistiOS/blob/master/README.md), [CD interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/cdrom.h), [FAT interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/addons/include/fat/fs_fat.h), [disc implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/cdrom.c), [httpd-ack documentation](https://github.com/sega-dreamcast/httpd-ack/blob/f5891519a01902d50b675ff38660ac87108de293/README).

### Raw GD-ROM access

A GD-ROM has separate low-density and high-density regions; the high-density region can contain both game data and audio. The implementation must enumerate both regions rather than assume every disc has three tracks. [Marcus Comstedt's GD-ROM documentation](https://mc.pp.se/dc/gdrom.html)

The independent `httpd-ack` reference documents raw 2,352-byte reads, both transfer modes, track boundaries and GDI generation. Its original README explicitly applies KOS's BSD terms to its own non-HTTP code. Its historical KOS patch adds configurable sector modes and PIO/DMA selection; corresponding capabilities now exist in upstream KOS. This makes a small adaptation plausible, but does not establish compatibility with the current KOS implementation or the user's console. [Original README](https://github.com/sega-dreamcast/httpd-ack/blob/f5891519a01902d50b675ff38660ac87108de293/README), [historical patch](https://github.com/sega-dreamcast/httpd-ack/blob/f5891519a01902d50b675ff38660ac87108de293/patches/kos-cdrom-ack.diff)

The first probe should request low- and high-density TOCs, identify the disc, and read small raw samples near track starts, in the middle and near track ends. It must check actual returned content and buffer coverage, not only the command's success code. Compare the data portion of raw data sectors with separately requested 2,048-byte data-sector reads.

### Timeout and cancellation risk

Static inspection of the pinned KOS source found this path:

1. `cdrom_exec_cmd_timed` takes the G1 semaphore using a scoped lock.
2. On timeout it calls `cdrom_abort_cmd` before the outer scope ends.
3. With no DMA marked active, the abort function waits on the same semaphore.
4. The semaphore is initialized with a count of one.

This is a potential self-deadlock inferred from source, not a hardware reproduction or a verified cause of any earlier NeXT failure. A focused test or new adapter must resolve it. Ordinary read wrappers also need explicit deadline handling. [Disc source](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/cdrom.c), [scoped-semaphore implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/sem.h)

The initial adapter should favor a simple PIO path with finite command polling and one owner of drive state. DMA becomes an optimization after byte equality and cancellation tests. A polling deadline cannot guarantee recovery if an underlying firmware call itself stops returning; that failure must remain visible in the hardware results.

### SD and exFAT

Use FatFs R0.16 with the official patch-1 and patch-2, through patch-2 dated 10 July 2026. Import the dependency from its own upstream source, independently of NeXT's integration. FatFs has a permissive license, supports FAT/exFAT, and separates filesystem operations from device access. [FatFs](https://elm-chan.org/fsw/ff/), [license and porting notes](https://elm-chan.org/fsw/ff/doc/appnote.html), [required maintenance patches](https://elm-chan.org/fsw/ff/patches.html)

KOS already provides SD block reads/writes and whole-device/partition adapters. Its convenience partition parser handles MBR, and its block-device flush currently returns success without extra work. That is insufficient evidence to assume a checkpoint is durable: the new integration must verify when writes finish and what `CTRL_SYNC` means for this driver. [SD implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/sd.c), [FatFs device-sync contract](https://elm-chan.org/fsw/ff/doc/dioctl.html)

The initial supported layout is one selected FAT32 or exFAT volume, using an MBR primary partition or a whole-device filesystem. Detect unsupported or ambiguous layouts and explain them. Do not format cards automatically. GPT support is a separate extension unless the actual test card requires it.

## 3. Included and deferred work

| Included in Milestone 1 | Deferred |
| --- | --- |
| Independent CD bootstrap and KOS runtime | Retail game launching, ISO-loader compatibility, Windows CE game support |
| Runtime update from a dedicated SD folder | BIOS flashing, region changes, custom BIOS requirements |
| Serial SD adapter, FAT32 and exFAT | IDE, network ripping, USB and optical-drive emulators |
| Disc identity and both TOCs | GD-R swap methods, arcade GD-ROMs and unusual/prototype media |
| Raw per-track files and a GDI descriptor | Subchannel archival, drive-offset correction and Redump submission compliance |
| Bounded retries and stop/resume after a confirmed checkpoint | Aggressive damaged-disc recovery and rebuilding unreadable sectors |
| Full saved-file reread, checksums and PC verifier | Downloading a large catalog or bundling third-party databases |
| Minimal K-UI screen and controller controls | Full desktop, VMU manager, music playlist, cover art and theme overhaul |

A failing sector stops the clean-dump workflow with a usable checkpoint and diagnostic details. Milestone 1 does not silently write zeros and label the dump complete.

The first release targets known-good retail discs. The design retains the failure cases learned from NeXT, including wrong-disc resume, stalled reads, incorrect recovered-sector totals and partial catalog matches. It does not promise identical recovery performance or compatibility with NeXT's existing `rip.state` files.

## 4. Architecture and interfaces

Use C11 for the new core, with a small platform interface so progress accounting, track planning, checkpoint parsing and verification can also run in host tests.

| Layer | Responsibility | Origin |
| --- | --- | --- |
| Bootstrap | Find the runtime, validate its size/integrity, load it completely and transfer control | New K-UI code with upstream KOS |
| User interface | Disc status, destination, progress, stop/resume and results | New K-UI code using KOS video/controller support |
| Job engine | State transitions, track/range accounting, retry policy and completion rules | New portable code |
| Disc adapter | TOCs, read mode, raw reads, error details, deadlines and abort/reset | New adapter to documented KOS/firmware interfaces; audited BSD reference where appropriate |
| Storage adapter | File creation, exact-length writes, sync, reread and checkpoint files | Upstream FatFs plus new glue to KOS SD |
| Verification | Streaming CRC32/SHA-256, saved-file reread and explicit reference comparison | Independently implemented or permissively licensed hash routines |
| Host verifier | Check descriptor/ranges/sizes and recompute hashes on Linux | New small command-line tool |

Keep a single worker in charge of disc and filesystem I/O during capture. The UI reads progress snapshots and sends requests; it does not independently poll the disc or write log files. This prevents status checks, directory scans and recovery routines from competing for the same hardware state.

A full KOS VFS integration for FatFs is unnecessary for the first milestone. A narrow storage interface shared by the bootstrap and ripper is enough. Broader VFS support can follow when file management becomes a goal.

### Memory and transfer policy

The initial buffer candidate is 32 raw sectors: 75,264 bytes. This is divisible by both 512-byte SD blocks and 32-byte DMA alignment units. Begin with one working buffer; consider a second only after measuring benefit.

Use explicit lengths and separate units for disc addresses, SD blocks and byte offsets. Check multiplication/addition overflow before allocating or writing. Keep total memory bounded independently of disc size. Allocate no whole-track buffers.

For later DMA support, test cache coherency, buffer alignment, exact byte counts and odd-sized tail reads separately. Upstream KOS documents distinct cache obligations for different memory mappings. [Raw-read API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/cdrom.h)

### Boot path

The first diagnostic can live entirely on a selfboot CD. Once the hardware path is working, add a small bootstrap that loads the runtime from a dedicated `/KUI/` SD directory. This allows later test updates by copying files to the card.

Use upstream KOS's execution handoff rather than a DreamShell bootloader. The bootstrap must check complete reads, expected binary format, maximum length, checksum, destination memory and compatibility version before executing. The inspected `arch_exec` implementation copies in four-byte units, so payload padding/alignment must be handled explicitly. [Execution API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/exec.h), [implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/exec.c)

Load required fonts, code and essential assets before asking the user to replace the boot CD with a game. During ripping, no files may remain dependent on the boot CD.

KOS's `makeip` and an independent image packager can produce the boot image. The `makeip` program has a BSD-2-Clause license; its documentation separately explains the bootstrap template's origins. Audit the embedded template as well as the packaging program. [KOS bootstrap tooling](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/makeip/README.md), [tool license](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/makeip/LICENSE)

## 5. Dump format and verification contract

### Output

Each job gets a new directory under `/KUI/dumps/`. Never replace an existing dump implicitly. Proposed contents:

| File | Meaning |
| --- | --- |
| `disc.gdi` | Descriptor for a fully completed supported dump |
| `trackNN.bin` | Raw 2,352-byte data sectors |
| `trackNN.raw` | Raw audio sectors in the documented output convention |
| `manifest.json` | Disc identity, TOCs, source build, track ranges, sizes, hashes and status |
| `checkpoint-a.bin`, `checkpoint-b.bin` | Versioned, checksummed recovery records with sequence numbers |
| `diagnostics.txt` | Concise failure and timing record |

Keep incomplete output marked incomplete; publish the final descriptor/completion marker only after every expected track has been written successfully. A file rename alone is not evidence of durable completion.

KOS's TOC values use FAD even though the extraction macro is called `TOC_LBA`. GDI uses a different address convention; the traditional dumper subtracts 150 when writing GDI starts. Perform that conversion in one place and test it. [KOS TOC definition](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/syscalls.h), [independent GDI writer](https://github.com/sega-dreamcast/httpd-ack/blob/f5891519a01902d50b675ff38660ac87108de293/src/httpd-ack.c)

Track ends and gaps must have a declared format convention. A TOC start address alone does not specify every pregap boundary. The historical dumper's gap policy is useful evidence, not a universal physical-disc specification. Test the chosen convention against known reference files, including adjacent audio tracks and data/audio transitions. Record excluded ranges; do not silently discard sectors to force a checksum match.

“Complete dump” in this milestone means all tracks/ranges of the declared GDI profile. It does not mean a full subchannel or forensic image of every physical region of the disc.

### Verification results

| Result shown to the user | Evidence required |
| --- | --- |
| Capture complete | All expected bytes were successfully read and written; no unresolved read errors |
| Saved data verified | After close/sync, rereading each saved file yields the same length and SHA-256 as its capture stream |
| Reference match | Every required track matches a supplied reference with the same layout, sizes and checksum convention |
| No reference / partial match / mismatch | The actual reference coverage or discrepancy, without upgrading it to a full match |

Compute CRC32 for continuity with the existing workflow and SHA-256 for captured-versus-saved comparison. The PC tool independently recomputes both. Hash routines need known-answer tests and tests for arbitrary chunk boundaries.

A capture hash and its saved-file hash can agree even when the disc supplied consistently wrong bytes. That proves storage consistency, not independent disc accuracy. At least one full known-good fixture must therefore also be compared with a separately verified reference dump in the same format. A repeat rip is useful additional evidence, but is not equivalent to independent validation.

Large catalog integration is optional later. Milestone 1 can accept a small user-supplied reference manifest; the provenance and layout of that reference must be recorded. Do not bundle NeXT's catalog by default.

## 6. Stop, resume and error behavior

Progress advances only after the corresponding data is successfully written. A checkpoint is committed only after track data has been synchronized; failure to sync prevents the checkpoint from being advertised as committed.

Use alternating checkpoint records with a format version, sequence, disc fingerprint, track/range position and integrity checksum. On restart, validate both records, file lengths and the saved prefix before selecting a resume point. Reject impossible offsets and records beyond actual file contents.

The disc fingerprint should combine both TOCs, product/version metadata and hashes of readable content samples. A title or TOC alone is insufficient. It identifies compatible content for resume; it does not claim to distinguish physically identical pressed copies.

Proposed behavior:

| Event | Required response |
| --- | --- |
| User presses Stop | Acknowledge promptly; finish or abort the active operation within its tested limit; sync and save a checkpoint |
| Read fails | Reduce the request to isolate the failing range; retry within an explicit budget |
| Retries exhausted | Stop with the track, address and remaining work reported; preserve prior committed data |
| Drive timeout | Attempt bounded abort/reset, restore the read mode, and report the outcome |
| Lid opens or disc changes | Invalidate the active read and require identity validation before resume |
| SD error or full card | Stop writing; do not advance committed progress |
| Saved-file hash mismatch | Mark verification failed and identify the affected track |
| Corrupt or incompatible checkpoint | Explain why resume is unavailable; preserve files for inspection |

For Milestone 1, use a single retry budget with clear semantics: additional attempts after the first read. Reinitialization must not silently reset the budget.

FAT/exFAT are not transactional filesystems. Periodic synchronization reduces loss but cannot promise survival of arbitrary power failure or SD-controller behavior. Controlled Stop/Resume is mandatory. Abrupt-reset testing belongs on a spare test card; passing it demonstrates the tested cases, not universal power-loss safety. [FatFs synchronization behavior](https://elm-chan.org/fsw/ff/doc/sync.html)

## 7. Source and license boundaries

The research originally recommended BSD-3-Clause. The repository owner subsequently selected GPLv3 when creating this independent repository. New K-UI code uses GPL-3.0-only; dependency licenses and original notices remain in force. There is no separate contribution or commercial-relicensing CLA.

| Component | Selection policy |
| --- | --- |
| KOS | Official upstream source under its applicable file licenses; preserve contributor notices |
| FatFs | ChaN's upstream R0.16 plus official patches and source notice |
| httpd-ack | Independent BSD reference; review any reused code and retain original author attribution |
| New K-UI modules | New code with documented origins; no function-by-function paraphrasing of DreamShell |
| Existing NeXT modifications | Requirements and observed failure cases are useful; copying code requires individual provenance review |
| Artwork, fonts and music | Only independently owned/licensed assets; no inherited assets by assumption |
| Game databases | User-supplied references initially; redistribution terms reviewed before any bundling |
| Build tools and runtime libraries | Record their own licenses; separately inspect code/templates embedded into generated outputs |

KOS may contain contributions by authors who also worked on DreamShell. Properly licensed upstream KOS contributions remain usable; independence is about the source and applicable permissions, not erasing contributor names.

This work is source-aware research and should not be described as a formal clean-room process. A final dependency and provenance inventory is required before claiming that a release no longer contains DreamShell-licensed material.

FatFs's permissive code license and any separate exFAT patent questions are different matters. Its application note flags commercial-product considerations; this scope does not claim blanket clearance for a future commercial product. [FatFs licensing notes](https://elm-chan.org/fsw/ff/doc/appnote.html)

## 8. Implementation stages and exit criteria

| Stage | Deliverable | Exit criterion |
| --- | --- | --- |
| M1.0 — Foundation | New standalone source tree, pinned KOS/toolchain, host checks, bootable diagnostic image | Cold boot reaches a visible screen; controller works; build ID and diagnostics are available |
| M1.1 — Capability probe | Minimal disc/SD adapter with finite operations | Both TOCs and raw samples from both density regions are validated; FAT32/exFAT test files survive close/reopen and PC comparison |
| M1.2 — Bootstrap | Independent SD runtime loader and fallback diagnostic screen | Correct runtime loads from SD; missing, truncated, oversized or invalid runtime fails clearly |
| M1.3 — Good-disc capture | Track planner, raw files, GDI generation and manifest | One known-good retail disc completes all expected tracks with exact sizes and no hidden errors |
| M1.4 — Verification and resume | Full saved-file reread, PC verifier, controlled stop/restart | Hashes agree; resumed output equals uninterrupted output; wrong-disc resume is rejected |
| M1.5 — Hardware acceptance | Minimal polished UI, diagnostic bundle and installation notes | The acceptance matrix below passes for the stated hardware and media |

The first user test should be M1.1, packaged as a single diagnostic image. It can combine boot, card checks and selected raw disc samples to reduce repeated test cycles. It is not necessary to complete the desktop or write a general game loader first.

If raw high-density samples do not work through the selected upstream interfaces, record command results and investigate the independent BSD approach. If a new low-level transport proves necessary, revise the estimate before expanding that work. Do not quietly import DreamShell to make the test pass.

### Toolchain and build defaults

Research baseline: KOS commit `fcfa7d869471591ca1c777543261a7bfea7cb726`, dated 14 September 2026. The inspected stable Dreamcast toolchain profile specifies GCC 15.2.0, Binutils 2.45.1 and Newlib 4.6.0.20260123. Pin these inputs and the host build environment for the first prototype. [KOS revision](https://github.com/KallistiOS/KallistiOS/commit/fcfa7d869471591ca1c777543261a7bfea7cb726), [stable profile](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/profiles/dreamcast/stable.mk)

Use a cached cross-toolchain and incremental builds. Host checks should run quickly; full console builds produce test artifacts without publishing a release. Record compiler versions, source revisions and artifact hashes in each build.

A Dreamcast cross-compiler was not present in the current research workspace. No compilation was attempted. Establishing the reproducible toolchain is an implementation task.

The candidate `mkdcdisc` packager produces bootable CDI images. Its canonical project was located; README and MIT license were inspected through a GitHub fork. Confirm the exact source revision and bootstrap inputs before using it in CI. [Canonical project](https://gitlab.com/simulant/mkdcdisc), [inspected README](https://github.com/Mark65537/mkdcdisc/blob/main/README.md), [inspected license](https://github.com/Mark65537/mkdcdisc/blob/main/LICENSE)

## 9. Acceptance tests

| Test | Pass condition | Where |
| --- | --- | --- |
| Repeated cold boot | Five consecutive boots reach the diagnostic/runtime screen and accept controller input | Dreamcast |
| Boot CD replacement | Runtime remains functional after the CD is removed and a retail disc inserted | Dreamcast |
| FAT32 and exFAT storage | Test files crossing cluster boundaries and at least a track-sized streaming write reread correctly | Host fixtures and Dreamcast |
| Raw-sector coverage | Guarded buffers show no underfill/overwrite; data-sector payload matches a separate cooked read | Host and Dreamcast |
| Low/high-density addressing | Samples correspond to the expected track and address; FAD/GDI conversion is correct | Host and Dreamcast |
| Full known-good dump | Every declared track/range has the expected size and reference hashes where a compatible reference exists | Dreamcast and PC |
| Additional track layout | A second disc with a materially different TOC passes; do not infer layout from its game title | Dreamcast and PC |
| Controlled resume | Stops in early, middle and late portions produce the same final bytes as an uninterrupted dump | Host and Dreamcast |
| Wrong-disc resume | Different content is rejected before writing to the previous job | Host and Dreamcast |
| Failure accounting | Short reads/writes, retry exhaustion and sync errors cannot produce false completion | Host fault injection |
| Checkpoint corruption | Invalid/newer-truncated records cannot skip unwritten data | Host fault injection |
| Saved-file corruption | Altered bytes are detected and cannot receive “saved data verified” | Host and Dreamcast |
| Timeout/cancellation | Tested command timeouts terminate or reach a visible recovery failure; no silent endless retry | Host and Dreamcast |
| Existing card contents | Existing files outside the new test/job directories remain unchanged | PC comparison |

Use a clean disc from the existing collection for the first full test, such as Evolution 2 if it remains known-good. Use a second clean disc selected for a different actual TOC. Damaged Omikron is not the first acceptance fixture.

Record console/BIOS/drive details where observable, SD capacity and filesystem layout, build ID, transfer mode, per-track speeds, retries and hashes. Console photographs are useful for failures before logging is available. No Broadband Adapter or new hardware purchase is required for this milestone.

There is no hard speed promise initially. The reported NeXT rates around 680 KB/s are a comparison point, not a requirement for the first PIO implementation. Measure read-only, write-only, capture and verification rates independently before optimizing.

## 10. Open questions and next action

| Question | What resolves it |
| --- | --- |
| Which current KOS read mode produces correct raw high-density and audio bytes on this console? | M1.1 capability probe and byte comparisons |
| Does the selected command path recover from errors without deadlock? | Focused timeout test and review of adapter ownership |
| Does SD synchronization actually wait for the relevant writes? | Device-layer audit plus controlled persistence tests |
| Which precise GDI gap/audio convention matches the supplied reference? | Reference metadata and complete track comparisons |
| What are the test card's partition layout and cluster size? | Read-only diagnostic output; no formatting assumption |
| Are further KOS changes required? | Probe results; keep any justified fixes small, attributed and separately tracked |

The next implementation task is **M1.0/M1.1: create the independent build foundation and a bootable capability probe**. It should report useful evidence even if ripping is not yet possible.

The earlier “weeks to months” discussion was an order-of-magnitude estimate, not a schedule. The high-density and storage probes are the information needed to estimate the remaining stages responsibly. Work should advance by observable deliverables and recorded test results.

## 11. Research limits and retained evidence

This scope is based on upstream source inspection, original project documentation and the existing user requirements. No physical Dreamcast, disc, SD card or dump file was available to inspect during this task. No emulator or hardware run was performed, and no new source was published.

Key pinned/reference inputs:

| Input | Revision or version examined |
| --- | --- |
| KallistiOS | `fcfa7d869471591ca1c777543261a7bfea7cb726` |
| httpd-ack source mirror | `f5891519a01902d50b675ff38660ac87108de293`; historical program changelog through July 2008 |
| FatFs | R0.16, official patch-1 and patch-2 |
| KOS `makeip` and execution handoff | Same pinned KOS revision |
| mkdcdisc | Canonical project identified; fork README/license reviewed; implementation pin still required |

An additional upstream report describes DMA stream problems with non-sector-aligned ISO9660 files. It is a reported issue, not independently reproduced here; include boot-file alignment and disc-switch tests when selecting the bootstrap read path. [KOS issue 1492](https://github.com/KallistiOS/KallistiOS/issues/1492)

All new architecture, stage boundaries, test criteria and initial parameter choices in this document are engineering proposals. Source-derived facts and remaining uncertainties are distinguished above.
