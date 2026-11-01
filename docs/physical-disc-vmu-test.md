# Physical disc with virtual VMU — @BUILD@

Power Stone's direct startup passed on `c01e8c96f5ab`: the owner reported that
the repeated logo sequence disappeared and it booted directly into the game.
This package adds a separate SCI virtual-VMU route to that physical-disc menu.
**The new VMU route still needs its first console save/reboot/load test.**

Build `c84eba792f18` reached native bootstrap 2, then refused at phase `2`,
detail `0A`: the combined Maple-driver/buffer-layout recognizer failed before
allocating the resident worker. Its zero address fields did not distinguish
the two components. This revision checks the complete staged executable in
GD Play first, names the unsupported component and permits **R Save log**.
The installer keeps its independent checks and reports more detail if it
still refuses after shutdown. This is a diagnostic improvement, not added
support for an unrecognized game's SDK. The earlier entry-return correction
is retained.

Successful insertion, disc-check and card-review reads now leave the spindle
ready for **45 seconds** of idle time. A launch queued during that grace does
not issue an intermediate stop. Errors and cancellation still stop the drive;
CD audio keeps its existing ownership and stop behavior.

## Copy the KUI folder

With the Dreamcast powered off, back up the existing matching files to your PC,
then merge this ZIP's **KUI** folder onto the SD root. Replace the matching
files; do not replace the whole existing KUI directory. No Python installer
is needed. The GUI should show build **@BUILD@**.

Only these installed paths change:

- `KUI/runtime.kui`
- `KUI/apps/disc/native-disc.kui` — physical-only direct boot
- `KUI/apps/disc/native-disc-vmu.kui` — optional SCI virtual-VMU module

Your existing virtual cards, imported saves, settings, Games readers and
defaults stay in place. Restore the backed-up runtime and matching modules
to return to the preceding GUI. The ZIP's source/artifact/evidence folders
stay on the PC. It contains no game images or executables.

## First test: compatibility diagnosis on SCI

1. Use the same SCI SD setup and the disc that produced the refusal. The earlier
   Power Stone direct-boot result establishes physical startup only. Existing
   virtual-VMU hardware evidence covers **Power Stone 2**, not Power Stone 1;
   the physical-disc VMU route still needs its own test.
2. Open **Disc tools → GD Play → Y Options** and set **Virtual VMU On**.
   Leave **Region-free On** for the current direct-boot behavior.
   Leave physical A2 empty so the game has
   one unambiguous A2 target. Physical A1 may remain connected.
3. Return to GD Play and press **A K-UI boot**. It uses the inserted native
   disc's product to select its card. If multiple versions need a choice,
   **A** uses once, **X** saves the default, and **Y** restores Always ask.
4. If GD Play reports an unsupported component, press **R Save log** and send
   the saved report. It contains the actual product, exact executable CRC and
   byte counts, recognition results and any independently proven addresses.
   Do not keep retrying a known refusal with the same build. For the first
   physical VMU save/load test, an original Power Stone 2 disc is a useful
   candidate because its native SDK already has virtual-VMU hardware evidence
   on the SD launch path. Physical-disc integration remains unconfirmed.
5. If installation succeeds, reach gameplay and save to virtual A2. Wait until the game finishes saving.
   Power off, launch the same disc using **A** again, then load the save.
6. Set Virtual VMU **Off** in GD Play Options, then repeat **A**: this uses the physical-only
   module and skips all virtual-card catalogue, image, journal and profile
   lookup. Confirm normal physical VMU behavior.

**X Stock retail boot** uses the original console BIOS path. **Y Options**
contains the shared Virtual VMU toggle and a saved Region-free toggle. With
Region-free Off, K-UI boot checks the physical disc against the console's
flash region and refuses a mismatch. With it On, the direct path accepts
Japanese, US and European headers without a console-region match. That
design still needs foreign-region hardware validation. No BIOS flashing is
involved. Background reader settings in Games do not apply to the disc route.

For the planned PAL Shenmue II test, start with **Virtual VMU Off** and
**Region-free On**, then use **A K-UI boot**. This isolates region compatibility
from the new VMU integration. Cheats support is a later addition.

Report the full build ID, selected card, whether the expected A2 appears,
gameplay/save result and power-cycle load result. Photograph the entire page
if the installer refuses. Frontend failures stay on GD Play; **R Save log**
records the precise preparation failure. A blue installer refusal page must
be photographed before powering off.

## What is resident

The compact VMU worker retains its full **72 KiB**, including its **4 KiB
private stack**. A separately relocated **8 KiB** tail holds its independent
SD service, so the complete game-allocator reservation is **80 KiB**. There
is no image reader or 192 KiB prefetch cache. Real GD firmware continues to
handle game reads and audio. No BIOS flash or firmware replacement is used.

The installer checks the actual native executable's Maple driver, allocator
and exit hooks. Unsupported layouts refuse rather than claiming a free RAM
range. The low BIOS firmware RAM and real GD service vector are preserved.
Card ownership, CID, selected identity, live filesystem maps and journal
recovery are rechecked before the worker is attached. The prepared card is
the same identity-pinned per-game card used by its matching SD release.

This first VMU route supports **SCI only**, native original Mode 1 GD-ROMs,
and executables up to **4 MiB**. SCIF save support, Windows CE, CD/MIL-CD,
foreign-region hardware validation and cheats remain follow-up work. Host
tests and linked-code checks do not establish console save persistence.

`SOURCE.md` provides exact source recovery. See `physical-disc-vmu-plan.md`
and the supplied evidence for the implementation and validation limits.
