<!-- SPDX-License-Identifier: GPL-3.0-only -->
# K-UI 2.0 RC14 Tyrfing: console acceptance from stable 1.8.5

**Candidate: 2.0-rc14. Build ID: use the accompanying `build.json` commit (first 12 characters).** Record the
actual `build.json` commit and on-screen build before testing. This plan covers
the complete change since stable **1.8.5**, including RC10–13 features retained
in RC14. RC14 applies the selected pixel artwork to the banner, runtime splash and
optional bootloader background. Those asset changes do not establish any
hardware test result. This document is a test plan, not
evidence that these checks have passed or that final 2.0 has shipped.

Use **Pass / Fail / Not run / No hardware / Expected limitation** for each ID.
Start with section A; complete B–F over later sessions. Section G needs the
specified hardware/media and remains explicitly unaccepted when not exercised.
An untested optional feature need not become a new release blocker, but its
release notes must retain that qualification. A failure affecting existing
working games, saved data, ordinary capture or recovery needs investigation.

## Setup and stop rules

- Keep the working **SCI/exFAT** card format, dumps and compatible independent
  boot CD. No reripping, conversion, ext4 formatting or new wiring is needed.
- With power off, archive the **complete current KUI folder**, including
  preferences and every Games payload. Merge the candidate's entire matching
  `KUI` tree; replacing only `runtime.kui` is insufficient. Keep the old complete
  package and known-working recovery runtime with its matching Games payloads.
- Safely eject before booting. Change cards, adapters or wiring with power off.
  Keep the boot CD available for BIOS-menu restarts. Never alter game files on
  the card while a game is running.
- Use disposable saves and spare formatted VMUs for writes. Back up important
  saves first. Do not switch third-party VMU banks during a job. Do not force
  power loss, unplug a VMU or remove storage to simulate faults.
- During an app operation, **B requests Stop**. Let it return to idle and finish
  its report; VMU metadata commits defer Stop through final verification.
  If a write is reported **Unverified**, stop further writes to that VMU and
  retain its SD backups. Do not repeatedly retry an uncertain metadata write.
- On a hang or unexpected return, photograph the full screen/build and record
  the last action. Use a game's supported return, or a cold reboot when needed;
  **A+B+X+Y+Start** works when the game invokes the BIOS-menu path, not universally.
- In runtime **Diagnostics**, **Y saves the log**, Up/Down scrolls and Start
  shows the newest lines. Keep automatic operation reports and source folders.
  Share comparison results/checksums, not BIOS images, flash contents, passwords
  or `/KUI/network-rip.cfg` secrets.

## A. Must-pass core: do this first

| ID | Action | Expected result |
| --- | --- | --- |
| A01 | Cold boot the complete installed package; photograph Home and its build. | Live **V2.0 RC14** and build agree with `build.json`; normal SCI/exFAT startup works. An older burned CD may still show its own older version/art. |
| A02 | Open Home's apps, return with B, and inspect the orange runtime artwork/header/icons on the actual display. | Text and controls remain legible, margins fit the TV, and there is no new input lag, crash or stuck menu. |
| A03 | Open Games; launch one established native title with CD music Off and normal choices. Play through an actual load. | Existing image still boots, input/audio work, and continued reads do not hang. Record exact image and reader. |
| A04 | On SCI, run the previously accepted **Worms Armageddon** image first with CD music Off, then On through **Y Options**. | Ordinary CE launch still works; On gives sustained music plus responsive gameplay through several loads/track changes. Record any repeat, gap or delay and when it starts. |
| A05 | Add an alphabetically early title while powered off, cold boot, then check Games on several pages. | New entry is in name order, equivalent entries remain grouped, and reopening Games restores the prior folder/highlight by name. |
| A06 | Perform one ordinary local GD capture: A, release A, press A again; B Stop partway, then X Resume, finish and Y Verify. | Resume continues the intended job; full completion and saved-byte verification are reported accurately. New never overwrites an earlier folder. Keep report and run PC verification below. |
| A07 | On a spare VMU, back up a selected save with X and open **R SD backups**. Preview restore to a free name/slot and cancel once. | Verified bundle appears; preview names the intended source/destination; cancellation leaves the card unchanged. Complete the restore/game check in D. |
| A08 | Confirm the retained recovery route remains usable from the compatible boot menu. | Explicit **Recovery**, or X on bootstrap Home, selects the retained recovery image; normal Start K-UI remains available. Do not replace recovery just to test RC14. |

