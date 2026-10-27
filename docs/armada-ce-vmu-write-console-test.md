# Armada CE virtual VMU save/load test

This is the revised writable Armada CE build after the loading freezes and
the controller-response fault in `E1D8E3811A94`. It uses your existing
SCI test card at `/KUI/vmu-test/p0001` as virtual **A2**. Your physical A1 stays
available. The full imported backup in `p0002` is not selected.

The module contains your prepared SD identity. Keep this personalized package
with your own console files. The source archive contains no private card
profile or game/kernel/DLL files.

The latest diagnostic reached a completed VMU read, then rejected a zero
controller response (A2 fault `05`). The previous continuation re-enabled
Maple DMA before CE consumed the current completion, allowing the old
descriptor to run again during storage work. This revision keeps Maple DMA
paused until the original consumer finishes. Interrupts remain enabled for
storage work and the bounded CE wait; the fixed CE argument-home adapter is
retained. Every owner, request, RX slice and held hardware witness is checked
again before publication. Invalid controller replies still stop the test.
This revision still needs the physical save/load result below.

## Install and save

1. With the console off, back up your current
   `KUI/apps/games/ce-probe.kui` and the complete `KUI/vmu-test/p0001` folder.
   Keep the original folder in place on the SCI card.
2. Copy this ZIP's `KUI` folder onto the same card. It replaces only
   `KUI/apps/games/ce-probe.kui`.
3. Cold boot. Launch the same original US raw Armada image on SCI, with the
   same background-reader settings used for the accepted baseline. Leave
   the CD audio option off. Skip the opening movie if you prefer.
4. Select the virtual **A2 / second slot in controller port A**. Use a normal
   new Armada save slot and wait for the game to finish saving. Leave the
   existing card formatted; do not accept a format prompt.
5. Power off after the game reports success. Cold boot the same build and
   choose that A2 save again. Confirm that the saved progress loads.

The result needed is whether **A2 appeared, saving completed, and the save
loaded after full power off**. This is a save/load test, not another FMV
comparison. Successful emulation requires the console result; host and native
checks alone do not establish it.

## If it stops

If automatic blue diagnostic pages appear, photograph all five pages and
the BUILD identifier. They retain the first fault. If the game instead shows
a card error, report its wording and when it occurred. Preserve the complete
`p0001` folder and journal so a later boot can recover an interrupted sector
commit; do not delete or replace its files to retry.

For diagnosis, A2 fault `00000070` (112) is a provider failure, and
`00000071` (113) is a queue/deadline failure. Its remaining four words contain
the SD reason, packed PIO/SD result, loan result, and first PIO detail. PIO
detail is `stage << 16 | SCR << 8 | SSR`; unsafe sampling contexts retain
only the stage. Cleanup cannot overwrite the first captured detail.

Preparation may stop before CE with a card ownership/recovery message if the
old profile no longer matches the live card/files. That refusal prevents any
guessed-sector save. A requested save can also return a card error if storage
cannot be borrowed within the bounded wait.

The visit-limit fallback is 36,000 Maple completions (about ten minutes at
60 completions per second), to allow ordinary save selection. It is a count,
not a wall-clock timer. Faults still capture immediately. An external CE
driver/process shutdown during an
acquired loan remains an unverified lifecycle: the outstanding save is not
acknowledged and storage stays excluded until restart.

## Restore

Your module backup restores the configuration immediately before this test.
Alternatively, copy `restore-baseline/KUI` onto the card and cold boot to
restore the exact accepted cached readonly **AF18576DBA18** module. Preserve
the current `p0001` image/journal together; the readonly module does not use
that saved card.

The current writable path is limited to the audited Armada CE driver on SCI.
Automatic adapter selection, general SCIF writes and retained physical-GD-ROM
boot remain separate integrations.
