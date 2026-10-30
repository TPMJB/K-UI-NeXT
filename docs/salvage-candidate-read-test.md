# Saved-candidate read diagnostic — @BUILD@

This GUI-only test retains the combined VMU menus, manual drive controls and
bounded four-profile trial from `41da46b52dc0`. It targets the saved-data check
that reported an invalid published candidate before that trial could begin.

The supplied job's header, journal and all 6,187 published repair records pass
independent checks, including record/sector CRCs, journal binding, expected FAD,
EDC and P/Q parity. Target 5160 is candidate slot 4700 at FAD 305895. This does
not establish that every saved track byte is intact, or identify the exact
console read/lookup/memory fault. No repair evidence is discarded or replaced.

The candidate I/O buffer is now explicitly aligned within its owned allocation
regardless of heap placement. This avoids the unaligned SCI PIO fallback for
ordinary full-record reads; it is a diagnostic change, not a confirmed cause.
On a candidate validation failure the report identifies the precise field,
expected/returned coordinates, job identity and record/payload CRCs. A single
fresh read-only reopen reads that record again in aligned 512-byte pieces.
The failed invocation remains stopped even if the fresh record validates.
Differing saved reads publish the existing job quarantine and block further
salvage. This build has no complete consistency scan that can clear quarantine
for an unfinished salvage job. The ordinary Advanced CRC scanner does not
validate its packed candidate and journal evidence. Preserve the entire job,
including the quarantine marker, and its diagnostic report for analysis.
I/O/close errors and Stop remain terminal. This reread never authorizes a repair
or overwrites a published record. Original candidate validation is unchanged.

## Install

After the current operation ends and its report saves, power off and mount the
card on the PC. Extract the complete ZIP and run inside its directory:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

Use the actual mount path when different. The installer replaces only
`KUI/runtime.kui`, saves its exact predecessor for rollback, and preserves the
installed personalized VMU-toggle-capable native modules, virtual cards,
assignments and saved salvage jobs. Restore uses the same command with
`--restore`.

## Console test

Boot with the same Power Stone disc. Open **Disc tools → Rip a disc → START
(Advanced) → Damaged disc salvage → Damaged disc controls → Run bounded
control trial**. Use the existing job; no new collection scan is required.

If the saved-data check passes, let the bounded trial finish and send the
profile result screen plus its saved diagnostic report. If it stops again,
save and send that diagnostic report. The additional candidate lines should
distinguish a consistently wrong read from an inconsistent or fresh valid
read. Retain the original job and quarantine, if one is reported, for analysis.

This is a console diagnostic candidate. The underlying console failure has
not yet been reproduced or confirmed fixed on hardware.
