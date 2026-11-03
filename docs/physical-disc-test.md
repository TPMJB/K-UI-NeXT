# Physical-disc launch test — @BUILD@

This is the first hardware test for K-UI's independent physical-disc launcher.
**It does not emulate a VMU yet. Use physical VMUs for this test.** It establishes
the disc boot and handoff that the standalone 72 KiB VMU service will later use.

The disc itself supplies all game data and CD audio. The SD card supplies only
the menu and the small launch program. No background image reader is installed
by this route, and it does not look up virtual cards, assignments or journals.

## Install by copying the KUI folder

1. With the Dreamcast powered off, mount its microSD on your computer.
2. Copy your current **`KUI/runtime.kui` to your computer as a backup**. If
   `KUI/apps/disc/native-disc.kui` already exists, back up that file too.
3. Extract this ZIP. Copy its **KUI folder onto the SD root**, merging folders
   and replacing the two matching files. Do not replace the entire existing
   KUI directory. No Python installer is needed.
4. Safely eject and boot. The GUI's build number should be **@BUILD@**.

Only these installed paths change:

- `KUI/runtime.kui`
- `KUI/apps/disc/native-disc.kui`

This GUI is based on Main's accepted VMU/import and Games catalogue build. It
temporarily replaces Claude's newer compatibility-test GUI, if installed. The
existing Background, standalone image readers, virtual cards, game assignments
and settings remain on the SD card. Use your backed-up GUI to restore its exact
previous version after the disc test; also restore the previous disc module if
one existed. The remaining ZIP folders contain evidence/source and stay on PC.

## Run the test

Insert an original native Dreamcast GD-ROM and open **Disc tools → GD Play**.

1. **X Check native disc** reads the boot metadata and full IP/boot-file hashes
   without launching. Record the title/product, boot-file length and result.
2. **Y Direct boot test**, then **A Direct boot** on its confirmation page,
   stages the same checked bytes and starts our new physical-disc boot path.
   Begin with Toy Commander or another native title with a small boot file.
3. Observe the first loading screens, title/menu, controller response and
   gameplay. When available, check streamed audio/video and CD audio.
4. Use a physical VMU to save. Power off after saving finishes, boot the same
   disc again through the test, and load that physical save.
5. If you already own an original GD from another region, optionally repeat
   the check and launch. The new path does not reject a disc for differing from
   the console's locale; an actual foreign-region boot is still unverified.

**A Boot via console BIOS** remains the existing normal disc-boot option. It does
not test the new controlled handoff. Virtual VMU settings in Games do not enable
an emulated card in this prototype.

This revision batches boot-content reads into runs of up to eight sectors. Each
run still uses the existing verified raw-pair read, checks every sector's address
and EDC, and preserves the complete boot-content fingerprints. Metadata reads
remain individual. Direct boot reads content once for its plan and again while
staging it. Preparation can still take time; report elapsed time and the last
visible message if it stops. The menu music cache is released before staging to
leave more RAM available. Oversized boot files are refused before hashing them.

Failures now stay on **GD Play** with the specific error, preparation stage and
last read location. Press **R Save log** there and retain the saved text log.
If the final RAM handoff is refused, the screen and log identify the precise
guard and source/stack addresses. The bounds themselves have not been relaxed.
The Power Stone log from build e91ea0a0d354 confirmed successful staging and
two exact cooked-sector reads, followed by refusal of mechanical drive status
3 (PLAYING). This revision admits that status at the final checkpoint after
both reads pass. Initial admission and all command ownership, media, size,
buffer guard and content checks remain in effect. Console boot still needs
testing beyond that checkpoint.

The subsequent 550edf18ef33 test reached Power Stone but showed a second
Dreamcast/Sega boot sequence. Its stage entered the lower IP bootstrap at
`0xac008300` while labeling it bootstrap 2. This revision enters the same
native bootstrap 2 address as K-UI's working image launcher, `0xac00e000`,
with the already staged executable. A linked-stage audit now checks the
actual handoff target and CPU literals. On this test, record whether the
full Dreamcast logo repeats and whether Power Stone still reaches gameplay.

For each result, send **the exact build number, game/product, X Check result,
Y Direct boot result, last visible screen, and approximate time**. Photograph
the complete failure screen and retain the text log using **R Save log**. Also
note physical-VMU save/reload and CD audio.

## Current boundaries

- Native, unscrambled **GD-ROM with Mode 1 data sectors only**, with the boot
  file extents wholly inside checked high-density DATA spans. Later data
  tracks after intervening audio are supported; reads into audio or gaps
  remain refused. See `shenmue-ii-disc-test.md` for bounded boot-read recovery.
  CD/MIL-CD, Mode 2 and
  Windows CE require later paths.
- X Check and direct boot accept boot files up to **12 MiB**, subject to
  available contiguous staging RAM. Direct boot needs matching updated modules
  for executables above the older 4 MiB module limit.
- Disc read, metadata, EDC, hashes and handoff admission can refuse the launch.
  Passing host checks does not establish a successful console boot.
- This build has no resident virtual VMU, no SD save writes, no cheat menu and
  no in-game disc switch. The independent VMU SD-owner core is host tested but
  is not connected to disc boot yet.

The next milestone installs the standalone VMU worker through this launch path.
See `physical-disc-vmu-plan.md` for the remaining work and `SOURCE.md` for exact
source recovery. No DreamShell source or firmware binary is used.
