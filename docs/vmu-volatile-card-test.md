# Toy Commander volatile RAM-save A1 proof

**RAM ONLY: saves made in this test disappear on power-off or relaunch. The
microSD image stays read-only. This is not a persistent-save build.**

The module exposes the prepared 128 KiB SCI image as virtual A1 and keeps up
to 16 changed 512-byte blocks in a private RAM overlay. Reads see those RAM
changes during this launch. The existing KUIPROOF file in the base image is
not a Toy Commander save. This test is for Toy Commander's real write/sync
sequence and a same-launch save/load attempt; successful game behavior still
needs console confirmation.

## Install and test

1. Back up the normal 2.0.0 `KUI/apps/games/retail-boot.kui` outside `KUI`.
2. Keep the same SCI card and `/KUI/vmu-test/p0001` files in place. Do not move,
   replace or defragment `card.vmd`; the embedded profile must still match its
   original bytes and physical allocation. Copy only the ZIP's
   `KUI/apps/games/retail-boot.kui` into that location.
3. Remove physical VMUs and other plug-in accessories from all connected
   controllers for this test. Keep controller 1 connected.
4. Power-cycle and launch the supported original uncompressed raw-sector
   **Toy Commander** image with **SCI**, **Standard** reader, **background
   loading Off**, **CD music/CDDA Off** and **RAM patches Off**.
5. Check the new build ID and **RAM SAVES ONLY / SD READ-ONLY** title. Allow
   enumeration, then try an ordinary Save. If the game reports success, try
   loading that slot within this same launch. Do not choose Delete or Format.
6. Photograph the game's result and all five diagnostic pages. The proof allows
   18,000 schedules after card traffic begins and 36,000 overall (about five
   and ten minutes at60schedules/second). Unsupported commands, a full 16-block RAM overlay, physical
   conflicts or ownership faults stop earlier. Photograph any early stop;
   it is not evidence that the SD image was written.
7. Power off when finished. Restore the normal module for ordinary play.
   A restart intentionally returns the original unmodified base image.

## Transport and storage contract

Before launch, the stage verifies the same card identity/capacity, all 256
sector CRCs and full image SHA-256, and rejects overlap with game tracks.
Unchanged blocks use the existing resident reader's scoped SCI lease with
CID and CRC verification. No SD writer or filesystem operation is enabled.

Each admitted memory write has four 128-byte phases. WRSYNC commits a complete
block only into one of 16 private RAM slots. Incomplete, out-of-order, mismatched
or over-capacity transactions fail closed. A successful RAM commit is not a
durable save acknowledgement and carries no power-loss guarantee.

Every virtual A1 request is removed from a separate physical Maple chain
before DMA starts. That chain admits only bounded main-controller DEVINFO,
ALLINFO and controller condition packets on ports A-D; a schedule with no real
controller request is refused. Original SDK TX bytes and bookkeeping are
preserved. Completion checks the private chain, ownership, idle DMA and
MDSTAR, restores the original MDSTAR, and checks physical-controller
advertisement before any virtual RX publication. A real A1 card produces a
controlled conflict stop. Parent advertisement is published last.

ABI 6 has 64-byte exports, 88-byte config and 708-byte telemetry. Page 3 records SD reads and CID/CRC checks. Page 4 records RAM write phases,
commits, used blocks, cache reads, refusal/capacity faults and the filtered-chain
lifecycle. The archive audit identifies mode 3, volatile writes enabled,
persistence disabled and SD writes disabled. The supplied ZIP has a unique
build/profile identity; the published 2.0.0 shell remains separate.