If A fails, retain evidence and resolve that issue before treating additional
optional-feature passes as release acceptance. If time is short, report exactly
which A checks passed; do not imply B–G were covered.

## B. Games library, saved choices and launch regression

Controls: **X Refresh** and **Y view** on the library; **A** opens an entry.
On image details, **Y Options** opens saved launch choices. **START** launches
from Options; **A Launch** is available from details/confirmation. This replaces
the old 1.8.5 A/X/Y reader shortcuts; select the reader in Options now.

| ID | Action | Expected result |
| --- | --- | --- |
| B01 | Visit a large folder, page forward/back, switch List/Compact/Gallery with Y, leave and reopen it. Repeat after X Refresh. | Full folder/file names sort alphabetically ignoring ASCII case on every page/cache visit; saved view and named selection persist. No automatic game launch. |
| B02 | Remove only a disposable test selection/folder while powered off, then reopen Games. | Missing row falls back to the first row; missing remembered folder falls back to `/Games`. No stale selection opens another title. |
| B03 | Inspect an Original/2048 pair and an equivalent GDI/CUE pair; also inspect genuinely different layouts and unrelated BIN files. | Original/2048 picker remains; equivalent descriptors collapse, while distinct images stay accessible. Referenced track BINs are hidden as payloads. |
| B04 | Change Background loading, then Advanced background speed/boot encoding where available; leave/reopen and cold boot. Repeat on the paired image. | Choices persist for the exact image path; Original and 2048 copies stay independent. Native defaults are Standard; CE requires SCI Background. Automatic encoding follows inspected metadata. |
| B05 | Enable **Advanced → Temporary baseline**, change a choice, leave/reinspect, then reopen. Also toggle baseline off directly. | Baseline uses detected reader, automatic encoding, music/patches Off, preserving the explicit ZSO codec. Temporary changes never replace the saved preset; ending it restores saved choices. |
| B06 | Use **Advanced → Restore defaults**, leave and reopen. | Defaults are saved for this image only. Inside temporary baseline, Restore defaults is temporary too. |
| B07 | Run **Advanced → Check executable** on a known image and cancel a check if long enough. | It checks current image/options/package and stays in K-UI; it never runs the game or rewrites source images. Diagnostics records a precise result. Preparation success is not gameplay acceptance. |
| B08 | After a normal preparation, inspect `/KUI/apps/games/last-launch-a.txt` and `last-launch-b.txt` on the PC. | Latest valid sequence records build/image/reader/options and says game start is unconfirmed. A failed/cancelled attempt can leave preparation evidence. |
| B09 | Rescan box art with **START → Scan box art** after adding a title; stop once with B if convenient. | Existing/custom art and titles remain usable; completed covers survive Stop; equivalent descriptors do not create false duplicate titles. |
| B10 | Retest **Sonic Adventure, Grandia II and Skies of Arcadia**, then DOA2/Power Stone 1/2 as available. | Preserve the working 1.8.5 baseline through an actual stage/area load; compare the same image/card and choices when a change is suspected. No full playthrough is implied. |
| B11 | Compare one established native title with Background Off and On; compare Normal/Aggressive only if useful. | Supported choices load and play without a new regression; unsupported storage/maps give an explanation. Record options rather than assuming the faster-sounding choice is better. |

Existing two-slot **one-time native boot patches** are a separate optional check
under Advanced → RAM patches: A edits eight hex digits, Left/Right chooses a
digit, Up/Down changes it, A saves and B discards; START must not launch from
the editor. Keep patches **Off** for acceptance unless you already have a patch
for that exact executable revision. Do not invent test addresses. These slots
do not add the deferred generalized patch/screenshot/VMU-emulation features.

See [library](games-library.md), [options/reports](games-launch-options.md) and
[image-format boundaries](games-formats.md).

## C. Disc Ripper additions and output verification

For C08/C10, first copy the test pack's `Optional-Test-Fixtures/KUI` folder
onto the card, merging it with `KUI`. These are disposable scan fixtures, not
real game dumps. The normal install does not require them.

