# Drive speed support probe — @BUILD@

This GUI test adds **Run speed support probe** alongside the existing bounded
damaged-sector trial. It retains the combined VMU/menu changes and the aligned
saved-candidate reader from `370191a7f0b1`.

## Install and run

Finish or stop any current recovery, wait for its diagnostic report to save,
then power off before changing the card. Extract the ZIP into a new folder.
`install.py` and `README-FIRST.md` are directly inside it. Open a terminal in
that folder and run:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

Use the card's actual mount path if different. This replaces only
`KUI/runtime.kui`, checkpoints its exact predecessor, and retains the installed
personalized VMU-toggle-capable native modules, virtual cards, assignments,
preferences, and salvage jobs. Rollback uses the same command with `--restore`.

Boot with a retail GD-ROM inserted. Your current Power Stone disc is suitable
if the probe finds three good data windows. Open:

**Disc tools → Rip a disc → START (Advanced) → Damaged disc salvage → Damaged
disc controls → Run speed support probe**

This is the new menu entry below **Run bounded control trial**. It uses fixed
settings; manual Read speed, Drive retries, and Recovery pass limit choices do
not affect it. It needs no saved salvage job and does not open, repair, or
change any recovery job. Only the usual diagnostic report is saved to SD.

When it finishes, send the newest `diagnostics.txt` under `/KUI/probes/`.
The result photo is optional. If stopped or failed, send that report too.

## What is tested

The probe snapshots the original firmware mode, then keeps firmware retries at
one while testing these selections:

| Selection | Requested speed code |
| --- | --- |
| Original speed | Entry snapshot |
| Maximum | 0 |
| 1x | 1 |
| 2x | 2 |
| 4x | 3 |
| 6x | 4 |
| 8x | 5 |
| 10x | 6 |
| 12x | 7 |

All nonzero speed codes are optional drive features. The complete speed,
standby timer, flags, and retry tuple must verify before optical reads under
that setting. A rejected or unverified setting is skipped only after verified
rollback. Original settings are restored afterward, including after Stop or
failure whenever ownership is safe. A failed restoration requires a restart.

Before comparisons, up to six spaced locations are tried to establish three
good 64-sector data windows. Each read contains at most 32 sectors, and every
sector must pass sync, expected FAD, EDC, reserved-byte, and P/Q checks. A
SHA-256 reference is retained for each complete window. Each admitted speed
then rereads all three windows, rotating their order. A fully checked but
different window stops the probe. Ordinary unreadable data under an optional
setting is reported as an unmatched window. An unstable original-speed
baseline stops the comparison. If three good windows cannot be found, use a
healthier retail GD-ROM for the speed-support check.

The five-minute soft budget covers reference acquisition, setting changes,
and comparison reads, beginning after disc preparation. The active bounded
command finishes before stopping, so wall time can exceed the budget. B stops
safely. No automatic drive INIT is performed during the comparison.

## Reading the report

Each attempted setting records the requested and returned mode tuple before
rollback can overwrite it. `diff_mask` identifies a changed field:

| Bit | Field |
| --- | --- |
| 1 | Speed |
| 2 | Standby timer |
| 4 | Read flags |
| 8 | Firmware retries |

A set/query failure says that no valid returned tuple was captured. Matching
the full tuple admits a setting; the result screen shows matched windows and
read time for that setting. Skipped settings are labelled **settings not
verified**. Those outcomes do not establish physical spindle behavior.

Read times exclude mode-selection calls, which are logged separately. The
regions exceed the documented 128 KiB drive buffer in aggregate, but this does
not prove optical-cache independence or measure spindle RPM. Successful mode
readback plus matching data is evidence of usable requested settings; a
timing difference alone is not proof of an exact physical 1x/2x/etc. speed.

This is a hardware diagnostic candidate, not a confirmed expansion of drive
capabilities. The existing damaged-sector trial and ordinary one-retry salvage
remain separate operations.
