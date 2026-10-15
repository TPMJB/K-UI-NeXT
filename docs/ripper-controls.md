# Disc Ripper: destinations, controls and results

The ripper saves new captures under a chosen SD folder, using the game's
title by default. The default parent is **`/Games`**. Tyrfing preparation adds
an exact saved-job selector and a custom New-job folder label; their console
acceptance remains pending.
Use the existing bootstrap disc and update the SD runtime as described in
[sd-bootstrap.md](sd-bootstrap.md).

## Controls

| Screen | Controls |
| --- | --- |
| Launcher | D-pad or stick selects; A opens Disc Ripper, Settings or Diagnostics |
| Disc Ripper | A opens New confirmation; release and press A again to begin. X resumes; Y verifies saved files. Left clears the exact saved-job selection and restores Automatic. L/R select bundled songs; Start opens Advanced. B returns home while idle |
| Destination browser | Up/Down selects; A opens a subfolder; B goes to its parent. Left/Right changes the listing page. Y saves the current folder as the destination. X opens path entry. Start cancels browsing and returns to the ripper |
| Path entry | Directions select a key; A enters it. X deletes one character; Y changes letter case. Start or the DONE key saves the path. B cancels typing and returns to the browser |
| Advanced | Up/Down selects Verify, Full Resume, Capture settings, Quick Resume, Destination folder, Advanced CRC, Salvage, Saved job for Resume/Verify or New dump folder label. A opens it; B returns to the ripper |
| Saved-job browser | A opens a folder; Y selects the highlighted child folder; X selects the current folder. B goes to the parent, or leaves the browser at its root. Start cancels and retains the earlier selection |
| New folder-label keyboard | Directions select a key; A enters it; X deletes; Y changes letter case. Start or DONE applies the label. B cancels and retains the previous label |
| Settings | Up/Down selects; Left/Right changes; A saves. B discards unsaved edits and returns to the screen that opened Settings |
| Diagnostics | A runs disc samples; X runs the SD test; Y saves the log. Up/Down scrolls; Start shows the latest lines. R retains the configured benchmark entry |
| Active operation | B requests Stop. Wait for the operation and automatic report save to finish before starting another action |
| Diagnostics | L records an mstats snapshot in the log |

Holding a direction repeats navigation. Action buttons require a new press, so
holding A does not pass through New confirmation. B takes priority over a
simultaneous launch or confirmation.

## Choose a destination

Open Start → Destination folder from the idle ripper. Browse existing folders, then press Y to use the
current folder. To choose a folder that does not exist yet, press X and enter an
absolute card path, such as `/Games/Imports`; Start or DONE saves that choice.
The small on-screen keyboard enters ASCII characters. Existing UTF-8 folder
names remain intact when selected from the browser, although the font cannot
display every character.

Saving a destination persists the choice on SD and returns to the ripper. It
does not create the game folder. **New creates any missing parent directories**
before creating its new job. Cancelling browsing or typing keeps the previously
saved destination. Invalid paths, `..`, reserved names and paths too long to
represent are rejected with an explanation.

## Choose an output format

Open **Start > Capture settings**, choose **Output format**, then save.
GDI is the default. BIN/CUE uses the same captured raw tracks with a CUE
descriptor. CSO, DreamShell-LZO ZSO and CHDv4 are verified post-capture
exports; they retain the raw capture and its internal `.capture.gdi` for
resume and verification. CSO/ZSO contain one cooked high-density data track;
audio stays in the raw files. CHD includes the captured data and audio tracks.
Compression needs additional space and time. These CSO, historical LZO ZSO
and CHD exports still require PC import before Games can launch them. The
separate experimental [native ZSO reader](games-zso.md) accepts standard LZ4
ZSO, which is a different dialect from this ripper's ZSO output. See
[Games formats](games-formats.md).

Resume and Verify use the job's saved format. Changing the setting affects
only New. Existing GDI jobs and their reference hashes remain compatible.

## New output names and existing dumps

Open **Start > New dump folder label** to enter an optional label for the
next New job. Start or DONE applies its sanitized FAT-safe form; empty or
whitespace-only input restores **Auto (disc title)**. B cancels the edit.
The label is held for the current launcher session and resets on restart.
It changes only the new folder, while the descriptor/container base and saved
optical title remain disc-derived. It does not rename an existing job.

For example, label `Archive Copy` with an MDK2 disc creates
`/Games/Archive Copy/MDK2.gdi`; another New uses `Archive Copy (2)` and keeps
`MDK2.gdi`. Resume and Verify ignore the current label and keep the selected
job's existing names. Choose a custom-labelled job explicitly when resuming
or verifying it; automatic selection follows the detected title's folders.

For an IP.BIN title of `MDK2`, the first capture produces:

| Item | Path |
| --- | --- |
| Job folder | `/Games/MDK2/` |
| GDI descriptor | `/Games/MDK2/MDK2.gdi` |
| Tracks | `/Games/MDK2/track01.bin`, `track02.raw`, and the remaining numbered tracks |
| Metadata/checkpoints | `manifest.json`, `checkpoint-a.bin`, `checkpoint-b.bin` inside that job folder |

Another New uses `/Games/MDK2 (2)/`, then `(3)`, and so on. Its descriptor is
still `MDK2.gdi`. Existing files also reserve names, with FAT's case-insensitive
comparison. New never replaces an existing capture. Unsafe title characters are
sanitized, long titles are bounded, and an unusable title becomes `DreamcastDisc`.

Named manifests add an optional **`gdi_file`** field naming their descriptor.
For default GDI jobs, track names, track bytes and checkpoint encoding are unchanged. Other formats add compatible format/mode flags. Legacy jobs keep
`disc.gdi` and their existing metadata. The PC verifier reads either form; use the
updated verifier when checking a newly named job.

