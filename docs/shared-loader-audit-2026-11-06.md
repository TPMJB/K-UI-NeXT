# Shared loader and VMU audit, 6 November 2026

The owner requested a broader review after repeated VC2 startup and reentry
failures. Review began from source91fa7cc6922b. The delivered91fa ZIP remains
unchanged while these additional fixes are reviewed. Source changes below are
not an installed global rollout or console acceptance.

## Confirmed defects and corrections

| Defect | Affected path | Reproduction and correction | Delivery state |
| --- | --- | --- | --- |
| Temporary VMU ownership becomes a permanent drive error | VC2 high GD wait gate | The actual946 gate and low busy branch return FFFFFFFF for DRIVE; the original WSEGACD branch returns C0000185. The corrected gate waits on the real CE stack, then retries eleven admitted control sites. | Included in91fa; hardware confirmation outstanding |
| A selected bundle can retain another bundle's compiled object | GUI build, `RUNTIME_MODULE_BUNDLE` | Selecting an existing older directory leaves the previous object untouched. Full path and input hashes now participate in its build dependency. | Additional audit source fix;91fa's actual embedded/staged comparison passed |
| Removed PIO callback leaves an undrainable completion flag | Shared WinCE GD core and VMU loan | Complete PIO39, consume CHECK, unregister the callback, then EXEC. The old due flag remains1 and the otherwise idle loan stays BUSY. Unregistration now clears the undelivered callback. | Additional audit source fix; current native/Smash preprocessing is preserved |
| A failed active DMA piece loses its completion wake | Shared WinCE DMA38 stream | Fail storage during an active piece. The old path clears the piece but raises only DRIVE. Failure now also raises DMA_END for the active piece's waiter. | Additional audit source fix; current native/Smash preprocessing is preserved |

The GUI issue was reproduced with the actual production Makefile and actual
existing bundle objects, then with a portable dependency fixture. The portable
test retains the forced build-ID dependencies: On/Off already relinked the GUI
and remains a preservation check, not another claimed defect.

The CE lifecycle regression executes production GD, asynchronous reader,
cursor/image mapping, SCI byte-stream and loan code with ASan/UBSan. The two
old-source controls must fail; a failed request without an active DMA piece
must not invent DMA_END. The current native preprocessing is compared to the
complete old core for both Standard and MMU/PIO/VRAM profiles.

Relevant maintained tests and notes:

- `tests/test_vc2_ce_vmu_gd_control_collision.py` and
  [the VC2 arbitration defect](vc2-gd-loan-control-race.md).
- `tests/test_runtime_module_bundle_make.py` and
  [the GUI build dependency correction](runtime-module-bundle-build-inputs.md).
- `tests/test_ce_gd_completion_lifecycle.py` for the shared CE completion paths.

The normal `make test` command runs both new broad regressions. GUI/updater
checks also run the bundle build-input regression through `make test-updater`.

## Coverage and unresolved items

| Area | Review result | Remaining boundary |
| --- | --- | --- |
| Native and CE storage loans | Native keeps the GD lock and interrupt mask through synchronous work; CE has its own deferred-storage scheduling. | Real preemption, DMA and transport timing need hardware evidence. |
| Save ordering and deferred jobs | Reviewed immutable requests, changed-commit refusal, uncertain-I/O poisoning, redo barriers, readback and release before ACK. Focused existing sanitizer fixtures passed. | Power-loss durability and every SDK's save protocol are not certified. |
| Return ownership | Native and CE guards refuse unfinished protocol work and queued commits before retirement. | Repeated console transitions still require acceptance; no further reset was justified. |
| Current cold reads and cache epochs | Current stage uses bounded16-sector chunks and physical-run reads. Preparation invalidates its FAT/VMU memo state at identity/write/sync boundaries. | This does not attribute the owner's remaining elapsed loading time. |
| Generic CE virtual VRAM writes | A valid small PIO piece can reach a byte-capable copy in generic CE. The current native VC2 route already has its halfword policy. | A separate mapping/width coverage gap; no new generic address policy was installed. |
| Optional session return CRC | The private bitwise payload CRC is slower than the existing byte-table CRC in linked SH instruction counts. | Separate optional legacy ABI, outside current compact/Smash delivery; [bounded evidence](audit-shared-cold-lifecycle.md). |
| Service configurations | Standard VMU On/Off and compact New/Legacy routing reviewed; embedded providers remain unavailable to pinned bank replacements and explicit REMOVE. | VMU Off in the compact reader-only mode retains its full lease. A smaller prefetch-only reservation is not established. |
| CE endpoint capabilities | RESET/KILL remain documented unsupported operations. | Existing capability gap, not a newly discovered regression. |

The15-file VMU lifecycle review and its focused checks are recorded in
[the source-bound evidence](evidence/shared-vmu-lifecycle-audit-91fa.json).
The cold/return review is recorded in
[the bounded lifecycle audit](audit-shared-cold-lifecycle.md).

The regressions model scheduler release, CPU mappings, storage arrivals and
interrupt consumption at explicit boundaries. They establish the named
software defects and corrections, not that every remaining console failure
has been identified. A future package must rebuild affected installed module
families, check their reservations and actual GUI admission, and compare its
embedded modules to the staged inventory before claiming a global rollout.
