# App round five: recovery, VMU management, music and system tools

Use the existing bootstrap CD and replace the SD update after the current
operation finishes. Copy the package's `KUI` and `Music` folders, preserving
existing games, backups and settings. The normal capture engine, raw/DMA reader
and command state machine are unchanged. No new reader benchmark is requested.

## Acceptance update — 2026-09-24

**Latest follow-up:** the owner confirms audio-CD playback worked and requests
Games next. Playback is accepted by owner report; no repeat is requested.
No build ID, mixed-disc identity or per-control results accompany that message.
See [the confirmation](evidence/m15-audio-cd-owner-confirmation-2026-09-24.json)
and [the Games plan](games-milestone-plan.md). The earlier log findings below
describe the preceding runtime.

Runtime `cc2320bb6d3a` passed the clean, damaged and GDI-only tiny-scan
expectations. The existing ARMADA dump also completed its saved-file scan, with
Mode 1 structure passing and five recorded track CRC32s. It has no K-UI manifest,
so this runtime reported **no expected hashes; audio unverified**. That is a
structural-only result, not a failed scan. The completed report has zero
bad/unsupported sectors. We compared its five recorded CRC32s, including audio,
with the bundled TOSEC Armada v1.000 US entry: **all five match**, as do total
bytes and data/audio sector totals. This comparison was made after the scan;
the tested UI did not do it. No additional full PC read is requested. Individual
track sizes and scan time are not recorded.

Clear cache returned cached/loading bytes to zero, with all nine file
allocations freed. Eight later snapshots retained the same 189,876-byte heap
in-use count. This establishes the tested clear-cache path; uninterrupted Ogg
playback and trigger cycling are not individually established by this log.

The owner completed a **VMU backup/delete/restore round trip with THPS2**: the
game saw no save after deletion and loaded it again after restore. This accepts
one save by game behavior; Copy between two VMUs remains untested. The second
audio CD played audibly, but logged not-ready messages after two successful Play
commands leave transport/status handling open. The refused disc's TOC contains
a data track; that build refused mixed CDs.

