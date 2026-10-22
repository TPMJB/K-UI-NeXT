# Short startup, background music and remaining app work

Keep the accepted bootstrap CD. This round updates the SD runtime and optional
music files; the launch-disc redesign comes after the application work.
The completed GD-ROM reader is not being redesigned or rebenchmarked.

## What the latest report establishes

The owner's Dead or Alive 2 report comes from runtime `1d48aaf1412e`.
Quick Resume took **0.375164 s** to check saved sizes, then completed the remaining
**387,858,912 bytes** in **548.953593 s**. The resumed capture used **0 DMA /
5,154 PIO chunks** and ended in **TOSEC FULL TRACK MATCH**. Full Resume was
stopped during prefix checking; a later Verify was cancelled during identity.
There is no completed saved-file reread in this submission.

Retries did run: the earlier visible failure exhausted ten retries at one
sector. The report starts mid-error because its log is truncated, so it cannot
show the original event that disabled DMA. Quick/full Resume controls prefix
validation, not the transfer mode. The already accepted stop/lid-open fix is
documented separately; the persistent timeout/repeated-failure guard must not
be removed merely to hide a PIO result.

The optical adapter remains unchanged in this round. Source review of
[`disc.c` at the starting revision](https://github.com/TPMJB/K-UI-NeXT/blob/c18a5d79d0b2ef15bee7c4b6dbc37ce28f6c0824/src/dreamcast/disc.c)
retains the accepted rule: a timeout or three consecutive DMA failures on an
unchanged disc disables DMA until reboot; a user stop or disc change does not
do that by itself. The missing earlier part of this report prevents attributing
its PIO state to a particular trigger.

Insertion detection, an allocated-4-MiB memory test (70/70 passes), in-session
settings persistence and no-adapter reporting are also evidenced. Normal
main-RAM use/reservation was about **4.815 MiB**. The later **8.815 MiB** sampled
peak includes the memory test's own 4 MiB allocation. Free RAM does not measure
audio scheduling or CPU contention with bit-banged SD I/O.

Exact counters, the input hash and limitations are in
[the sanitized evidence](evidence/m15-doa2-resume-2026-09-23.json).

## Original music sample

The update supplies **Harbor Lights**, an original 60-second synthesized piece
at `Music/harbor-lights.wav`. Copy the `Music` folder alongside `KUI` on the card.
It is mono PCM16 at 22,050 Hz: **2,646,044 bytes**, about 2.52 MiB, with no external
samples. Its manifest records the exact content hash. Keep it playing past
48 seconds while navigating; that exercises a song beyond the old 2 MiB limit.
The package includes `MUSIC-DEMO.md` with the focused listening check.

This runtime supports WAV, not Ogg playback. Compressed Ogg/Vorbis can reduce
stored file size, but RAM savings depend on retaining compressed data and
decoding into a bounded buffer instead of expanding the entire song up front.
The decoder also needs CPU time. The demo generator's optional PC-side Ogg
output is a comparison file, not evidence that the console supports the codec
or that decoding has no ripping cost.

The background WAV player preloads a selected file before returning to normal
navigation. It accepts PCM16 mono/stereo at 8–44.1 kHz, with a **6 MiB file
limit** and an **8 MiB total cache budget**, including a replacement allocation.
Only the existing storage worker loads files; the audio service reads cached
PCM. Selecting a file that cannot fit must keep the previous song and show the
reason. This bound is explicit: arbitrary-length WAV streaming and Ogg decoding
are not claimed by this update.

Triggers cycle the five bundled songs. A cached song can change while a rip
owns SD; loading a missing song waits for the storage worker to become idle.
The header shows that a change is queued while the current song continues.
Idle work preloads the five bundled songs subject to the same cache budget.
The selected custom WAV remains the background song until another selection,
Music Player's **Y Stop music**, or muting. Bundled songs and custom selections
loop. B/Start navigation itself should not pause it.
Ripper Y retains its separate Verify action.

## Focused acceptance for this update

These are application acceptance checks, not a request for another optical
benchmark or a repeat of the accepted reader experiments. New behavior remains
pending hardware observation until the owner reports it.

1. Boot normally and check that the SD runtime's splash gives way to Home
   within three seconds. Also check B skip. Loading the runtime from the
   existing CD bootstrap is a separate earlier stage.
2. Select a supplied song, return Home, then enter Settings and Ripper. The
   selected song and its title should persist without stopping playback just
   to leave Music Player. Listen for gaps during navigation and saving a
   preference; report which action produces a gap if one remains.
3. Use L/R on Home and Ripper for previous/next song. Check that the top-right
   title follows the selection, Ripper memory remains visible, B still stops
   foreground work, and Y still means Verify in Ripper. Confirm the destination
   browser remains reachable through Ripper Advanced.
4. If an existing partial job needs continuation, retain its checkpoint and use
   the desired Resume mode. Save the report afterward. A terminal reset guard
   still requires reboot. Do not produce a new full dump just to reproduce the
   already recorded PIO-only segment. Any change to the protected reader would
   additionally need the existing host gates and a reference-verified hardware
   rip before acceptance.
5. During ordinary capture, check whether preloaded music remains continuous
   and Stop stays responsive. This listening check does not establish zero
   performance cost. SD song selection that needs new file I/O must respect the
   capture worker's ownership of storage.

Saved-file verification remains available separately. A size-only resume plus
a catalogue match does not reread an older prefix or prove the card's current
contents. A PC check of an already completed dump can supply that evidence
without another console capture.

## One Games app, with a substantial loader behind it

The intended interface is **one Games app**: a library of recognized games,
an advanced file browser for manual selection, and storage-source selection
when each backend is actually supported. GDI/ISO format does not require a
separate top-level app. IDE/CF belongs in that same source picker after its
mounting and read path have been implemented and verified.

Listing images and starting a KOS executable are only the shell layer. Running
commercial games also needs an independent image loader, the services games
expect while running, and compatibility testing. The finished raw-disc reader
does not itself provide those services. This remains the largest unfinished
subsystem; a library screen alone would not establish DreamShell feature parity.

The broader remaining work is:

| Area | Existing independent behavior | Still needed |
| --- | --- | --- |
| Disc Ripper | Accepted reader, resume, saved-file checks, catalogue matching | Dedicated damaged-disc salvage, repair accounting, explicit zero-fill policy and retry-pass controls; full-card hardware case |
| Games | GD Play exits to normal stock BIOS | Unified image library/browser, independently sourced loader and compatibility layer, supported storage backends |
| VMU Manager | List and verified SD backup | Restore/copy and other write actions, with device and overwrite checks |
| Network | Adapter/configuration inspection | Actual connection configuration and tests, followed by supported clock sync |
| Music | Original synthesized assets and WAV playback foundation | Background controls/continuity acceptance, compressed codecs and CD-audio support |
| Settings | Video timing, memory display and music preferences | Display shape/filter, sound choices, startup behavior, clock and related system controls below |
| Maintenance | Capability/provenance review | Read-only identity/backups first; region/BIOS writes only through supported independent hardware profiles |
| Boot disc | Accepted reusable SD bootstrap | Visual redesign and optional benchmark entry after app completion |

The original recovery helpers already ported into host tests are a foundation,
not a shipped salvage mode. Region/BIOS maintenance boundaries are recorded in
[independent app parity](independent-app-parity.md); port details are in
[the recovery plan](recovery-port.md).

## Settings gaps checked against the prior K-UI build

The comparison uses the earlier K-UI Settings app at pinned revision
[`2a5309298dde8fb100da1e2e4e10517695c9780f`](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/settings/modules/module.c).
This is a feature inventory; no mixed inherited application code is imported.

| Prior section | Useful independent equivalents still missing |
| --- | --- |
| Display | Screen shape/virtual width and sharp/smooth scaling; current runtime already has cable/output timing choices |
| Sound | Separate menu, button, selection and startup-chime toggles; preserve master/music volume choices |
| Startup | Initial app and return destination; configurable resource/source location where supported |
| Clock | Date/time editing and time zone; network sync only after the network path works |
| System | Connection-at-startup preference, device inventory, reviewed restore-defaults and restart |

The earlier resource roots and Lua startup-script paths are implementation
details of the previous environment. Independent equivalents should expose only
working choices. Ripper-specific options stay inside Ripper settings.
