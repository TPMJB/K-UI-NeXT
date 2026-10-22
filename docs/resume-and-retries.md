# Resume and read retries

## Why a resume can run at about 600 KiB/s

Resume normally checks every previously saved byte before continuing the disc
read. During **Checking saved prefix**, the progress rate measures reading from
SD and hashing those saved bytes. It is not the resumed optical acquisition rate.

This cost is already measured: Trip 5a's CRC32/full-prefix check ran at about
**606.8 KiB/s** with a 2 Hz UI. Its size-only check took about 3.9 ms for the
9,633,792-byte benchmark job, excluding disc-identification setup. The large
reported size-only "KiB/s" is a metadata-only calculation, not SD throughput.
See [derived results](evidence/t5a-capture-matrix-2026-09-20.json) and
[raw benchmark log](evidence/t5a-capture-matrix-2026-09-20.txt).

The accepted reader handoff records roughly 677 KiB/s SD reads versus
1,130 KiB/s writes; an SD reread can therefore take longer than the original
overlapped disc capture. **Projection, not another measurement:** reading
Omikron's 1,201,396,896 committed bytes at 600 KiB/s would take about 32.6 minutes.
See the [reader handoff](HANDOFF-disc-reader.md) and
[Omikron failure evidence](evidence/m15-omikron-timeout-2026-09-23.json).

If the screen instead says **Capturing**, distinguish that from prefix checking.
After a DMA timeout the adapter keeps DMA disabled until reboot; further capture
in that boot can use the slower PIO path.

## Explicit Quick Resume

The ripper's Advanced menu offers **Quick Resume** with a confirmation that
existing file contents will not be reread. It selects the existing size-only
resume policy for that operation. Normal Resume retains the configured policy
(default `resume_check=full`); no preferences or `bench.cfg` are rewritten.

1. Keep the partial job and use the same disc and destination.
2. If the previous report says **RESET REQUIRED**, reboot the console first.
3. Choose Quick Resume only when you accept trusting the checkpoint's saved
   CRC and the existing bytes on the card. Otherwise use normal Resume.
4. After completion, **Verify saved files** or the PC verifier can independently
   check the actual stored bytes against the capture record.

Quick Resume still uses disc/checkpoint identity, track layout and file-size
checks. CRC32-only jobs continue their checkpoint CRC without rereading the
committed prefix. Old SHA-256 jobs must still reread it to rebuild hash state;
changing today's hash preference cannot convert that old job.

**It cannot detect same-size corruption in the existing prefix.** Stream CRC or
a catalogue match after a quick resume does not prove those saved bytes were
reread. With end readback off, use the separate Verify action or PC verification
for that claim. There is no tail-check resume mode: existing policies are
`full` and `size`.

## Why Omikron reported zero retries near the end

The report shows a failed new capture at 99.7855%, before verification:

1. The DMA command timed out; the adapter disabled DMA until reboot.
2. Capture attempted the existing PIO fallback.
3. PIO abort recovery failed and reported **RESET REQUIRED**.
4. The partial checkpoint was preserved and acquisition stopped.

The fallback is not counted as an ordinary application retry. Once abort
recovery fails, firmware may still own the command buffer; issuing more reads
cannot be treated as another ordinary bad-sector attempt. The fatal guard stays
in place until reboot. The log alone does not identify whether the original
stall arose from the medium, drive, firmware or another physical cause.

Normal recoverable read errors already have a bounded policy: a failed batch
falls back to single-sector reads, with ten retries (eleven attempts total).
The [scratched Omikron test](evidence/scratched-omikron-retry-2026-09-20.json)
proved that policy on hardware and recorded the bad FAD. It stops without
zero-filling when attempts are exhausted.

The older K-UI/DreamShell target queue, multi-pass recovery, patch backups and
repair accounting are a broader salvage workflow. Those independently authored
features are recorded for selective adaptation in [salvage-plan.md](salvage-plan.md);
they are not yet implemented in this runtime.

## Validation boundary

The Quick Resume addition is in the console adapter and UI. The protected
acquisition engine, optical adapter and command state machine are unchanged.

The adapter tests cover an invocation-local override, unchanged configuration,
subsequent normal actions and failure before I/O on invalid paths or settings.
Existing FAT32/exFAT capture-option tests cover size-only resume correctness,
wrong-size rejection, SHA-256 fallback, same-size corruption and later full
verification. The new menu and confirmation still require console acceptance;
the accepted reader benchmarks need not be repeated.
