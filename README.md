# K-UI NeXT

An independent Dreamcast environment built directly on upstream KallistiOS.

The first implementation is a **hardware capability diagnostic**. It boots from
a selfboot CD, stays in RAM after you swap discs, probes raw GD-ROM samples, and
tests FAT32/exFAT storage through the standard external serial SD adapter.

**This is an early diagnostic, not a game dumper yet. Physical-console validation
is still required.** New project code uses GPLv3; dependencies retain their own
licenses. There is no separate contribution or commercial-relicensing agreement.

## Try the diagnostic

Use the `diagnostic` artifact from a successful **Diagnostic build** workflow run.
It includes `kui-diagnostic.cdi`, hardware instructions, a PC verifier, build
identifiers, checksums and source/license records. These are test artifacts, not
published releases.

Read [the hardware test instructions](docs/hardware-test.md) before burning a CD.
Use a spare FAT32 or exFAT card for initial testing.

| Controller | Action |
| --- | --- |
| A | Read both TOCs and selected raw samples from the inserted retail GD-ROM |
| X | Write a new SD fixture; close/remount/reopen and verify every byte |
| Y | Save the current diagnostic log into a fresh SD directory |
| B | Request Stop; an active operation finishes or follows its bounded abort path |
| D-pad Up / Down | Scroll the diagnostic log |
| Start | Return to the latest log lines |

The screen explicitly identifies incomplete operations. A sample pass is evidence
about those reads, not full-disc accuracy. The app has no formatting command and
creates new files only under `/KUI/probes/pNNNN/`. GPT, multiple-partition cards,
FAT12/16 and the internal SCI adapter are not supported by this diagnostic.

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
| M1.0: source/build foundation and visible boot diagnostic | Implemented; console boot test pending |
| M1.1: raw-disc and FAT32/exFAT capability probes | Implemented; physical SD and GD-ROM tests pending |
| M1.2: validated runtime loading from SD | Planned |
| M1.3: full-track GDI capture | Planned |
| M1.4: SHA-256 capture verification and controlled stop/resume | Planned |
| M1.5: hardware acceptance and minimal UI refinement | Planned |

See [the research scope](docs/milestone-1-research.md),
[the implementation decisions](docs/diagnostic-design.md), and
[dependency provenance](THIRD_PARTY.md).

## License and contribution policy

New K-UI code is **GPL-3.0-only**, unless a file states otherwise. Contributions
use the applicable existing file license. Contributors retain their copyrights;
there is no separate CLA or grant of proprietary relicensing rights.

This is a fresh repository, not a DreamShell fork. Requirements and hardware
observations can inform new implementations. Do not import DreamShell source,
binaries, artwork, build environments or a modified file by changing its name.
Independently licensed upstream KOS contributions remain attributed and usable,
including contributions by people who also work on other projects.
