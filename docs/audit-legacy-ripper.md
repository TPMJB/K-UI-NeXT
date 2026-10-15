# Legacy GD Ripper comparison for 2.0 Tyrfing

Source audit updated 2026-10-15. Legacy: `TPMJB/DreamShell_NeXT`, `master`
at `2a530929` (GD Ripper app 2.2.5). Independent implementation:
`TPMJB/K-UI-NeXT`, Tyrfing integration sources, including the new CD/acquisition,
network-destination, cooked-export and repair/evidence workers. Legacy line
references identify the inspected old implementation; current K-UI files are
named directly because integration moves their lines.

All seven remaining workflow differences identified after exact job selection
and custom folder labels now have independent source implementations. Their
bounds differ deliberately from the legacy app, as documented below. This is
source parity for the listed workflows, not full compatibility with every disc,
network mount or repair case. The integrated console runtime still needs
acceptance. No final 2.0 release is announced, and G1 SD board experiments
remain excluded. These new workflow implementations do not copy SWAT or
DreamShell runtime/drive code. The pre-existing sector validator separately
credits checksum helpers adapted from TPMJB's original legacy ripper.

## Existing functionality, independently implemented

| User workflow | Legacy evidence | Current K-UI behavior |
| --- | --- | --- |
| Read the title and create a named game folder | `module.c:522,1410` | `src/core/capture.c` identifies the disc; `src/core/destination.c` sanitizes titles and reserves numbered collision suffixes. Present |
| Choose local destination folders | `module.c:2555–2637`, `modules/folders.h` | Saved parent, browser and path entry in `src/core/shell.c` and `destination_file.c`. Local SCIF/SCI SD and IDE/CF transports already exist; IDE ripping was not missing |
| Stop and resume a partial capture | `module.c:983–1016,2003,2147` | Alternating checkpoints, full optical identity, committed-prefix checks and explicit Quick Resume. SHA jobs retain full prefix reads |
| Retry reads and stop on fatal drive/storage failures | `module.c:2339–2408` | Bounded single-sector fallback, exact writes, sync/error checks and no normal zero-fill. Default remains ten additional retries |
| Stream CRC and Redump/TOSEC matching | `module.c:1557–1574`, `modules/verify.c:708–749` | `known_dumps.c` distinguishes full/data/listed-data/partial/inconclusive results. Catalogue matching is separate from saved-file reread verification |
| Verify saved bytes and scan raw sectors | `module.c:1905–1937`, `modules/verify.c:196–300` | Normal Verify rereads recorded hashes. Advanced CRC scans completed jobs or supported imported raw GDI, including Mode 1 sync/address/EDC/reserved/P/Q, without an inserted disc |
| Recover tracked holes in a separate damaged-disc job | `modules/recovery.c`, `module.c:1535–1543` | `src/core/salvage.c` has separate journals, explicit zero-fill consent and 1/5/10/20/50 recovery sweeps. It does not turn ordinary capture into a repair operation |
| Progress, speed/time, retry/FAD and diagnostics | `module.c:1656–1744,229–281` | `capture_display.c`, native optical timing and automatic operation reports. Present |
| Output formats | `module.c:1019–1031` | Raw GDI, BIN/CUE, CSO, historical LZO ZSO, CHDv4 and new GDI 2048. Raw source files remain authoritative |

## Exact-job and naming controls

| Legacy capability | Independent implementation | Limits |
| --- | --- | --- |
| Select an existing dump, using its `rip.state` and exact name (`module.c:2555–2557,2589–2592,2623–2634`) | Advanced > Saved job for Resume/Verify sets `kui_capture_options.job_directory`. Only that candidate is checked; invalid state never falls back to another job | Browser lists local storage. Full path, optical identity, checkpoint, format/hash mode and prefix checks remain. Automatic selection is restored with Left on idle Disc Ripper |
| Edit the new output folder name (`app.xml:168`, `module.c:512–520`) | Advanced > New dump folder label changes New directory presentation using existing sanitization and collision rules | Session-only label; optical title, descriptor base, identity and existing jobs remain unchanged. Resume/Verify ignore the current label |

## Seven additional workflows now implemented

