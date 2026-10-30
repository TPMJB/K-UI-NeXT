# Full saved-salvage revalidation — @BUILD@

The preceding CMD17 console build passed all four saved-candidate profiles:
6,190 records each, matching SHA-256, no read errors or DMA fault latch. This
update retains that single-block SD read fix and adds the missing full-job
revalidation action.

The earlier candidate comparison deliberately leaves quarantine in place.
It did not validate the complete journal or saved tracks. This new action
checks that evidence before permitting quarantine removal.

## Install

With the console powered off, mount the card on the PC. Extract the ZIP into
a new folder and run from the folder containing `install.py`:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

Use your actual mount path if different. The installer is at the ZIP root.
This updates only `KUI/runtime.kui`, checkpoints its predecessor, and
preserves native game modules, VMUs, preferences, and salvage jobs. The same
command with `--restore` rolls back the GUI update.

## Revalidate once

Keep **Power Stone** inserted, then open:

**Disc tools → Rip a disc → START (Advanced) → Damaged disc salvage → Damaged
disc controls → Revalidate saved salvage data**

This is a full saved-data check. Power Stone's saved tracks total about
1.19 GB, and every byte is independently reread. Expect roughly **40–50
minutes** at the measured SD throughput, plus metadata checks. Progress is
shown while it runs. **B** stops safely and does not authorize recovery.

The check admits collected packed v2/v3 salvage jobs and strictly validates
the saved header, ordered journal, target mapping, candidate records, and
track contents. Repaired sectors must match their candidates; unresolved
placeholders must be zeros. Reconstructing the original track baseline must
match the saved journal checksums. Source files are read-only. A disposable
index may be created under `/KUI/probes/` to keep memory bounded.

Only a complete, consistent, uncancelled check can clear this job's matching
quarantine. It does not apply repairs or automatically start disc reads.
This initial check requires the packed candidate container `patches.bin`,
including an empty container for a job with no candidates. Unsupported legacy
jobs, unfinished first passes, ambiguous pending repairs,
torn tails, I/O errors, or mismatches remain blocked without source repair or
truncation. If quarantine removal itself fails, recovery is blocked for the
rest of that boot and marker restoration is attempted; FatFs/card firmware
cannot promise physical failure atomicity.

## After it finishes

Send the automatically saved `diagnostics.txt` under `/KUI/probes/`, whether
the check passes or stops. No photo or G1 test is needed.

If the screen confirms that saved salvage data passed and repair is
unblocked, choose **Retry unresolved sectors** to continue the existing job.
Do not create a new salvage job or delete its files. If revalidation fails,
leave the job intact and send the report before attempting recovery.

This check does not establish a Redump/TOSEC match or claim that any unresolved
disc sectors have been recovered. It validates the saved state used to resume
the existing recovery workflow.