**START opens Advanced**; Up/Down selects its 13 tools, A opens/toggles, B
returns. Active B stops. In a folder browser A enters, B goes up, and START
cancels. The table calls out Y/X because their meaning depends on the browser.

| ID | Action | Expected result |
| --- | --- | --- |
| C01 | **Destination folder**: browse, Y saves current folder; X enters a missing absolute path. Cancel a change once. | Saved destination is correct; New creates missing parents only when needed. Cancel retains the old path. No dump starts merely by selecting a destination. |
| C02 | **New dump folder label**: apply a short custom label; start New twice, stopping when convenient. Clear the label for Auto. | Only the new folder label changes; second New gets a suffix and leaves the first intact. Descriptor/disc identity remain disc-derived. Keyboard: A types, X deletes, Y changes layer, START/DONE applies, B cancels. |
| C03 | **Saved job for Resume/Verify**: A enters a folder, Y selects highlighted child or X selects current. Resume/Verify it. Cancel selection once; idle ripper Left clears it. | Exact intended job is used, even with a custom label. Wrong-disc/missing/incomplete-for-Verify jobs fail clearly without trying another folder. Selection resets on launcher restart. |
| C04 | **Capture settings**: Left/Right changes CRC/SHA, readback, output, retries and strict checks; A saves, B discards. Reopen after restart. | Saved values persist; discarded edits do not. Retry choices are 1/5/10/20/50 additional attempts. Existing `bench.cfg` overrides, if present, take precedence. |
| C05 | Capture a known healthy Mode 1 GD with **Strict sector checks On**; restore normal defaults afterwards. | Newly acquired data passes framing/address/EDC/reserved/P/Q checks. Failure is reported with bounded retries; it never silently zero-fills. Strict Mode 2 is a refusal, not a pass. |
| C06 | Capture/export **GDI 2048**, stopping during export once if practical, then Resume. | Raw tracks and hidden `.capture.gdi` remain; data exports are cooked `trackNN.iso`, AUDIO remains raw. Resume can rebuild missing export output; visible GDI appears only after verified export. |
| C07 | Confirm retained BIN/CUE and one CSO/ZSO/CHD export if these matter to your workflow. | Original raw capture remains and post-capture exports verify; Resume/Verify keep the saved job's format/hash mode despite current preferences. CSO/CHD are PC-import containers, not new direct Games formats. Ripper ZSO is **Historical LZO1X**. |
| C08 | **Advanced CRC scan**: enter `/KUI/tests/scan/clean`, `damaged`, then `gdi-only`; Y starts on current folder. | Clean: 10 data + 4 audio, no issues. Damaged: 2 suspect sectors and 3 CRC mismatches. GDI-only: structural-only without a matching reference. New report per run; original files unchanged. |
| C09 | Scan one completed real raw job with the bundled catalogues available on the destination. | Saved-byte/hash result and catalogue result stay separate. Green FULL TRACK MATCH requires every raw track size/CRC; partial/no match never becomes full verification. |
| C10 | **Suspect-sector reread evidence** on the small damaged fixture or a duplicate supported raw dump; Y starts on current folder. | Storage-only reads need no disc; new `/KUI/recovery/readback-NNNN` retains initial/two repeat samples, up to 64 suspects. Stable bad bytes are not described as repaired. |
| C11 | **Repair to a separate copy** with a supported completed raw job and its matching GD; Y starts on current folder. Cancel once if useful. | New `/KUI/repair/job-NNNN` preserves originals. Final GDI requires full copied-byte verification; stopped/unresolved copies stay partial. Wrong disc is refused. Actual replacement acceptance additionally needs a disposable duplicate with a known supported Mode 1 defect. |
| C12 | Optional **Quick resume (sizes only)** on a stopped CRC job: read and confirm its reduced-check warning; compare with normal Resume. | It uses the intended selected job and still checks optical identity/checkpoints; it is not a full saved-prefix read. SHA jobs retain full prefix checks. |

On the PC, from the extracted package/repository tools directory, run:

```sh
python3 tools/verify_dump.py "/path/to/card/Games/Actual Job Folder"
```

