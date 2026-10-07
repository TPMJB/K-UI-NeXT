# Complete-image preflight and retail observation

Run **13 — image preflight** first. Install **14 — retail observation** only
after 13 reports PASS for the complete original Toy Commander image you will
launch. Both tests are supplied together; 14 does not need another package to
be built after 13's console result.

13 checks the selected image without launching it or starting audio. 14 uses
the standard SCI retail reader to observe the game's requests. These tests
establish image identity, mapping admission and baseline behavior; they do not
enable integrated CDDA music or reserve the game's sound/timer resources.

## Keep the working installation

Power off before changing card files. Preserve the working 1.8.5 runtime at
`/KUI/runtime-before-cdda.kui`; do not replace that backup with a test runtime.
If you retained the backup under another name, use that same working file.

Leave the complete Toy Commander image in its existing game folder, with
`TOY_COMMANDER.gdi` and all 15 sibling track files. The original raw descriptor
has 451 bytes and SHA-256
`96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803`.
An optimized descriptor, the earlier `/KUI/tests/cdda/` selected-track setup,
or only `track14.raw` does not satisfy this complete-image check.

The ZIP contains no game backing files and installs no GDI descriptor.
Extraction replaces `/KUI/runtime.kui` with 13. It places 14 at
`observation/14-retail-observe.kui` and leaves `/KUI/apps/games/` alone.

| File in this ZIP | Purpose |
|---|---|
| `KUI/runtime.kui` | Default 13 preflight runtime |
| `runtimes/13-image-preflight.kui` | Identical 13 runtime for restoring the test |
| `observation/14-retail-observe.kui` | Separately installed observation reader |
| `build.json` and `SHA256SUMS` | Source/build identity and package checksums |
| `evidence/` | Linked ELFs, maps and compiler stack reports |
| `source-snapshot.tar`, `source-fatfs/`, `LICENSES/` | Corresponding source and notices |

## Run 13

1. Extract the ZIP at the card root, preserving the directory structure.
2. Safely eject, boot the existing bootstrap CD and select **SCI**.
3. Run the card runtime. Its build ID must match `build.json`.
4. Wait for the final report. On PASS, photograph all six pages, including
   the build ID and selected image path. Each page remains for 15 seconds;
   the six-page cycle repeats automatically.

13 searches the card for the exact original descriptor and all 15 backing
files. It excludes `/KUI/tests/cdda/`. Discovery is bounded to depth 8,
128 directories and 4096 entries; multiple complete matches or a scan limit
refuse rather than choosing an arbitrary image.

If discovery refuses because the image is outside those scan limits or there
is more than one complete copy, create `/KUI/tests/cdda/preflight.cfg` with one
absolute ASCII card path to the intended descriptor, for example:

```text
0:/Games/Toy Commander/TOY_COMMANDER.gdi
```

Use the actual path already on your card. The configuration accepts one line,
optionally ending in LF or CRLF. It does not bypass descriptor, backing,
extent or identity checks, and it refuses a path inside the old CDDA test
folder.

The preflight opens every backing, derives complete track ranges from actual
file lengths and keeps gaps unmapped. It inspects IP.BIN and the ISO boot-file
extent, checks raw Mode1 sector sync/mode/FAD headers, and hashes the exact
32 KiB IP payload and boot-file bytes. It also measures each backing's card
extent map and checks the resulting full-map admission budget. The displayed
hashes identify the bytes read; this is not a whole-disc content hash. It
requires a complete physical map for all 15 tracks, including the 12 audio
tracks, to fit the observer's 64 slots. It does not omit audio extents to fit.

The final title is `PROFILE13 PREFLIGHT / page N of 6`. These pages carry the
information needed to identify the image and the card map:

| Page | Photograph |
|---|---|
| 1 | `Completed stages / failures: 6 / 0`, `Backed tracks / audio: 15 / 12`, and `Extent slots / 64-slot fit: … / 1`; title, product/version, region, boot file/LBA/bytes, CRC32s, hashed byte counts, private stack and checked card blocks |
| 2 | IP, boot, physical-map and GDI SHA-256 values; each digest occupies two lines |
| 3 | Tracks 1–5: D/A type, exclusive FAD range, file bytes, extent runs and first physical card block |
| 4 | Tracks 6–10, in the same format |
| 5 | Tracks 11–15, including the backed track 14 end and gap before track 15 |
| 6 | Selected descriptor path, scan/match counts, explicit-path flag and card/partition geometry |

The IP hashed-byte count must be 32,768; the boot count must equal its reported
boot-file length. The private stack must be nonzero and no greater than 65,472
bytes. Every PASS page includes the result; capture the pages together.

