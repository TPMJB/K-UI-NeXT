# K-UI NeXT

An independent Dreamcast environment built directly on upstream KallistiOS.

The current SD runtime adds **raw-track GDI capture, saved-file verification and
controlled resume** to the independently booting hardware diagnostic. It stays
in RAM after you swap discs and uses FAT32/exFAT through the standard external
serial SD adapter. Keep the reusable CD and update `/KUI/runtime.kui`.

**Disc reading is done and verified. For the full state, read
[docs/HANDOFF-disc-reader.md](docs/HANDOFF-disc-reader.md) first.** A whole GD-ROM now rips in
about 20 minutes (it was over 100 with the original settings) and has been proven byte-exact
against the TOSEC catalogue on two discs: Sword of the Berserk (3 tracks) and MDK2 (31 tracks,
27 of them audio), on the console and again on a PC. MDK2 was also interrupted twice mid-disc and
resumed to a verified finish.
The capture engine also passes host-image tests. Earlier SD runtime launch and
disc/exFAT probes passed on a physical console. The user also
confirms B selects CD fallback and reports successful cold boots; the boot count
was not specified. Missing-file detection, checksum rejection and restoring the
good runtime have now passed by user report; see the
[hardware evidence](docs/hardware-evidence.md) for remaining coverage.
New project code uses GPLv3; dependencies retain their own licenses. There is no
separate contribution or commercial-relicensing agreement.

## M1.5 shell update

The SD runtime opens six apps: **Disc Ripper**, **VMU Manager**, **Memory Test**,
**Network Test**, **Settings** and **Diagnostics**. New captures use a selectable
parent folder, defaulting to `/Games`, with title-based folders and GDI filenames.
The hardware-proven acquisition engine remains unchanged.

Use the existing bootstrap CD and the `sd-update` artifact. Replace
`/KUI/runtime.kui`, and copy `KUI/apps/music/` for optional original menu music.
See [this app acceptance round](docs/apps-test.md) and
[app boundaries and the later executable-loader plan](docs/app-architecture.md).
These new app paths still need physical-console acceptance.

- Select with D-pad or stick and open with A. B returns home while idle.
- Ripper: A confirms a new dump, X resumes the newest matching job, Y verifies.
  R chooses or types a destination; Start opens Advanced and **Capture settings**.
  During work, B requests Stop. Reports still save automatically. Idle insertion
  detection shows the disc title; moving capture/verification phases show an ETA.
- System Settings: 640x480 TV timing with reversible preview, memory display,
  music enabled and volume. Capture hashes/readback remain inside the ripper.
- VMU Manager: read saves and make new SD backups with byte/CRC readback. No VMU
  writes, restore, delete or format. Memory Test checks only its allocated RAM.
- Network Test reports adapter/configuration evidence; it does not start DHCP or
  claim Internet reachability. Diagnostics retains disc/SD probes, log export,
  mstats and benchmarks.

Defaults now select **CRC32, automatic end readback Off, DMA and 2 Hz busy redraws**:
these are the policy choices used by the accepted fast captures. Explicit
`bench.cfg` keys override saved preferences, and the log prints the effective
options. Resume keeps the existing job's hash mode. **Y Verify always rereads saved
files**. Only an independent **FULL TRACK MATCH** gives the stream CRC badge a
green result; partial matches and saved-file verification remain separate.

A title such as MDK2 produces `/Games/MDK2/MDK2.gdi`; another New uses
`/Games/MDK2 (2)/MDK2.gdi` without overwriting the first. Resume/Verify select the
greatest numbered matching-disc checkpoint in the chosen parent, with legacy
`/KUI/dumps` fallback. Existing jobs keep their format. See
[ripper controls](docs/ripper-controls.md) and [the capture format](docs/capture-format.md).

The reusable CD's built-in diagnostics remain available by holding B during
startup. Missing or invalid runtime packages fall back automatically. Keep using
the working CD; this UI update needs no new burn. See [SD bootstrap](docs/sd-bootstrap.md)
and [hardware evidence](docs/hardware-evidence.md) for the established boot checks.

## Build and test

On Ubuntu 24.04 / a compatible Linux or WSL installation:

