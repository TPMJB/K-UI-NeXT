# Launch map: up to 99 tracks

A launch reads a game's track files straight from card blocks, through a map
K-UI builds from the card's allocation table. The map used to hold 16 tracks
and 128 file pieces (extents), so Bust-a-Move 4 (25 tracks) and MDK2 (31
tracks) were refused. It now holds up to 99 tracks, GD-ROM's limit, in the
same memory.

## How it fits

Tracks and file pieces share one table of 12-byte slots: the tracks first,
then each track's pieces in track order (`union kui_retail_slot`,
`include/kui/retail_image.h`). A track entry holds its start, end, first
piece, piece count and whether it is data or audio: 12 bytes, half the old
24. A track's number is its position. The readers keep the 1,920 bytes they
used for 16 tracks and 128 pieces:

| Reader | Slots | For example |
|---|---|---|
| Standard (SCIF, SCI, IDE, Windows CE) | 160 | 31 tracks and 129 pieces; 99 tracks and 61 pieces |
| Background SCI reader | 64 | 31 tracks and 33 pieces |

Each reader is 64 bytes smaller than before (proxy build). The disc service
now reads one track layout instead of two.

Read speed does not depend on the track count.

## Audio tracks

K-UI plays no CD audio: play commands are accepted silently, as before. When
every track's file fits, all of them are mapped, as before, so raw reads of
audio sectors still work. When they do not fit (99 tracks need 198 slots at
one piece each), audio tracks are listed in the table of contents without
their files. A game that tried to read one of those sectors would stop on the
reader's `GD REQUEST REJECTED` screen. That is not expected: games read audio
tracks only to play them. The launch log says when it happens:
`Retail boot: audio tracks listed without their files to fit N tracks`.

With the background reader requested, its 64 slots are tried first, with
audio and then without. If the data tracks alone do not fit, the standard
reader is used, as before.

## Package format

The map travels in the launch package as `KUIRTI02` (version 2). The 320-byte
header is unchanged; the slots follow from byte 320 as 12-byte records. A
track's record holds its start, its end, a control byte, a piece-count byte
and two zero bytes; first pieces follow from the counts. K-UI and the stage
are built together, so no version 1 map is ever read.

## Tests

- `test-retail-image`: 99 tracks in all 160 slots, one piece or track more
  refused, 31 tracks in the background reader's 64, and unmapped audio refused
  before any card read.
- `test-retail-gd` (native, background and Windows CE builds): a 99-track
  table of contents, a seek, and position reports by track number.
- `tests/test_games_retail.py` (CI, FAT32 and exFAT): launches prepared with 31
  and 99 tracks, and the background reader with 31 tracks (audio mapped) and
  40 (audio listed only).
