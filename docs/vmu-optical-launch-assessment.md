# Persistent virtual VMU with an original optical disc

Source assessment: 2026-10-20. No optical virtual-card build or console test
has been performed by this assessment.

## Current answer

The virtual card need not obtain game data from SCI: Maple emulation and the
card journal can in principle accompany a game reading its original disc.
SCI would still provide the virtual card's microSD backing. That combination
is **not implemented by the current GD Play handoff** and is not established
by the Toy or Silver image-loader results.

`src/apps/gd_play.c` selects `ARCH_EXIT_REBOOT` and calls `arch_exit()`.
`src/dreamcast/main.c` parks the I/O worker and shuts down app audio first.
There is no executable inspection, title admission, retained allocation,
Maple patch, SD ownership handoff or post-BIOS installation point on this
route. Returning to the stock BIOS does not provide a contract preserving
an arbitrary K-UI hook through the subsequent disc boot. Installing the
existing Silver module therefore cannot add a card to GD Play.

## Reusable parts and missing integration

| Part | Existing implementation | Optical work still required |
| --- | --- | --- |
| Card protocol and persistence | `vmu_emulation.c`, `vmu_store.c`, `vmu_durable.c`: standard card packets, block journal, identity/CRC checks, durable acknowledgement | Reuse their contracts; repeat save/power-off/load with the optical path |
| Native Maple interception | `vmu_maple_observe_worker.c`, `vmu_maple_hook.S`: separate exact-Toy and exact-Silver submission/completion paths | Verify the optical executable, startup state, banks and completion consumers match the admitted profile |
| Owned game memory | `vmu_maple_observe_stage.c` and boot assembly: wrap the native heap initializer, obtain and validate the game's `0x30000` allocation at `0x8cfd0000` | Reach the installer after the optical bootstrap, with its code, stack and immutable inputs still owned |
| SCI storage | `vmu_sd_lease.inc` borrows the image reader's initialized card under its GD-entry lock | A VMU-only retained SD owner and typed lease; optical firmware does not supply this image-reader lock/card |
| Storage admission | `vmu_persist_stage.c` checks the live filesystem, card identity, allocation map and journal recovery against a validated SCI game manifest | A separate optical launch contract retaining full card ownership checks, without inventing SD game extents |
| Launch | Image stage loads IP/executable, inserts an entry trampoline, runs owner bootstrap 2, checks the restored executable, then arms heap hooks | Original-disc loading/authentication, entry relay and firmware preservation; GD Play currently only reboots |

The current durable worker validates its lease entry inside
`0x8c004000..0x8c007800`. Its low bootstrap/control/failure code also lives in
the image resident. A replacement storage owner needs a measured linked
layout and the corresponding stage, worker and package audits. It is not
enough to point the existing worker at a menu-owned SD descriptor.

## Practical implementation route

Build a separate **exact-title optical launch experiment**, retaining ordinary
GD Play. A controlled launch can load the owner's IP/executable from the
inserted authenticated disc, pass through the owner bootstrap with a verified
entry relay, and install the existing native heap/Maple profile before its
first use. The game must continue to use real optical firmware for its data.
This is a new launch mode; it cannot use the final BIOS reboot as its handoff.

Minimum work:

1. Read and validate the optical TOC/session, complete IP and executable. Keep
   the current exact executable SHA, size, instruction witnesses and live
   allocator checks for either Toy or Silver. A matching title string or
   product ID alone cannot admit the hook. GDI-file CRC and SD track extents
   are image-source facts and require a separate optical representation,
   rather than weakening the existing image predicates.
2. Prove optical bootstrap/entry handoff without a virtual card. Reuse the
   existing bootstrap/trampoline CPU and cache discipline only where its
   assumptions still hold. Preserve physical GD initialization and access;
   do not install the existing image GD replacement, synthesized drive state
   or image-only unchanged-disc answer.
3. Retain a bounded VMU-only bootstrap and SD lease. Establish ownership for
   its low code/control/private stack and its temporary high installer until
   heap initialization completes. The image loader's low reservation is not
   a proof that real optical firmware never uses that memory: the existing
   low-resident evidence explicitly leaves dynamic firmware scratch open.
4. Adapt persistent preparation around the actual SCI card and pair, retaining
   live allocation validation, CID pinning, clean recovery/checkpoint and
   immutable durable handoff. Optical game sectors do not occupy the SD
   filesystem; unrelated SD allocations still need protection. Keep the same
   backing pair identity across image and optical launches when deliberately
   sharing that card.
5. Admit optical GD activity alongside the Maple worker. The image resident
   currently rejects reads into the live worker/header and serializes SCI
   use. Real optical reads bypass those checks, and interrupt masking does
   not stop an already active GD DMA. Prove destination ownership and the
   relevant DMA/interrupt/firmware interactions, or add a narrowly admitted
   preserving wrapper that validates and forwards real optical operations.
   Also verify SCI register restoration and save latency during disc activity.
6. Package and audit this as a distinct optional module, then test discovery,
   read-only/imported-save loading and finally durable saving on hardware.

## Next native test

The next useful original-disc test is a bounded **optical boot and passive
hook observation** for one exact existing title, after steps 1–3 above have a
compiled and audited implementation. Silver offers the stronger existing
owner-reported save/power-off/load baseline; Toy has a smaller executable.
Either is conditional on matching the actual optical executable and startup
contracts. Current source cannot produce this test by changing a launcher
filename or disabling an SCI/title gate.

First demonstrate that the real disc reaches gameplay with original GD
services, the allocator lease remains intact, and both Maple hook boundaries
see the admitted state. Then use the same optical path to load an existing
virtual save with writes disabled. Enable journaled writes only after those
observations and optical/SCI ownership checks pass; finish with a new save,
full power-off, reboot through the optical path, and load of that save.

Useful source references: `src/apps/gd_play.c`, `include/kui/gd_play.h`,
`src/loader/vmu_maple_observe_stage.c`, `src/loader/retail_stage.c`,
`src/loader/vmu_sd_lease.inc`, `src/loader/vmu_maple_observe_worker.c`,
`src/loader/vmu_persist_stage.c`, `include/kui/vmu_toy_observe_admission.h`,
`include/kui/vmu_silver_observe_admission.h`,
`docs/evidence/native-low-resident-2026-10-06.md` and
`docs/vmu-emulation-status.md`.
