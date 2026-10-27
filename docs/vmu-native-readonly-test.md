# Native read-only virtual VMU test

The physical Maple submission/completion test passed on Power Stone 2. This
next diagnostic advertises a blank virtual VMU at **A1**, preserves unrelated
physical replies and lets the game request card information and read blocks.
The virtual card exists only in RAM for this test; no saves or storage writes
are enabled.

1. Copy this ZIP's **KUI** folder onto the card, replacing the existing files.
   No installer or backup/restore script is required. Safely eject the card.
2. Leave **physical A1 empty**: remove any VMU or other accessory from the
   first expansion slot of the controller in port A. Keep the controller
   connected.
3. Launch **original Power Stone 2 GDI**, with **SCI / Standard**, default/Auto
   encoding, CD music off, background tasks off and RAM patches off. Use the
   original tracks, rather than the `-2048` conversion.
4. If prompted, select **A1**, the virtual VMU. **Do not save, format or erase.**
   Photograph any final diagnostic screen, then power off.

Success says **READ ONLY VMU READ VERIFIED** and **PHASE 0000000D**. It requires
at least one completed card information reply, one media information reply and
one block read. The build stops immediately after publishing that proof; you
do not need to reach gameplay. A refusal is useful too: photograph the full
screen, including phase, detail and counters.

The test is bounded to **3,600 submission/completion pairs**. If the game never
requests the required card information and block read, it stops with a refusal
report at that limit. A write, sync or unsupported A1 request also stops the
test before the physical transfer starts. If a physical device occupies A1,
the probe refuses before changing its reply.

This is a diagnostic for the tested native SDK family. It does not establish
general game compatibility, persistent save/load, long-session stability,
CDDA coexistence or SCIF VMU backing. Copy your normal KUI folder back before
ordinary play.

The ZIP includes the complete source checkpoint, build audit and SHA-256
checksums. It contains no installer.
