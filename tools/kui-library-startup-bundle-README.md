# Whole-library startup bundle (v2)

One command inventories every Dreamcast image below a folder and collects what
K-UI needs to check each game against its launch routes: the disc header
(`IP.BIN`), the boot file the header names, any Windows CE program files, small
runtime or sound-driver files, and the disc's file listing (names and sizes
only). No audio, movies, textures or other game data are copied.

Keep these files together in one folder:

- `kui-library-startup-bundle.py`
- `kui-cdda-startup-bundle.py` (reads each image)
- `already-sent.json` (startups already received; they are listed, not copied)

Then run Python 3 against your games folder:

```sh
mkdir -p ~/kui-library
python3 kui-library-startup-bundle.py /run/media/apollonius/Dreamcast/Games --output-dir ~/kui-library
```

The output folder must already exist and must not already hold
`Library-startup-*.zip` files. The helper prints one line per game and writes
`Library-startup-01.zip`, `Library-startup-02.zip` and so on, each at most
25 MiB so that each part fits an upload. Upload every part. Each part lists
every game, so part 01 alone already gives the complete inventory. Use
`--part-mb 0` for a single ZIP, or another size in MiB.

What it does with each image:

- **GDI with raw 2352-byte or 2048-byte data tracks** (including `-2048` /
  ISO-track copies) and **Redump-style CUE/BIN**: read and copied. That covers
  native and Windows CE games, with or without CD audio tracks.
- **Startup already sent** (listed in `already-sent.json`): listed with its
  header details, not copied again. Use `--resend-all` to copy it anyway.
- **Same startup as an earlier image** (a second disc with the same boot file,
  or a 2048-byte copy of a raw GDI): listed as a duplicate, not copied again.
- **Anything it cannot read:** listed with the reason; the rest continue.
- **CDI, CHD, CSO, ZSO and other formats:** listed by name only. To include
  one of those games, convert it to GDI or CUE/BIN first.

The tracks are only read, never changed, and existing ZIPs are never
overwritten. Symlinked folders and macOS `._` files are skipped. This prepares
evidence for analysis; it is not a game patch or a compatibility claim.

## Why the startup count can be smaller than the collection

The `bundled` summary counts unique copied startups, not different games.
Already-sent startups and duplicate discs are listed only. Multi-disc games
such as Shenmue are copied once per distinct boot file; every disc still
appears in the listing with its disc number.
