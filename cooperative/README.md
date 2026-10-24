# New native candidates — f518592088f9

Two separate compiled modules from source `f518592088f9e397aa11780514535d0dbd52b42a`. Install one at a time.

| Folder | Purpose | Loader setting |
| --- | --- | --- |
| Hybrid | Test independent, bounded background-reader progress in Evolution 2 | SCI, Background On, Normal speed |
| Native-A2 | Investigate virtual A2 on other ordinary native games | SCI, Background Off |

Both use Auto/default encoding, CD music Off and RAM patches Off. Leave physical
A2 empty; physical A1 remains available. Use the existing prepared VMU pair.

## Hybrid first

Open a terminal in `Hybrid` and run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2
```

Safely eject and cold boot. Compare intro pace/audio and the same gameplay area
as the previous video. This normal mode has no intentional profiling stop.

For a diagnostic run, add `--profile-runtime` to that same command. During
gameplay, hold both triggers fully and B for about one second, then release.
Play through the problem for roughly 20 seconds until the intentional report
stop. Record at least 25 seconds of the cycling pages. READ MAX spans a whole
read; AGE MAX samples pending age; PUMPS/BLOCKS show the independent service.
The VMU COMPLETE measurement now includes the added loader work.

The candidate keeps the full 20/25-block foreground supply and eight-block
SCI interrupt cap, then adds at most two blocks after a successfully proved
physical controller completion. It restarts an idle SD run and yields when
DMA is pending. The existing one-block PIO allowance remains. This bounds
data work, not all command/recovery waits. No console improvement is claimed yet.

## Other native games

Open a terminal in `Native-A2` and run without a title selection:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

This leaves the startup cache empty and tries cold SDK-family discovery for
the launched game. The six `--game` entries are optional startup accelerators.
Recognition still requires supported allocator, layout, lifetime and ownership
proofs; it is not universal compatibility. Test loading, scene changes, saving,
full power-off and reloading. Record the title/revision and any complete refusal.

If a new family is refused, `collect-native-startup.py` can make an analysis
bundle from its original raw 2352-byte GDI. See the folder guide. Keep CE/CDDA
outside these native tests. The installer preserves existing image/journal/
identity/profile files; extracted CLI checks cover this for both modules.

## Source and evidence

`DESIGN.md` explains the CE, Saturn and PlayStation lessons and a proposed
16–32 KiB watermark read-ahead cache. That cache is not implemented here.
`evidence/` contains the test results and the earlier console measurements.
The source update preserves exact commits, including rejected candidates.
See `source/SOURCE-RECOVERY.md` for full-history checkout instructions.
