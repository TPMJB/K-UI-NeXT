<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Experimental CD music across compatible images

This candidate builds on the owner's nearly lag-free WinCE CDDA RAM-copy
baseline, source `1aa16821209b`. It broadens adapter selection and adds
accurate evidence labels. Its incremental package updates an existing K-UI
installation. The accepted feature binaries still need the complete matching
release package's integration checks before final 2.0 publishing.

## Launch options and labels

Open a game and its launch options. **CD music** is available for inspected
native or WinCE GDIs with high-density audio tracks. It is Off for new images;
the existing per-image saved preference remains. WinCE CD music selects
background loading; native CD music selects the standard reader.
These CD music paths currently require SCI microSD. Plain WinCE launching
and music-Off behavior remain separate.

Unknown images show **Experimental / untested**, including familiar game
names with different or modified runtime bytes. Known tested versions are
recognized using disc metadata, the full IP fingerprint and the full boot
runtime SHA-256. A title string, selected option or saved preference cannot
produce a tested label. This establishes a tested runtime/version identity,
not a full-disc or whole-game compatibility result.

The inspected US Armada, Bust-A-Move 4 and Worms Armageddon runtime versions
have console audio evidence. Worms Armageddon's converted2048 build also
passed three owner-reported cold boots. The inspected raw Toy Commander
version has working audio evidence and a separate warning that FMV issues remain.
Cooked native variants and MDK2 retain experimental status here. Existing
limitations and earlier observations are preserved. World Party's successful
owner-reported run and diagnostic evidence are scoped separately below.

## What is broader

WinCE CD music selects a verified kernel/sound adapter from the complete
IP-selected kernel SHA-256. Product strings, boot filenames, track counts,
fixed track LBAs and game-specific sound-device holder addresses no longer
restrict the full-stream route to the three original applications.
The binder discovers the unique owning process and checked DirectSound
singleton, retaining object, code, owner-generation and API checks.
The player follows the validated audio inventory in the image.

This can attempt unfamiliar applications that use one of the four unchanged
kernel/native-sound implementations. It does not admit an arbitrary new
kernel, rebase, sound ABI or memory reservation. The detached stage rechecks
the complete kernel before pool modifications or native calls. Unrecognized
kernels are refused explicitly and music can be turned Off.

Worms World Party has a fourth kernel/sound adapter with its own module and
automatic launch routing. It is not admitted by the three older kernel
identities. Build `b08802f8945f` now has an owner-reported successful console
run. Its diagnostic video shows two successful native starts 33 seconds apart
and no recorded guard, bind, underrun or audio fault. The clip does not show
live gameplay, audible continuous music or the image's raw/cooked geometry.
The profile remains **Experimental** for 2.0; that scoped result does not
establish other revisions, all formats or unfamiliar applications. See the
[acceptance record](evidence/world-party-status-restart-2026-10-15.md).

Native CDDA has additional lifecycle and memory contracts. Separate
experimental native packages accept other disc layouts/IP identities when
the complete executable and installed sound driver match an existing checked
binding. The launcher checks executable size/CRC as a candidate selector;
the stage checks its complete SHA and the existing heap, hooks and driver
guards before use. This broadens compatible revisions/repackings. It does
not implement arbitrary unfamiliar Katana executable families.
Exact known-disc native routes retain their prior package identities. If the
older exact MDK2 module is absent, the launcher can use the newly built
experimental native adapter; corrupt or unreadable installed modules do not
trigger that fallback.

Quake III Arena is not admitted by either native adapter. Its GINSU transfer
overwrites the current worker address, and its main executable uses a
different active allocator. A new adapter needs a proven memory reservation,
state preservation or reinstallation across executable transfers, and checked
main-game service and sound ownership. Recognizing its Audio64 driver alone
does not provide those contracts. See the
[runtime audit](evidence/quake3-runtime-audit-2026-10-15.md).

## Formats, capacity and performance

WinCE supports raw2352 AUDIO with raw2352 or converted 2048-byte Mode 1 DATA.
Native compatibility packages support Toy's raw or cooked DATA contracts,
and MDK2's raw DATA contract. Cooked AUDIO, scrambled images and arbitrary
CDI/CUE/ISO music routes are not admitted by this candidate.

The full WinCE audio source map can represent up to 99 tracks. The separate
resident DATA map still has 64 slots for tracks plus file extents; capacity
and fragmentation can therefore reject an image earlier. Native adapters
retain their own bounded capacities. Failure reports identify the reason.

The successful 32-pump playing budget, source request size, worker priority,
sleeps, copy guards and aligned four-byte audio copies are retained. The
broader owner discovery replaces application-specific holder assumptions
during sound binding; the playing worker budget is unchanged. Known-version
inspection reads the boot runtime once to establish its evidence label;
unknown-image browsing does not hash whole game files.

## Console checks

Install the complete update's KUI folder over the existing installation with
the console off. Keep the prior native CDDA packages already installed.
Cold boot and check the existing Worms match with CD music On for audio and
responsiveness. Then inspect a different supported GDI: it should show
experimental status unless its tested-version fingerprints match.

An unfamiliar runtime can be rejected before handoff; selecting the option
is not a universal compatibility claim. For a runtime that is admitted, check
gameplay with simultaneous DATA and music reads, track changes, stop/restart
and return to the menu. No per-title tuning is needed merely to gather a
small read-only startup bundle with `tools/kui-wince-startup-bundle.py`.
That helper requires an original raw2352 GDI and omits audio payload.

Host fixtures exercise unfamiliar identities and track layouts, digest and
ownership failures, source-map capacity, UI labels and saved options. Linked
memory/stack audits and source/build identities accompany the package.
Host tests and static audits do not establish console compatibility.

## 2.0 scope

The accepted World Party candidate adds the fourth optional CE module and
matching menu to the existing installation. The owner reported successful
operation on `b08802f8945f` and requested the mainline merge. Keep its label
Experimental for 2.0; broader compatibility testing can follow later. Preserve
the working Armageddon module. Quake III music remains deferred.

The complete public 2.0 package is assembled on the release branch with its
ordinary modules and source/provenance sets. This incremental test update is
not a fresh-card installation. See [the merge handoff](cdda-2.0-handoff.md).
