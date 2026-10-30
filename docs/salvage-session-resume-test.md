# Reuse checked recovery progress within one session — @BUILD@

The previous build correctly stopped and resumed recovery, but every Continue
rebuilt the journal-derived index and checked all durable targets again. On
Power Stone that added about two minutes even immediately after a safe Stop.

This update can retain a compact validation receipt in RAM for a collected
packed recovery job. A later Continue in the same boot can reuse the checked
state after lightweight identity and checkpoint checks. It retains the SD
CMD17 fix, full saved-data revalidation, and reorganized recovery menus.

## Install after the current operation finishes

Wait for READY and the automatic report, power the console off, and mount the
SD card on the PC. Extract the ZIP and run from its root:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

Use the actual mount path if different. Only `KUI/runtime.kui` is replaced;
the predecessor is checkpointed. Jobs, native game modules, VMUs, and saved
settings are preserved. Use the same command with `--restore` to roll back.

## Focused console check

Keep **Power Stone** inserted. Open **Disc tools → Recover a damaged disc**.
Under **Recovery options**, leave **Read speed: Unchanged**, select
**Firmware retries: 1**, and set **Recovery pass limit: 1**.

1. Choose **Continue recovery**. The first Continue after this installation
   still performs the full journal and durable-target checks. The RAM receipt
   cannot survive the reboot needed to install this build.
2. Once actual disc retries begin, press **B** and wait for READY and the
   automatic report. A clean, eligible stop retains its checked state.
3. Choose **Continue recovery** again without restarting, changing the disc
   or card, or running another app/diagnostic. This invocation should skip
   the full journal rebuild and durable-target sweep when its receipt checks
   pass. Short identity and checkpoint checks still run.
4. Let the bounded pass finish or stop safely, then send the automatic
   `diagnostics.txt` under `/KUI/probes/`.

The report records whether session state was reused or normal checking was
required. Recovering no additional sectors does not make this performance
and Stop/resume test a failure.

## Validation retained

The receipt is separate from the dashboard display cache. It is never saved
as persistent repair authority. The fast path checks the disc/job identity,
SCI card CID, volume binding, source metadata, journal tail, quarantine, and
the complete SHA-256 of the small derived target index. Individual repair
validation, candidate publication, sync, and exact saved-sector readback
remain in place.

Reboots, other source-changing operations, media changes, storage errors,
uncertain cleanup, a changed checkpoint, unfinished collection, legacy jobs,
an oversized derived index, or an interrupted candidate publication prevent
reuse. A clean checkpoint
mismatch returns to the normal checking path; actual I/O failures stop the
operation. The initial optimization supports the SCI card identity path;
other transports retain normal validation.
Keep the card in the console while it is powered on. Power off before using
the card on a PC; this also discards the RAM receipt.

While a reusable receipt is retained, background card-cache/preload work is
paused. Already loaded music continues. Opening another app invalidates the
receipt and resumes normal background work. Opening recovery menus and
changing pass/retry settings do not themselves invalidate it.

The full revalidation operation is unchanged. It does not export its temporary
index as a warm-recovery receipt, so the first ordinary Continue still builds
and validates the repair engine's own state.
