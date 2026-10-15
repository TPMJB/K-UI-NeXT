<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Worms Armageddon Windows CE CD music test

This experimental candidate targets the original US **Worms Armageddon**,
product **T40601N**, version **V1.001**, with its original **eight-track GDI**.
Its native sound/runtime profile is separate from Armada and Bust-A-Move 4.
Test 1 booted on Dreamcast hardware but produced no CD music. Test 2 corrects
the runtime layout admission described below and has audible console playback,
with three recovered buffer underruns in the supplied recording. Keep Test 2
as the working Worms baseline; zero-dropout playback remains unverified.
The subsequent [diagnostic candidate](wince-cdda-diagnostic-test.md) adds a
fourth frozen-underrun page and cooked-DATA admission. The three-page test
instructions below describe the retained Test 2 baseline.

## Test 1 result and Test 2 correction

The console recording `62684.mp4` identifies build `d12e010170a4`. Its final
diagnostic page reports a COREDLL section-table rejection: reason 9, detail 2,
observed `03e8c0f8`, expected `03e8b0f8`. There were zero worker launches,
play attempts and audio source bytes. The rejection occurred before playback.

Test 1 used the wrong COREDLL data and exception-table RVAs for Worms' ROM
module. The supplied kernel places those sections at `1c000` and `1d000`,
respectively. Its loaded section table belongs at `base + 1c0f8`, which matches
the console. Test 2 corrects those three Worms-only constants and the synthetic
fixture, including a regression against the observed console addresses. It
retains the existing playback engine, refill timing, source reader and modules
for Armada and Bust-A-Move 4. No new playback success is inferred from this
admission fix alone.

## Test 2 hardware result — 2026-10-15

Build `5be6d79357b3`, recording `62685.mp4`. The owner reported a minor audible
cut. This listening report establishes audible playback; the diagnostic
recording is used to read counters, not to independently assess sound.

Runtime admission and worker startup succeeded. The terminal snapshot shows
five successful native Play calls, zero refusals, zero HRESULT and three
recoveries. The retained event rows contain three UND events at native clocks
62,550, 73,900 and 87,900 ms, each followed by a successful Play. Fresh data
before those observations was 2,112, 4,416 and 1,344 bytes, respectively.
Each UND row records a 50 ms interval since the preceding cursor observation.
For the final event, the cursor consumed 5,632 bytes against 1,344 fresh bytes:
a 4,288-byte deficit, equivalent to about 24.3 ms of stereo PCM. This deficit
does not measure the audible interruption.

The interval from each UND event to its next recorded Play was 2,653, 764 and
1,502 ms. Those intervals do not establish uninterrupted silence: native
buffering and subsequent game commands also affect what the listener hears.
The whole run's maximum worker interval was 1,100 ms and longest source job
600 ms; neither maximum is temporally tied to a particular UND row by these
retained snapshots. Zero fresh bytes in the final samples coincide with a
stopped stream and a final stop command, not a failure to read any audio.

BAM4 and Armada also have recorded recovered underruns. The common symptom
is the native consumer overtaking fresh PCM; the shared refill/supply path is
a plausible common cause. These snapshots do not prove an identical underlying
stall in all three games, or explain which recovery the owner noticed. Keep
the working adapters unchanged while investigating that shared limitation.

## Install and launch

With the console off, merge the ZIP's `KUI` folder onto the SCI microSD card.
There are exactly two installed files:

- `KUI/runtime.kui` — matching menu and optional CE CD music selection.
- `KUI/apps/games/ce-cdda-worms.kui` — separate Worms audio module.

Keep your existing Armada RC9 and Bust-A-Move 4 modules. This package includes
neither module and does not replace the ordinary CE/native/MDK2/Toy loaders.
Keep your game files and saved settings. Cold boot after copying. The runtime
still identifies itself as **K-UI 2.0 RC9**; use the build identifier to identify
this candidate.

Choose the original US Worms Armageddon image, open **Y Options**, turn
**Background loading On** and **CD music On**, and launch with **START**.
These choices save automatically for this image. CD music defaults **Off**
for an image without a saved choice.

## Console test

1. Enter a real match and play for **at least three minutes**. Check music and
   sound effects during normal gameplay; there is no required wait for music
   on the title screen or main menu.
2. Start another match or restart the current match. Check that music starts
   or returns after loading and that effects continue. Note silence, repeated
   short audio, interruptions, delayed starts, distortion or a lockup.
3. After a failure, or after completing that test, press **A+B+X+Y+Start**
   together to return to diagnostics. Record **15 seconds**, covering one
   complete cycle of the three pages, which change every five seconds.
   Then power-cycle.

Send that diagnostic recording with what you heard, especially whether music
continued or returned after the match change/restart. If launch stops before
gameplay, photograph the error screen instead.

## Optional reuse with other Windows CE games

The **CD music** toggle is offered for original Windows CE GDIs containing
high-density audio tracks, and remains optional with a default of Off.
Availability of the toggle is not a universal compatibility promise.

The current adapters cover the admitted Armada, Bust-A-Move 4 and Worms
Armageddon profiles. Each requires its matching module and exact runtime
admission. An unknown runtime is refused clearly before handoff; the loader
does not guess sound addresses or borrow another game's native sound profile.
Use **CD music Off** for the ordinary CE loader if a profile is unavailable.

This candidate makes no new claim about FMV smoothness or all remaining audio
timing issues. A successful host audit does not establish sustained console
playback.

## Memory and source evidence

Worms reserves a separate 512 KiB pool below its original CE RAMend and OEM
region. The audio source map, controller and work areas move together. The
ordinary SCI DATA reader remains shared; the admitted Worms kernel/module and
native sound guards select the title-specific path.

Packaging requires a clean committed HEAD, matching ELF/envelope/display build
identifiers, the Worms linked memory/stack/bootstrap/owner-reader audit, and a
blank complete CEX2 source map. The map is filled from the selected image before
boot. No private game executable, CE kernel, ARM firmware or PCM sample is
distributed.

The ZIP includes current complete source from that commit, verified dependency
sources and licenses, build/audit evidence, and complete member SHA256/CRC
manifests. It includes no Git history bundle, inherited test replay, or rebuilt
Armada/Bust-A-Move 4 module.
