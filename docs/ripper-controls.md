# Disc Ripper: destinations, controls and results

The ripper now saves new captures under a chosen SD folder, using the game's
title for its folder and GDI descriptor. The default parent is **`/Games`**.
Use the existing bootstrap disc and update the SD runtime as described in
[sd-bootstrap.md](sd-bootstrap.md).

## Controls

| Screen | Controls |
| --- | --- |
| Launcher | D-pad or stick selects; A opens Disc Ripper, Settings or Diagnostics |
| Disc Ripper | A opens New confirmation; release and press A again to begin. X resumes; Y verifies saved files. L/R select bundled songs; Start opens Advanced (including Destination folder). B returns home while idle |
| Destination browser | Up/Down selects; A opens a subfolder; B goes to its parent. Left/Right changes the listing page. Y saves the current folder as the destination. X opens path entry. Start cancels browsing and returns to the ripper |
| Path entry | Directions select a key; A enters it. X deletes one character; Y changes letter case. Start or the DONE key saves the path. B cancels typing and returns to the browser |
| Advanced | Up/Down selects Verify, Full Resume, Capture settings, Quick Resume or Destination folder; A opens it; B returns to the ripper |
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

## New output names and existing dumps

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
Track names, track bytes and checkpoint encoding are unchanged. Legacy jobs keep
`disc.gdi` and their existing metadata. The PC verifier reads either form; use the
updated verifier when checking a newly named job.

## Resume and Verify select by disc identity

Resume and Verify search the **selected parent**, considering the current title
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

## Next hardware check

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

No new optical benchmark is required for this UI change. Advanced currently
contains Verify, Resume and Settings. A dedicated salvage/recovery workflow is
still planned; it is not an available action in this build. Current bounded read
retries, stop behavior and preservation of partial jobs remain in force.

## Implementation boundary

Both baseline host suites passed on clean `54711b931554` before protected-file
edits, with the three recorded source hashes unchanged throughout those checks.
The additions to `src/core/capture.c` concern destination selection, descriptor
publication and result observation. The accepted acquisition loop, raw/DMA
reader, command deadlines, retry policy, hashes and checkpoint encoding are
unchanged. Named capture tests independently compare track hashes with the legacy
output, exercise stop/resume and publication failure, and check reference grades.
Console throughput and named-file acceptance remain separate hardware results.
