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

- Read native GD boot metadata through the real optical drive. Admit bounded
  Mode 1 sectors in one high-density data extent; identify the exact IP and
  executable with CRC32 and SHA-256. Read/stage bytes again and require the
  complete fingerprints to match.
- Stop menu workers, release the SD filesystem and enter a small independent
  launch program. Its high stage validates the complete handoff before writing
  the native IP/executable destinations and entering the native bootstrap.
- Leave real GD-ROM service calls in charge of game reads and disc audio. No
  virtual-card lookup or save writes occur, regardless of Games' VMU setting.
- Keep X Check and the original BIOS boot accessible so check-only failures and
  direct-handoff failures can be reported separately.

The new path has no console-locale match gate and enters the native bootstrap
after that gate. This is the region-free design direction; foreign-region game
compatibility remains a hardware test, including the title's own video behavior.

## 2. Independent VMU storage owner — core prepared, not wired

The SD-backed VMU worker currently borrows storage from the emulated image
reader. A physical-disc game needs an independent resident storage callback
instead. The new owner core is host tested separately; this disc baseline does
not install it or perform VMU writes.

The integration must bind the chosen SCI/SCIF transport, physical SD identity,
partition and exact card/journal ownership. Every borrowed hardware register,
DMA state and interrupt setting must be restored. An uncertain restoration
must latch a terminal fault rather than permit another save operation. The
real disc and its firmware service remain independent of that SD ownership.

## 3. Install the standalone 72 KiB worker

Discover the supported native game's Maple driver, allocator lifetime and exit
hooks from its actual disc executable. Reuse the reviewed allocator-backed
reservation, or an explicitly reviewed exact-game arena where supported; do not
assume a currently unused RAM range will remain unused throughout every game.

Adapt card admission to physical media without constructing an imaginary SD
game-image map. Retain journal recovery, exact identity/ownership checks, private
stacks, bounded Maple transfers and physical-device passthrough. Link and audit
the independent SD/entry bridge before stating the total permanent footprint:
the worker target is 72 KiB, with no 192 KiB prefetch cache.

## 4. Connect existing per-game card choices

Use the physical disc's product identifier to find its existing virtual cards.
Reuse separate 200-block cards, multiple save versions, the default/always-ask
picker and supported slot selection. A disc and its matching SD game should
select the same identity-pinned card rather than creating unrelated saves.

VMU Off must bypass virtual-card lookup, preparation and journal access. SCI
and SCIF need individual console save/reboot/load checks; compile-time transport
support alone is not that evidence. Start with native families already working
with the standalone worker; older SDK families and Windows CE expand separately.

## 5. Compatibility and later features

Test normal drive reads, FMVs, CD audio, physical VMUs, both save transports,
cancelled preparation and explicit unsupported-game refusals. Add CD/MIL-CD,
Windows CE and more complex boot layouts only through their corresponding
checked paths. No in-game disc-switch feature is included in the first stage.

GameShark-style support can build on K-UI's existing RAM-patch infrastructure.
Begin with explicit boot-time address/value patches checked against the exact
executable. Conditional and continuously applied codes need a separately bounded
runtime executor and their own compatibility tests; they are not implied by
adding a direct disc handoff or resident VMU.
