# CMD17 versus CMD18 — one console comparison

**Completed:** build `93b1e6022592` passed on the owner's console. See the
[measurements](evidence/games-cmd18-comparison-2026-09-24.md). The current
gameplay candidate is covered by [the retail test guide](games-retail-test.md);
the instructions below retain the original standalone diagnostic procedure.

The playable DOA2 baseline remains pinned at `6c02bd8b22f4`, on branch
`baseline/doa2-sd-6c02bd8b22f4`. This package runs a separate read-only storage
diagnostic; it stops at its results screen instead of starting the game.

## Run once

1. Keep the pinned baseline ZIP for rollback. Use the same card, DOA2 dump,
   controller and boot CD as before.
2. Extract this package onto the SD root, replacing `KUI/runtime.kui` and
   `KUI/apps/games/retail-boot.kui`. Existing dump and music files stay in place.
3. Open Games, select the same DOA2 dump, and use the usual experimental retail
   launch action (Y, then A to confirm).
4. Photograph the final **SD CMD17 AND CMD18 COMPARISON** screen. One run is
   enough; no new rip or selected-image probe is needed.
5. Power off. Restore the pinned baseline ZIP to play again.

The launcher still performs its existing image preparation before the
diagnostic starts. The timed section reads about 263 KiB total and should be
short. It never writes card data, starts the game, or changes your dump.
If a failure screen appears, photograph that instead; do not keep retrying.

## What the screen measures

The six rows compare CMD17 with CMD18 at **2, 8 and 10 physical 512-byte
blocks per call**. Every row uses the same contiguous 20 KiB window inside a
validated high-density data-track extent. Allocation padding is excluded.
The order is CMD17/CMD18 then CMD18/CMD17, with two passes per row. An initial
CMD17 reference read is outside the timed comparisons.

- **KIB/S:** physical SD payload throughput, including pin acquisition,
  command/token waits, per-block CRC checking, CMD12 stop and pin release.
- **MAX … US:** longest complete read call, in microseconds. This is useful
  for choosing a future synchronous chunk size; it is not game frame time.
- **DATA AND STOP CHECKS PASSED:** every result matched the reference bytes,
  each physical block passed its card CRC, and a subsequent CMD17 read worked
  after every CMD18 pass. The reference CRC32 identifies the sampled window.

Timing uses TMU2 at the stock Dreamcast peripheral clock divided by four
(12.5 MHz), after the launcher has shut down. Its register configuration is
saved and restored before the final screen. The game reader does not borrow
this timer. Comparisons, CRC32 and screen drawing occur outside timed calls.
Overclocked hardware would need adjusted absolute time conversion.

CMD18 has bounded token/total-work limits. Every issued stream attempts CMD12
cleanup before deselection, including failure paths. CMD12 skips the ordinary
ready poll, consumes its SPI stuff byte, checks R1 and waits for busy release.
Uncertain cleanup invalidates the card until reinitialization. Both native
builds retain the existing memory, stack and linked-instruction checks.

The normal `sd-update` artifact paired with the tested `93b1e6022592` comparison
remained on the CMD17 game path. Later gameplay candidates are documented
separately; the diagnostic buffers remain outside the low resident. This
comparison does not leave a stream open across interrupts.

## Decision after this result

Adopt CMD18 in the image reader only if the measured gain justifies it and
the chosen chunk bounds preserve the baseline's improved audio. If the gain
is small, move on. One small warm window is a transport comparison, not a
promise about every card, fragmented file or game load.

Optional derived 2048-byte data tracks would remove 304 bytes of raw framing
per sector (about 12.9% of transferred track bytes, at most about 14.8% more
useful throughput when transfer alone is limiting). This is a format option,
not a unique capability: DreamShell also supports optimized GDI tracks.
Retain original raw dumps and audio tracks if adding such copies later.

The next app milestone is broader **native GD-ROM** launch eligibility with
per-title evidence, rather than more serial-pin tuning: replace the exact
DOA2 title gate with validated native boot/image requirements, recheck the
loaded IP flags, and test one additional owned native title. Startup memory
use and unsupported GD commands still need to be handled explicitly. WinCE,
image CDDA, VMU emulation and compressed images remain separate capabilities.

Focused host validation: `ASAN_OPTIONS=detect_leaks=0 make test-retail-sd-bench`
passed with address/undefined-behavior sanitizers. This covers the SD protocol
and benchmark bounds, measured-call accounting, byte comparison and release
on failure. Native gates run for both console variants before packaging;
actual card performance and timer behavior require the one console run above.

Hardware reference: [Renesas SH7750 hardware manual, section 12 (TMU)](
https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware).
