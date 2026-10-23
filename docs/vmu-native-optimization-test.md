# Native VMU optimization: morning test

Start with ordinary VMU mode on Sonic Adventure 2. The previous same-loader
VMU-off test was substantially smoother. Its gameplay capture then measured
44.08% of wall time inside the sampled submission, completion and heap-free
C bodies, with zero VMU storage calls. This update targets that repeated CPU
work. The percentage includes sampling overhead and excludes assembly entry
and exit; it is not a frame-rate measurement or a promised speedup.

The new build prepares immutable patch expectations and allocator geometry
once, retaining live patch, allocator identity and complete free-list checks.
In the compiled SH model, the shared hook check falls from 3,574 to 226
instructions; a release callback with 64 free nodes falls from 15,612 to
4,211. Those synthetic counts exclude common helper bodies and are not
console timings. The detailed evidence and comparison are under `docs/`.

Keep the current SCI hardware. G1 work comes later, after SCI/SCIF software
and the lessons from these tests are settled.

## Install the new ordinary module

Extract the **new** test ZIP into a fresh folder and run the command from
that folder, with the SD card mounted:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game sa2
```

This binds the new module to your existing p0005 card and installs
`KUI/apps/games/retail-boot.kui` directly. Your prepared card, journal, identity
and profile stay in place. There is no backup/restore script to run. Use the
personalized module when copying KUI manually; `base/KUI` contains the
unpersonalized template.

Safely eject and use the same launch settings for every comparison:

- Original GDI, SCI / Standard, Auto/default encoding.
- CDDA/CD music off, background loading off, RAM patches off.
- Same controller, display and scene; start with one controller and no
  physical VMU or other accessories.

The matching game should show `USING VERIFIED NATIVE MAPPING CACHE` during
startup. Record the build shown on screen and time to the bootloader and
from the bootloader to game launch. If it scans instead, record that message.
If the game unexpectedly asks to format the card, photograph the screen and
stop that test.

## Test ordinary gameplay first

| Game | Test | Report |
| --- | --- | --- |
| SA2, first priority | Load the existing save; replay the same stage and camera/movement sequence used for the lag comparison for 5–10 minutes | Input delay, choppiness, pauses and whether it approaches the same-loader VMU-off result |
| SA2, persistence | Save, wait for the game's save-complete indication, fully power off, reboot and load | Correct saved progress and any card error |
| Skies | Reinstall using `--game skies`; load the existing save, try the same ship controls and one scene transition, then save/power off/reload | Gummy control feel, streaming/audio and correct progress |
| Power Stone 2 | Reinstall using `--game ps2`; short FMV/gameplay and save/power off/reload | Whether its previously smooth behavior and persistence remain intact |

For Skies or Power Stone 2, change only the `--game` value in the command;
keep the same explicit p0005 profile. These mappings select executable
metadata, not a different card. Photograph the complete first refusal/error
screen if a test fails. The useful comparison is ordinary mode; diagnostic
timing adds its own cost.

## Optional: capture the remaining overhead

After the ordinary SA2 comparison, install timing mode from the same new
package folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game sa2 --profile-runtime
```

Load the same save and reach active gameplay. Hold **L + R fully down and B**
for about a second, then release and play normally for about 20 seconds. The
gesture requires 30 consecutive completed controller polls. It starts a fresh
window so loading and menus are excluded. An unsupported timer uses the
1,200-poll fallback, so the duration can differ.

The report stops the game after the current transfer completes and no storage
transaction is in flight. Photograph the full screen, then power off. It
shows call counts, total/maximum ticks, invalid samples, clock/cache values,
polls and commits. Completion includes storage time, so those two categories
overlap and should not be added together. Saving remains subject to the usual
journal, readback and bus-restoration checks.

Return to ordinary mode by running the original command without
`--profile-runtime`:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game sa2
```

If substantial lag remains, an optional matched no-card comparison uses the
same command with `--control`. Startup then shows
`NATIVE VMU CONTROL / NO VIRTUAL CARD`; start a new game because this mode
cannot load the virtual save. It preserves the SD save for the next ordinary
launch. The control compares the entire VMU path, including its reserved
memory, with the same loader. It does not isolate an unrelated loader change.

This test keeps the current single virtual A1 and reviewed native-family
scope. It does not enable A2, new game families, SCIF VMU backing, CDDA with
VMU, or physical-disc VMU support. Those are subsequent stages once gameplay
and save correctness are accepted.
