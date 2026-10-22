# Native relocated worker test

This follows the successful Power Stone 2 automatic allocation test. It copies
and relocates a real VMU protocol worker into the allocated memory, executes it
through P2 on a private 4 KiB stack, and checks its return to the loader. All
256 blank-card blocks are read through the relocated callbacks. It deliberately
stops before installing game-facing Maple hooks. It does not expose a VMU to
the game or write saves.

Extract this ZIP into its own folder. From that folder, with your card mounted:

```sh
python3 install.py /run/media/apollonius/Dreamcast --restore
python3 install.py /run/media/apollonius/Dreamcast
```

The first command restores the original loader if the earlier allocator probe
is still installed. Existing verified backups are preserved. Safely eject the
card, then launch **original Power Stone 2 GDI** with **SCI / Standard**,
default/Auto encoding, CD music off, background tasks off and RAM patches off.
Use the original tracks rather than the `-2048` conversion.

There will still be a pause for integrity checks and discovery, with progress
labels showing the current work. Photograph the single final report.

| Field | Expected value |
| --- | --- |
| PHASE | `0000000A` |
| ALLOCATED PAYLOAD | `0C38A880` |
| PROTOCOL REQUESTS | `00000103` |
| BLOCKS READ | `00000100` |
| BLANK IMAGE CRC | `3D826179` |
| Final message | `WORKER VERIFIED - PHOTO THEN POWER OFF` |

A `PROBE REFUSED` screen is a diagnostic result: photograph its phase and
detail. Power off after the report. Before ordinary play, reconnect the card
and restore the normal loader from this extracted folder:

```sh
python3 install.py /run/media/apollonius/Dreamcast --restore
```

The included source checkpoint and build audit preserve this test for future
work. This proof does not establish sustained worker residency during gameplay,
generic save/load support, CDDA coexistence or SCIF VMU backing. The next step
is validating the live SDK layout and submission/completion paths with the
relocated worker before allowing virtual replies.
