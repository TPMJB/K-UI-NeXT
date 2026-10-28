# Legacy GD Ripper comparison for 2.0 Tyrfing

Source audit updated 2026-10-28. Legacy: `TPMJB/DreamShell_NeXT`, `master`
at `2a530929` (GD Ripper app 2.2.5). Independent implementation:
`TPMJB/K-UI-NeXT`, Tyrfing integration sources, including the new CD/acquisition,
network-destination, cooked-export and repair/evidence workers. Legacy line
references identify the inspected old implementation; current K-UI files are
named directly because integration moves their lines.

The recovery expansion adds bounded drive reinitialization after released commands,
identity-read retries, CD-profile salvage, direct payload-only GDI capture and
explicit repair/resume controls. These are implemented capabilities with stated
format and media contracts, not a claim of universal disc compatibility. The
integrated console runtime still needs damaged-disc and throughput acceptance.
No PC recovery expansion is included in this update. No DreamShell runtime or
drive implementation is imported; independently authored checksum/legacy-format
algorithms retain their provenance.

## Existing functionality, independently implemented

| User workflow | Legacy evidence | Current K-UI behavior |
| --- | --- | --- |
| Read the title and create a named game folder | `module.c:522,1410` | `src/core/capture.c` identifies the disc; `src/core/destination.c` sanitizes titles and reserves numbered collision suffixes. Present |
| Choose local destination folders | `module.c:2555–2637`, `modules/folders.h` | Saved parent, browser and path entry in `src/core/shell.c` and `destination_file.c`. Local SCIF/SCI SD and IDE/CF transports already exist; IDE ripping was not missing |
| Stop and resume a partial capture | `module.c:983–1016,2003,2147` | Alternating checkpoints, full optical identity, committed-prefix checks and explicit Quick Resume. SHA jobs retain full prefix reads |
| Retry reads and stop on fatal drive/storage failures | `module.c:2339–2408` | Bounded single-sector fallback, exact writes, sync/error checks and no normal zero-fill. Default remains ten additional retries |
| Stream CRC and Redump/TOSEC matching | `module.c:1557–1574`, `modules/verify.c:708–749` | `known_dumps.c` distinguishes full/data/listed-data/partial/inconclusive results. Catalogue matching is separate from saved-file reread verification |
| Verify saved bytes and scan raw sectors | `module.c:1905–1937`, `modules/verify.c:196–300` | Normal Verify rereads recorded hashes. Advanced CRC scans completed jobs or supported imported raw GDI, including Mode 1 sync/address/EDC/reserved/P/Q, without an inserted disc |
| Recover tracked holes in a separate damaged-disc job | `modules/recovery.c`, `module.c:1535–1543` | `src/core/salvage.c` has separate journals, adaptive collection, automatic alternating recovery sweeps (Until stopped or 1/5/10/20/50), versioned CD/GD identity and durable per-target evidence. It does not turn ordinary capture into a repair operation |
| Progress, speed/time, retry/FAD and diagnostics | `module.c:1656–1744,229–281` | `capture_display.c`, native optical timing and automatic operation reports. Present |
| Output formats | `module.c:1019–1031` | Raw GDI, BIN/CUE, CSO, historical LZO ZSO, CHDv4, raw-retaining GDI 2048 and explicit direct GDI 2048. Direct mode saves payloads plus raw audio; other exports retain raw sources |

## Exact-job and naming controls

| Legacy capability | Independent implementation | Limits |
| --- | --- | --- |
| Select an existing dump, using its `rip.state` and exact name (`module.c:2555–2557,2589–2592,2623–2634`) | Advanced > Saved job for Resume/Verify sets `kui_capture_options.job_directory`. Only that candidate is checked; invalid state never falls back to another job | Browser lists local storage. Full path, optical identity, checkpoint, format/hash mode and prefix checks remain. Automatic selection is restored with Left on idle Disc Ripper |
| Edit the new output folder name (`app.xml:168`, `module.c:512–520`) | Advanced > New dump folder label changes New directory presentation using existing sanitization and collision rules | Session-only label; optical title, descriptor base, identity and existing jobs remain unchanged. Resume/Verify ignore the current label |

## Seven additional workflows now implemented

