# Universal loader integration roadmap

Current delivered candidate is `9e3295bcfaec`. It includes the twelve native
cold families from `f8807cb16561` and the shared extent/ownership corrections
in five ordinary virtual-VMU readers. The accepted Smash collection has its
own qualified component set. The latest owner report suggests another two to
three seconds saved, with COLD 03 revised to seven to eight seconds from
approximately 21. See [the acceptance ledger](accepted-progress.md) and
[current library coverage](universal-library-coverage.md).

## Current owner-library sequence, November 6, 2026

The updated manifests identify 42 game products after excluding the owner's
incomplete Soul Calibur. There are 43 usable startup identities because
Shenmue Passport is a separate startup. Raw/cooked copies and duplicate disc
executables share evidence only when their exact bytes and runtime agree.
The new pair of version 2 bundles and the later version 2.1 supplement supply
Power Stone, the five WinCE runtimes, native audio drivers, Shenmue, Shenmue II,
and secondary Quake III/Le Mans executables. These extend the available
analysis inputs. Console playback and save tests remain separate.

| Order | Deliverable | Completion check |
| --- | --- | --- |
| 1 | Give ordinary WinCE its own installed reader and preserve the exact compatible Armada virtual-VMU route. The current ordinary path shares a filename with that adapter. | Final GUI and ZIP select the intended reader with the inherited module present, absent or damaged. Nightmare Creatures II and Armada reach gameplay using the selected route. |
| 2 | Close bounded native family gaps, beginning with Blue Stinger's initializer and Power Stone's Maple buffers, now using the supplied originals. | Original installer/driver execution, refusal controls and actual package admission pass. One changed family representative boots, loads a demanding area, and handles its selected physical or virtual card on console. |
| 3 | Prove whole-game executable lifetime through secondary loads. Quake III and Test Drive Le Mans now include the main and movie executables. Investigate the distinct Shenmue, Passport and Shenmue II runtime families. | Reader and VMU hooks survive the real intro-to-game transition and later executable loads. Allocations and firmware ownership remain valid across each transfer. |
| 4 | Broaden virtual saving and independent service modes. Keep separate accounting for neither service, VMU only, New/read-ahead only, and combined operation. | The selected modes retain the proved memory, physical cards work, and a game save survives power-off and reload. DOA2's working standalone route and the exact accepted Smash set provide regression checkpoints. |
| 5 | Extend native CD music beyond Toy/MDK2 and improve native/WinCE streaming. Use supplied audio drivers and CE applications to identify shared runtime contracts. | Continuous music, concurrent loading, track changes and the same FMV scene improve on console, with responsive play and durable saves. Each admitted runtime family gets an explicit result. |
| 6 | Close the 42-game coverage ledger, including multi-disc transitions and repeated launches. Then broaden the relevant physical-disc, compressed-format and transport combinations. | Every complete inventoried game has a recorded launch, actual play, loading and available save outcome on its chosen image and reader. Multi-disc games cross a real requested disc boundary with the correct card. |

Each source change answers one compatibility or performance question. A small
regression set covers the affected runtime family and its memory contract.
The full library ledger accumulates results as games are played. Record the
actual reader path/build and image variant alongside the GUI identity.
Deliver manual-copy ZIPs and validate the actual final package through cold
CD/ROOT boot.

Further bootstrap work can target the cold GUI's repeated hashes and a fresh
ownership transfer after the last GUI writer. The latter needs a recovery and
invalidation contract across resets and card changes. Keep each timing
comparison isolated. First finish the missing ordinary WinCE deployment and
the newly actionable runtime coverage above.

Goal: one maintained K-UI build which selects the best verified launch backend,
virtual VMU and optional read-ahead for each runtime. Keep successful games
working while extending shared support. A runtime may use demand delivery
without a speculative cache. Physical VMUs remain available where virtual
VMU is not yet supported.

Current scope is **Games only**. Startup/splash, menus, updater implementation
and branding are frozen. Their older follow-up notes below are deferred and
do not authorize work during this integration. Prioritize shared launch work,
complete routed-module coverage and proof of the final reader actually used.
The current operation-level corrections and the remaining allocation-proof
cost are recorded in `shared-game-launch-performance.md`.

