# Capture speed and resume: next tests

The optical profiling round is complete. Runtime **92484724c538** still captured
MDK2 audio at **57.00 KiB/s**. This revision implements a performance change;
follow [the 60-90 second audio test](optical-test.md) using **sd-update** and the
existing bootstrap CD. Keep all partial jobs and the completed Sword dump.

## Evidence and implementation

| Measured fact | Implemented response |
| --- | --- |
| Two PIO commands per capture block; second reads took 138.97 of 305.61 seconds | One sequential PIO command per capture block |
| 257.99 seconds inside scheduler waits; nominal 1 ms sleeps averaged about 8 ms | Service PIO continuously, with runnable scheduler yields every 2 ms |
| Mode changes took only 0.0053 seconds | Retain mode setup; it is not the significant delay |
| SHA-256/CRC32 took 4.26% of this audio capture | Retain both hashes and full saved-file readback |
| SD write calls averaged 794.63 KiB/s; no retries or memory growth | Keep storage path and chunk size stable for this test |
| The 72.08 MiB prefix check took 212.45 seconds | Keep existing jobs compatible; fast resume remains a separate versioned change |

[Exact counters and caveats](evidence/mdk2-optical-waits-2026-09-17.json) are
recorded with the upload fingerprint. Wait time includes real drive waiting and
other thread execution; not all of it is recoverable overhead. Subtracting only
the second read with all other costs fixed gives 104.53 KiB/s, so avoiding the
per-service-call sleep is essential to address the larger gap. This arithmetic
is not a hardware speed prediction.

## Independent source comparison

The legacy project is now named K-UI_DS. Its
[ripper at 2a530929](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/gd_ripper/modules/module.c)
uses one 16-sector PIO command and prepares mode per track. Its timeout wrapper
uses the KOS polling mechanism. Our
[pinned KOS optical wrapper](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/cdrom.c)
also uses scheduler polling, which allows prompt firmware service rather than
sleeping after every busy status. In the
[scheduler](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/thread/thread.c),
`thd_sleep(1)` blocks the worker until a timed wakeup; `thd_pass()` keeps it runnable.

K-UI's change is independently authored around these general techniques. It
retains its own bounded command/abort loop, 32-sector chunks, static guarded
buffers, sector EDC and retry handling. No DreamShell implementation is imported.
The
[KOS status definition](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/syscalls.h)
defines `size` as transferred bytes; successful short/overreported raw transfers
are rejected before forwarding bytes. Single-read CDDA relies on the firmware's
status and count; it does not perform repeat comparison or offset correction.
Paired identification samples keep the existing fingerprint and v1 job identity.

## Acceptance order

1. Resume the existing MDK2 job, allow the prefix check, capture audio for 60-90
   seconds, then Stop and upload the automatically saved report. Expect capture
   `policy=single`, `read2 calls=0`, no transfer-size errors, responsive controls,
   and lower optical wait time. Compare phase/per-track averages with 57 KiB/s.
2. After that short result, test a short data-track interval to confirm EDC and
   speed. Different ranges and drive conditions limit the comparison; use an
   identical fixed range only if the result is ambiguous. Do not claim a fixed
   640 KiB/s target from another program's peak display.
3. Once speed and Stop are sound, finish a mixed-track capture with final SD
   readback and PC verification. A faster capture is not yet a verified dump.

Automatic reports, phase/track/optical timers and fixed-memory accounting stay
in place. Reports run after capture I/O and remain outside its timing buckets.
Host tests cover actual-adapter fast service and fairness, single capture vs
paired identification, transfer-count rejection, cancellation, deadlines,
failed-abort/guard poisoning, and non-PIO scheduling. Filesystem tests retain
CRC/SHA equality, Stop/Resume, corruption/failure rejection and report preservation.
Physical speed and firmware byte-count compatibility require the console test.

## Plan faster resume separately

Ordinary fast resume should restore a validated checkpoint and incremental hash
state rather than reread every committed byte. This is a proposed change, not
a control available in the current build. Keep an explicit full-prefix-check
resume path and final full saved-file verification. Fast resume would trust
the recorded prefix until a full readback checks it; it must not label those
old bytes newly verified merely because a checkpoint checksum passes.

Before the next long-rip acceptance run:

- Define a versioned portable checkpoint representation for the partial track's
  incremental SHA-256 state, CRC32 state, byte count and buffered bytes. Preserve
  completed tracks' recorded digests without rehashing them at every checkpoint.
  Validate state invariants, identity, lengths, record checksum and sequence.
- Keep both alternating checkpoint records and conservative publication order.
  Existing v1 jobs need the current verified prefix reread to reconstruct hash
  state once; do not promise instant first resume of an old-format job. Preserve
  them if conversion or writing the new state fails.
- On FAT32/exFAT host images, compare resumed and uninterrupted output across
  hash-buffer and track boundaries. Cover torn/invalid newest records, wrong
  discs, short files, uncommitted tails, cancel and write/sync failures. Test the
  distinction between fast resume and full-prefix-check resume: changed old
  bytes must fail a later full verification and never produce a verified
  manifest, while the strict path rejects the corrupt prefix before appending.
- Then run one short controlled Stop, reboot and resume on the console. Confirm
  the selected job/offset, final byte/hash equality and time-to-first-new-byte.
  Test larger prefixes in host fixtures before asking for another lengthy
  console capture. Arbitrary power-loss recovery is not established by a
  controlled Stop/reboot test.

## Close the correctness checks with minimal repeat work

Run the bundled PC verifier on the **existing completed Sword of the Berserk
directory**, including track 3, and retain its output:

```sh
python3 verify_dump.py "/path/to/completed-sword-of-the-berserk-dump"
```

This requires no new Dreamcast capture. Track 3's recorded CRC32 is `2cfb5dcb`;
the verifier must independently calculate the full lengths, CRC32, SHA-256 and
GDI layout rather than merely inspect the manifest. An independent compatible
reference comparison is still a separate acceptance check.

After short performance and resume tests pass, perform one complete mixed-track
hardware acceptance run using the improved runtime, with a controlled stop and
resume, final SD readback and PC verification. Keep the existing completed dump
as a baseline. No new CDI is planned for logging, profiling, optical-loop or
checkpoint changes that stay within the existing SD runtime package format.
