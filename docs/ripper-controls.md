# Disc Ripper: destinations, controls and results

The ripper saves new captures under a chosen destination, using the disc title
by default. The default parent is **`/Games`** on local storage. Tyrfing source
adds exact job selection, optional folder labels, a direct PC volume target,
cooked GDI export, a declared CD profile, strict acquisition, retry controls,
repair copies and saved-sector reread evidence. The integrated runtime still
needs console acceptance; this guide does not announce a final 2.0 release.
Use the existing compatible bootstrap and the complete matching SD runtime
package described in [sd-bootstrap.md](sd-bootstrap.md).

## Controls

| Screen | Controls |
| --- | --- |
| Launcher | D-pad or stick selects; A opens Disc Ripper, Settings or Diagnostics |
| Disc Ripper | A opens New confirmation; release and press A again to begin. X resumes; Y verifies saved files. Left clears the exact saved-job selection and restores Automatic. L/R select bundled songs; Start opens Advanced. B returns home while idle |
| Advanced | Up/Down selects one of 15 tools; A opens the tool or changes its session toggle. The list scrolls after eight visible rows. B returns to the ripper |
| Destination browser | Up/Down selects; A opens a subfolder; B goes to its parent. Left/Right changes page. Y saves the current destination; X opens path entry. Start cancels |
| Saved-job browser | A opens a folder; Y selects the highlighted child; X selects the current folder. B goes to the parent, or leaves at the browser root. Start cancels and retains the previous selection |
| Scan, repair or evidence browser | A opens a local folder; Y starts the chosen operation on the current folder. Start cancels |
| Path or folder-label keyboard | Directions select a key; A enters it; X deletes; Y changes the keyboard layer. Start or DONE applies the value. B cancels |
| Capture settings | Up/Down selects one of five rows; Left/Right changes its value. A saves. B discards unsaved edits |
| Active operation | B requests Stop. Wait for the operation and automatic diagnostic report save to finish before another action |
| Diagnostics | A runs disc samples; X runs the storage test; Y saves the log. Up/Down scrolls; Start shows the latest lines. L records an mstats snapshot; R retains the configured benchmark entry |

Directions repeat while held. Action buttons require a new press, so holding A
does not pass through New confirmation. B takes priority over a simultaneous
launch or confirmation.

The Advanced tools appear in this order:

| Tool | Action |
| --- | --- |
| Verify saved files | Reread the selected or latest matching completed job |
| Resume interrupted dump | Check and continue the selected or latest matching partial job |
| Capture settings | Open the five saved capture preferences |
| Quick resume (sizes only) | Confirm the reduced prefix check; SHA jobs retain full prefix reads, and direct 2048/repaired jobs retain complete saved-byte verification |
| Destination folder | Browse or enter the local destination path |
| Advanced CRC scan | Scan saved raw sectors/hashes and compare catalogues; no optical repair |
| Damaged disc salvage | Open the separate tracked-hole recovery workflow |
| Saved job for Resume/Verify | Select an exact local candidate folder |
| New dump folder label | Set an optional label for the next New folder |
| Repair / resume a separate copy | Choose a supported raw dump and its matching disc; continue its checked repair job |
| Suspect-sector reread evidence | Save bounded repeat-read evidence from a local raw dump |
| Rip destination: local / PC | A switches the destination for normal New/Resume/Verify |
| Single-session CD source | A enables/disables the user-declared CD TOC profile |
| Repair / resume in place | Choose a supported raw job; confirm targeted repair and continuation with retained sector backups |
| Start a new repair copy | Create a fresh numbered transaction while retaining previous repairs and evidence |

## Choose a destination

Open **Start > Destination folder**. Browse existing local folders and press Y
to use the current folder. To choose a missing path, press X and enter an
absolute path such as `/Games/Imports`; Start or DONE saves it. New creates
missing parent directories before creating its exclusive job folder. Saving a
destination alone creates no dump. Cancel keeps the previous destination.
Invalid paths, `..`, reserved names and paths beyond the supported length are
refused with an explanation. Existing UTF-8 folder names remain intact when
selected, even when the font cannot display every character.

The local storage backend is selected globally; SCIF/SCI SD and IDE/CF are
separate transports. The ripper's local/PC toggle is not a new hardware selector.

For direct PC capture, first follow [network-rip.md](network-rip.md) to create a
dedicated FAT32/exFAT volume image, run the authenticated Python receiver, and
copy its configuration to local **`/KUI/network-rip.cfg`**. Then open
**Start > Rip destination: local / PC** and press A to select PC. This session
choice resets on launcher restart. Switching either direction clears the exact
saved-job selection so a local job is not silently reused as a remote choice.

