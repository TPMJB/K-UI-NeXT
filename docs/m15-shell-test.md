# M1.5 shell: first console check

This update adds the launcher, persistent preferences and a clearer ripper screen.
The accepted disc reader is unchanged. This is a UI acceptance session, not another
reader benchmark. Keep the existing bootstrap CD and all existing dumps.

## Install

Download `sd-update` from the successful build for `milestone/m15-shell`. With
the console powered off, retain the previous working runtime on your PC and replace
only `/KUI/runtime.kui`. Existing catalogues, jobs and `bench.cfg` stay in place.
The package includes the source commit and checksums. Use the same burned disc.
Shell branches ending in `-shell` build on their pull request so GitHub can reuse
the milestone base branch's dependency/toolchain caches. The build ID identifies
the PR's tested merge commit; `build.json` records that exact source revision.

The final screen should say **K-UI NeXT / SD runtime**, show the new build ID and
offer **Disc Ripper**, **Settings** and **Diagnostics**. The embedded CD fallback
keeps its original diagnostic interface.

## Controls

| Screen | Controls |
| --- | --- |
| Launcher | D-pad or stick selects; A opens |
| Disc Ripper | A opens new-dump confirmation; release and press A again to start; X resumes; Y verifies; B returns home while idle |
| Settings | Up/Down selects; Left/Right changes; A saves; B discards unsaved changes and returns home |
| Diagnostics | A disc samples; X SD write/read test; Y saves log; R runs configured benchmarks; Up/Down scrolls; Start shows latest lines |
| Any active operation | B requests Stop; no second operation can start; wait for the report save and READY |
| Any screen | L logs an mstats snapshot; view it in Diagnostics |

## Short acceptance checklist

1. Check all three screens for readable text and visible controls on your RF
   display. Move with D-pad and stick. Confirm B consistently returns home.
2. In Disc Ripper, press A to open the confirmation, then B to cancel it. It
   should return to the ripper without starting work. Holding B while pressing A
   must not launch an operation.
3. In Settings, change **Memory display** to Off, save, and return to the ripper.
   Reboot using the same CD: the choice should persist. Turn it back on and save.
   Changing it and pressing B without saving should discard the edit.
4. Open Diagnostics and save a log with Y. The screen must remain responsive,
   report the saved path, and return to READY. L should still log memory stats.
5. During your next normal capture, check the progress bar, track, speed and B
   Stop behavior. The UI should distinguish stopped, failed and complete jobs.
   There is no need to repeat an accepted full-disc performance test for this UI.

Send the new diagnostic log and any screen with clipping, unclear labels or input
problems. The build and host tests do not establish physical display acceptance.

## Preferences and existing bench.cfg

The normal defaults now use the already-proven fast policy: CRC32, automatic end
readback Off, DMA, and two busy-screen redraws per second. **Y Verify always
rereads saved files.** SHA-256+CRC32 jobs require full readback in the existing
format; the settings screen explains this. Resume retains the job's recorded hash
mode, and the existing full-prefix resume check remains in place.

Precedence is **defaults → saved preferences → explicit bench.cfg keys**. The UI
edits preferences without rewriting the experiment file. If an existing
`/KUI/bench.cfg` sets `capture_hash` or `end_readback`, those keys still win. To test
different preferences, retain a copy and temporarily rename `bench.cfg` while the
console is off. Restore it afterwards if wanted. The operation log prints the
effective options.

Settings use two alternating 32-byte, versioned, CRC-protected files:
`/KUI/settings-a.bin` and `settings-b.bin`. Each save keeps the prior valid slot,
syncs/closes the new slot and reads it back. Invalid/truncated records fall back
to the other valid slot, or defaults if neither is usable. This does not provide
stronger power-loss guarantees than the card and filesystem themselves. No
settings files ship in an update, so installing a runtime does not overwrite them.

## Reading results

- **Capture complete** means all tracks were captured and the engine completed
  publication. It does not by itself mean saved bytes were reread.
- **Saved files verified** is shown only when the engine reports a full saved-file
  verification. This comes from its existing statistics, not a progress percentage.
- A **reference match** is separate. The diagnostic report names the catalogue
  and result; the shell does not infer it from capture completion.
- Stop/failure preserves the engine's existing partial-job behavior. The report
  supplies details and the job location when one was created.

## Implementation boundary

The three accepted engine files (`src/core/capture.c`, `src/dreamcast/disc.c` and
`src/core/command.c`) are byte-for-byte unchanged from `4f2d67ef9fb4`. The console
adapter supplies the engine's existing optional statistics pointer so the shell
can report verification truthfully. No read strategy, chunk size, retry policy,
checkpoint format, reference matcher or DMA code is replaced.

The UI performs no filesystem or drive work. Settings, reports, probes and captures
all run on the existing single I/O worker. Frame drawing remains buffered, with
the existing busy redraw policy. Original prior-project additions are inventoried
in [prior-work-reuse.md](prior-work-reuse.md) for selective reuse in later apps.