The separately authorized `f7229ee` splash repair restores the original RAM
artwork immediately after CD handoff. It is a narrow display exception, not a
Games reader change or evidence of a gameplay/performance improvement.

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
The owner reports preparation is now about four seconds. The next actual
five-service bootstrap rollout and remaining console check are recorded in
`shared-native-bootstrap-rollout.md`; later GUI builds must retain its exact
default admission bindings.

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
| Sonic Adventure | Historical ordinary/standalone New play reports; exact original now passes linked plain/nesting standalone callbacks | Current combined immutable resolution passes; live service lifetime and actual console callback behavior need evidence |
| Silver | Exact Standard VMU adapter; current original passes immutable combined resolution | Qualify live executable reclamation, exit consumers and lock behavior |
| Crazy Taxi | Standalone New | Reviewed retained arena |
| Zombie Revenge | Standalone New | Six-input Maple layout and allocator/reservation |
| Blue Stinger | Shared early-family admission incomplete | Earlier interrupt-table initializer and associated ownership |
| House of the Dead 2 | Virtual VMU saving and reload | Residual lag; bounded scheduling work without disturbing persistence |
| PSO / Spawn | Booting; recorded intro problems | Resolve delivery/scheduling evidence; Spawn audio distortion remains an observation |

Keep successful image results for Time Stalkers and EGG. Preserve exact
Standard adapters until their shared replacement qualifies. Admit structural
SDK families through source-bound contracts rather than game-title guesses.
No percentage of universal game compatibility is implied by the current corpus.

The delivered standalone New reader exported the early plain handler in both
roles, losing an already proved nesting alternative. Public IRQ source
`0d1691d` corrects this structurally. Clean standalone build `a0dc9a62e79f`
passes linked instruction models using original EGG, The House of the Dead 2
and Time Stalkers executables in both modes, plus actual-module GUI preparation
on FAT32/exFAT. The later `f8807cb16561` standalone also passes the same
linked test on the owner's exact Sonic Adventure original in both modes,
including old-reader failure and refusal controls. Actual console callbacks
and the selected runtime remain a separate checkpoint. The Background 7c route
already handled the alternative.
See `native-zr-family.md` and the exact module/evidence record in
`shared-game-launch-performance.md`.

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

The delivered CE CRC16 table folding preserves all checked bits but adds
per-byte conditional work. Armada FMV timings from that cohort are therefore
not a clean baseline for the original full-table receive loop. Restoring the
256-entry table with unchanged resident/stack guards remains blocked. The
four CE music owners overflow by 192 bytes without compaction and by 64 bytes
after the final narrow lossless compaction/alignment trial. Experiments have
stopped; no CE replacements were promoted. The generic full-table fit is not
an exception, and all installed CE bytes retain the folded cost. All-family
fit, integrity and admission must pass before claiming restoration. Keep
coordinated native CDDA/WinCE streaming, service combinations and hardware
acceptance outstanding.

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

The standing delivery is a manual-copy ZIP with both identical GUI aliases,
the intended physical software tree, and an exact changed/retained inventory.
Validate cold CD/ROOT boot and actual module admission. When a KUP is explicitly
requested, use the current `tools/make_update.py` producer and actual C consumer.
A complete composition retains accepted components with their real identities
and matches the GUI's admission/provider decisions.

User-facing test filenames include purpose, delivery date/time with an explicit
zone, and the actual binary build identity, for example
`KUI-Unified-Test-06NOV2026-1133-CST-72db614.kup`. Use Chicago local time with
the applicable CST/CDT suffix. A new delivery filename does not imply rebuilt
software; preserve the original binary identity and checksum when renaming.

The accepted cc81 package and first consolidated test both contain updater
engine 3. Menu Restart can select a saved bank by activation sequence, so manual
ZIP comparisons use the cold ROOT entry and its actual reader receipt. The
earlier updater-and-test bootstrap is historical delivery evidence.

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