## Choose an exact saved job

Open **Start > Saved job for Resume/Verify**. The browser starts at the
current destination. Use A to enter a folder, **Y to select its highlighted
child folder**, or **X to select the current folder**. Start cancels without
changing the earlier selection. B moves up, then returns to Advanced at the
browser root. A folder too long to enter can still be selected directly with
Y when its complete job path fits the capture engine's limit.

The selection is a candidate, not proof that the folder is resumable. Normal
**X Resume**, **Y Verify**, Advanced Full Resume and explicitly confirmed
**Quick Resume** use that exact folder. The engine checks the path, matching
inserted disc, full identity, checkpoint and recorded job format/hash mode;
normal Resume retains its prefix verification. Verify rereads without writing
saved game data. Missing/corrupt checkpoints, wrong-disc jobs or I/O errors
stop the operation; they never redirect it to another job.

The exact selection is held only for this launcher session and resets on
restart. It can also identify a legacy `/KUI/dumps` job, which keeps its
existing `disc.gdi` name. Merely selecting a candidate folder performs no
disc/checkpoint reads and does not start an operation.

On the idle ripper, press **Left** to clear the exact selection and restore
**Automatic** latest matching-job selection. **New** ignores the selected
existing job and creates a separate capture without clearing that selection.
The saved destination choice remains
independent. Advanced CRC is still the separate read-only route for scanning
an exact supported raw GDI or completed dump without using normal capture
Resume/Verify.

## Automatic Resume and Verify select by disc identity

With no exact job selected, Resume and Verify search the **selected parent**,
considering the current title
and its numbered folders. They choose the greatest matching suffix whose valid
checkpoint matches the inserted disc's full identity and track plan. A newer
folder with the same title but different disc contents is skipped. Missing or
invalid checkpoints do not make a folder a matching job; a storage I/O error
stops selection instead of silently choosing another job.

If no matching named job exists there, selection falls back to legacy jobs in
`/KUI/dumps`. It does not search every other named destination automatically;
choose that parent again to resume its job. Existing jobs retain their recorded
hash mode. The existing resume-prefix checks and explicit `bench.cfg` overrides
still apply.

## Read the two result lines separately

| Result | Meaning |
| --- | --- |
| Green **STREAM CRC: FULL TRACK MATCH** | Every captured track's size and CRC32 matches one independent catalogue entry |
| Amber data/listed-data/partial match | Some reference coverage matches; this is not a full-disc reference match |
| No database, no match, cancelled or unavailable | No full reference match was established; the label gives the reason |
| **Saved bytes reread and verified** | The saved files were reread and matched their recorded hashes |
| **Saved bytes not fully reread** | Capture completed without a full saved-file readback |

Only the structured full-match result makes the stream CRC badge green. A
completed progress bar, a partial reference match or successful saved-file
readback does not promote that badge. Catalogue matching uses captured-stream
CRCs; saved-file verification checks what reached the card. Copy the supplied
`redump.db` and `tosec.db` into `/KUI/` for catalogue checks.

Defaults remain CRC32, automatic end readback Off, DMA and the existing busy
redraw cap. Advanced → Verify, or Y on the ripper, rereads saved files when
requested. Settings can enable automatic readback or SHA-256; SHA-256 jobs keep
their existing required full-readback behavior. Explicit `bench.cfg` keys
continue to override saved preferences.

## Evidence and optional checks

The new exact-job and folder-label controls are source changes prepared for
Tyrfing. They still need console acceptance with the integrated runtime.
Keep the existing compatible bootstrap CD and install the complete matching
package; this guide does not announce a final 2.0 release.

The destination, collision, resume-selection, metadata and reference-result paths
pass host tests on FAT32 and exFAT. **The new named output flow still needs console
acceptance.** The separately supplied completed MDK2 log for restored-menu
runtime `8bae3efe7c2f` averages 973.96 KiB/s and matches all 31 tracks against
TOSEC. That validates the earlier runtime's capture, not this new destination
flow; saved-file/PC verification of that job was not supplied. See the
[sanitized evidence](evidence/m15-mdk2-2026-09-23.json).

During the next normal rip:

1. Check destination browsing, typed-path cancellation and saving. Confirm the
   saved parent reappears after a normal reboot using the same CD.
2. Capture a known disc there. Check the displayed title, final named folder/GDI
   and catalogue result. Keep the automatically saved diagnostic report.
3. On a PC, check the complete saved job with the updated verifier, for example:

   ```sh
   python3 tools/verify_dump.py "/path/to/card/Games/MDK2"
   ```

   This checks saved bytes against the capture metadata. A matching independent
   reference manifest can also be supplied with `--reference`.
4. When another New or a controlled Stop/Resume is needed in normal use, confirm
   it uses a separate numbered folder or resumes the correct existing job.

No repeat optical benchmark is required for this release. Advanced includes
Verify, Full Resume, Capture settings, Quick Resume, Destination folder,
Advanced CRC, Salvage, Saved job for Resume/Verify and New dump folder label. Salvage is a separate explicit recovery job; it does
not overwrite normal captures. See [salvage controls and limits](salvage-worker.md).
Current bounded capture retries, stop behavior and preservation of partial jobs
remain in force.

## Implementation boundary

Both baseline host suites passed on clean `54711b931554` before protected-file
edits, with the three recorded source hashes unchanged throughout those checks.
The additions to `src/core/capture.c` concern destination selection, exact-job
selection, New-job folder labels, descriptor publication and result observation.
The accepted acquisition loop, raw/DMA
reader, command deadlines, retry policy, hashes and checkpoint encoding are
unchanged. Named capture tests independently compare track hashes with the legacy
output, exercise stop/resume and publication failure, and check reference grades.
Console throughput and named-file acceptance remain separate hardware results.
