# Damaged-disc read comparison — @BUILD@

This test tries different ways of approaching the remaining unreadable sectors
on the same Dreamcast drive. It retains the single-block SD fix, saved-progress
revalidation, recovery menus, and ordinary same-session Stop/Continue support.
It does not need another optical drive or a replacement disc image.

This combined build also includes Main at `7cddad936d5d`: the saved Games
catalogue, multi-disc grouping, automatic per-game VMU imports, and compact
72 KiB loader support. The GUI-only installer preserves the tested native
VMU/loader modules already installed by the VMU import-wizard build.

## Install

Stop the current recovery with **B**, wait for READY and its automatic report,
then power off before moving the SD card to the PC. Extract this ZIP and run
from its root:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

Use the actual mount path if different. The installer replaces only
`KUI/runtime.kui` and checkpoints its predecessor. Recovery jobs, settings,
VMUs and native game modules are preserved. Add `--restore` to roll back.

## Run one comparison first

Keep the damaged **Power Stone** disc inserted. Open:

**Disc tools → Recover a damaged disc → Recovery options**

Select **Read speed: Unchanged** and **Firmware retries: 1**. Return to
recovery, open **Diagnostics**, and choose **Recovery read comparison**.
The recovery pass limit does not change this bounded comparison.

Saved journal/target checks run first. This diagnostic deliberately performs
full admission checks and does not use or retain the ordinary Continue RAM
receipt. The ten-minute soft budget starts after those checks, when the trial
begins. A read already in progress and safe publication/cleanup can extend
the wall time beyond the budget. **B** stops through the usual bounded drive
cancellation path and retains committed progress.

Up to **16 unresolved data sectors**, distributed across the remaining target
list, are frozen for the comparison. Each is tried under four approaches:

| Result name | Approach |
| --- | --- |
| Single | Read only the selected sector. |
| Lead-in 4 | Start up to three sectors earlier and read through the target. |
| Centered 16 | Read a window of up to sixteen sectors around the target. |
| Reseek + 16 | Read and verify a distant saved identity anchor, then read up to sixteen sectors ending at the target. |

Windows stay inside the selected track's planned extent and are shortened or
shifted at its edges. All four methods use the same verified drive settings.
Their order rotates between sampled sectors. A sector recovered by one method
is still tested by subsequent methods, so the comparison sample does not shrink.
Only the selected target is eligible for repair; neighboring bytes are used for
the approach, not automatically published as additional repairs. Audio targets
are not included. If no saved data anchor is available, Reseek is skipped.

## Send the report

Let the comparison finish, reach its budget, or stop it safely. Wait for READY
and the automatic diagnostic report. Send the resulting `diagnostics.txt` from
`/KUI/probes/`; a photograph is optional.

The result screen stays visible until dismissed. Each method shows:

- **valid / attempts**: fully checked target results divided by admitted method
  attempts; an unsuccessful approach also counts as an attempt;
- **new**: repairs first committed by that method;
- **I/O**: time spent on its optical approach and target reads, including the
  extra anchor read for Reseek, excluding SD publication time.

`new` depends on which method gets the first successful read. Compare the
valid fraction and I/O time as well. A small trial is evidence about these
sampled locations, not proof that a method will recover every remaining sector.
The report records target addresses, request windows, method order, approach
verification, read errors, validation results, and per-method totals under
`READTRIAL` lines. A ten-minute result may contain an incomplete set of steps;
unattempted reads are not failures.

If that first report is healthy, the same comparison can be rerun with
**Firmware retries: 4**, then **8**. Each run freezes the then-current unresolved
sample; save each report, since changing the remaining set prevents treating
separate runs as a perfectly controlled comparison. The initial test keeps
retry count constant so a read-pattern benefit is easier to identify.

## Recovery guarantees and scope

Every accepted raw data sector must pass address, EDC, reserved-field and P/Q
checks. Returned bytes from a terminal medium error are tentative and undergo
the same checks. A later valid read that disagrees with an existing repair
stops the comparison and retains the saved repair. Journal/candidate publication,
sync and exact saved-sector readback use the existing recovery protocol.
Identity, quarantine, fatal drive and storage failures still stop the operation.
Drive settings are restored on exit through the existing verified cleanup path.

This build adds a bounded experiment. Ordinary Continue keeps its existing
single-sector recovery policy. Software ECC decoding, donor-image import,
alternate DMA/stream commands, and physical speed control are not added here.
The comparison report will determine which read approach merits a full recovery
mode. No additional console recovery success is claimed before hardware testing.
