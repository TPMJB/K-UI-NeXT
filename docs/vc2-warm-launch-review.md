# VC2 warm-launch review

Current source contains a known VC2 warm-launch failure. Game experiments have
stopped for review. **No production warm-entry fix was made after `ca96612c27fc`.**
Publishing this source does not accept the failing sequence or certify a fix.

The confirmed sequence is **Sega Swirl → collection → Shining Force →
collection → Phantasy Star II → collection → VC2**. VC2 goes black during
startup, before its title screen; this is not the subsequent VC2 Return action.
The latest report does not establish a failing instruction, exact logo boundary,
or a fresh-power-on regression. Earlier white-screen reports must not silently
supply those missing facts.

| Revision | Established result and scope |
| --- | --- |
| `e3714617ad28` | Owner reported that the requested VC2-first/title/Return check “actually worked.” No separate after-Swirl or multi-game matrix was supplied. |
| `ca96612c27fc` | Restores the Genesis options menu and Save Game by removing the direct combo shortcut. The multi-game sequence above fails at VC2 startup. |
| After `ca96612c27fc` | Read-only source/artifact audits only; no additional production fix or replacement test package. |

`ca966` leaves both CE controller binaries, raw512 readers, immutable 41,152-byte
helpers and full 49,152-byte prepared transports byte-identical to `e371`.
Native worker reservation remains 73,728 bytes. Its native change preserves the
original combo → options → Save Game/explicit Return flow; the existing checked
reset hook remains. This review does not authorize changing that native flow,
Swirl, workers, integrity checks, updater or unrelated loading behavior.

**What the software checks establish.**

- Both revisions' outer templates have 103,028 payload bytes, 103,424 padded bytes
  and 202 sectors. Their outer return instructions/layout match except build
  labels; the 12,640-byte handoff is exact. The native child shrank from 533 to 531
  sectors, so the GUI-generated collection plans and configured capsule CRC can
  differ. Actual console LBAs, sealed632-byte CE runtime and configured capsule
  CRC were not captured. Template CRCs are not those console values.
- The actual linked replay loads 202 sectors for both revisions, validates
  CRC, carries ROOT/video/receipt and executes the real outer prefix through
  `Cmain` at `8CE00E3C`. Raw-card bytes, coherent RAM/MMIO and bulk RAM-clear
  effect are explicit fixtures. It stops before `Cmain` performs card/profile
  rebind or further loading; it is not a console warm-start replay.
- The original VC2 ResetToFirmware wrapper supplies command 1 and return address
  `01D21902` to E0. Actual `e371`/`ca966` entries produce the same frame for the
  same authored state. This excludes a fixed wrapper/entry mismatch, not live
  CPU-state failures or an earlier startup fault.
- The startup containment gate pins copy/runtime/BSS/previous-E0 slots and
  actual startup write-address/cache traces to accepted 3337 geometry. Expanded
  `cd3f` is a failing control. Containment is not a full-game low-RAM ownership
  proof.

**Original startup already owns these resets.** Closed original SH windows
confirm MAINANA masks all nine ASIC banks, blanks scanout, requests PVR reset 3,
disables its producer list, and writes `FFFFFFFF` to ISTNRM/ISTERR. A separate
120-instruction table replay writes SCIEB/MCIEB = 0 and SCIRE/MCIRE = `7FF`.
The following SoundReset routine's voice clearing, ARM reset, 2 MiB sound-RAM
clear and dummy ARM installation are static source evidence, not physical
device execution. ISTEXT is device-owned; its physical deassertion is unmeasured.

The original CPU table replay also initializes MMU-related registers, QACR,
UBC and TMU0 before its timed GD-reset helper. FPU/frame restoration is static
source evidence. These findings do not support another blind PVR/AICA/FPU reset.
The audits cover closed windows; the entire original initialization and later
driver execution are not one uninterrupted hardware replay.

**What remains open.** No failing PC, live register snapshot, late-game RAM
ownership violation, stale external interrupt or specific VMU/SD failure has
been demonstrated. Multi-game VMU commit/recovery/rebind behavior, physical
DMA/interrupt/cache timing and later CE/game RAM ownership remain unproved.
The restored native postloop calls enqueue sound commands in the bounded trace;
they do not perform immediate MMIO writes there, and a later hardware effect
was not established. Do not turn that context into a diagnosed audio cause.

Compact hashes, source windows, replay results and limits are in
[the review evidence](evidence/vc2-warm-launch-review.json). Start code review at
[the CE child relay](../src/loader/smash_ce_child_stage.inc),
[the terminal controller](../src/loader/smash_ce_return_loader.c),
[the helper-bound idle predicate](../src/loader/smash_ce_return_view.c), and
[the collection epoch import/rebind](../src/loader/smash_card_session_stage.inc).
Reusable checks include the startup contract, full controller, raw-image,
late-installation and original Swirl-source tests under `tests/`.

## Main integration scope

This publication combines the current reader work with Main
`2b4b8c673b7c71a58c41e8a79252313940ea944e`. It preserves Main's engine3
producer/consumer, software ownership restrictions, legacy-fixture gates,
workflow and23 updater-only files. The software catalog retains Main's roles
and appends the Smash roles. Main's pinned standard/CDDA admission defaults
are retained alongside the current Smash build objects.

The GUI integration restores Main's internal VMU-history action117 at its two
dispatch sites; updater action120 stays separate. No game-entry, return,
worker or reader code changes as part of this merge. The frozen engine2
negative fixture now contains its original runtime header/image inputs at
their already-recorded provenance hashes, so current bundle APIs do not alter
the old program under test.

The historical GUI-only source guard and its517-file record remain unchanged.
Its exact recipe is available as `make test-updater-source-preservation`.
`make test-updater` exercises the functional updater tests during this explicit
reader integration; it does not assert that reader sources still match the
historical GUI-only baseline. Publishing this work is a review handoff, not
console acceptance of the unresolved sequence above.
