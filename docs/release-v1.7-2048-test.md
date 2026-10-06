# K-UI 1.7 — 2048-byte selector test

This is an experimental build for comparing converted GDI loading with raw GDI
and the original disc. It retains 1.7's accepted 256-byte SCI allowance. No
Dreamcast hardware result or disc-speed parity is claimed.

Back up your current `KUI` folder. Install this download's `KUI/runtime.kui`
and matching `KUI/apps` together on the same card; keep your compatible boot CD.
Restore the saved folder to roll back. Public 1.7 cannot launch these converted
images, so do not mix its runtime or Games payload with this test.

Keep the original dump. On your computer, run the source tree's converter:

```sh
python3 tools/gdi_optimize.py "/path/to/Games/Original game/disc.gdi" "/path/to/Games/Original game-2048"
```

Python 3 is the only dependency. The destination must not exist and must be
outside the source folder. The converter extracts Mode 1 data payloads into
2048-byte tracks, preserves raw audio, and records separate source/output
CRC32 and SHA-256 values in `conversion.json`. These hashes identify processed
bytes; they do not verify the dump against a catalogue or validate EDC/ECC.
The original files remain untouched. Full conversion requirements are in the
[2048-byte test guide](gdi-2048-test.md).

For a whole collection, the updated converter also accepts
`python3 tools/gdi_optimize.py --batch "/path/to/Games"`. It creates separate
`Game-2048` folders beside the originals. This selector build groups matching
single-GDI siblings as one entry; choose **Original** or **2048-byte copy**
after selecting it. Layout checks cover track count, LBAs, types and lengths;
they do not verify full file contents. Unmatched copies remain individually
accessible. A converter ZIP alone does not install the new console UI.

Copy the new folder alongside the original under `Games`. Select the game with
**A**, select a version with **D-pad**, then press **A** to inspect it. Its detail
and launch screens identify the chosen version. Press **A** again to open
launch confirmation. For native games, **A**
selects the standard reader, **X** selects background SCI with 20-block call
batches, and **Y** selects background SCI with 25-block call batches. First
compare both images with the standard reader. Repeat background SCI as a
separate comparison. Existing Windows CE confirmation uses **A** for standard
and **X** for background SCI; conversion does not establish CE compatibility.

Use the same game, card, connection, clock, settings and loading event. Time
three cold starts each for raw GDI on this build, converted GDI on this build,
and the original retail disc. Compare medians. Check a repeatable FMV for
audio/video sync and record stutter or failures. Measure initial K-UI launch
separately: cooked executables receive an extra preparation checksum pass.

Data backing tracks shrink by about 12.9%; audio does not shrink. Games that
request complete 2352-byte data sectors from a cooked track are refused by
this prototype. Existing CD audio playback limitations remain. This test
adds no on-console converter, ripper conversion option or game presets.

See [test changes and limitations](release-v1.7-2048-test-notes.md) before
reporting a result. Include title/region, build ID, connection, reader mode,
three timings and playback observations.
