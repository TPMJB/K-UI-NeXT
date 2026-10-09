# Toy video test and launcher logo update

Copy the contents of this bundle's `KUI` folder onto the SD card, merging the
existing folder and replacing these two files:

| File on the card | Purpose |
| --- | --- |
| `/KUI/runtime.kui` | Launcher with cleaner header artwork and clearer package diagnostics |
| `/KUI/apps/games/retail-boot.kui` | Three-sector Toy Commander video test, build `d4d18c11ffeb` |

The files are already named for their destinations. Keep the working CD,
settings, original game files, and the preserved `7b55156aafa2` CDDA fallback.
Cold boot with SCI, then launch Toy Commander with A as before. No new CD is
needed for this SD update.

The launch diagnostics should identify `/KUI/apps/games/retail-boot.kui`, then
`Package d4d18c11ffeb (61652 bytes)`. The full game-launch file is 61,716 bytes
including its 64-byte envelope. The launcher's own build is recorded separately
in `build.json` and displayed in the launcher header.

The photo of the refused launch instead reported build `e4c521ddd8b5` with
102,332 payload bytes. That differs from the delivered video test. Its exact
provenance has not been established. The original loader printed “Loading SD
runtime” for every package, including a game-launch package; the updated loader
prints the file path and package identity separately. It also names the file to
replace when its layout is refused.

The supplied video test passes the launcher's actual C layout check. Its code
is byte-for-byte identical to the previously delivered `d4d18c11ffeb` test;
its CDDA worker is byte-for-byte identical to the console-tested eight-block
worker. No loader acceptance rules or audio scheduling changes were added.

The original helmet and K-UI wordmark now fill the header slot more clearly.
The blue plaque and tiny duplicate raster credit were removed from the derived
display. The original artwork file is preserved; the readable header credit
remains. See `evidence/launcher-logo-comparison-2026-10-08.png` for a host-rendered
comparison.

Compare the same intro's motion and continuous audio. If launch succeeds,
capture report pages 0–7 using A+B+X+Y+Start. The video test still awaits console
confirmation; the package refusal provided no evidence about its video or
audio performance.

For a game regression, restore the preserved eight-block `retail-boot.kui`.
Back up the previous `/KUI/runtime.kui` before installing if you want to revert
the logo update separately.

The bundle includes the current source snapshot and the original video test's
source and linked evidence. `build.json` distinguishes their build identities.
No game executable, music track, or extracted retail sound driver is included.
