# DOA2 startup GD routing correction, 2026-09-24

## Hardware evidence

The user's photograph of build `255e63f79d8d` shows:

| Field | Value |
| --- | --- |
| Stop | GAME REQUESTED BIOS MENU RETURN |
| Menu command | 00000001 |
| Caller PR | 8C012450 |
| Caller stack | 8C00F5D8 |
| Last GD function | 00000000 |
| Last GD command | 00000000 |
| Last GD LBA | 00000000 |
| Resident SD blocks read | 00000000 |

The caller address lies inside executable RAM. The menu-return trap worked.
Zero resident reads does not describe the temporary stage's earlier executable
load. No new loading time was reported. Gameplay still failed.

These counters did not include supervisor/miscellaneous calls. In particular,
the previous resident rejected a BC miscellaneous set call with R4=0 before
recording GD diagnostics. Therefore the photograph does not prove that DOA2
bypassed the hook, nor does it identify the exact failing startup call.

## Source comparison and correction

The previously authorized DreamShell comparison was narrowed to
`kui-1.0.1/firmware/isoldr/loader/gdc_syscall.s` and the firmware-entry patch
in `syscalls.c`. SHA-256 of the former:
`d4c469d3a2cdf3efeec8650e18a39de806691d00c146c88b83279681a2d94b49`.
The latter's hash is in the bootstrap-2 correction record.

DreamShell's BC wrapper acknowledges R6=-1 setup/registration calls with zero;
otherwise it clears R6 before GD dispatch. Its C0 wrapper dispatches by R7
without interpreting incoming R6. Both direct firmware entries, RAM+0x1000
and RAM+0x10f0, redirect to its BC wrapper.

The independent resident now implements those entry conventions:

- BC supervisor vector `0x8c0000bc`: acknowledge miscellaneous setup locally,
  preserving the image-backed service; normalize R6 for other GD calls.
- C0 raw GD vector `0x8c0000c0`: dispatch GD regardless of incoming R6.
- Direct RAM entries `0x8c001000` and `0x8c0010f0`: aligned 12-byte tail jumps
  to separate counted wrappers with the BC convention.
- Remove original GD forwarding, which would recurse through the patched
  entries. Independent font/flash/system BIOS vectors are unchanged.

The small direct-entry stubs are installed only during resident initialization.
The existing P2 bootstrap handoff purges dirty RAM and invalidates instruction
cache before their first use. General registers R4–R7 reach the dispatcher
unchanged so diagnostics capture the original call. Wrapper source is published
only after acquiring the existing resident lock.

If the game still returns to BIOS, the screen now shows separate BC, C0 and
direct-entry counts, miscellaneous setup count, last R6/R7 and result, plus
last GD command/LBA and resident SD reads. This distinguishes intercepted
setup failures from unhandled entry paths. The screen fits sixteen rows.

## Validation scope

Only native resident routing and documentation changed. The broad portable
and filesystem suites are not repeated. Validation is the console compilation,
linked instruction/layout/stack checks and packaging, plus focused source
review of the entry ABI and cache-publication order. The accepted optical
capture, drive and command source hashes remain unchanged. The next hardware
launch determines whether this resolves DOA2's startup failure.
