# Grouping collision-named multi-disc captures

K-UI allocates capture folders from the inserted disc's IP title. A second
capture with the same title becomes `Title (2)`, then `Title (3)`. These are
collision counters: another disc, a repeated dump and another release can all
receive the same suffix. They are never interpreted as disc numbers.

Games → Library options → **Scan for changes** now checks matching collision-name
families. It parses each selected image descriptor and reads one logical IP
sector. It does not hash game or audio files. Eligible native GD-ROM headers
must provide an actual `GD-ROMn/m` disc number and total, nonempty title,
product, version and region. Members within one release bucket must agree on
the complete release fields, have distinct actual disc numbers and retain the
same normalized name base. Different releases use separate buckets;
singletons and unknown headers remain ordinary visible rows. Multiple proven
releases with the same name get region/version/product qualifiers. Rare
remaining label collisions get a stable set number; truncation preserves UTF-8.
Original/2048 pairing still happens first; a paired representation is
one member with two selectable physical paths.

The scan log records each candidate's actual disc number, product, version,
region and title. Missing/unsupported headers stay separate, and duplicate
disc numbers prevent their exact release bucket from grouping. A storage read failure
or cancellation keeps the previous complete catalogue instead of publishing a
partially checked result. No folders, filenames or game bytes are changed.

Saved catalogue version 4 stores the bounded evidence under `KUI/games-library`.
Older version 2 and 3 catalogues remain readable and upgrade on an explicit
scan. Cold pages and disc choosers reconstruct groups from saved evidence;
warm pages stay in RAM. Neither path rereads image metadata for grouping.

New metadata-backed groups inherit old member-path/verified-anchor favorites
until an explicit group record supplies a choice. Recent uses the latest
group or member timestamp. Unfavoriting a group clears inherited member
favorites in the same checked activity save, preserving timestamps; a failed
save preserves all prior flags. This avoids losing history when a formerly
ungrouped capture family becomes a disc set, without scan-time activity writes.

Existing explicit `Disc`, `Disk`, `CD` and separate `D1` naming rules remain
unchanged, including their region/revision qualifiers and duplicate-disc
refusal. Bare sequel numbers remain ordinary title text.

## Evidence for Shenmue

Redump's own verified dump records publish the IP header fields for the USA
release:

| Disc | Media field | Title | Internal product | Version | Region |
| --- | --- | --- | --- | --- | --- |
| [1](https://redump.info/disc/3028) | GD-ROM1/4 | SHENMUE | MK-51059 | V1.003 | U |
| [2](https://redump.info/disc/3029) | GD-ROM2/4 | SHENMUE | MK-51059 | V1.003 | U |
| [3](https://redump.info/disc/3034) | GD-ROM3/4 | SHENMUE | MK-51059 | V1.003 | U |

These fields support grouping the three game discs even though their capture
folder names do not contain explicit disc markers. This is evidence for that
release, not a claim about an owner's uninspected dump. The
[USA Passport disc](https://redump.info/disc/3035) has version V1.004 and is not
treated as the same exact release identity. Other regions/revisions are not
special-cased by a title-only rule.

Synthetic tests use factual release fields with original generated payloads;
they contain no retail executables, audio or artwork. Run:

```
make build/test-games-group build/games-library-image
ASAN_OPTIONS=detect_leaks=0 ./build/test-games-group
ASAN_OPTIONS=detect_leaks=0 python3 tests/test_games_library_images.py
```

The image suite formats FAT32 and exFAT with a private host formatter when
external mkfs tools are absent. It covers collision captures, duplicates,
release conflicts, invalid headers, read failure/cancellation, old cache
migration, unchanged explicit groups and Original/2048 paths.
