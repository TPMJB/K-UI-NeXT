<!-- SPDX-License-Identifier: GPL-3.0-only -->
# VMU working theory and integration direction

## Current result

The cached full readonly build `af18576dba18` reached the existing loader's
playback baseline by the user's qualitative report. It restored all 1,200 DMA
fences and recorded 1,199 matched ownership proofs and advertisements, one
virtual A2 INFO reply, and no recorded fault. Its run did not exercise
MEDIA/READ/finalization or WRITE. Earlier build 137 independently exercised 54
READ/finalization pairs before the normal 34-word WRITE hit the readonly guard.

Playback parity with physical disc remains a loader performance issue. See the hardware evidence in
`evidence/armada-af18576-cached-readonly-console.json` and the implemented write
components, host verification and remaining integration contracts in
`armada-ce-vmu-write-integration.md`.

The newly supplied exact owner files establish four WRITE phases followed by
phase 4 finalization, a 4-byte WRITE RX, an 8-byte finalizer RX, and no transparent
retry for finalizer AGAIN. Timeout pokes leave the original IRP pending.
The opt-in writable runtime now connects the CE endpoint, deferred queue,
separate unmasked worker, dedicated SCI PIO provider, fresh pending-request
proof and pre-NK prepared-card handoff. Disc requests keep real positive tokens
and stay pending during a storage loan; the old busy return is not used.
Host integration and native admission are separate from console save/load and
power-cycle acceptance. See the write integration document and console test
for precise evidence, selected-card scope and remaining work.

## Common service, multiple validated adapters

The protocol and persistence abstractions are already independent of game
media: `include/kui/vmu_emulation.h` supplies read_block/commit_block operations;
`vmu_persist_session.h` supplies identity, read/write and sync. Reuse those as
the common packet, card, journal and durable-ack layer.

The hook/lifetime side is separate. `vmu_maple_observe_profile.h` has exact
owner profiles for Toy Commander, Silver, Grandia II, PSO and Skies.
`vmu_sdk_descriptor.h` shares native SDK geometry while retaining explicit
startup, allocator, producer, timer and lifetime admission. Armada instead
uses a CE worker adapter, CE page-table/cache handling and a pre-NK 24 KiB
reservation. A byte-pattern match alone does not establish a safe allocator,
live DMA ownership or compatible completion semantics.

A launch-time service can automatically choose validated driver families
using multiple structural/code/layout checks, then give each adapter the
same VMU backend. The intended user experience requires no manual profile
choice for recognized families. Custom Maple drivers, different SDK/CE
revisions and later executable changes can require additional adapters;
all-game coverage has not been established.

The current save lease is concretely transport-coupled:
`include/kui/vmu_sd_lease.h` borrows the image reader's initialized SCI card,
bus and shared GD lock. `games_retail.c` applies the associated launch and
format restrictions. Factor this into a save-storage provider with its own
identity, ownership and durability contract, so a physical-disc launch does
not depend on an image reader existing. Keep interrupt-time packet work
bounded; acknowledge save writes only after the backend's durable contract
has completed, using an explicitly admitted safe execution context.

## Physical GD-ROM launch

`src/apps/gd_play.c` currently calls arch_set_exit_path(ARCH_EXIT_REBOOT) and
arch_exit(). `include/kui/gd_play.h` explicitly describes BIOS reboot, not a
retained loader. Rebooting loses installed VMU state. Add a retained physical-
disc launch/bootstrap that identifies the executable, reserves live runtime
memory, installs the selected VMU adapter and leaves normal GD firmware reads
operational. Initialize/own SD save storage independently of disc-image I/O.
This is an additional launch path, not an existing GD Play option toggle.

Practical order: finish Armada runtime storage integration and durable save/reload tests;
extract the common launch-time service plus native/CE adapters and independent
storage provider; add verified automatic family matching; then add retained
physical-disc boot and its compatibility tests. The experimental 48 KiB layout
provides the next code/RAM budget; actual persistence and worker allocations
still require native link and stack validation.
