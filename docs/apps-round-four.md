# File dates, music memory, saved-track scans and VMU restore

Keep the existing bootstrap disc. This update changes the SD runtime and adds
small optional scan fixtures. Finish the current rip before copying the update.
The accepted GD-ROM acquisition engine and its defaults are unchanged.

## What changed

| Area | Implemented in this update |
| --- | --- |
| File dates | New files use the console's local clock instead of the fixed September 2026 date. Existing dates are preserved. |
| Music memory | Manual selection of a bundled song shares the trigger-controlled cache. Logs show cached/loading/peak file bytes and allocation/free counts. |
| Disc Ripper | Advanced CRC scans a selected completed dump's saved tracks, checking recorded hashes and Mode 1 sector structure/EDC/P/Q. It writes a separate report. |
| VMU Manager | Browse verified SD backups, preview a destination, confirm restore into a free filename, then verify the written save. Existing names are refused. |
| System Settings | Startup chime, initial app, local clock editor, and reviewed restore-defaults, with migration of existing preferences. |

The new reports show stable 9.224 MiB RAM in ten rip-session snapshots and
11.013 MiB at the end of a separate music session. The old runtime has no cache
counters, so these observations do not by themselves settle the leak question.
The five retained menu WAVs total 4,674,286 bytes. Cache filling can therefore
raise RAM use, and manually selecting those files previously allowed a duplicate
copy. Host stress tests now independently track allocation/free behavior across
switches, replacements and failures; all file allocations are released at
shutdown. That does not rule out a separate console audio-service problem.
The cache budget remains 8 MiB including replacement staging, in addition to the
runtime's other RAM. Stop/mute retains cached files for later switching.

## Focused tests for this update

These are app tests. They do not repeat the accepted optical benchmarks or
require another full capture.

1. **Dates:** leave the already-correct clock alone. Save a diagnostic report,
   then compare its new folder/file date on the PC with the console. The log
   includes the local clock at startup. Old rip dates will remain unchanged.
2. **Music:** after copying the music folders, select a bundled song manually,
   cycle L/R on Home, and return to that song. Save a report after repeating
   this several times. Compare current RAM and `Music RAM` counters, not only
   the sampled peak. No new song read/allocation is needed for cached switches.
3. **Settings:** disable startup chime, choose an initial app, save, and reboot
   using the same CD. VMU/Music startup lists are read-only. Check that the
   splash still ends within three seconds. Restore defaults first changes the
   draft; it requires Save. The clock editor has its own explicit confirmation.
4. **Advanced CRC:** copy the optional `KUI/tests/scan` folders. Ripper →
   Start/Advanced → Advanced CRC scan opens the folder browser. First select
   `/KUI/tests/scan/clean`, then `/KUI/tests/scan/damaged`, and press Y in each
   folder. These are tiny synthetic files, not games. Clean must pass; damaged
   must show two bad data sectors and three track CRC mismatches. Afterward,
   selecting an actual completed game folder scans that saved dump. This choice
   does not change the destination for future rips. B stops the scan; its report
   lives separately under `/KUI/recovery/`. Original track files are read-only.
5. **VMU:** first back up a disposable save with this update, preserving its
   `.vms`, `.dir` and `.crc` files together. In VMU Manager, select the destination
   slot and press R for SD backups. Select the backup, review the preview and
   confirm. Keep the VMU connected until the operation and readback finish.
   Check the restored save with the BIOS/game afterward.

For the first restore test, a spare VMU and one disposable save are enough.
Two VMUs make source-to-destination testing convenient, but a direct-copy app
path is not required: backup to SD, then restore to the other card. A destination
with an existing same-name save is deliberately refused. Legacy backups without
a CRC proof remain untouched; make a fresh verified backup before restoring.
VMU metadata writes cannot be made power-loss atomic by host tests. A destination
metadata snapshot is saved before writes, and an unverified commit is reported
as a failure rather than automatically retried.

## What the scan means

Advanced CRC is a saved-file diagnostic. It compares with the job's recorded
hashes; it does not make a new TOSEC/Redump claim. Data sectors receive the
original project's Mode 1 validation checks. Audio receives saved-hash checking;
Mode 2 is reported as unsupported instead of being called repaired or clean.
It performs no optical rereads and no track modifications. A large dump still
takes time to scan because every saved byte is read.

The dedicated damaged-disc salvage engine is still open: persistent bad-sector
queues, explicit zero-fill policy, targeted retry passes, durable patch backups
and recovery accounting. The current scan is useful groundwork for that engine,
not an assertion that recovery is complete. See [the salvage plan](salvage-plan.md).

## Remaining order

Finish the non-Games apps first: durable Ripper salvage; VMU restore acceptance
and later managed copy/write features; remaining display/sound/system settings;
network connection tests; compressed/CD music; supported maintenance tools.
Then build the unified Games app and independent game-loader machinery. Redesign
the launch disc after those applications. An image browser alone will not supply
commercial-game compatibility.

Remaining Settings work includes display shape/filter, menu/button sound choices,
return behavior and supported resource locations, device inventory/restart, and
network clock sync after real network configuration exists. No inactive timezone
or network controls are exposed in this update.

The latest Omikron report confirms DMA on the final resumed segment and a
five-track TOSEC match; no new reader measurements are needed. See
[the hardware evidence](hardware-evidence.md).

See [clock details](clock-and-file-dates.md) and
[VMU restore details](vmu-restore.md), plus
[music host evidence](evidence/m15-music-cache-host-2026-09-23.json).
New console behavior remains pending hardware acceptance.
