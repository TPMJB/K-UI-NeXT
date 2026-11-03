<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Games scan reset fix

The owner reported a hard reset while running **Games → START → Scan for
changes** in test runtime `1feeb7611bcd`.

## Cause and change

The optimized SH-4 compiler's stack records show a reachable scan path using
78,732 bytes before filesystem/library callbacks. The `kui-io` worker has a
65,536-byte stack. The newly added captured-disc metadata path alone uses
72,588 bytes before callbacks. Both exceed the allocated stack. These are
compiler-derived call-path sums, not a captured exception trace from the
owner's console.

The image parser previously kept complete 99-track image structures in nested
stack frames. Public parsing functions now use a checked, per-call heap
workspace and publish the output only on success. Named GDI parsing reuses
that workspace. The catalogue's eight-row page also moves to the heap and is
freed before disc-header grouping. Allocation failure returns an error and
keeps the previous complete catalogue. The worker stack size remains 64 KiB.

## Console check

Copy only the replacement `KUI/runtime.kui` over the existing file with the
console powered off. Start K-UI, open Games and run **Scan for changes** once.
Check that the scan completes and captured Shenmue discs group by their actual
IP disc numbers. Normal library browsing does not run the new diagnostic
writes.

If another reset occurs, return to K-UI and create a support report from
**Diagnostics → START → A** before starting another scan. The report includes
the last saved scan phase and selected image path; startup also replays that
record into Diagnostics. A scan log is diagnostic evidence, not a guarantee
that the storage device has committed its internal cache after a power loss.

## Validation

The target stack check uses production SH-4 compile flags and compiler-emitted
stack/callgraph records. It models the scan/list/image paths and reserves
additional space for unmodeled filesystem, libc and KOS descendants. It does
not certify every worker action or replace physical-console acceptance.

The repaired explicit library scan models 33,648 bytes. With the additional
12,288-byte reserve, it leaves 19,600 bytes of the unchanged 64 KiB stack.
The same check also covers cold listing, title search/jump, image verification
and the cover-scan visitor's bounded recursion. Pre-fix compiler artifacts are
rejected by the budget check. Run it with the configured Dreamcast SDK:

```sh
make -f Makefile.dc check-games-scan-stack
```

Checkpoints alternate between `/KUI/games-library/scan-a.txt` and `scan-b.txt`.
Each bounded record has a sequence and CRC and is synced, closed and reread
before publication. An interrupted update leaves the last valid slot usable.
Support reports contain the recovered plain text as `games-scan.txt`.

Host validation covers parser failures, catalogue persistence and grouping,
scan cancellation/faults, saved scan records and support-report export using
the actual FatFs implementation. The test ZIP includes the target stack
report and validation logs.
