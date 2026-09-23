# App round: existing boot disc, SD update only

These new app paths have host checks; physical-console acceptance is pending.
The hardware-proven disc reader is unchanged. This round does not require a new
reader benchmark or a new full rip merely to measure it again.

## Install

Copy the package's `KUI/runtime.kui` onto the card. Copy `KUI/apps/music/` too if
you want the five original menu loops. Keep the existing CD. Existing `/Games`,
checkpoints, ripper settings, catalogues and `bench.cfg` stay on the card.
Music defaults off; enable and save it in System Settings. Missing music does
not prevent app use. The included MUSIC.md records its provenance and format.

## The submitted Omikron log

The submitted run was a new capture, not a saved-file verification attempt. It
committed 1,201,396,896 of 1,203,979,392 planned bytes before a final-track DMA
read timed out and the PIO fallback could not abort safely. The log explicitly
requires a drive reset. Verification never began, and no reference result was
produced. The existing partial job was preserved.

Reboot the console, keep the same disc and destination, then choose **Resume**.
Do not choose New dump to recover that job. After completion, choose **Verify**
if a saved-file reread is wanted, and save the resulting diagnostic log. If the
same read fails again, keep that log; do not repeatedly retry a poisoned drive
without rebooting. The new UI distinguishes capture/prefix/verification failures
and shows a reboot-required message when the drive reports that state.

## Short acceptance sequence

1. **Disc title:** from the launcher, open the lid, insert a GD-ROM and close it.
   Confirm its title appears once ready. Try a different disc and the boot CD:
   the old title should clear; a normal CD should not be called a GD-ROM. The
   single optional title read happens only while idle and is cancellable. There
   is no idle status polling during a capture or test. Previous dump results
   retain their own title so a new inserted title cannot relabel an old CRC.
2. **ETA:** on an already-needed Resume or Verify, confirm the current phase
   shows an estimate after it has a usable rate. It should hide during startup,
   a stalled update, retry indication or after Stop. It estimates that phase,
   not a later optional reread. No extra full rip is needed for this check.
3. **Settings:** global Settings should show video, memory display and music.
   Ripper > Advanced > Capture settings should show only ripper preferences.
   Save/reboot to check persistence. `bench.cfg` remains an explicit override.
4. **Video:** try one appropriate TV mode. Let its ten-second preview expire,
   then repeat and confirm with A. B cancels immediately. Confirm other app
   screens remain within the 640x480 canvas. VGA remains progressive regardless
   of TV preference. If a saved mode is unsuitable, hold Y as the SD runtime
   starts; this uses safe timing without deleting preferences.
5. **Memory Test:** start once, check the reported allocated size/pass result,
   then try Stop. It owns up to 4 MiB and frees it afterward; it does not claim
   to test all physical RAM, VRAM or audio RAM. Check that returning to Home and
   launching another app still works.
6. **VMU Manager:** select the attached port/slot, list saves and page through
   them. Back up one save, then all if desired. Look for a verified result and
   the new folder under `/KUI/backups/vmu/`. Repeating a backup must create a new
   folder. `.vms` files contain each save's allocated payload blocks; `.dir`
   sidecars preserve the original directory entry. A completion marker is
   written only after the requested files pass exact reread and CRC checks.
   This version never writes, restores, formats or deletes anything on the VMU.
   A failed/stopped backup can leave a clearly incomplete new SD folder.
7. **Network:** run once with your current hardware. No adapter should be a
   clear result, not a hang or an Internet failure. BBA/LAN detection reports
   configuration and the limits of what was checked. This build does not start
   DHCP or send a reachability probe; do not treat adapter detection as a link
   or Internet pass. Modem dial-up is not implemented.
8. **Music:** enable/save, adjust volume/save and use X on a music setting to
   choose the next loop. Confirm it stops before a test/SD operation and resumes
   afterward without crackles or app stalls. Try music off as well. One file is
   retained in RAM while off/paused; this is visible in memory statistics.

Save Diagnostics with Y after any failed app operation, and report the app,
build ID and which step failed. Hardware settings, audio quality, VMU hardware
access and adapter detection remain pending until these checks run on console.
