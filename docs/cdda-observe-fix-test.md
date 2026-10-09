# Test 14 — intro read diagnostic

Use `K-UI-CDDA-Retail-Diagnostic.zip` for the next Toy Commander run. This
replaces the original observer at build `324c330bdb6c`; the new build identity
is recorded in the ZIP's `build.json` and shown on the console. Do not rerun
tests 00–13 for the same unchanged image/card.

The original observer stopped near the second intro screen. Its five supplied
screens show an image-service I/O failure, 2,113 observed GD calls and no
accepted CDDA PLAY20/PLAY21 requests. The underlying storage or image error was
not printed. The user also reported approximately three seconds of repeating
sound. That listening report does not identify the failed read or prove CDDA
playback. See the [hardware record](evidence/cdda-retail-observe-hardware-2026-10-07.md).

This replacement retains CPU/TMU observations and accepted PLAY parameters,
adds read-failure details, and omits live AICA/G2 sampling. Removing that
sampling tests a possible source of interference; it does not establish the
cause of the first failure. It neither enables CDDA music nor changes the
ordinary 1.8.5 reader. Its low-memory reservation and guarded stack stay within
the original limits.

## Install

With the console powered off:

1. Keep the existing working-reader backup at
   `/KUI/apps/games/retail-boot-before-observe.kui`. If you have already restored
   the normal reader and have no backup, copy it there first. Do not overwrite
   that backup with an observation build.
2. Copy this ZIP's `observation/14-retail-observe.kui` to
   `/KUI/apps/games/retail-boot.kui`.
3. Keep your normal working runtime at `/KUI/runtime.kui`. If test 13 is still
   installed there, restore the working `/KUI/runtime-before-cdda.kui`, keeping
   the backup.
4. Safely eject, boot SCI, then use the normal Games menu and standard SCI
   reader to launch the same complete original Toy Commander image.

The archive contains no `/KUI/runtime.kui`, automatic application replacement,
card-path configuration, game tracks or modified descriptor. Installing it
does not require copying or modifying the game image.

## Run and report

Let the introductory screens play without skipping them, and watch whether
the game passes the second screen. If it reaches
the title/menu, play briefly, then hold **A+B+X+Y+Start** together to collect
the observation pages. If it stops, photograph the read-failure screen and all
subsequent observation pages. The failure report is diagnostic evidence,
not a successful compatibility result.

There are **four observation pages**, including the final **AUDIO NOT
SAMPLED** page. A read failure adds a **14 STOP** page before them, for five
screens in total. Photograph all five on a failure, or all four after a normal
controller return. The replacement prints separate image, storage and cleanup
results instead of only the BIOS function number.

The first failure screen uses these hexadecimal rows:

| Legend | Values, in order |
|---|---|
| `FN ARG FLAG G BAD` | BIOS function, argument, latched hook fault, first current guard word, mismatch mask for all four guard words |
| `CMD LBA N BPS` | Active command, request start LBA, request sector count, bytes per sector |
| `IO LBA N DONE STEP` | Most recent read callback's start LBA and sector count, credited bytes, current sector budget |
| `IMG PRE STOP SD` | Image result, storage result before cleanup, cleanup result, final storage result |
| `DST BLK ERR CARD` | Destination address, successful physical-block count, GD error, last attempted physical card LBA |

An intact guard has first word `4B554947` and mismatch mask zero. The fault
details describe the failed read when the reason is `IMAGE READ FAILED`.
For another reason, read fields may describe the most recent read instead.

Pages remain for 1,200 video frames each, approximately 20 seconds at 60 Hz
or 24 seconds at 50 Hz. The observation pages do not repeat. A stopped reader
holds its final page until power-off; a normal controller return reboots after
the final page. Photograph any earlier staging refusal too.

Send the photographs together with whether the game passed the intro,
reached its title and gameplay, and whether the short sound loop occurred.
There is no numerical PASS target for this observation run. A report marking
audio as unsampled supplies no sound-channel, DMA or sound-RAM ownership
evidence.

`FFFFFFFF` in an image or storage result field means that operation was not
attempted during the failed read. The image-service's credited byte count is
the completed prefix, not a claim that a failed chunk left its destination
untouched. The next trace retains the read's arguments and error results; it
does not silently retry, reinterpret damaged raw sectors or suppress the stop.

## Restore

Power off and copy `/KUI/apps/games/retail-boot-before-observe.kui` back over
`/KUI/apps/games/retail-boot.kui`, preserving the backup. Leave the normal
working runtime installed. Safely eject before rebooting.

The source snapshot, linked ELF/map/stack evidence and checksums are included
for reproduction. The replacement's console behavior remains unverified until
this new run is supplied.
