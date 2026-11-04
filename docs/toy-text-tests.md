# Toy Commander: verified selection and TXT reports

Start with **one Observer run**. No report-page video is needed.

Both supplied recordings show the old `9739EB693B02` reader and its legacy
SCHED/TRACE pages. They do not contain the clean/observer modules from the
previous package. The normal menu preferred an installed exact adapter;
copying a new compatibility adapter did not reliably change that selection.
The new test bank explicitly selects its own Toy adapter, checks the bank
and module IDs, and refuses missing, corrupt or mismatched replacements.

This is a diagnostic delivery, not a claimed FMV speed fix. It preserves the
AP transport, CRC, budgets, audio handoffs and original ownership checks.
It adds a verified module-selection path and saves frozen counters to TXT.

The retained baseline is the AP receive-ahead path: DATA schedule 2,
budget 4, follow-up 2, tuning 1, startup IRQ 1 and pipeline 2. This is not
another sweep of the old timing, mute, raw/cooked conversion or worker-size
tests. The earlier supplied clips do not establish the clean-versus-observer
comparison because that newer code never ran. Fixing selection and capturing
its existing bounded observer is the next unresolved measurement.

Native loader work remains separate: standalone VMU-only is the established
72 KiB cacheless path; combined Background 72 retains its different 284 KiB
layout. This package changes neither layout nor prefetch allocation.

## Install once

This requires the working offline updater from the updater thread.

1. With the console off, back up the existing `KUI/reports` files if present.
2. Merge this ZIP's `KUI` directory onto the card. It supplies `.kup` files
   under `KUI/updates` and two fixed-size report slots under `KUI/reports`.
   It does not replace root `KUI/runtime.kui` or root game-reader files.
3. Open **Settings → Updates**, refresh, and select
   **Toy-Observer-TXT-d20261104032.kup**.
   Review build `d20261104032`: exactly three bank replacements (the test
   menu and two Toy formats), with all 23 other paths inherited.
4. Activate it and restart. Confirm the menu says **TOY TXT TEST**. If that
   label is absent, do not start a test through the old menu.

The selected bank ignores the old exact Toy-module precedence. No renaming
or deleting installed readers is required. All native VMU, Background,
prefetch and WinCE modules remain inherited from the existing installation.
Other Games-image launches refuse in this test bank.

## One Observer run

Use the same existing Toy image/format and bad movie scene. Use SCI,
Background Off, CD music On, Virtual VMU Off and RAM patches Off.

The new module must show build **d20261104032**. It verifies the report slot
before game handoff; a missing or unsafe slot refuses before gameplay.
The explicitly labeled **VERIFYING TXT REPORT SLOT** step checks filesystem
ownership before allowing raw-sector writes later. It can take extra time
on a large card; this preparation occurs before measurement and uses batched
metadata reads. It does not reserve a runtime prefetch cache.
Let the chosen bad scene play naturally for about 20–30 seconds. While it is
still playing, press **A+B+X+Y+Start** once. Do not press Skip first.

The capture freezes before shutdown or logging. The terminal stops the owned
SD stream, saves the text, syncs and reads it back, then publishes the CRC
completion footer. The game remains stopped throughout this process.

Wait for **REPORT TXT SAVED - POWER OFF**. Then power off, mount the card on the computer,
and send **KUI/reports/TOYOBS.TXT**, plus a sentence about picture and sound.
Do not film cycling pages. If saving fails, take one photo of the error;
the file may be an older result, so do not call it the current capture.

The report file deliberately stays 32 KiB and its filesystem timestamp may
stay unchanged. Its content/build ID and checked completion footer identify
the result. Do not copy the ZIP's blank slot over a result before sending it.

## Clean bank, only if needed later

**Toy-Clean-TXT-d20261104031.kup**, build `d20261104031`, uses the same AP path with the older
trace/probe/history hooks removed and the new observer disabled. It writes
**KUI/reports/TOYCLEAN.TXT**. It is included for a matched follow-up if the
Observer result makes that comparison useful. No second run is requested yet.

## Return to the working installation

Open **Settings → Updates**, press **Y — Restore previous**, confirm, then
restart. Holding **B**
when an updater-enabled menu starts requests the root baseline for that boot.
These operations select the existing bank/root; no native modules are
installed by the experiment. Saved games and preferences are not rolled back.

## What the TXT proves

The text includes the actual module ID, image geometry, all 356 frozen
header/bin/summary words, clock validity, original caller masks, VBR/SCI
priority witnesses, DATA progress, elapsed service classes and AP state.
The eight bins target the recent sixteen seconds; expired or invalid history
is explicit. AP block counters exclude a separate low-reader fallback.
Nested GD/service spans, hardware waits and cross-bin flags must be respected;
the report does not invent decoder CPU usage or sound-interrupt measurements.

Only a completed file with matching body length/CRC is accepted by the bundled
analysis tool. Logging performs no SD work during the measured movie interval,
no FAT/directory allocation at terminal time, and no game resumption afterward.

The next decision comes from the actual scene's recent window: whether DATA
reaches the physical transport limit, how long the loader holds the caller,
which caller/interrupt context supplies progress, whether CDDA is active,
and whether clock/error flags permit a timing conclusion. Those observations
separate a traffic reduction investigation from a scheduling investigation.
They do not measure movie decoder CPU time directly. A frozen report from an
inactive or expired window will be identified as insufficient evidence.