| Legacy capability and evidence | Current independent source implementation | Material scope and limits |
| --- | --- | --- |
| **Direct PC rip destination.** Legacy `/pc` mount offered alongside SD/IDE (`app.xml:199–206`, `modules/folders.h:13`) | Advanced > Rip destination: local / PC selects `capture_network` for normal New/Resume/Verify. The authenticated `network_rip` client and `tools/network_rip_server.py` expose a dedicated PC FAT32/exFAT volume image to the existing FatFs capture engine | W5500/Wi-Fi socket adapters; no SMB/NFS or arbitrary PC-directory mount. Local card supplies `/KUI/network-rip.cfg`. Session toggle defaults local and clears exact-job selection. Browsers remain local; remote Resume/Verify use automatic matching in the same remote volume. Disconnect stops without automatic reconnect. Console/network throughput acceptance pending. See [PC destination](network-rip.md) |
| **Cooked 2048-byte GDI data tracks.** Legacy cooked ISO plus raw audio (`module.c:1019–1026,2483–2507`) | Capture settings > Output format > GDI 2048. `src/core/capture_cooked.c` exports `trackNN.iso` after full raw readback, retains original raw tracks/checkpoints/hidden GDI, and publishes the primary GDI after cooked readback | Original LBAs/audio remain; Mode 1/Mode 2 Form 1 payloads require valid framing/EDC. That existing option remains a derivative. The new **GDI 2048 direct** option validates each raw optical batch and saves only its 2048-byte data payloads plus raw audio, with stride-aware checkpoints and mandatory saved-byte readback. See [direct capture](direct-cooked-capture.md). Stream reference CRCs remain the raw tracks' CRCs. Stop/failure preserves the raw job for explicit Resume |
| **CD/CD-R/audio track capture.** Legacy reachable non-GD TOC path (`module.c:1429,1060–1065`) | Advanced > Single-session CD source admits the separate `cd-toc-index01-raw2352-v1` plan in `capture_plan.c`; native adapter forces BIN/CUE and uses generic CD titles when no actual Sega title exists | Explicit user-declared single-session opt-in, default off. Native checks ready CD type and TOC but does not query/prove session count. CD salvage and repair use media-aware plans and CD-specific content identity; no guessed BIOS GETSES ABI. Capture and CD-profile recovery use INDEX01 windows only; no measured INDEX00/subchannels, no multisession claim, no fabricated GD start. Unlocated type-changing pregaps can fail closed. See [CD profile](cd-capture.md) |
| **Optional acquisition P/Q checks.** Legacy Advanced CRC changes ordinary read validation (`module.c:2085–2094`) | Capture settings > Strict sector checks enables existing independent `recovery_sector.c` validation on newly acquired data sectors through both synchronous and DMA paths | Default off preserves normal EDC acquisition. Strict supports Mode 1 sync/address/EDC/reserved/P/Q; Mode 2 is explicitly refused. Audio has no equivalent parity. No repair, retrospective saved-prefix parity promise or independent-reference claim |
| **Repair an ordinary/imported saved dump.** Legacy suspect queues and explicit optical replacement | Separate-copy repair resumes saved progress and collects missing-source holes before alternating recovery sweeps. Explicit in-place repair patches completed raw sources with durable original/candidate records and restart reconciliation. Identity checks use bounded exact retries or broad imported anchors. Separate-copy repair also offers explicit checked-prefix alternate anchors when original samples are unreadable; successful repairs receive catalogue comparison | Normal captures, supported raw GDI/CUE and the validated legacy subset retain distinct admission contracts. Unsupported forms, unlocalizable audio/hash damage and generic ECC reconstruction remain outside automatic structural repair. In-place active guards block ordinary Resume/Verify until consistent source metadata is finalized. See [repair copies](repair-copy.md) |
| **Storage-instability forensic samples.** Legacy two reopen passes, byte differences and raw evidence (`modules/readback.c:14–31,109–174`, `module.c:1422–1426`) | Advanced > Suspect-sector reread evidence scans local raw files and saves three samples per selected suspect: initial bytes plus two independent reopen/read/close passes, difference counts/offsets and CRCs | Storage-only, no optical reads or original-track writes. At most 64 suspects sampled; remaining suspects still counted. Stable reads do not prove correct data or localize audio/hash damage. Separate evidence `.part` stays incomplete; see [saved-sector rereads](repair-copy.md#saved-sector-rereads) |
| **Configurable ordinary-read retries.** Legacy cycles 1/5/10/20/50 (`module.c:503–509`) | Capture settings > Read retries persists the same five choices; `capture.c` bounds additional attempts, including identity reads, without replenishing the budget on batch shrink | Default remains ten additional attempts. Fatal/Stop exits remain immediate. A DMA failure can first enter the existing synchronous fallback. These are additional read attempts, distinct from salvage sweeps and repair candidate-pair limits |

The 14 Advanced tools and five capture settings are listed in
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

## Remaining scope boundaries

- Drive reinitialization refuses failed-abort poison, active commands, media change
  and cancellation. Exact content checks follow a successful layout reprepare.
- CD support uses user-declared single-session firmware TOC INDEX01 windows;
  INDEX00, subchannels, audio offsets and multisession reconstruction are not added.
- Raw repair cannot silently synthesize missing ECC/audio from cooked-only or
  compressed containers. Unsupported input is refused before mutations.
- PC volume support remains the pre-existing normal New/Resume/Verify workflow.
- Physical keyboard/mouse, FAT16 and descriptor-name editing remain out of scope.
- Host fault tests do not establish damaged-disc console performance.
