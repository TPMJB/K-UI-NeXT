# Bounded damaged-disc control trial

This test build combines the VMU/menu work through `9e76a9722153` with an
explicit, bounded comparison of drive settings. It keeps the installed native
game modules, virtual cards and saved salvage jobs. It does not make automatic
multi-speed repair the ordinary recovery policy.

## Install and start

Finish or stop the current operation with B, wait for its report to save, then
power off before changing the card. Extract the complete test ZIP, enter its
folder and run:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

The installer saves the exact previous GUI for rollback and checks the installed
personalized, VMU-toggle-capable native module. It replaces only `KUI/runtime.kui`.
Existing modules, cards, journals, assignments, preferences and salvage files
are retained. Use the card's actual mount path if different. Rollback uses the
same installer with `--restore`.

Boot with the same disc, then open **Disc tools → Rip a disc → START (Advanced) →
Damaged disc salvage → Damaged disc controls → Run bounded control trial**.
Use the displayed menu routes if the ripper was opened directly at startup.
The trial finds the latest matching salvage job; collection must already be
complete. It selects data sectors only, which fits the current Power Stone
recovery queue. It does not start a new full-disc scan.

Manual speed, drive retry and recovery-pass selections do not change this trial.
It uses the following fixed profiles:

| Profile | Speed request | Firmware retries per command |
| --- | --- | --- |
| Original | Entry snapshot | Entry snapshot |
| Original / 1 retry | Entry snapshot | 1 |
| 4x / 1 retry | 4x requested | 1 |
| 2x / 1 retry | 2x requested | 1 |

## What runs

After normal disc identity and saved-job checks, the trial:

1. Freezes up to eight unresolved data sectors, spread across the remaining
   queue. A repaired sector stays in this fixed comparison sample.
2. Selects each profile and checks the complete firmware mode tuple. Unsupported
   settings are skipped only after verified rollback to the previous tuple.
3. Qualifies each accepted profile against both exact saved identity anchors.
   Ordinary unreadable anchors leave that profile unqualified; different fully
   checked identity bytes or a fatal drive result stop the trial.
4. Reads every sampled sector once under each qualified profile, rotating over
   the admitted profiles. Starting positions are balanced across the sample.
   At most 32 damaged-sector comparison reads are scheduled, plus qualification.
5. Saves every new sector that passes the existing FAD/EDC/P/Q checks using the
   existing durable patch protocol. Later comparisons never overwrite that
   repair. A conflicting fully checked reread stops and preserves the first
   saved repair.

The five-minute soft budget starts after saved-job admission and includes sample
selection, profile changes, qualification and comparison. It finishes the active
bounded command before stopping, so wall time can exceed five minutes. Initial
disc preparation, job discovery, saved-data checks, final restoration and report
saving are outside that budget. B stops safely and retains committed repairs.

Automatic recovery INIT after eight ordinary failures is suppressed only inside
the bounded comparison, so resets do not silently confound the profiles. The
ordinary recovery path retains its existing recovery policy. A failed abort,
guard corruption or media change remains fatal; no trial callback bypasses
firmware ownership rules. Restart if the drive requires it.

## Read the result

The result screen remains visible after the trial, with one line per profile:
**valid / reads**, **new** saved repairs and optical **read** time. `new` gives
the first successful profile credit for saving a sector; `valid / reads` includes
later agreeing reads of already saved samples and is the fairer yield comparison.
The progress bar counts scheduled steps; a setting rejected during comparison
can consume a skipped step without an optical read.

The report records selected/returned mode values, each FAD, transport result,
validation flags, read time, mode-change time, qualification and per-profile
totals. Return the diagnostic report saved after the trial, along with the
profile result screen or its numbers. **A** returns to drive controls;
**B** returns to Advanced. Ordinary **Retry unresolved sectors** remains available.

Firmware readback and successful identity checks establish that the requested
settings were retained through format setup and produced expected bytes. They
do not measure physical spindle speed or prove optical-cache independence.
The two identity-anchor reads are integrity qualification, not a sustained
throughput benchmark. This trial is intended to establish which profiles, if
any, improve this disc's validated read yield and latency before a larger policy
is adopted. No reconstructed or guessed payload is accepted.
