# Universal loader integration roadmap

Goal: one maintained K-UI build which selects the best verified launch backend,
virtual VMU and optional read-ahead for each runtime. Keep successful games
working while extending shared support. A runtime may use demand delivery
without a speculative cache. Physical VMUs remain available where virtual
VMU is not yet supported.

This work starts at Main `d393b859a9ee23d372daca54ef13c7839072097a` on
2026-11-06, after acceptance of Smash Pack source `cc81e63ca4d2`.
The initial implementation branch is `codex/universal-loader-integration`.
The milestones below are ordered dependencies, not elapsed-time estimates.

The canonical repository is `TacitusVox/K-UI-NeXT`. The account name is
TacitusVox; older TacitusVoxt and TPMJB links are migration work, not alternate
current publishing destinations.

## 1. Shared correctness and reproducible deployment — active

Shared GUI preparation is also an immediate integration requirement. The
owner's Power Stone 2 pre-bootstrap report is tracked in
`shared-gui-launch-preparation.md`. Audit the actual GUI path before attributing
delay to cold-stage work; apply fixes to common operations across all routes.

Apply the demonstrated lessons across the actual affected reader families:

- Reject lost-byte-marked SCI receptions before payload delivery; preserve
  full block CRC, bounded same-LBA retries and existing PIO fallback.
- Publish the shared CE callback-cancellation and active-DMA-error completion
  corrections already present in Main source into qualified rebuilt modules.
- Close generic CE byte-write coverage for valid CPU mappings backed by VRAM,
  including synchronous and asynchronous delivery, partial callbacks and odd
  boundaries. Preserve mappings, permissions, completion order and main-RAM
  copy performance; do not broaden address admission merely to fix copying.
- Audit every storage-loan control operation and completion owner. The exact
  VC2 scheduler gate is source-specific; it is not a universal callable thunk.
- Build ordinary, standalone, compressed, native CDDA and WinCE CDDA cold stages
  through a single recorded build procedure. Inventory Standard, compact
  Background, physical disc and collection components separately so omission
  cannot be described as a global rollout.
- Qualify staged output before invoking the current Main KUP producer. Refuse
  stale extra files, user data, mixed components and unqualified substitutions.

Completion evidence: meaningful old-source failing controls, production-code
and linked checks, clean-source compiler receipts, complete installed-file
hashes, actual GUI admission and current C-consumer validation. Record source,
native-build, package and hardware results separately. An unbuilt or failed
family remains explicitly pending.

Accepted Smash modules/helpers are preserved as an independent component set.
Do not rebuild them to obtain a matching build label or retune VC2. Existing
worker memory contracts, save menus, return behavior and updater revision 3
remain preservation boundaries.

The first integration provides these repeatable entry points:

```sh
python3 tools/build_universal_loader.py plan
python3 tools/native_service_inventory.py
make test-universal-loader
source .deps/kos/environ.sh
python3 tools/build_universal_loader.py build --jobs 4
```

The build command requires committed source and records each phase. It creates
the 12-role cold-reader subset and a generated native CDDA admission manifest,
not an installable update. The current GUI must be linked with that manifest
and the independently preserved Standard/Smash bindings before qualification:

```sh
python3 tools/build_universal_loader.py check \
  --runtime-elf build/kui-runtime.elf --output build/universal-cold-rollout
```

The output directory must be new or empty. A successful subset check does not
qualify the separately listed Standard/Background/disc/collection components
or their final composition. The current producer is used only after those
remaining delivery gates pass.

The first test composition must inherit `apps/games/ce-probe.kui`: an existing
personalized Armada writable-VMU adapter uses that same path. Replacing it with
the ordinary CE build would remove a working service. The ordinary build is
still compiled and qualified, but its installation stays deferred until the
VMU-aware route is separated and admitted. Do not publish the personalized
adapter or its prepared card identity to recover this capability. Report the
deliberate omission as partial deployment rather than global completion.

## 2. Independent services and memory

Establish actual routing and retained memory for all four configurations:

| Requested configuration | Required retained objects |
| --- | --- |
| Neither | Ordinary reader; no optional VMU/cache lease |
| VMU only | VMU code/state/stacks and its storage adapter; no speculative cache |
| Reader/read-ahead only | Reader/cache and required stacks; no virtual-card authority |
| Combined | Both services under a checked memory and storage ownership contract |

The first pass inventories actual Make configuration and production geometry.
The compact Background VMU-Off route currently retains 284 KiB including a
192 KiB cache; do not report it as a smaller prefetch-only design. Standard's
72 KiB VMU service remains distinct. Changing reservation size requires new
linker bounds, relocation/stack checks, allocator and complete lifetime proofs.

Completion evidence: actual route selection and rejection behavior, all
retained owners accounted for, unchanged physical VMU behavior, and save/reload
checks on the changed configurations. A disabled setting alone proves none
of these properties.

## 3. Extend shared native runtime coverage