Only normal New, Resume and Verify use the selected PC volume. The folder
browser and saved-job picker still list **local storage**, and scan/repair/
evidence tools remain local. The destination path is reused inside the PC
volume; the UI does not browse remote directories. Use Automatic selection for
remote Resume/Verify, with the same volume image and identity. Folder labels
work for remote New jobs. This is a dedicated block-volume destination through
W5500 or Wi-Fi, not an SMB/NFS share or a mounted `/pc` folder. Disconnects stop
the operation without automatic reconnect; reconnect explicitly and use Resume
or Verify. Adapter compatibility and throughput require console acceptance.

## Capture settings and output formats

Open **Start > Capture settings**, change a row with Left/Right, then press A
to save. Explicit `KUI/bench.cfg` keys continue to override saved preferences.

| Row | Choices and effect |
| --- | --- |
| Capture hashes | CRC32, or CRC32 + SHA-256. Existing jobs keep their recorded hash mode |
| Read back saved files | Off/On for CRC-only raw GDI/BIN-CUE. SHA jobs and post-capture exports require full raw readback |
| Output format | GDI, BIN/CUE, CSO, ZSO, CHD or GDI 2048. Applies to New; Resume/Verify keep the job's format |
| Read retries | 1/5/10/20/50 additional attempts after a failed request; default 10. Shrinking a batch to one sector does not reset this budget |
| Strict sector checks | Off by default. On checks newly acquired Mode 1 sync/address/EDC/reserved bytes/P/Q. Strict Mode 2 is refused; audio has no equivalent sector parity |

GDI remains the retail-GD default. BIN/CUE references the same captured raw
tracks. CSO, historical DreamShell-LZO ZSO and CHDv4 are verified post-capture
exports that retain the raw source files and hidden `.capture.gdi` for Resume,
Verify and reference checks. CSO/ZSO export one cooked high-density data track;
audio stays in the raw files. CHD includes captured data and audio. These
CSO and CHD formats require PC import before Games can launch them. The separate
[native ZSO reader](games-zso.md) supports LZ4 and historical LZO1X through
explicit codec selection for its bounded native SCI Standard path. Select LZO
for this ripper's ZSO output; it is not the LZ4 default. The container contains
the data stream only, with raw audio remaining beside it. See
[Games formats](games-formats.md) for launch restrictions and import options.

**GDI 2048** first verifies every saved raw byte, then exports each data track
as `trackNN.iso` with 2048-byte payloads. The visible GDI keeps the actual track
LBAs and references those ISO files plus the original raw audio. All raw data,
checkpoints and `.capture.gdi` remain. Every source sector passes its framing
and EDC check; the new payloads receive complete saved-byte readback before the
primary descriptor is published. Stop or export failure retains the raw job;
Resume recreates missing output without recapturing completed tracks; optical
identity sampling still runs.
The stream CRC/catalogue badge still describes the **raw capture**, not the
cooked payloads. Use the updated PC verifier to check both forms.

## Declare a single-session CD source

**Start > Single-session CD source** is a session-only opt-in, default off.
Enable it only for a CD/CD-R/audio CD independently known to have one session.
The adapter checks ready CD media, but **does not query the firmware session
count**. Selection declares the source scope; it is not proof of one session.
With the toggle off, retail GD-ROM keeps its original two-area preparation.
Enabling it selects the media-aware preparation, which still accepts GD-ROM.

An admitted CD forces BIN/CUE regardless of the saved New format. Its separate
profile captures complete TOC INDEX01 intervals without the GD type-change tail
exclusion. There is no fabricated high-density start, intertrack INDEX00 or
subchannel data. Multisession CD-R, including typical two-session self-boot
media, is outside this scope. An unlocated pregap that changes a data window's
sector type fails closed. Audio bytes remain in drive order with no jitter or
offset correction. See [CD capture and acquisition checks](cd-capture.md).

## New output names and existing dumps

**Start > New dump folder label** sets a session-only optional New folder label.
Empty or whitespace-only input restores **Auto (disc title)**. Start or DONE
applies its sanitized FAT-safe form; B cancels. It changes only the new folder,
while the descriptor/container base and optical identity remain disc-derived.
It does not rename existing files.

For an MDK2 disc, label `Archive Copy` produces
`/Games/Archive Copy/MDK2.gdi`. Another New uses `Archive Copy (2)` and keeps
`MDK2.gdi`. With Auto, the folders are `MDK2`, `MDK2 (2)`, and increasing
suffixes. Existing files also reserve names through FAT's case-insensitive
comparison. New never replaces a previous capture. Resume/Verify ignore the
current label. Select a custom-labelled local job explicitly; automatic
selection follows the detected title's folders.

