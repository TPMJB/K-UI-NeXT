<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native virtual A2 and Background reader test

This candidate presents the prepared virtual card as **A2**. Physical **A1**
remains available for the real VMU's memory and LCD, including the intended
Skies display. Keep physical **A2 empty**: a real device there conflicts with
the virtual card and causes refusal. The prepared card/profile stays the same;
moving its presentation from A1 to A2 requires no conversion or formatting.

Evolution 1 and Evolution 2 have owner-reported save/load success on the earlier
A1 builds. The owner still reports FMV slowdown. This new A2/Background package
has **no console confirmation yet**. It retains the current cached worker,
startup mapping cache and controller polling improvements; it needs a new
console check for A1 coexistence, playback and saving together.

## Choose and install one module

The ZIP has two self-contained folders. Each installs the same destination,
`KUI/apps/games/retail-boot.kui`, and binds it to your existing prepared card.
Run the command from the extracted ZIP's root with the SD card mounted:

| Choice | Install command | Launch setting |
| --- | --- | --- |
| Standard A2 fallback | `python3 Standard/personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2` | Background loading **Off** |
| Background A2 candidate | `python3 Background/personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2` | Background loading **On**, Normal speed first |

Change `--game evolution2` to `evolution1`, `skies`, `sa2`, `ps2` or `grandia2`
for another supplied mapping. `--list-games` lists the exact packaged inputs.
The command updates the launch module and preserves p0005's image, profile and
journal. Safely eject and cold boot after installing.

Use the original native GDI, SCI microSD, Auto/default encoding, CD music Off
and RAM patches Off. Start with one controller. A real VMU may occupy A1;
leave A2 empty. The game should show the prepared virtual card in slot 2.

On the current confirmation screen, **Y** opens Options. Set **Background
loading: On**, then press **START** to launch. Advanced → Background speed
selects Normal (20 blocks) or Aggressive (25). Older test confirmation screens
instead offer **X** for 20 and **Y** for 25. Use the controls shown by your
installed frontend; this package does not replace it.

Background requires SCI and a map fitting **64 combined track/extent slots**.
If the frontend logs a fallback to Standard, install the Standard folder's
module and set Background Off. The two modules are separate choices; merely
turning Background Off does not turn the installed Background module into the
Standard fallback.

## Short console check

| Check | What to verify |
| --- | --- |
| A2 recognition | The existing prepared save is visible in slot 2. Stop if an existing card unexpectedly requests formatting. |
| Physical A1 | In Skies, verify the real VMU remains detected and its LCD updates. Keep the virtual save selection on A2. |
| Playback | With the Background candidate, repeat the same opening FMV and scene that slowed down before. Record audio/video pauses and the selected 20/25-block setting. |
| Responsiveness | Briefly repeat the familiar gameplay/input scene. Report any change from the current responsive VMU build. |
| Persistence | Save to A2, wait for the game's completion indication, fully power off, reboot the same configuration and load that progress. |

Begin with Normal/20 blocks. Try Aggressive/25 only as a matched comparison
after a successful Normal run. To return to Standard, run its command above
with the same profile/game and select Background Off. Record the build shown,
game/revision, reader setting, A1 device, earliest failure and any refusal
screen. Save/load success and FMV quality are separate results.

## What the historical comparison establishes

The recovered SCI-resume source is
`edf20f0f11a3ed6bb562929139cdad839d82655b`. Its repair applied to the native
**Background X/Y** reader (20/25 blocks). It restored the shared SCI/DMA
ownership checks after game activity. Standard SCI/SCIF/IDE kept their prior
behavior, and Windows CE used its separate route. The owner's earlier report
of good FMV behavior belongs to that scoped baseline.

The later VMU package used a Standard-only resident, so the old Background
repair could not run there. This candidate makes Background available with
the current A2 worker and existing responsiveness improvements. The audit
establishes that route difference; the console comparison must establish
whether it restores the reported FMV behavior. It is not a claim of broad
2.0 performance parity.

Software checks cover six original SDKs, A2 filtering/source identity,
unchanged physical A1 packets and replies, invalid-batch refusal, journal
durability ordering, heap ownership and bounded stacks. Flat replay does not
measure console scheduling, physical VMU flash/LCD behavior or FMV timing.
This test does not add SCIF/IDE VMU backing, combined CDDA/VMU, physical-disc
VMU service or general multi-disc acceptance.

## Recoverable source checkpoints

Keep each delivered module with its checksum manifest, build configuration and
source checkpoint. Before another behavior change, preserve the exact commit
plus tracked edits and new source files; unpack that checkpoint into a fresh
directory and verify its contents. Keep the earlier accepted binary alongside
it. A build stamp or audit report alone cannot recover source. Checkpoints
exclude retail executables, tracks and saves. Promote a new candidate's
hardware status only after recording its own console result.

The detailed software and historical identities are in
`Standard/docs/evidence/native-a2-background-2026-10-24.json` and both probe
reports. The Full ZIP includes the new Git source bundle and the recovered
`edf20f0f11a3` module/source under `source/historical-edf20f0f11a3/`; those
historical files are reference copies, not the module to install for this test.
