# K-UI 1.7 — disc formats development test

This build extends Games image discovery, metadata, covers and detached
readers. It is a development hardware test, not a final release or a claim
that every supported image boots on a Dreamcast.

Back up your current `KUI` folder. Merge this download's complete `KUI` folder
into the card root, replacing the supplied files and keeping your preferences
and game dumps. Install its runtime and Games payloads together: the new track
map is version 3, so payloads from earlier builds cannot be mixed. Keep your
compatible bootstrap disc. Restore the saved folder to roll back.

Put each image and all its referenced tracks in a folder under `/Games`.

| Image | Path in this build |
| --- | --- |
| GDI | Direct launch; raw 2352-byte or cooked 2048-byte data |
| ISO | Direct launch of a complete Dreamcast data session |
| BIN/CUE | Direct launch; shared/separate BINARY tracks and supported session geometry |
| CDI | Direct launch; supported DiscJuggler v2, v3 and v3.5 layouts |
| BIN/IMG | Direct launch of a standalone 2352-byte Mode 1/Mode 2 Form 1 data track |
| CSO/ZSO | Import to ISO on a computer with `tools/game_image_import.py` |
| CHD | Import CD/GD tracks with that tool and an installed MAME `chdman` |

Select a game with **A** and inspect its details, then press **A** for launch
confirmation. Matching Original/2048 GDI folders retain their version picker.
Native readers use **A** standard, **X** background 20, **Y** background 25.
Native CD confirmation uses **Left/Right** for **Plain** or **Scrambled** boot
encoding. Choose Scrambled only when the executable uses the MIL-CD transform;
the container extension does not identify its encoding. Windows CE uses **A**
for the background SCI reader; its regular reader is no longer offered.

Keep original images. CDDA playback, Mode 2 Form 2, subchannel emulation,
WAV/MP3 CUE tracks, NRG, MDS/MDF, CCD and arbitrary archives are unsupported.
Compressed files do not decode inside the resident reader. Standalone ISO
discovery requires valid Dreamcast IP/ISO metadata and its matching root record
within the first 64 physical sectors. Ambiguous shared-IMG session padding is
rejected. The first Games scan still validates Original/2048 pairs; pages reuse
its snapshot and **X Refresh** rebuilds it.

See [the formats guide](games-formats-test.md) for import commands, detailed
layout limits, converter reports and FMV investigation. Report title/region,
build ID, selected image/version, boot encoding, storage connection and reader,
plus boot and FMV/audio results. Host verification does not establish console
compatibility or loading speed.