Track 14 is 3,071 sectors and 1,805,748 stereo frames. Its backed range is
FAD `[374351,377422)`; track 15 starts at 377572, leaving 150 unmapped sectors.
The preflight must preserve this gap and refuse incomplete or overlapping
backings rather than extending audio to the next track's start.

Reads have a 180-second deadline. There is no manual cancel button. If 13
stops, photograph the `PROFILE13 PREFLIGHT / stopped` screen and its reason,
and stop there. Restore the working runtime if you want to use the normal
Games menu; install 14 only after a PASS for the image you intend to launch.

## Run 14 after PASS

With the console powered off:

1. Back up the current `/KUI/apps/games/retail-boot.kui` as
   `/KUI/apps/games/retail-boot-before-observe.kui`. Preserve an existing backup
   rather than overwriting it.
2. Copy `observation/14-retail-observe.kui` to
   `/KUI/apps/games/retail-boot.kui`.
3. Restore the retained working runtime from `/KUI/runtime-before-cdda.kui`
   to `/KUI/runtime.kui`, keeping the backup.
4. Safely eject and boot with **SCI**. Use the normal **Games** path and select
   the same complete Toy Commander image. Use the standard SCI reader.

14 retains the normal retail entry/staging/resident architecture. Its
observation does not use the detached CDDA client's three stacks, add an
audio ring or start the AICA channels used by earlier controlled tests. The
complete map must pass the 64-slot admission check; the reader refuses an
insufficient map rather than silently dropping audio extents for this test.

Record whether Toy Commander reaches its title screen and gameplay, and any
visible stalls or failures. Visit the title/menu and play a short section that
would normally request disc music. Then hold **A+B+X+Y+Start** together to
return through the reader's diagnostics. If the resident reader stops,
photograph its `14 STOP` screen and the four
collected observation pages that follow. The final sound page then stays until
power-off. An earlier staging refusal stays on its first failure screen.

Photograph all four observation pages in order, including the build ID. All
four use the title `PROFILE14 OBSERVE /`. On a normal controller return, each
remains for 1,200 video frames, about 20 seconds at 60 Hz or 24 seconds at
50 Hz. After the fourth page the reader reboots to firmware; the pages do not
repeat. A resident fault instead holds the final page until power-off.

| Page | Information and value order |
|---|---|
| 1 — commands | `CALL 20 21 BAD N`: GD calls, accepted PLAY20/PLAY21 requests, guard fault and sound samples. `20F`/`20L` and `21F`/`21L`: first/last three playback parameters. `CPU TMU AICA SKIP`: changed-field masks and skipped sound samples. `KEY0 KEY1`: observed configured KYONB masks |
| 2 — CPU | `F`/`L` values: caller SR, VBR, GBR, PR, SP, MMUCR. `C N`: changed-field mask and sample count |
| 3 — TMU | `F`/`L` values: TSTR, FRQCR, then TCOR, TCNT, TCR for channels 0, 1, 2. `C N`: changed-field mask and sample count |
| 4 — sound | `F`/`L` values: master control, ARM control, G2 DMA mask, ARM IRQ enable, ARM IRQ pending, channel hash, key masks 0 and 1. `C N`: changed-field mask and sample count. `DMA F L OR CHG`: first, last, ever-set and changed DMA masks |

Values on 14's report are hexadecimal. `F` and `L` mean first and last
observed snapshots; consecutive rows continue the same ordered list.

The KYONB masks record configured key bits; they do not establish that a voice
is active or audible. Each sequential sound-register sweep is not an atomic
snapshot.

PLAY counters record accepted game requests; the baseline reader still
completes its CDDA commands silently. CPU/TMU snapshots are taken at observed
GD calls, and AICA sampling is sparse. Enabled/active G2 DMA or a busy FIFO
causes a sound sample to be skipped. Skipped or zero observations do not prove
that a resource is free, and call-bound snapshots do not measure the longest
interval without a GD call. Send the photos and the title/gameplay outcome
together; there is no invented PASS counter target for this observation run.

## Restore the normal reader

Power off. Restore `/KUI/apps/games/retail-boot-before-observe.kui` over
`/KUI/apps/games/retail-boot.kui`, preserving the backup. Keep the working
runtime at `/KUI/runtime.kui`. Safely eject before rebooting.

Do not leave the observation reader installed for ordinary use. The package
does not overwrite your original reader on extraction, and the restoration
steps require no change to game files.

The [CDDA roadmap](cdda-roadmap.md) separates this image/admission and retail
observation evidence from later scheduling, DMA, sound sharing and actual
retail CDDA integration.
