<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Worms Armageddon Windows CE CD music test

This experimental candidate targets the original US **Worms Armageddon**,
product **T40601N**, version **V1.001**, with its original **eight-track GDI**.
Its native sound/runtime profile is separate from Armada and Bust-A-Move 4.
Test 1 booted on Dreamcast hardware but produced no CD music. Test 2 corrects
the runtime layout admission described below; sustained playback remains
unverified on hardware.

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
admission fix.

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
