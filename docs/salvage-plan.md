# Explicit damaged-disc recovery

**2026-09-23 implementation update:** the independent first-pass/repair worker,
FatFs durable journal, explicit UI actions and PC verifier are now implemented.
See [salvage-worker.md](salvage-worker.md) for the shipped behavior, bounded
limits, host fault tests and pending hardware acceptance. The earlier design
and provenance below remain the rationale; they are not a statement that the
worker is still missing. Normal capture and its accepted reader remain unchanged.

Status: backend port started. The [first helper gate](recovery-port.md) now
contains selectively adapted CRC replacement and Mode 1 address/EDC/PQ checks
with host tests. The Mode 1 checks now power a separate
[Advanced CRC saved-file scan](advanced-crc-scan.md) exposed in the app. It diagnoses
completed saved jobs without optical reads or patching their files. The durable
hole-aware salvage worker, repair UI and hardware acceptance remain pending.
Normal capture continues to stop on exhausted reads; it does not write
placeholders or silently enable recovery. The current UI work
adds destination/name selection, readable reference results and access to the
existing Resume and Verify saved files actions. Those actions are not sector
repair. A saved-file reread checks the stored bytes; stream CRC/reference matching
reports the bytes observed during capture. The UI must keep those claims distinct.

## Original work to preserve