Keep the full folder, automatic log and verifier output. For cooked output,
check both raw capture and exported form; a raw catalogue badge is not a
cooked-byte hash. [Controls](ripper-controls.md), [repair/evidence](repair-copy.md)
and [scan meanings](advanced-crc-scan.md) give format limits. Damaged-disc salvage
is a separate retained workflow, not a substitute for normal Resume/Verify;
optional start/Stop/Resume testing uses its own folders and
[verify_salvage.py](salvage-worker.md#pc-verification), with zero-fill Off by default.

## D. VMU tools, external import and bulk copying

**A Refresh, X Back up selected, Y Back up all, R SD backups, L Save/card
actions** on the VMU page. In Actions select the operation with Up/Down;
Left/Right chooses the destination for copy. Preview and confirmation are
separate; **B cancels/stops**. Use free names and spare cards for writes.

| ID | Action | Expected result |
| --- | --- | --- |
| D01 | With an LCD VMU connected at boot, inspect its display; also boot with no VMU if convenient. | K-UI/version displays in correct orientation and fits 48×32; missing/busy LCD never blocks startup. No hotplug LCD refresh is promised. |
| D02 | Back up selected/all disposable saves; restore a verified bundle to a free slot/name, cancel once, then confirm. Test in BIOS/game and back up again. | `.vms/.dir/.crc` remain together; restored payload matches, original source/prior saves remain. Duplicate-name restore is refused. |
| D03 | **L → Backup card image** on a normal and, if available, empty formatted VMU. | New verified `card.vmd` is exactly **131072 bytes**, with sidecar and completion marker; no VMU writes. Earlier backups remain. |
| D04 | **R SD backups → A** on that VMD; **X Extract selected**, then **Y Extract all**. Repeat without a physical VMU attached after a powered-off change. | Names/sizes agree; extraction makes new verified bundles without touching VMUs or source VMD. Empty image browses empty. Preview/restore an extracted save separately. |
| D05 | Browse an external standard raw VMD placed as `/KUI/backups/vmu/A1-0001/imported.vmd` without a K-UI sidecar. | Valid raw image opens read-only; wrong length/headered/structurally invalid images refuse. Folder prefixes A1–D2 are organizational, not required attached slots. |
| D06 | **R SD backups → Y Import save**: test a known VMI+VMS, DCI and bare VMS where available; cancel a preview once, then A stages. | Preview shows exact internal name/type/protection/blocks/date and provenance; new verified SD bundle plus `import.txt` appears. Import itself needs/writes no physical VMU. |
| D07 | Restore a staged known save to spare free space and check its game. Include a mini-game only if you have valid VMI/DCI metadata. | VMI/DCI recorded metadata survives. Bare VMS derives its 1–12 printable-character SD basename as VMU name and reports unknown timestamp; arbitrary renaming can break game recognition. |
| D08 | Select malformed/truncated input or a VMI with missing companion on a disposable copy. | Clear refusal; original files/prior backups remain. No physical restore follows automatically. Selecting VMI uses its recorded same-folder VMS resource, not the VMI filename stem. |
| D09 | With two VMUs, **L → Copy all saves to VMU**, choose target Left/Right, preview/cancel, then confirm with sufficient space and no conflicts. | Entire batch is preflighted and all sources backed up before writes; source/prior destination saves stay. Every copy verifies and works in BIOS/game. |
| D10 | Preview Copy all again to the same target; try insufficient capacity only if a suitable spare is already available. | Any name conflict/insufficient capacity refuses the entire batch before writes; it does not skip or overwrite individual names. |
| D11 | With multiple disposable saves and a fresh target, B Stop after a verified copy. | Completed copies/backups stay; Verified/Remaining counts match reality. Unverified is zero unless a commit could not be confirmed; any nonzero Unverified ends the test pending inspection. |

Selected-save copy/delete/restore are retained regression checks: use a
disposable save, cancel each preview once, then confirm only after its verified
SD backup exists. Keep the complete backup directory for any partial job.
See [VMD/restore](vmu-restore.md), [imports](vmu-import.md),
[Copy all](vmu-copy-all.md) and [LCD](vmu-startup.md).

## E. Read-only system comparison

Open **Settings → System Tools**. A opens the highlighted backup/compare;
in its file picker A enters folders or selects a file. B cancels/stops.

| ID | Action | Expected result |
| --- | --- | --- |
| E01 | Back up settings flash and visible BIOS bank, then compare each with its own `image.bin`. | Verified backup sizes **131072** and **2097152 bytes**; unchanged console gives **Exact match**. Settings can legitimately change between backup and comparison. |
| E02 | On the PC, make a duplicate with one changed byte, and another with wrong length; compare the duplicates. | Changed image reports one difference and correct zero-based offset; wrong length refuses. Neither image nor console memory is written. |
| E03 | Stop a comparison with B if long enough; keep source/image hashes. | Cancellation reports no successful comparison and creates no output. Only the currently visible BIOS bank is compared; no chip programming/bank switch/region change occurs. |

See [system backup and comparison scope](system-backups.md).

## F. Short retained-app regression

- [ ] **F01:** Open File Manager, music/audio CD, Network, memory/storage tools,
  Settings and GD Play as applicable. New Home icons should not change launches.
- [ ] **F02:** On disposable local files, copy/rename/delete once; play menu music
  and return through Home. Existing settings persist; foreground actions stay usable.
- [ ] **F03:** If a storage fault is suspected, run the existing Diagnostics
  storage comparison using temporary test data; retain its report. Repeating
  throughput/long-soak benchmarks is unnecessary just to accept artwork/menus.

## G. Hardware-dependent and experimental coverage

Mark missing hardware/media **No hardware**. Do not reformat the working card
or change wiring merely to complete this section.

| ID | Setup/action | Expected result and scope |
| --- | --- | --- |
| G01 | Optional new bootstrap: copy its `KUI/tools.kui`, pause compatible CD startup, **Card tools**, then B for menu. Check RC14/build; **Start K-UI**. | Matching orange artwork fits countdown/menu controls; card preview works without another burn. Only a newly burned CDI changes the under-SEGA sword badge. |
| G02 | On the new CDI if burned, check unattended countdown, pause by input, explicit SCI source with Left/Right, Recovery and log viewer Y. | Normal runtime boots; manual Start bypasses optional `boot.kui`; explicit source does not silently fall back. Preserve the existing recovery image. |
| G03 | Native CD music: **Toy Commander** raw image, Y Options → CD music On; include track/scene changes and playable DATA reads. Test MDK2/cooked variants only if available. | SCI Standard native route; Toy audio should remain continuous, while its known FMV stalls remain recorded. MDK2/cooked/unfamiliar matches stay Experimental. |
| G04 | CE CD music: available Armada/BAM4/World Party profiles, plus Armageddon Original/2048 where owned; several minutes of music and actual gameplay. | Four checked adapters, raw2352 AUDIO retained. Track restart/stop and DATA reads work within recorded limits. World Party stays Experimental; earlier per-title FMV/audio limits are not silently certified fixed. |
| G05 | If already owned, inspect an unsupported music runtime and use Check executable with music On. | Clear refusal before handoff for unknown kernel/driver contract; music Off remains an ordinary launch attempt. Do not obtain another game solely for this check. |
| G06 | Native ZSO on SCI: known **Standard LZ4**, then known **Historical LZO1X**, one cooked 2048-byte data track; Background/music/patches Off. **Y Options → ZSO codec → A**, or Y after failed inspection. | Explicit choice saves/reinspects; boot, real load and continued gameplay reads pass. Compare the same title's uncompressed image. Wrong codec never triggers automatic fallback; raw-only blocks cannot identify a codec. |
| G07 | ZSO unsupported combinations or excessive fragmentation, if naturally encountered. | Refused with reason before handoff: no CE/CDDA/Background/patches or non-SCI route; at most 31 physical fragments. No universal compressed-game claim. |
| G08 | Ordinary W5500/Wi-Fi networking: **power off and use SCIF storage**, then Network A Inspect, X Test, Y FTP. | SCI is available to the adapter; SCI-storage sessions instead refuse ordinary network probing. Do not expect simultaneous SCI card + FTP. |
| G09 | W5500 and C5 separately: FTP upload/download the same 16–64 MB test file; start client immediately after Ready. Stop B, restart A, reconnect repeatedly. | No listener race/hang or new close/reconnect regression; upload/download SHA-256 values equal the PC original. Record speed, adapter/bus/READY and errors; old speeds are not pass thresholds. |
| G10 | C5: **Network START Wi-Fi**; first-row Bands Left/Right, stop FTP first. Test 2.4-only and 5-only using reachable matching SSIDs; return A Inspect. | Successful change saves/reconnects; actual inspected band/channel agrees when online. Separate SSID requires separate join; band restriction alone cannot join another name. Optional Wi-Fi loss/recovery reports status clearly. |
| G11 | C5 firmware update only with matching separately supplied app image+SHA sidecar in `/KUI/firmware`. Wi-Fi page START checks; B cancels once, then recheck/A installs. | Check identifies C5/version/path; update to intended firmware preserves credentials, restarts adapter and verifies it. Repeat FTP/hash check. Do not assume firmware is bundled just because runtime supports updating. |
| G12 | Direct PC rip: prepare dedicated authenticated exFAT/FAT32 volume image/helper per linked guide; local `/KUI/network-rip.cfg`; Ripper START → Rip destination PC. | Separate path loads config then releases local storage for SCI network ownership. New/Stop/automatic Resume/Verify target the same remote volume; no temporary local rip. Browsers remain local, not remote. |
| G13 | After PC capture, stop receiver before mounting/extracting the image; run `verify_dump.py`. Optionally stop/restart helper between capture and Resume. | Complete saved output verifies; disconnect gives explicit stop, not silent retry/reconnect. Same volume identity is required. Destination toggle resets on launcher restart. Restore Local afterwards. |
| G14 | Independently known **single-session** CD/audio CD: Ripper START → Single-session CD source, A toggle; capture/Stop/Resume/Verify. | Session-only opt-in, default Off; admitted CD forces BIN/CUE with actual INDEX01 intervals. Session count is declared/unqueried; no invented GD high-density area/subchannel capture. Restore Off for ordinary use. |

Network details: [Wi-Fi](wifi.md), [performance/hash check](wifi-performance-test.md),
[firmware update](wifi-console-update.md), [FTP](ftp.md),
[PC volume setup](network-rip.md), [CD capture](cd-capture.md).
Game limits: [CD music](experimental-cdda.md), [ZSO](games-zso.md).

## Known limits: record them without promising a fix

- Time Stalkers menu return; Toy Commander and some other FMV stalls/skips.
- Universal CDDA, unfamiliar native drivers/CE kernels, Quake III CD music,
  cooked AUDIO and arbitrary CDI/CUE/ISO music routes are unsupported.
- Direct CSO/CHD remain PC-import workflows. ZSO has the narrow G06/G07 scope.
- IDE/CF is unaccepted synchronous PIO; ATA DMA/CE on IDE or SCIF, runtime ext4,
  multi-volume apps and shared SCI card/network use remain future work.
- No full-card VMD restore/format/overwrite, BIOS programming/permanent region
  changes, gameplay VMU emulation/screenshots, or skin/splash/boot selector.
- Multisession/self-boot CD, INDEX00/subchannel/jitter/offset preservation,
  Mode 2 optical repair and generic ECC reconstruction are not established.

## Result to send back

```text
Package: RC14; build.json commit: ______; live build: ______
Console/BIOS/video: ______; storage: SCI or SCIF / exFAT / card: ______
Network when tested: W5500 or C5; firmware/band/channel/READY: ______
VMUs: slots/models/free blocks; disposable saves used: ______
Passed IDs: ______
Failed IDs: ______
Not run / No hardware IDs: ______
Game failure: title/revision/product, raw or 2048, GDI/other format,
  Standard/Background Normal/Aggressive, CD music, codec, patches: ______
Exact action/scene/time until failure and repeat count: ______
Expected vs actual; old stable result with SAME image/options if checked: ______
Ripper: disc, output/hash/strict/retry settings, exact job path: ______
VMU: source/destination slots, operation/name, Verified/Remaining/Unverified: ______
Evidence: screen/video + saved Diagnostics + launch reports or job folder: ______
PC verification/hash results: ______
```

Keep partial output and reports. Final publishing follows acceptance of the
complete matching package, with untested experimental paths still qualified;
host/build/checksum success alone does not establish these console outcomes.