| Legacy capability and evidence | Current independent source implementation | Material scope and limits |
| --- | --- | --- |
| **Direct PC rip destination.** Legacy `/pc` mount offered alongside SD/IDE (`app.xml:199–206`, `modules/folders.h:13`) | Advanced > Rip destination: local / PC selects `capture_network` for normal New/Resume/Verify. The authenticated `network_rip` client and `tools/network_rip_server.py` expose a dedicated PC FAT32/exFAT volume image to the existing FatFs capture engine | W5500/Wi-Fi socket adapters; no SMB/NFS or arbitrary PC-directory mount. Local card supplies `/KUI/network-rip.cfg`. Session toggle defaults local and clears exact-job selection. Browsers remain local; remote Resume/Verify use automatic matching in the same remote volume. Disconnect stops without automatic reconnect. Console/network throughput acceptance pending. See [PC destination](network-rip.md) |
| **Cooked 2048-byte GDI data tracks.** Legacy cooked ISO plus raw audio (`module.c:1019–1026,2483–2507`) | Capture settings > Output format > GDI 2048. `src/core/capture_cooked.c` exports `trackNN.iso` after full raw readback, retains original raw tracks/checkpoints/hidden GDI, and publishes the primary GDI after cooked readback | Original LBAs/audio remain; Mode 1/Mode 2 Form 1 payloads require valid framing/EDC. It is a derivative, not a replacement capture. Stream reference CRCs remain the raw tracks' CRCs. Stop/failure preserves the raw job for explicit Resume |
| **CD/CD-R/audio track capture.** Legacy reachable non-GD TOC path (`module.c:1429,1060–1065`) | Advanced > Single-session CD source admits the separate `cd-toc-index01-raw2352-v1` plan in `capture_plan.c`; native adapter forces BIN/CUE and uses generic CD titles when no actual Sega title exists | Explicit user-declared single-session opt-in, default off. Native checks ready CD type and TOC but does not query/prove session count. No guessed BIOS GETSES ABI. Captures INDEX01 windows only; no measured INDEX00/subchannels, no multisession claim, no fabricated GD start. Unlocated type-changing pregaps can fail closed. See [CD profile](cd-capture.md) |
| **Optional acquisition P/Q checks.** Legacy Advanced CRC changes ordinary read validation (`module.c:2085–2094`) | Capture settings > Strict sector checks enables existing independent `recovery_sector.c` validation on newly acquired data sectors through both synchronous and DMA paths | Default off preserves normal EDC acquisition. Strict supports Mode 1 sync/address/EDC/reserved/P/Q; Mode 2 is explicitly refused. Audio has no equivalent parity. No repair, retrospective saved-prefix parity promise or independent-reference claim |
| **Repair an ordinary/imported saved dump.** Legacy `.suspect` scan plus Start/Resume optical replacement (`module.c:2177–2180,2950–3015`) | Advanced > Repair to a separate copy invokes `src/core/repair_copy.c` on supported completed raw-track jobs or imported raw-2352 GDI folders, with matching GD-ROM, stable saved rereads, durable original/candidate evidence and full copied-byte readback | Localized Mode 1 structural damage only; no Mode 2 repair, automatic localization of audio/whole-track hash damage, generic ECC reconstruction or in-place changes. Imported identity uses two healthy high-density data anchors; imported audio may remain unverified. No GDI until completion. Failed/stopped/unresolved partial copies are retained; another attempt creates a new copy, without automatic repair resume. See [repair copies](repair-copy.md) |
| **Storage-instability forensic samples.** Legacy two reopen passes, byte differences and raw evidence (`modules/readback.c:14–31,109–174`, `module.c:1422–1426`) | Advanced > Suspect-sector reread evidence scans local raw files and saves three samples per selected suspect: initial bytes plus two independent reopen/read/close passes, difference counts/offsets and CRCs | Storage-only, no optical reads or original-track writes. At most 64 suspects sampled; remaining suspects still counted. Stable reads do not prove correct data or localize audio/hash damage. Separate evidence `.part` stays incomplete; see [saved-sector rereads](repair-copy.md#saved-sector-rereads) |
| **Configurable ordinary-read retries.** Legacy cycles 1/5/10/20/50 (`module.c:503–509`) | Capture settings > Read retries persists the same five choices; `capture.c` bounds additional attempts, including identity reads, without replenishing the budget on batch shrink | Default remains ten additional attempts. Fatal/Stop exits remain immediate. A DMA failure can first enter the existing synchronous fallback. These are additional read attempts, distinct from salvage sweeps and repair candidate-pair limits |

The 13 Advanced tools and five capture settings are listed in
[ripper-controls.md](ripper-controls.md). PC target, CD opt-in, exact job and
folder label are session choices; capture hash/readback/format/retry/strict
preferences are saved. Explicit `bench.cfg` values retain precedence.

## Verification and acceptance

The changes retain separate meanings for acquisition validity, storage
consistency and independent reference equality. EDC/P/Q can reject invalid
Mode 1 sectors; full saved-file readback compares storage with the captured
stream; only a full matching independent catalogue/reference covers that
reference's track sizes and hashes. None of these alone supplies missing CD
subchannels, audio offset correction or generic ECC reconstruction.

Real FatFs host coverage includes existing GD capture/stop/resume/verification,
the new CD plans and strict/retry paths, cooked exports, repair/evidence faults,
and FAT32/exFAT preservation. PC destination tests use actual loopback TCP and
the capture engine. These source and host results do not establish console
adapter compatibility, new UI acceptance or whole-disc archival equivalence.

The default local retail-GD path retains its two-area preparation and normal
DMA reader. CD opt-in alone selects the new media-aware preparation; it still
accepts GD media while enabled. Strict checks default off, normal retries remain
ten and GDI stays the retail default. Broader CD session/index/subchannel work,
other remote filesystems and unsupported repair cases remain outside the
implemented scope rather than being implied by an accepted extension or menu.