Manifests name the selected descriptor explicitly. Default raw GDI track bytes
and old checkpoint meanings remain compatible. Legacy jobs keep `disc.gdi`;
new formats carry their recorded format/mode flags. Keep the complete folder
and use the updated `tools/verify_dump.py` for new formats.

## Choose an exact saved job

Open **Start > Saved job for Resume/Verify**. The local browser starts at the
saved destination. Use A to enter a folder, **Y to select its highlighted
child**, or **X to select the current folder**. Start cancels; B moves up and
leaves at the root. A child too long to enter can still be selected with Y
when its complete job path fits the capture engine's bound.

This selects a candidate without reading disc/checkpoint contents or starting
capture. Normal X Resume, Y Verify, Advanced Resume and confirmed Quick Resume
then check only that exact folder. A missing/corrupt/wrong-disc job fails without
falling back elsewhere. Full optical identity, checkpoint, recorded format/hash
mode and normal prefix checks still apply. Verify writes no saved game data.
The choice resets on restart. **Left on the idle ripper** clears it and restores
Automatic. New ignores an existing selection and creates a separate job.

Without an exact selection, Resume/Verify search the selected parent for the
highest matching title suffix whose checkpoint matches the complete inserted
disc identity and plan. Newer same-title jobs for different contents are
skipped. I/O failures stop discovery. With no named match, the legacy
`/KUI/dumps` location is checked. The engine does not search every destination.
On a PC destination, this search operates within the selected remote volume.

## Scan, repair copies and reread evidence

**Advanced CRC scan** reads a supported completed raw job or imported raw GDI
without an inserted disc. It writes a separate report, checks saved hashes when
available, and scans Mode 1 sync/address/EDC/reserved/P/Q. It does not patch
tracks. Saved chunks, including audio, are independently reopened and compared
to detect inconsistent storage reads. Structural suspects also receive bounded
repeat-read evidence. Disagreement blocks repair and remains recorded until a
complete consistent rescan clears the block. Audio content still needs saved
hashes or an independent reference; repeat agreement alone does not prove it.

**Repair / resume a separate copy** requires a matching prepared disc and a
supported raw-track job or imported raw 2352-byte GDI folder. It can
localize supported Mode 1 structural damage. Stable saved-sector rereads,
identity checks and two equal validated optical reads precede each replacement.
Originals remain read-only. New output and durable original/candidate evidence
go under `/KUI/repair/job-NNNN`; every copied byte receives full readback before
a GDI is published. Unsupported sector forms, unlocalizable audio/hash damage and generic
ECC reconstruction are outside this scope. Failed/unresolved/stopped copies
remain partial. Imported audio can remain unverified even in a completed
copy. Selecting the same source again resumes its matching durable repair job
from checked progress. Source identity and committed copied bytes are checked
before continuation; unrelated sources and damaged state are refused. Originals
remain read-only. See [repair copies](repair-copy.md) for supported partial jobs
and exact restart behavior.

**Start a new repair copy** creates a separate numbered transaction, including
when a source at the same path has changed since its previous repair. It keeps
the old jobs and their evidence. After starting, the page returns to repair/resume
mode so A continues the new job if stopped. Use the explicit New action again to
start another transaction. A failure before transaction creation keeps the New
request selected for retry. Resume continues to refuse changed source bytes.

**Repair / resume in place** is an explicit separate action. After choosing the
folder, release and press A to confirm original-track changes. Normal capture
Resume/Verify is blocked while its active marker exists. Repair retains original
sectors and checked candidates, and updates normal source hashes only after
full saved-byte verification. LEFT/RIGHT on either idle repair page selects
recovery passes, including Until stopped; B stops with durable progress retained.
Normal checkpointed partial jobs can append their missing tails in place. Legacy
DS jobs first import through a separate repair copy; their original metadata is
never rewritten as independent state.

On the idle **separate-copy** repair page, X explicitly enables saved-content
identity fallback if an original fixed identity sector cannot be read. This
first verifies the complete saved prefixes against their recorded hashes, then
binds two healthy matching source/disc anchors. It preserves the old identity
as provenance and reports a new recovery binding, not a revalidation of that
original optical digest. A readable identity mismatch still refuses the disc.
Resume and drive recovery require the same recorded anchors. In-place repair
continues to require the original identity.

**GDI 2048 direct** saves data payloads as they are read and keeps raw audio.
It does not store full raw data tracks. The existing GDI 2048 option still creates
a verified derivative from retained raw tracks. Direct-mode hashes cover saved
payloads and are not raw Redump/TOSEC hashes. See
[direct capture](direct-cooked-capture.md).

