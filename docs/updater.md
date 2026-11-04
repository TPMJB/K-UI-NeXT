# Offline updates

The first updater uses an SD card and a complete `.kup` update file. Internet
access on the Dreamcast is not required. An eventual online downloader will
obtain release packages from
[TacitusVoxt/K-UI-NeXT Releases](https://github.com/TacitusVoxt/K-UI-NeXT/releases)
and feed this same installer. It must not install arbitrary `main` builds.
Automatic compatibility, compressed images, themes and additional storage
hardware are separate work.

## First installation and ordinary updates

Install the initial updater-enabled release using its normal complete SD-card
installation instructions. Keep a boot/recovery CD available. This one-time
manual installation establishes the retained baseline: `KUI/runtime.kui` and
its matching root `KUI/apps` modules. Existing bootstrap CDs can continue to
load that runtime. The console updater never replaces those root files.

For subsequent updates:

1. Download a trusted `.kup` release package on a computer.
2. Copy it into `KUI/updates` on the card, then insert the card in the Dreamcast.
3. Open **Settings → Updates**, refresh the list and select the package.
4. Check the displayed installed/target build and stable/testing channel.
5. Confirm installation. Wait for the final result before restarting.

The package checker verifies every member before installation; installation
checks the reviewed package again. A changed package requires another review.
The install writes a separate complete version, synchronizes and reads it back,
then publishes its selection record. Cancelling during staging leaves the
selected version available. A storage error while committing selection can
make the outcome uncertain: restart and check the selected version.

**Restore previous** verifies the retained previous version before selecting
it. The retained root baseline is also a restore target. Holding **B** when the
updater-enabled runtime starts requests the root baseline instead of the
selected version. This is a manual escape route, not an automatic watchdog.
A runtime with valid checksums can still hang or have a hardware-specific bug.

Games, VMU images/imports/backups, preferences, network credentials, covers,
music and custom assets are never update destinations. Recovery and boot-CD
images are not replaced. Updates require a supported writable FatFs volume
(FAT32/exFAT); this is not an ext4 installer. Do not switch cards/devices while
an operation is running. Old version folders are retained, so sufficient free
space for the complete new version is required.

## Version banks and selection

Each generation resides in `KUI/system/<12-character-build-id>/`. It contains
the runtime and the complete explicitly allowed executable module cohort.
The installer saves the checked `package.bin` header/manifest receipt last.
A directory without a valid receipt is not a completed version.

Two separate CRC-protected selection records, `active0.bin` and `active1.bin`,
retain the selected and previous build IDs. A new record is written to the
older slot; the current valid slot is retained. The selected GUI pins all
module reads and module presence checks to its own version bank. Missing,
corrupt or unexpected modules fail closed; they do not fall back to the root
module tree. Module SHA-256 verification consumes the same file stream used
to load it, before any launch-specific patching. Most components must match
the release build ID. Three historical CDDA adapters are explicit exceptions:
their path, original build ID, exact length and full-file SHA-256 must match
the approved pins in `update_legacy.inc`. They are included in the bank rather
than read from the root installation. Arbitrary old modules are refused.

Missing or corrupt update files can fall back to an older checked selection
or the baseline. A physical device read failure invalidates the mounted
session; the selector then blocks module loading and requires a restart or
Recovery. It does not switch to another device to complete a failed read.

The retained updater-enabled root GUI acts as the initial dispatcher. It
validates the selected runtime and propagates the bootstrap's storage
transport before handing over. A selected GUI with the same compiled build
continues with its module bank pinned. Restoring the baseline hands control
back to the retained root runtime and its root module cohort.

This first implementation uses the retained GUI as its dispatcher. It reads
and fully verifies the selected runtime once, then stamps its staged RAM-only
handoff slot. The launched GUI checks the fresh selection/receipt, its on-disk
header and the still-empty source slot before adopting that handoff. It reads
256 bytes of the GUI file in this path rather than the whole runtime again.
Missing/stale context uses full verification. The normal baseline path only
rereads its 64-byte header. No filled on-disk handoff is accepted in a package.
Boot time on SCIF/SCI/IDE still needs hardware measurement; the initial root
GUI remains an extra load compared with a future compact dispatcher.

The records and readback checks protect against partial updater writes. They
do **not** make FAT/exFAT directory or allocation metadata power-loss atomic,
authenticate an untrusted package, or prove the new release boots. A failed
sync/readback after a complete selection record was written may leave either
the old or the new complete version selected. The updater reports an
unconfirmed activation instead of promising an unchanged selection. Preserve
an independent backup and the recovery route.

## Building a package

Only a complete explicitly admitted cohort is accepted. The authoritative ordered file
catalog is [update_catalog.inc](../include/kui/update_catalog.inc). The first
format contains 26 `.kui` members, including all referenced module names and
their compatibility aliases. Existing partial development ZIPs and older
release ZIPs are not automatically suitable updater payloads. Static fixture
`probe.dat` and databases remain part of the retained base installation; v1
does not update shared data files.

The v1 cohort is fixed in the retained selector as well as the running GUI.
Updates can replace those components, but adding/removing module paths or
changing this package protocol requires a deliberate baseline upgrade. A
future protocol migration must retain the same recovery guarantees; an older
selector must not guess how to load an unfamiliar component set.

```sh
python3 tools/update_package.py catalog
python3 tools/update_package.py build path/to/complete/KUI update.kup \
    --build-id 0123456789ab --channel testing
python3 tools/update_package.py verify update.kup
```

The builder checks envelopes, payload CRCs, the release build ID (or exact
approved historical pin) and the GUI's bank-capability and empty handoff
markers. It rejects symlinks and incomplete/unapproved mixed cohorts,
rechecks source bytes while writing, and verifies the resulting package before
publishing the output file. Files outside the catalog are never included.
Testing packages are explicitly identified in the review screen. No signing
key or network credentials are embedded in the package.

The full native build orchestrator also runs each component's existing
layout/profile/slot/instruction admission before packaging:

```sh
source .deps/kos/environ.sh
python3 tools/update_release.py --output build/update-release/BUILD \
    --legacy-dir path/to/previous-complete/KUI --channel testing
```

It requires a clean source commit, builds all 23 fresh components, checks the
three preserved adapters, and writes build receipts, checksums and a complete
`.kup`. It never reads a game image or personal card profile. Silver/Toy VMU
components use a generated public blank profile that the launcher personalizes
in RAM before use.

`tools/package_update_test.py` combines two admitted builds and corresponding
sources into a hardware-test ZIP. It runs the real-release FatFs harness first;
the two different build IDs exercise actual chaining, checked RAM adoption and
rollback. Relabelling a single binary is not a substitute for these builds.

## Wire format, version 1

All integers are unsigned little endian. The package is a 128-byte header,
`count × 128` manifest bytes, then member contents in catalog order. Limits
are 64 entries and 128 MiB of payload. Each `.kui` member retains the existing
runtime envelope's stricter size/address/memory limits.

| Header offset | Field |
| --- | --- |
| 0 | `KUIUPD1` followed by NUL, 8 bytes |
| 8 / 12 | Version 1 / header length 128, 32-bit each |
| 16 / 20 | Entry count / manifest length, 32-bit each |
| 24 | Payload length, 64-bit |
| 32 | Build ID, 12 lowercase hexadecimal ASCII bytes |
| 44 | Channel: 0 stable, 1 testing |
| 45–63 | Zero |
| 64 | Manifest SHA-256, 32 bytes |
| 96–123 | Zero |
| 124 | CRC32 of preceding 124 header bytes |

| Manifest-entry offset | Field |
| --- | --- |
| 0 | Exact catalog-relative ASCII path, NUL padded to 80 bytes |
| 80 | File length, 64-bit |
| 88 | SHA-256 of the entire `.kui` file, 32 bytes |
| 120 | Policy: 0 current release, 1 exact approved historical adapter |
| 121–127 | Zero |

The GUI payload must contain exactly one initialized 16-byte capability marker:
ASCII `KUIBANK1`, 32-bit protocol 1 and its 32-bit inverse. Legacy GUIs cannot
be installed into a bank because they could load unrelated root modules.

It also contains exactly one 192-byte empty handoff slot: `KUIHOF1\0`, 32-bit
version 1 and size 192, with all remaining bytes zero on disk. Only the checked
RAM copy receives transport, selection, manifest/runtime digests, original
header fields, an integrity token and CRC. These establish transfer coherence,
not cryptographic authenticity. The running GUI consumes its slot once before
storage I/O; a source package with a populated/duplicate slot is refused.

Selection records are 128 bytes: magic `KUIACT1\0`, version and record length,
64-bit nonzero sequence at offset 16, selected and previous build IDs at 24 and
36, selected manifest SHA-256 at 48, zero bytes 80–123 and CRC32 at 124. An empty
selected ID and zero manifest hash select the retained baseline. Sequence
overflow is refused rather than wrapping.

## Console acceptance

A hardware-test ZIP contains a complete initial `KUI` baseline and another
clean build's testing `.kup` in `KUI/updates`. The two builds can differ only
in documentation while their compiled build IDs and checked envelopes differ;
this exercises real chaining without adding a game compatibility change.

Back up the current `KUI` folder before the initial manual copy. Use a separate
card when testing alongside unmerged loader experiments: the initial copy is
a complete matching software cohort, not a patch to those experiments.

Check the initial build ID, review/install the package in Settings → Updates,
restart through the existing boot CD, and check the target build ID. Confirm
that cards/settings remain present and familiar game launches still work.
Then restore the previous version, restart and check that the baseline ID
returns. Test the held-B baseline escape and a second installation of the same
package. Keep the build IDs and a saved log with any failure report.

## Validation boundary

Host validation covers package admission, real FatFs installation/rollback,
write/sync fault injection, protection of root/user files, selection records,
bank module resolution and UI states. Native linking is a separate check.
Neither replaces Dreamcast testing of storage transports, held-B baseline
recovery, restart and launching games from the selected bank. Do not label a
build as hardware tested based only on host tests or successful compilation.
