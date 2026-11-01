# Physical-disc launcher and standalone VMU integration

The existing GD Play command exits KOS and reboots the console BIOS. That path
continues to be the ordinary disc-boot option. A VMU worker cannot rely on its
hooks and RAM reservation surviving that reboot, so VMU-enabled disc play needs
our controlled game handoff.

This work uses K-UI's existing checked disc reads, native executable models,
Maple hooks and durable VMU backend. It does not incorporate DreamShell source
or its `rungd.bin` firmware patch. SWAT's GD Play frontend itself uses a separate
disc bootstrap; its ISO-loader VMU initialization is a distinct path.

## 1. Physical-disc boot baseline — this test

The owner confirmed Power Stone's startup with `c01e8c96f5ab` on November 1,
2026: the repeated logo sequence disappeared and it booted into the game.
This validates that startup handoff only; virtual-VMU save/reload remains the
next hardware milestone. See `evidence/physical-disc-powerstone-c01.json`.

- Read native GD boot metadata through the real optical drive. Admit bounded
  Mode 1 sectors in one high-density data extent; identify the exact IP and
  executable with CRC32 and SHA-256. Read/stage bytes again and require the
  complete fingerprints to match.
- Stop menu workers, release the SD filesystem and enter a small independent
  launch program. Its high stage validates the complete handoff before writing
  the native IP/executable destinations and entering the native bootstrap.
- Leave real GD-ROM service calls in charge of game reads and disc audio. No
  virtual-card lookup or save writes occur, regardless of Games' VMU setting.
- GD Play exposes A K-UI boot, X Stock retail boot and Y Options. The earlier
  check-only shortcut is no longer shown.

The saved Region-free option controls the console-region match gate. With it
On, this path enters the native bootstrap after that gate; foreign-region game
compatibility remains a hardware test, including the title's own video behavior.

## 2. Independent VMU storage owner — SCI integration ready for console test

The new optional physical-disc module gives the worker its own serial SD
callback. Its support code, descriptor and ownership state occupy an 8 KiB
tail of the same game-allocator lease as the 72 KiB worker. No GD service,
kernel callback, image map or prefetch cache is retained.

The integration must bind the chosen SCI/SCIF transport, physical SD identity,
partition and exact card/journal ownership. Every borrowed hardware register,
DMA state and interrupt setting must be restored. An uncertain restoration
must latch a terminal fault rather than permit another save operation. The
real disc and its firmware service remain independent of that SD ownership.
SCI is wired for the first console save/reboot/load test. The independent
SCIF adapter is linked and host tested, while the prepared-card/profile and
console integration remain SCI-only.

## 3. Install the standalone 72 KiB worker

Discover the supported native game's Maple driver, allocator lifetime and exit
hooks from its actual disc executable. Reuse the reviewed allocator-backed
reservation, or an explicitly reviewed exact-game arena where supported; do not
assume a currently unused RAM range will remain unused throughout every game.

Adapt card admission to physical media without constructing an imaginary SD
game-image map. Retain journal recovery, exact identity/ownership checks, private
stacks, bounded Maple transfers and physical-device passthrough. Link and audit
the independent SD/entry bridge. The linked permanent reservation is now
80 KiB: the full 72 KiB worker/private stack and 8 KiB serial support. The
cross-ELF runtime stack audit includes both modules. Original low BIOS RAM
and the GD firmware vector are retained. The actual saved entry PR is rebound
to a separately verified SDK exit veneer inside the game's protected executable;
installation retargets that veneer to the owned terminal before gameplay.
This supports ordinary startup prologues that save PR without leaving a
return into temporary-stage memory. The earlier immutable-entry-only guard
was too restrictive, as the `71cfaaff012b` console refusal demonstrated.

The next console result, `c84eba792f18`, passed bootstrap 2 and refused at
phase 2/detail 0A while resolving the Maple driver and allocator. That gate is
the combined immutable Maple-driver/layout recognizer, before heap discovery
or worker allocation. The original report cleared both candidate objects, so
its zero addresses do not identify the unsupported component. The new shared
immutable probe runs in GD Play against the complete staged executable before
card preparation and KOS shutdown. It records the exact product, CRC, full and
program byte counts, component result and independently proven metadata.
Unsupported input stays on GD Play with a saveable log. Bootstrap admission
still repeats its own discovery, placement, live-state and installation checks;
preflight metadata grants no patch or allocation authority. A failure-only
bootstrap probe improves the fallback screen without relaxing recognition.

Insertion and successful optical review now earn one finite 45-second idle
grace instead of immediately stopping the spindle. The sole I/O worker consumes
the deadline before issuing STOP and yields to queued foreground operations.
Media removal, reset and CD-audio ownership cancel that grace. Failed or
cancelled optical work keeps immediate cleanup. This changes menu drive
scheduling only; real GD firmware still owns game reads.

## 4. Connect existing per-game card choices

GD Play uses A for K-UI boot, X for stock retail BIOS boot and Y for Options.
K-UI boot uses the physical disc's product identifier to find its existing
identity-pinned per-game cards. A single card or saved default launches
automatically; duplicate save versions without a default use the existing
picker. A missing card gets the normal blank 200-block per-game card.

VMU Off must bypass virtual-card lookup, preparation and journal access. SCI
and SCIF need individual console save/reboot/load checks; compile-time transport
support alone is not that evidence. The shared VMU toggle and a saved
Region-free toggle are exposed in Options. Region-free Off requires the
actual flash console region to match the disc; On permits the direct region
handoff, still pending foreign-region hardware testing. Start with native families already working
with the standalone worker; older SDK families and Windows CE expand separately.

## 5. Compatibility and later features

Test normal drive reads, FMVs, CD audio, physical VMUs, both save transports,
cancelled preparation and explicit unsupported-game refusals. Add CD/MIL-CD,
Windows CE and more complex boot layouts only through their corresponding
checked paths. No in-game disc-switch feature is included in the first stage.

The future Cheats feature can build on K-UI's existing RAM-patch infrastructure.
Begin with explicit boot-time address/value patches checked against the exact
executable. Conditional and continuously applied codes need a separately bounded
runtime executor and their own compatibility tests; they are not implied by
adding a direct disc handoff or resident VMU.