```sh
sudo apt-get install build-essential git curl wget patch python3 bison flex \
  texinfo gettext libgmp-dev libmpfr-dev libmpc-dev libisl-dev \
  meson ninja-build pkg-config libisofs-dev libpng-dev libjpeg-dev \
  dosfstools exfatprogs mtools
python3 tools/fetch_deps.py --fatfs-only
make test test-images
bash tools/setup_kos.sh
source .deps/kos/environ.sh
make diagnostic
python3 tools/package.py
```

Packaging requires a clean committed source tree, so the source archive matches
the executable. The initial compiler build is substantial; subsequent builds
reuse it. Dependencies are pinned in [dependencies.json](dependencies.json).
The scripts use upstream KOS's stable SH-4 compiler profile: GCC 15.2.0,
Binutils 2.45.1 and Newlib 4.6.0.20260123. No DreamShell checkout is needed.

Host tests run the same portable C command, TOC, block-device and filesystem code
as the console app. Filesystem tests use disposable regular image files and
independent `mkfs`/`fsck` tools. They do not operate on physical disks.

## Milestone 1

The complete goal remains: boot independently, use FAT32/exFAT serial SD storage,
capture every supported retail GD-ROM track, and verify and resume saved dumps.

| Stage | Status |
| --- | --- |
| M1.0: source/build foundation and visible boot diagnostic | **Done.** Diagnostic runs and display fix confirmed; dozens of cold boots with no controller problems |
| M1.1: raw-disc and FAT32/exFAT capability probes | **Done.** Disc probes (Sword of the Berserk, MDK2) verified; exFAT and FAT32 both verified end to end. On FAT32 the runtime loads, the storage test passes and a whole disc rips byte-exact, about 2.4% slower than exFAT ([evidence](docs/evidence/fat32-sword-dma-2026-09-20.json)) |
| M1.2: validated runtime loading from SD | **Done.** exFAT handoff, B-selected fallback, missing-file rejection and good-runtime restoration confirmed; all five malformed fixtures rejected on hardware, each with its own reason, with a usable fallback ([evidence](docs/evidence/m12-runtime-rejection-2026-09-20.json)) |
| M1.3: full-track GDI capture | **Done.** Sword of the Berserk and MDK2 (31 tracks, 27 audio) captured and verified against TOSEC on the console and on a PC; Sword ripped three ways, identical by SHA-256 ([handoff](docs/HANDOFF-disc-reader.md)) |
| M1.4: SHA-256 capture verification and controlled stop/resume | **Done.** MDK2 stopped twice mid-disc and resumed to a finish verified on the console and against TOSEC; Sword track 3 PC-verified. The lid opened mid-capture stopped cleanly and resumed to a TOSEC-verified finish on hardware (Omikron). A scratched disc retried a fixed 10 times, named the bad sector and stopped with the partial job kept. A B stop and a lid-open in the same boot both resume on DMA ([evidence](docs/evidence/dma-stop-fix-confirmed-2026-09-20.json)). **Open:** a card that fills mid-capture, host-tested only |
| M1.5: hardware acceptance and minimal UI refinement | Launcher, named dumps, insertion title/ETA, system/ripper settings split and initial VMU/Memory/Network/music apps implemented; latest app hardware acceptance pending. Full-card failure remains host-tested only |

See [the research scope](docs/milestone-1-research.md),
[the implementation decisions](docs/diagnostic-design.md), and
[dependency provenance](THIRD_PARTY.md).

## License and contribution policy

New K-UI code is **GPL-3.0-only**, unless a file states otherwise. Contributions
use the applicable existing file license. Contributors retain their copyrights;
there is no separate CLA or grant of proprietary relicensing rights.

This is a fresh repository, not a DreamShell fork. Requirements and hardware
observations can inform new implementations. Do not import upstream-derived DreamShell source or binaries by changing their names.
Independently authored additions from our earlier fork can be reused after checking
their origins, dependencies and intended license; see [the reuse inventory](docs/prior-work-reuse.md).
Original project artwork and the five original synthesized music loops are reused with recorded provenance; inherited assets keep their own terms.
Independently licensed upstream KOS contributions remain attributed and usable,
including contributions by people who also work on other projects.
