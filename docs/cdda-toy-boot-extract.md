# Extract Toy Commander's verified boot executable

Run these from the downloaded source directory, with Python 3. Use the **original**
`TOY_COMMANDER.gdi` from the successful preflight, beside its unchanged track files.
The utility reads only the GDI and the boot-file sectors in `track15.bin`.

```sh
python3 tools/extract_cdda_toy_boot.py '/path/to/TOY_COMMANDER.gdi' 'ToyCommander-1GUTH.BIN'
```

On Windows, the same command can use your real drive path:

```powershell
python tools/extract_cdda_toy_boot.py 'D:\Games\TOY_COMMANDER\TOY_COMMANDER.gdi' 'ToyCommander-1GUTH.BIN'
```

Upload **only `ToyCommander-1GUTH.BIN`** after the command reports verified success.
No audio tracks or full disc image are needed. This is a computer extraction,
with no new console test or game-file changes. Existing output paths are refused;
choose another filename if necessary. Write the output to your computer's normal
filesystem: atomic publication requires support for a same-directory hard link.

The extractor requires the original 451-byte GDI SHA256
`96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803`
and track 15's known raw-sector geometry. It checks all 366 boot-sector sync,
Mode 1 and logical-address headers, strips the raw-sector header/trailer, and
clips the last sector to the exact **748,444 bytes** found by preflight. Before
creating the output, it requires CRC32 `cdc493b3` and SHA256
`ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd`.

This executable permits inspection of the exact game's sound-driver and allocator
interfaces. Its identity alone does not establish ownership of main RAM, sound
RAM, channels or a periodic-service resource; those contracts still need review.
