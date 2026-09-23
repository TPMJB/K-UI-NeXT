# App round five: recovery, VMU management, music and system tools

Use the existing bootstrap CD and replace the SD update after the current
operation finishes. Copy the package's `KUI` and `Music` folders, preserving
existing games, backups and settings. The normal capture engine, raw/DMA reader
and command state machine are unchanged. No new reader benchmark is requested.

## What the latest console reports establish

Runtime `6f1be4cf53c3` correctly detected the damaged Advanced CRC fixture.
Two existing-folder scans failed before reading tracks, but the old error did
not identify the folder or missing file, so their exact cause is unresolved.
The DOA2 job retried once at an earlier sector and ten times at the terminal
sector: 11 cumulative retries. A stopped resume added two; a later resume
completed with a TOSEC full-track match. Saved-file readback was not performed.
Music cache bytes and allocation/free counters remained stable across the
snapshots. The retained custom-song slot was omitted from trigger cycling.
See [the source-backed evidence](evidence/m15-doa2-app-round-four-2026-09-23.json).

## Implemented changes

| App | This update |
| --- | --- |
| Disc Ripper | Percentage, stable active title, cumulative retry total and current attempt/FAD |
| Advanced CRC | Exact folder/file errors, completed named K-UI jobs, manifest-only jobs and GDI-only structural scans |
| Salvage | Separate durable jobs, explicit zero-fill policy, resume, bounded retry passes, verified patch backups/readback and PC verifier |
| VMU | Preview/confirm delete and copy, with verified restorable SD backup before writes |
| Music | Custom song in trigger cycle, clear-cache action, bounded Ogg decoding and audio-CD player |
| Settings | TV safe area, short menu sounds, system inventory, verified settings-flash/visible-BIOS backups and Restart |
| Network | Temporary DHCP/address-conflict/gateway ARP/ping test with bounded waits |

New code is independently implemented, with the pinned MIT Vorbis decoder and
upstream KOS interfaces attributed. These app paths are host-tested; new physical
console behavior still needs acceptance. They do not establish full DreamShell
parity or game-image compatibility.

## Focused hardware checks

1. **Music first:** select `Music/harbor-lights.ogg`, listen through a loop, visit
   menus and cycle L/R away and back. Music X confirms cache clearing. Check the
   current cached-byte count returns to zero; an allocator retaining free heap
   pages is different from live song allocations. The WAV is included too.
2. **Ripper display:** observe title, percentage and retry labels during the next
   ordinary capture. No deliberate retry or new full benchmark is necessary.
3. **Advanced CRC:** scan the tiny `KUI/tests/scan/clean`, `damaged` and `gdi-only`
   folders. Clean passes; damaged reports two bad sectors and three CRC
   mismatches; GDI-only says structural only. Select the actual game folder for
   an existing dump, not its `/Games` parent. Scans read every saved byte and
   take time. Original track files remain read-only.
4. **VMU:** use one disposable save. Preview Delete, confirm its actual filename,
   let the automatic SD backup and deletion finish, then restore that new backup.
   Check the save in the BIOS/game. A second VMU enables Copy testing. Keep the
   involved card(s) connected through verification; uncertain writes are not
   automatically retried. See [VMU details](vmu-restore.md).
5. **Settings/tools:** enable menu sounds, choose a safe-area inset, save and
   reboot. Try inventory and each backup; the app reads the console and writes
   only new SD files. These backups do not enable flash programming.
6. **Audio CD:** Music L opens the CD player. Insert a normal audio CD, R refresh,
   A play, Y pause/resume, X stop. Verify routing/volume and menu navigation before
   trying another optical operation. Mixed/data CDs are refused. See
   [music details](music-round-five.md).
7. **Network, if an adapter is available:** Network X runs the temporary local
   connection test. No adapter is a clear unavailable result. An unanswered ping
   does not prove Internet failure; the app does not test DNS or Internet access.
8. **Salvage last:** follow [the recovery guide](salvage-worker.md) on a damaged
   test disc. Begin with zero fill Off and a short stopped first pass; test resume.
   Zero fill On is explicit consent for a separate job with unresolved holes.
   Retry passes patch only recorded targets. Run `verify_salvage.py` on the PC
   after resolution. A resolved job is not automatically a TOSEC/Redump match.

After each group, save Diagnostics Y and keep the operation's automatic report.
For music, include reports before cycling and after clearing. For a scan failure,
the new report includes the precise selected folder and failing metadata file.

## Remaining boundaries and order

The normal reader remains accepted. New salvage, VMU writes, Ogg/CD audio,
network hardware and system-backup paths need the focused checks above. Flash
programming and permanent region changes require the actual motherboard,
programmable chip and bank/unlock wiring; this build deliberately supplies
inspection/backups rather than inventing a hardware profile. Send those hardware
details if those write operations are wanted next.

Networking is currently a temporary diagnostic, not a persistent service;
static-address UI, DNS and NTP remain future work. The renderer keeps its 640x480
canvas and supported TV/VGA timings; safe area is not arbitrary-resolution or
widescreen layout support. VMU formatting/overwriting and separate executable
app loading are not implemented. These are explicit remaining parity items,
not hidden enabled controls.

Next comes the unified Games app and independent loading machinery. Redesign the
boot disc after the applications, retaining the working SD-update/fallback path.