| Runtime | Existing working route | Shared-service work |
| --- | --- | --- |
| DOA2 | Standalone demand reader | Prove memory ownership before adding virtual VMU or cache; earlier apparent free arenas were contradicted or incomplete |
| Sonic Adventure | Ordinary/standalone New | Executable-loader and exit lifetime |
| Silver | Exact Standard VMU adapter | Executable reclamation, exit consumers and lock behavior |
| Crazy Taxi | Standalone New | Reviewed retained arena |
| Zombie Revenge | Standalone New | Six-input Maple layout and allocator/reservation |
| Blue Stinger | Shared early-family admission incomplete | Earlier interrupt-table initializer and associated ownership |
| House of the Dead 2 | Virtual VMU saving and reload | Residual lag; bounded scheduling work without disturbing persistence |
| PSO / Spawn | Booting; recorded intro problems | Resolve delivery/scheduling evidence; Spawn audio distortion remains an observation |

Keep successful image results for Time Stalkers and EGG. Preserve exact
Standard adapters until their shared replacement qualifies. Admit structural
SDK families through source-bound contracts rather than game-title guesses.
No percentage of universal game compatibility is implied by the current corpus.

Completion evidence per changed family: launch, loading-heavy gameplay, virtual
card detection, game-requested save, power-cycle reload and appropriate exit.
Use one representative runtime to establish a shared mechanism, then record
the remaining coverage honestly.

## 4. Coordinated FMV and CDDA streaming

Investigate a safely reserved sequential streaming ring, with optional full
movie preload when the asset fits. Reuse the shared stream and verification
primitives; native Toy/MDK2 and CE still require their own scheduling and
completion adapters. Preserve working audio playback and save durability.

Measure request sizes, sequentiality, buffer occupancy, underruns, command
count, completion cadence, audio-refill deadlines and masked execution windows.
Retained bad Toy/Armada intros did not always have an active CDDA worker, so
CDDA traffic alone is not an established cause. A finite buffer can absorb
stalls but cannot repair sustained undersupply.

Keep demand traffic and audio deadlines ahead of speculative reads. Deferred
VMU commits must retain journal/readback and acknowledgement ordering. Do not
grow a reservation from apparent RAM headroom or silently suppress saves.

Completion evidence: improved same-scene playback on hardware, continuous
music under concurrent data reads, seek/track changes and save/reload, with
unchanged gameplay responsiveness. Keep the four existing CE kernel/sound
profiles explicit; unfamiliar kernels need their own contracts.

## 5. Broaden routes and complete the lifecycle

- Physical discs: preserve Power Stone 2 SCI save/reload; resolve Power Stone 1
  buffer recognition and Time Stalkers' different startup-clear lifetime.
  Qualify SCIF, CE and CD/MIL-CD independently.
- Formats: integrate existing CHD/CSO experiments with actual launcher routing;
  qualify compression together with VMU, cache and CDDA where supported.
- Transports: carry source fixes into each affected reader, retain SCI/SCIF/IDE
  boundaries and qualify future ATA DMA on actual hardware.
- Return/disc change: extend verified owner retirement, current save recovery
  and authenticated reload. Check the game's real disc-change boundary rather
  than merely switching a map. Preserve original engine save/options menus.
- CE protocol: complete presently unsupported endpoint operations only after
  their caller, scheduling and completion contracts are reviewed.

Completion evidence: the requested combination works from cold launch and
after another game returns. Disc change retains the current virtual card and
retires stale reads/cache before rebinding. Report unsupported combinations
explicitly; existing boot success is retained independently.

## Delivery and acceptance ledger

Each candidate records base commit, intended behavior, rebuilt and preserved
roles, effective service/reservation inventory, actual artifact hashes,
host/linked/native/package results and the smallest outstanding console check.
Keep generated build evidence separate from hardware acceptance.

Use the current `tools/make_update.py` producer and actual C consumer for new
KUPs. A complete composition must retain accepted components with their real
identities and match the GUI's admission/provider decisions. A cold-stage
subset must never be presented as a complete universal installation.

User-facing test filenames include purpose, delivery date/time with an explicit
zone, and the actual binary build identity, for example
`KUI-Unified-Test-06NOV2026-1133-CST-72db614.kup`. Use Chicago local time with
the applicable CST/CDT suffix. A new delivery filename does not imply rebuilt
software; preserve the original binary identity and checksum when renaming.

The accepted cc81 package and first consolidated test both contain updater
engine 3. Prefer normal KUP installation, which retains the effective selected
software inventory. The manual updater-and-test bootstrap deliberately selects
physical ROOT and clears the previous selection; bank directories and user data
remain, but bank-only software is no longer effective and may need reinstalling.
Do not describe that fallback as preserving bank activation or automatic
rollback. A future migration should preserve the effective software selection
while repairing the updater.

## Branding and documentation follow-up

Keep the account migration separate from loader qualification. Update active
source links, download/release tooling, shell credits and their tests to
TacitusVox. Regenerate current branding from its source assets and update any
derived images, embedded arrays and hash checks together. Inspect image pixels
before deciding which artwork needs changing; a text search alone is not an
artwork audit. Review hardware artwork and generator output in the same pass.

Preserve historical release records, original credits and provenance. Historical
TPMJB references require individual review rather than a repository-wide text
replacement. Current publishing targets must resolve to TacitusVox even while
that historical material remains.

Reference contracts: `accepted-progress.md`, `global-launch-policy.md`,
`shared-loader-audit-2026-11-06.md`, `shared-loader-vmu-regressions.md` and
`loader-integration-baseline.md`.
