# Toy Commander read-only virtual A1 card proof

This experimental module exposes a **synthetic empty 200-block VMU** in A1,
the first controller's first VMU slot. It is a card-discovery and read test.
There is **no SD backing, no persistence, and no virtual saving**. Its generated
128 KiB card contents reset on every launch; it does not create a card image,
save file, journal, or filesystem on your SD card.

The ZIP replaces only `KUI/apps/games/retail-boot.kui` through the normal
native Games route. Your K-UI 2.0.0 shell and boot CD stay in place. The passive
observer is a separate build named `vmu-maple-observer-test.zip`; this proof is
`vmu-readonly-card-test.zip` and deliberately supplies virtual responses.

## Install and run

1. Back up the installed 2.0.0 `KUI/apps/games/retail-boot.kui` outside `KUI`.
   Keep the original module so you can restore normal game loading afterward.
2. **Remove every physical VMU from controller 1**, including A1 and its other
   slot. Leave the controller connected. A1 must be physically empty for this
   test; a physical-card conflict stops the proof.
3. Copy the ZIP's `KUI/apps/games/retail-boot.kui` over the card's installed copy.
4. Launch the exact supported original, uncompressed **raw-sector Toy Commander**
   image over **SCI**, with the **Standard** native reader, **background loading
   Off**, **CD music/CDDA Off**, and **RAM patches Off**. Other titles, cooked
   sector variants, transports, and unsupported launch options are refused.
5. Open the game's **LOAD/card menu** and allow it to enumerate/read A1. This
   card is empty: an empty save list or 200 free blocks is the useful result.
   Do not choose Save, Delete, Format, or a card-management write operation.
6. Photograph the card menu and **every diagnostic report page**. Record whether
   the game recognized A1, showed an empty card, or stopped before reaching the
   menu. Photograph an early refusal or `IMAGE READ FAILED` screen in full.
   Power the console off when finished.
7. Restore the backed-up 2.0.0 `KUI/apps/games/retail-boot.kui` before ordinary
   play or reinserting physical VMUs in controller 1.

The proof stops on an unsupported virtual-card request, an attempted write or
sync, a physical-card conflict, invalid response ownership/capacity, or an
unsafe observer context. These are useful diagnostic results; they are not
instructions to retry a Save or Format operation. The observer's schedule,
private-memory, and bounded capture windows still apply.

## What happens on the Maple bus

The original game still submits its real DMA chain. With A1 physically empty,
the hardware times out that A1 transaction. At the existing exact SDK completion
hook, the proof checks the completed schedule, destination ownership, empty-slot
result, and response capacity, then replaces that slot's RX response **before
the SDK consumer runs**. It does not rewrite the DMA chain or suppress the
original DMA-start instruction. The controller's original traffic remains in
the same schedule.

The read-only backend admits the supported A1 device-information, extended
information, memory-information, and block-read requests. It generates the
canonical blank card's root, FAT, directory, and free blocks in memory. It never
enters a storage commit path. Unknown virtual-card commands and write/sync
requests stop this proof rather than extending it into save emulation.

## Evidence to retain

The package's `observer-artifact-audit.json` identifies ABI version 4, the
60-byte export table with `readonly_mode: 1`, and the 612-byte private snapshot.
It reports `virtual_vmu_enabled: true`, `storage_writes_enabled: false`, and
`mode: "READ-ONLY virtual blank A1 proof"`. The passive observer retains
`readonly_mode: 0` and `virtual_vmu_enabled: false`.

The linked native audit still checks both integer trampolines, the original
DMA-start replay, instruction restrictions, linked/embedded byte identity,
private P2 data, and compiler stack bounds. Strict ASan/UBSan host tests cover
the blank-card backend, completion overlay, and integrated proof worker.
These checks do not simulate Dreamcast cache behavior, bus timing, or hardware
DMA completion. Recognizing and reading the synthetic card on the console is
the next evidence needed. Successful discovery/read is not evidence of
durable virtual saves.

`SHA256SUMS.txt` covers the installed module, this guide, and the audit report.
The separate native-proof CI artifact retains the proof's ELF files, link maps,
disassembly, stack reports, and audit JSON for review.
