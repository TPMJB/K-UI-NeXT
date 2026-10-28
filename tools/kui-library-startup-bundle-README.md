# Whole-library startup bundle

One command collects what K-UI needs to check every game in a folder against
the New reader and the virtual VMU: each game's `IP.BIN` and the boot file its
header names. No other game files and no audio are copied.

Keep `kui-library-startup-bundle.py` and `kui-native-startup-bundle.py` in the
same folder, then run Python 3 against your games folder:

```sh
python3 kui-library-startup-bundle.py /run/media/apollonius/Dreamcast/Games --output-dir ~/kui-library
```

The output folder must already exist. The helper prints one line per game and
writes `Library-startup-01.zip`, `Library-startup-02.zip` and so on, each at
most 25 MiB, so that each part fits an upload. Upload every part. Use
`--part-mb 0` for a single ZIP, or another size in MiB.

What it does with each image:

- **Original raw2352 GDI, native header:** copied (`IP.BIN`, boot file, the
  `disc.gdi` text and a hashed `startup-info.json`, exactly as the single-game
  helper writes them).
- **Same boot file as an earlier game:** listed as a duplicate, not copied again.
- **Windows CE:** listed with its title, not copied. It uses a separate reader.
- **CD audio in the high-density area:** listed, not copied. Add
  `--include-cdda` to copy these too.
- **Anything else it cannot read:** listed with the reason.
- **CDI, CHD, CUE, ISO and other formats:** listed by name only; the native
  route needs a GDI.

The tracks are only read, never changed, and existing ZIPs are never
overwritten. Symlinked folders and macOS `._` files are skipped. This prepares
evidence for analysis; it is not a game patch or a compatibility claim.