**Network (7) is on hold until hardware is available. Salvage (8) is deferred to
a future version at the owner's request.** Neither blocks the current app
acceptance round. Settings/tools acceptance is still unconfirmed.
See [the line-backed evidence](evidence/m15-app-round-five-2026-09-24.json).
The preceding retry/cache findings remain in
[the prior round's record](evidence/m15-doa2-app-round-four-2026-09-23.json).

## Follow-up changes after that report

The next SD update increases the retained diagnostic log to **1,500 lines**.
Advanced CRC compares the CRCs it has already calculated with the installed
catalogues, including GDI-only dumps; this adds no second track-read pass.
The CD player accepts audio tracks on mixed/enhanced CDs while keeping data
tracks unselectable, and corrects the readiness handling after Play. These are
new fixes following `cc2320bb6d3a`; their host validation and upcoming console
acceptance must not be confused with the earlier hardware observations above.
No normal reader change or new speed measurement is part of this update.

## Implemented changes

| App | This update |
| --- | --- |
| Disc Ripper | Percentage, stable active title, cumulative retry total and current attempt/FAD |
| Advanced CRC | Exact folder/file errors, completed named K-UI jobs, manifest-only/GDI-only scans and catalogue comparison of the already-calculated CRCs |
| Salvage | Separate durable jobs, explicit zero-fill policy, resume, bounded retry passes, verified patch backups/readback and PC verifier |
| VMU | Preview/confirm delete and copy, with verified restorable SD backup before writes |
| Music | Custom song in trigger cycle, clear-cache action, bounded Ogg decoding and CD audio; follow-up adds audio-only selection on mixed/enhanced CDs |
| Settings | TV safe area, short menu sounds, system inventory, verified settings-flash/visible-BIOS backups and Restart |
| Network | Temporary DHCP/address-conflict/gateway ARP/ping test with bounded waits; owner hardware test deferred |
| Diagnostics | Retain 1,500 log lines for longer app sessions |

New code is independently implemented, with the pinned MIT Vorbis decoder and
upstream KOS interfaces attributed. These app paths are host-tested; the scoped
console results above accept some paths while others remain open. They do not establish full DreamShell
parity or game-image compatibility.

## Focused hardware checks

1. **Music follow-up:** select `Music/harbor-lights.ogg`, listen through a loop, visit
   menus and cycle L/R away and back if those audible/control checks have not
   already been completed. Cache clearing has passed; it does not need repeating.
   An allocator retaining free heap pages is different from live song allocations.
   The WAV is included too.
2. **Ripper display:** observe title, percentage and retry labels during the next
   ordinary capture. No deliberate retry or new full benchmark is necessary.
3. **Advanced CRC — tiny fixtures passed:** the tiny `KUI/tests/scan/clean`,
   `damaged` and `gdi-only`
   folders have passed: clean matches; damaged reports two bad sectors and three
   CRC mismatches; GDI-only says structural only. They do not need repeating
   unless scanner changes require a targeted check. The new catalogue display
   still needs a console check on the next ordinary saved-file scan; no repeat
   rip or extra full ARMADA scan is requested to validate the supplied results.
   Select the actual game folder, not its `/Games` parent. Scans read every saved
   byte and take time. Catalogue comparison uses that same pass. Original track
   files remain read-only.
4. **VMU — one-save round trip passed by owner report:** the THPS2 save was
   absent after deletion and usable after restoration. No repeat is requested.
   A second VMU enables the still-open Copy check. Keep the
   involved card(s) connected through verification; uncertain writes are not
   automatically retried. See [VMU details](vmu-restore.md).
5. **Settings/tools:** enable menu sounds, choose a safe-area inset, save and
   reboot. Try inventory and each backup; the app reads the console and writes
   only new SD files. These backups do not enable flash programming.
6. **Audio CD — playback accepted by owner follow-up:** no repeat requested.
   Individual transport/mixed-disc cases remain unitemized rather than assumed
   passed. Music L opens the CD player; R refresh, A play, Y pause/resume, X stop.
   The update also permits audio tracks on mixed/enhanced CDs while keeping data
   tracks unavailable and refusing data-only CDs/GD-ROMs. See
   [music details](music-round-five.md).
7. **Network — deferred until hardware is available:** Network X runs the
   temporary local
   connection test. No adapter is a clear unavailable result. An unanswered ping
   does not prove Internet failure; the app does not test DNS or Internet access.
8. **Salvage — deferred to a future version by the owner:** no current test is
   requested. When revisited, follow [the recovery guide](salvage-worker.md) on a
   damaged test disc. Begin with zero fill Off and a short stopped first pass;
   test resume.
   Zero fill On is explicit consent for a separate job with unresolved holes.
   Retry passes patch only recorded targets. Run `verify_salvage.py` on the PC
   after resolution. A resolved job is not automatically a TOSEC/Redump match.

After each group, save Diagnostics Y and keep the operation's automatic report.
For music, include reports before cycling and after clearing. For a scan failure,
the new report includes the precise selected folder and failing metadata file.

## Remaining boundaries and order

The normal reader remains accepted. VMU backup/delete/restore has owner-reported
game acceptance; two-VMU Copy remains open. Ogg/CD transport and system-backup
paths retain the focused checks above. Network hardware and salvage are deferred
as stated above, not required for this round. Flash programming and permanent
region changes require the actual motherboard,
programmable chip and bank/unlock wiring; this build deliberately supplies
inspection/backups rather than inventing a hardware profile. Send those hardware
details if those write operations are wanted next.

Networking is currently a temporary diagnostic, not a persistent service;
static-address UI, DNS and NTP remain future work. The renderer keeps its 640x480
canvas and supported TV/VGA timings; safe area is not arbitrary-resolution or
widescreen layout support. VMU formatting/overwriting and separate executable
app loading are not implemented. These are explicit remaining parity items,
not hidden enabled controls.

Next comes the [unified Games app and independent loading machinery](games-milestone-plan.md). Redesign the
boot disc after the applications, retaining the working SD-update/fallback path.
