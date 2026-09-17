# K-UI NeXT

An independent Dreamcast environment built directly on upstream KallistiOS.

The current SD runtime adds **raw-track GDI capture, saved-file verification and
controlled resume** to the independently booting hardware diagnostic. It stays
in RAM after you swap discs and uses FAT32/exFAT through the standard external
serial SD adapter. Keep the reusable CD and update `/KUI/runtime.kui`.

**Sword of the Berserk has completed capture and console saved-file verification.**
Uploaded tracks 1 and 2 independently match their CRC32/SHA-256 records; track 3's
PC check and an independent reference match remain pending. MDK2 has now resumed
from a saved checkpoint, checked its prefix, written new audio and stopped safely;
final verification of a complete resumed hardware dump remains pending.
The capture engine also passes host-image tests. Earlier SD runtime launch and
disc/exFAT probes passed on a physical console. The user also
confirms B selects CD fallback and reports successful cold boots; the boot count
was not specified. Missing-file detection, checksum rejection and restoring the
good runtime have now passed by user report; see the
[hardware evidence](docs/hardware-evidence.md) for remaining coverage.
New project code uses GPLv3; dependencies retain their own licenses. There is no
separate contribution or commercial-relicensing agreement.

## Try the diagnostic

For a console with the working bootstrap CD, download **sd-update** and follow
[the combined capture/resume test](docs/capture-test.md). A new burn is unnecessary.
The runtime opens on the capture page: A creates a new dump, X resumes the newest
matching job, Y verifies it, and B stops. Left/Right switches to diagnostics.
The **left trigger runs mstats** at any time, including during capture; a live
RAM estimate and sampled peak appear on the capture page. See
[memory counter definitions](docs/memory-stats.md) for comparisons with other software.
Capture, resume and verification also log elapsed-time breakdowns on Stop or
completion, including separate SHA-256, disc and SD time. See
[the short timing test](docs/capture-test.md#measure-capture-time).
The first MDK2 data-track measurement attributes about 63% of capture time to
the paired optical-read callback, 23% to SD writes and 11% to SHA-256; see
[the measured evidence](docs/hardware-evidence.md#mdk2-capture-timing-and-resume-overhead).
The isolated audio-track sample now measures **57.46 KiB/s**, with **88.75%**
of capture time inside the optical callback. Its 56.86 MiB resume prefix took
167.47 seconds, mostly SD reads (75.36%) and SHA-256 (20.40%). This baseline
test is complete; the next work is finer optical timing and faster resume. See the
[performance and resume test plan](docs/performance-test-plan.md).
Read [the capture format](docs/capture-format.md) for gap/audio conventions and
the distinction between saved-data verification and an independent reference match.

Use the `diagnostic` artifact from a successful **Diagnostic build** workflow run.
It includes `kui-diagnostic.cdi`, `sd/KUI/runtime.kui`, loader rejection fixtures,
hardware instructions, a PC verifier, build identifiers, checksums and
source/license records. These are test artifacts, not published releases.

Read [the SD bootstrap instructions](docs/sd-bootstrap.md) before the next burn.
The aim is to reuse one bootstrap disc and update only `KUI/runtime.kui` on SD
for ordinary application changes. Hold B during startup to use the embedded
diagnostics; missing/invalid runtime files also fall back. The old diagnostic
disc cannot load an SD runtime. Keep it as a working fallback.

Use a spare FAT32 or exFAT card for initial testing. Bundle boot, loader-error,
disc, storage and display checks into the same session using the same burned CD.
See [the hardware test instructions](docs/hardware-test.md) for the probes.

| Controller | Action on diagnostics page |
| --- | --- |
| A | Read both TOCs and selected raw samples from the inserted retail GD-ROM |
| X | Write a new SD fixture; close/remount/reopen and verify every byte |
| Y | Save the current diagnostic log into a fresh SD directory |
| B | Request Stop; an active operation finishes or follows its bounded abort path |
| D-pad Up / Down | Scroll the diagnostic log |
| Start | Return to the latest log lines |
| Left / Right or stick | Switch capture/diagnostics pages while idle |
| Left trigger | mstats snapshot, including during an active operation |

The screen explicitly identifies incomplete operations. A sample pass is evidence
about those reads, not full-disc accuracy. Probes use `/KUI/probes/`; captures use
new directories under `/KUI/dumps/`. Resume validates disc identity and all saved
prefixes before appending. GPT, multiple-partition cards, FAT12/16 and the internal
SCI adapter are unsupported. There is no formatting or zero-fill command.

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
| M1.0: source/build foundation and visible boot diagnostic | Diagnostic runs and display fix confirmed; cold boots reported working, count unspecified |
| M1.1: raw-disc and FAT32/exFAT capability probes | Nine disc samples and exFAT fixture verified; FAT32 console-reported pass, PC verification pending |
| M1.2: validated runtime loading from SD | exFAT SD handoff/runtime probes and B-selected fallback confirmed; missing-file/checksum rejection and good-runtime restoration pass by user report; four other malformed fixtures remain untested on hardware |
| M1.3: full-track GDI capture | Sword of the Berserk completion supported by user report, manifest and valid final checkpoint with zero retries; tracks 1/2 independently checked; full PC/reference verification pending |
| M1.4: SHA-256 capture verification and controlled stop/resume | Console reread passed by user report and published manifest; MDK2 checkpoint/prefix validation, resumed audio writes and controlled Stop logged in a fresh runtime session; final verification of a complete resumed dump and Sword track 3 PC check pending; fault injection and resume equality pass on host |
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