**Suspect-sector reread evidence** is storage-only. It independently reopens
and rereads at most **64 structurally suspect sectors** twice, saving the
initial and both repeat samples plus differences under
`/KUI/recovery/readback-NNNN`. It continues counting later suspects after the
sample bound. No optical reads or original-track writes occur. Stable samples
do not establish correct data or localize whole-track audio/hash mismatches.
See [repair copies and evidence](repair-copy.md) for bounds and result meanings.

**Damaged disc salvage** remains a separate journaled job format. It records
unreadable holes, continues collection and automatically runs alternating
targeted recovery passes; it does not overwrite normal captures. **Until stopped**
is the default recovery-pass choice; 1/5/10/20/50 finite pass limits are also
available. B stops and retains progress. The next Resume continues the saved job.
See [salvage controls and limits](salvage-worker.md).

## Salvage completion checks

The salvage page has a fifth setting, **Read back recovered files**, Off by
default. This session choice applies to New, Resume and Retry unresolved:

- **Off:** after every target resolves, compare the final reconstructed track
  CRCs automatically with Redump and TOSEC. No whole-track saved-byte readback
  is claimed.
- **On:** first reread every saved byte of every data/audio track and compare
  its CRC with the completed recovery record, then run the same catalogue check.
  This takes an additional full storage read.

Both paths require a fresh successful salvage-engine check of the same job's
journal, original target evidence, repairs and durable completion record. The
completion check also binds the unchanged header SHA-256 and exact track plan.
It does not substitute an arbitrary imported GDI or structural scan for that
recovery evidence.

Each run writes a unique `completion-check-NNNN.part` inside the salvage job.
It records the header identity, track layout, expected CRCs, original/recovered
target counts, requested readback and catalogue outcome. A completed result is
synced, closed, read back exactly, renamed to `.txt`, and read back again.
Only the published `.txt` with its COMPLETE footer is completed check evidence.
Stopped or failed checks retain their `.part`; report write/sync/readback or
publication failure does not report verification success. The recovered job
and its original evidence remain intact, so checks can be retried.

The screen distinguishes **all targets resolved**, **saved track bytes read back
and verified**, and **FULL TRACK MATCH**. Missing databases, partial matches,
mismatches and cancelled checks retain their separate meanings. A catalogue
lookup error does not erase a successful saved-byte readback, and it never
produces a full-match badge.

To check a previously resolved salvage job, enable readback and choose **Resume
latest salvage** with its matching disc. The engine validates the saved recovery
state, then runs completion checks without recapturing its good tracks.

## Read the result lines separately

| Result | Meaning |
| --- | --- |
| Green STREAM CRC: FULL TRACK MATCH | Every raw captured track's size and CRC32 matches one independent catalogue entry |
| Amber data/listed-data/partial match | Partial reference coverage; not a full-disc match |
| No database, no match, cancelled or unavailable | No full reference match was established |
| Saved bytes reread and verified | Saved files were reread and matched recorded hashes |
| Saved bytes not fully reread | Capture completed without a full saved-file readback |

Catalogue matching and saved-file consistency are separate. Copy `redump.db`
and `tosec.db` into `/KUI/` on the actual destination volume for catalogue
checks. Completion, strict parity checks or a partial match do not make the
reference badge green. Defaults remain CRC32, automatic readback Off, DMA,
ten additional retries, strict checks Off, local destination and CD opt-in Off.
Exports and SHA jobs require readback regardless of the ordinary CRC preference.

## Evidence and acceptance

Host tests cover CD profiles/acquisition, raw GD/BIN-CUE/cooked exports,
checkpoint recovery, destination/identity refusal, repair/evidence faults and
real FAT32/exFAT behavior. The PC destination has actual TCP loopback coverage.
The integrated console UI, CD admission, network adapters and new recovery
operations still need hardware acceptance with the matching runtime.

The completed MDK2 log for earlier restored-menu runtime `8bae3efe7c2f` averages
973.96 KiB/s and matches all 31 tracks against TOSEC. It validates that earlier
capture path; it is not acceptance of these new controls or saved-file PC
verification. See [sanitized evidence](evidence/m15-mdk2-2026-09-23.json).

For a normal acceptance capture, confirm the destination and toggles, capture a
known disc, retain the automatic report and check the complete folder on a PC:

```sh
python3 tools/verify_dump.py "/path/to/card/Games/MDK2"
```

An independent reference manifest can be supplied with `--reference`. Confirm a
controlled Stop/Resume selects the correct existing job and that New chooses a
separate folder. Exercise optional tools only when needed; these source changes
do not require repeating the established optical throughput benchmark.
