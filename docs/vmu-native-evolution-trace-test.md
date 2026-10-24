# Evolution: SDK startup correction

Build `f7f4ac7027e9` introduced an SH-4 slot-illegal instruction before the
first logo. Its new uniform gateway put a PC-relative `MOV.W` immediately
after `JMP`. The CPU raises exception `1A0` before the original SDK initializer
can resume. The replay incorrectly executed that slot as an ordinary load.
Its previous positive results did not validate this CPU rule.

The gateway now loads the constant before the jump. A legal register move in
the slot restores the original register state. The corrected instruction
models and linked-code audit reject the failed release. The cache handoff
remains in place. The older freeze after MPEG Sofdec still needs a console
result; this package does not establish full Evolution compatibility.

## Install and test

Extract into a fresh directory, then run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1
```

The command installs the personalized module directly and preserves the
prepared card pair. Use the same Evolution image and launch settings as
before. Try the opening FMV and menu. If they work, save, power off and reload.

NORMAL adds no game trace patches, live counters, automatic diagnostic stop or
exception-vector redirect. The cold cache handoff runs at SDK initialization;
it does not add a purge to every controller poll.

## Earlier cache failure and retained handoff

The original SDK wrapper normalizes its lock pointer to the uncached P2 alias.
It stores that pointer through the cached P1 cell at `8C1DC538`. The surrounding
32-byte cache line also contains the initially zero lock flag at `8C1DC53C`.

The original SDK then uses `TAS.B` through `AC1DC53C`, setting the RAM flag to
`80`. A TAS through noncacheable memory does not purge the cached alias. In the
failed linked worker, a literal read at `8CFD04B0` fetches `8CFD053C`. That read
has the same cache index, evicts the dirty SDK line and writes its old `00`
flag over the held `80` in RAM. The subsequent witness produces exactly the
reported refusal.

The installer now redirects the existing four-byte initializer call-target
literal to a cold gateway. It validates the original wrapper, initializer
prefix, literal bindings and owned fields before changing that word. The SDK
initializer's code remains intact, including possible interior references.

The gateway runs through P2 after pointer normalization. Before any cached
operand or game-stack access, it verifies masked interrupts, idle DMA and an
unlocked P2 flag. It checks the cached copy and pointer, executes ordered
`OCBP` instructions on the pointer cell and its adjacent flag, verifies
publication to RAM, and resumes the original SDK after replaying its original
twelve-byte initializer prefix. Both addresses are covered because the flag
can begin the next cache line: Sonic Adventure 2 has its pointer at
`8C15CEBC` and flag at `8C15CEC0`. Evolution has both in one line. Uniform and
explicit-48-byte SDK prefixes have separate validated paths.

The cached runtime stack, checked-reader inlining and controller fast path
remain. Their earlier instruction counts are historical overhead measurements;
they did not establish cache correctness or solve the older FMV freeze.

## Evidence and limits

The cache-aware replay executes original SDK normalization and TAS instructions,
the linked stage installer, actual cold gateway and compiled worker. Its 14
cases include both modeled cache index modes (`CCR 105` and `185`), forced
same-index reads after lock acquisition, and two SDK initializations. The
candidate retains the lock and submits its first transfer in those cases.
Replacing the two purge instructions in the second initialization with `NOP`,
with every other instruction unchanged, restores the exact detail-5 refusal. A private P2-stack rollback alone still fails.

A longer cache-aware replay also passes 256 complete SDK transfer pairs across
both index modes. Every poll forces the same-index conflict after lock acquisition;
the held lock survives, TX stays intact, and completion publishes only the expected
controller presence byte after enumeration. Assembly frame saves are modeled as
cached CPU stores; their save/restore instructions are bridged.

The cold gateways pass 412 independent instruction cases and reject 194
instruction/literal mutations. Preparation passes 236 metadata checks; original
prefix replay covers 40 cases across five game startups, with eight additional
Power Stone 2 register-liveness cases. Installer tests refuse malformed inputs
before patching. These are software checks, not console compatibility results.

The cache model starts from an explicit empty operand cache and executes a
reachable dirty-line history. It omits IRQ-registration scaffolding, scheduled
interrupts, bus and write-buffer timing, the game main loop and FMV playback.
It does not reconstruct every cache access before the console failure. The
older post-Sofdec issue may have a separate cause.

Reproduction commands, failed-release and development-artifact hashes, actual
flag transitions and limits are recorded in
`docs/evidence/native-sdk-lock-cache-2026-10-24.json`. The package's compiled
proof identifies its final release. Historical polling measurements remain in
`docs/evidence/native-linked-worker-cost-2026-10-24.json`. No original game
executable is included.

## Existing optional capture

The same package retains PROFILE mode if the game passes startup and the
older black-screen failure remains:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1 --profile-runtime
```

For the exact Evolution revision, selected CPU exceptions through the witnessed
general vector report **EXPEVT, SPC, SSR, TEA** on a terminal page. Photograph
that page if it appears. The addition preserves VBR and the SDK dispatch table;
other exceptions and changed recovery callbacks follow the original handler.
An unsupported witness disables this optional capture without refusing VMU
admission.

If polling continues during a black screen, holding **B and both triggers fully
down for two seconds**, then releasing, starts the existing capture after 30
pairs. It cycles timing and phase pages after about 20 seconds or 1,200 pairs.
There are no automatic blank-screen deadlines or live VRAM markers. Sampling
adds work. An absent report cannot identify a bus hang, blocked exception,
replaced vector or software deadlock.

Other modes remain exclusive: `--control`, `--lease-only`, `--hooks-only`,
`--raw-chain`, `--evolution-trace`. RAW retains full validation.