This plan examines `TPMJB/K-UI_DS` at
[`2a5309298dde8fb100da1e2e4e10517695c9780f`](https://github.com/TPMJB/K-UI_DS/tree/2a5309298dde8fb100da1e2e4e10517695c9780f).
The maintainer authorized reuse of independently authored contributions. The
original modules below are candidates for selective adaptation; the mixed
DreamShell ripper, its GUI, drive wrappers and filesystem framework are not an
independent implementation to import wholesale.

| Candidate | Provenance and useful behavior | Adaptation boundary |
| --- | --- | --- |
| [`recovery.c/.h`](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/gd_ripper/modules/recovery.c) | Added in [`102a59f5`](https://github.com/TPMJB/K-UI_DS/commit/102a59f523c684c55c88fc64f3eb1d5bf2263178). Persistent target queue, immutable baseline, old-sector backups, restart reconciliation, bounded passes and per-patch readback. | Replace `fs_*`, `ds.h`, legacy sidecars and worker/progress interfaces with K-UI adapters. |
| [`checksum.c/.h`](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/gd_ripper/modules/checksum.c) | Added in [`77186c20`](https://github.com/TPMJB/K-UI_DS/commit/77186c2043a2ee8a8c33903f3d9f3ed6a3c8e306); `gd_crc_replace` added in `102a59f5`. CRC replacement can update a track checksum using the changed sector CRCs and suffix length. | Extract and test the pure algebra separately from filesystem checkpoints and zlib calls. Existing K-UI hashing remains the normal capture implementation. |
| [`readback.c/.h`](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/gd_ripper/modules/readback.c) | Added in [`2e2b6083`](https://github.com/TPMJB/K-UI_DS/commit/2e2b608355766ef69fe8beb02980ff0cf64773d9). Bounded evidence collection for inconsistent saved-file reads. | Diagnose storage separately from optical recovery; do not replace file contents with whichever read happened to succeed. |

The legacy first-pass policy, integrated into the mixed
[`module.c`](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/gd_ripper/modules/module.c),
tries a failed sector twice, records an unresolved target, writes an explicitly
marked zero placeholder and continues. It prompts before targeted recovery.
That behavior is a requirement to reimplement behind an explicit recovery action,
not a reason to copy the old read loop or add repeated reads to healthy captures.

Targeted passes alternate forward/backward order. Data-sector candidates must
pass Mode 1 address, EDC and ECC checks. Audio has no equivalent sector parity,
so the legacy mode requires two identical reads before accepting a candidate.
The old pass limit is selectable from 1, 5, 10, 20 and 50. Data validation checks
parity; it is not a general ECC correction algorithm. The separately documented
Time Stalkers reconstruction addressed one known corruption pattern and is not
a generic repair feature to expose here.


## Requested legacy controls and their exact behavior

| Control | Behavior to preserve | Independent port boundary |
| --- | --- | --- |
| Recover damaged disc | Raw BIN and full Mode 1 checks; collect good data first. After a failed batch, try each failed sector twice, record a durable target and write a marked placeholder. Prompt before targeted passes. | Separate salvage job/worker; healthy acquisition stays unchanged. |
| Advanced CRC (ECC + repair) | P/Q parity adds to sync/address/EDC. A saved-file scan records suspect addresses; explicit repair accepts validated optical rereads. | Separate scan/repair actions, not generic ECC reconstruction or a green result merely because a catalogue matched. |
| Zero-fill unreadable sectors | OFF by default. The standalone legacy option continued after its configured attempts for recoverable sector errors and recorded holes. FATAL/RESET REQUIRED always stops; it never becomes a placeholder. Recovery mode disabled that independent toggle but still used explicitly unresolved placeholders. | Route any hole-producing operation through the hole-aware salvage format. Do not add zeros to normal capture checkpoints. |
| Retry / recovery pass limit | Choices 1, 5, 10, 20 and 50. Ordinary legacy retries counted per-sector read attempts; targeted recovery uses the limit as whole forward/backward sweeps over unresolved targets. Its first pass retains the separate two-attempt budget. | A bounded salvage preference; do not silently alter the frozen normal capture's ten-retry policy. |
| Track format | Raw 2352-byte BIN is the present format. The legacy 2048-byte ISO option predates our authored recovery work and cannot directly match raw-track catalogues. | Future conversion/output work, after recovery correctness; no placeholder format selector now. |

The older Advanced CRC suspect-repair path invalidated the rolling CRC and
required a full stored-track reread after changes. The newer targeted recovery
path used the immutable baseline and CRC replacement to avoid that reread.
They must not be presented as equivalent operations or promised the same speed.

The old standalone zero-fill mode could produce an extraction-complete marker,
but verification still rejected unresolved holes. The dedicated recovery mode
withheld completion until both unresolved targets and pending finalization
were cleared. The independent port uses the stronger explicit incomplete-job
state for every hole-producing path.

Recovery accounting fixes in
[fa96afbd](https://github.com/TPMJB/K-UI_DS/commit/fa96afbd6286233f7f61992f7f443063acb4a118)
are part of the authored behavior to retain: original targets, recovered and
remaining counts must be reconstructed from saved state, including interrupted
repairs and zero-remaining jobs whose finalization has not committed.

## Separate recovery job contract

1. Create an explicit salvage job with its own versioned metadata and disc/track
   identity. Freeze its destination before starting. Reject ordinary resume,
   export-as-complete and green completion while it contains unresolved targets.
   A placeholder is missing data, even when its file has the expected length.
2. Store full-size raw tracks plus a durable unresolved-sector map. Commit the
   target record before a placeholder can become checkpointed progress. Stop,
   disc removal, storage failure or a changed disc preserves an incomplete job.
   Storage/memory failures are fatal to that attempt, not optical retry events.
3. Before changing any recorded target, commit an immutable baseline identifying
   the original target set, track CRC and original target-sector CRCs. Reject a
   damaged published baseline; never silently rebuild it from possibly patched
   data. Confirm the rest of the track still has trusted baseline evidence.
4. Preserve old bytes before each patch. Validate the optical candidate, write
   and sync it, then read back the patched sector and compare its bytes before
   marking it recovered. Audio needs a durable confirmation record as well.
   Keep the original baseline and backups after completion for audit/recovery.
5. On restart reconcile every original target against the baseline, including a
   torn patch. Rebuild the current track CRC with the CRC replacement operation;
   do not reread healthy sectors just to update that checksum. This shortcut
   depends on unchanged non-target bytes and valid baseline evidence. It is not
   an independent saved-file verification.
6. Expose target count, recovered count, remaining count, current pass/FAD and
   Stop. Offer the existing full reread separately. No known reference, mismatch,
   storage instability and unresolved sectors retain distinct result states.
   Only a fully resolved, durably committed job may enter the normal complete
   state; the UI must still say whether its reference result used stream or
   saved-file hashes.

Use the established single storage/drive worker and current bounded command
adapter. Keep healthy acquisition unchanged. Any later algebra extraction must
record the selected original source and tests; no unused recovery runtime code
is added as part of this UI milestone.

## Focused implementation gates

| Gate | Required checks |
| --- | --- |
| CRC replacement | Compare with a full host CRC after replacements at the first, middle and final sector; no-op, repeated patches, multiple patches and long suffixes. Match the project's CRC initial/final convention. |
| State durability | Inject interruption/failure around queue, baseline, backup, patch, sync, readback and completion writes. Resume preserves unresolved targets; malformed/truncated metadata never creates a complete job. |
| Candidate acceptance | Wrong address, bad EDC/ECC and mismatched audio reads remain unresolved. Two matching audio reads and valid data can progress. Disc change, drive timeout and storage errors remain distinguishable. |
| Patch accounting | Reconcile an interrupted/torn patch without recounting an already recovered target; retained baseline reconstructs the same CRC as an independent full-file hash. Non-target corruption must not be reported as covered by targeted verification. |
| UI/ownership | Stop works during every stage; entering/leaving Advanced does not start writes; later destination edits cannot retarget an active recovery; no ordinary capture or resume accepts placeholder completion. |
| Hardware | First exercise a small reproducible damaged region, retain logs/maps/backups, and independently hash the saved result on PC. Use the same bootstrap disc and an SD runtime update. |

The first implementation gate is now the pure helpers and host tests described
in [recovery-port.md](recovery-port.md). Next are new salvage identity/state
records and interruption tests, an isolated first-pass/targeted recovery worker,
FatFs storage adapters and verifier support, then explicit UI wiring and a
bounded hardware test. A full damaged-disc rip is not the first correctness
test. No ordinary capture, optical adapter or command-state file changes are
part of the helper gate.
