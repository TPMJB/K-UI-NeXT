# EDF Standard SCI binary audit

This is a read-only investigation of the user's report that Evolution 2 FMVs were flawless on build `edf20f0f11a3` using **regular Standard loading**. The report accepts that mode description and does not substitute Background behavior for it. Current comparison uses `9fa65fc1cae0441cb33d6b737e244ad6f4fa6946` Standard VMU resident from `build/retry-standard`.

## Historical binary identity

The exact historical package is `fmv-baseline-input/K-UI/K-UI-SCI-Resume-Test.zip`. It contains Standard resident admission hashes but only the Background SCIA ELF/map. I rebuilt the Standard resident from its recovered source with the exact historical Standard compile/link flags and pinned cross compiler.

Rebuilt Standard resident binary: **11,140 bytes**, SHA256 **`2eeb1341723ab7fcc65d4f55564402b9889da22cc0457c1143d9b3a072c446fe`**. This matches the historical package's `evidence/general-sci-final-admitted.json` candidate Standard SCI hash exactly. Thus `rebuilt/resident-sci.elf` supplies symbolized machine code for the actual packaged Standard binary, not merely a similar source build.

## Actual ordinary data path

The comparison follows each function's reachable integer control flow, resolves PC-relative literals and direct branch destinations, and excludes linker-relaxation padding/overlapping symbol tails. Address relocations are normalized. Reachable instruction counts below are static function sizes, not dynamic counts or cycle measurements.

| Function | EDF / current reachable instructions | Result |
|---|---:|---|
| `read_run` | 188 / 188 | Same |
| `read_sectors` | 100 / 100 | Same |
| `kui_loader_sd_stream_next` | 196 / 196 | Same |
| `kui_retail_image_read` | 304 / 304 | Same |
| SCI `transfer_block` | 343 / 343 | Same |
| SCI byte transfer | 51 / 51 | Same |
| SCI acquire | 80 / 80 | Same |
| image `file_read` | 153 / 153 | Same |
| resident `step` | 153 / 153 | Same |

Also equal: data mapping body, video sampling, prepare/select-card, byte bit reversal, stream finish and bus callbacks. Both bus descriptors contain their block-transfer callback; current Standard has not accidentally fallen back to a missing block/DMA capability.

This rules out a changed instruction sequence inside these selected ordinary read functions. It does **not** establish identical execution time: runtime data layout, chosen image, memory/cache state, interrupt traffic and VMU work can still differ.

## Concrete differences and bounded instruction costs

I executed the actual linked integer leaf functions with the project's existing bounded SH integer model, extending only its defined integer opcodes and MMIO byte/word access. This model measures instructions and accesses, not SH-4 timing, cache misses, dual issue or peripheral stalls.

| Operation | EDF | Current | Difference |
|---|---:|---:|---:|
| Valid map, excluding identical cache operation (`writing=2`) | 27 instructions | 84 instructions | +57 |
| Successful SCI release | 80 instructions | 131 instructions | +51 |
| SCI release MMIO reads | 6 | 13 | +7 |
| SCI release MMIO writes | 9 | 9 | Same |
| Aligned `memcpy`, 512 bytes | 793 instructions | 916 instructions | +123 |
| Aligned `memcpy`, 2048 bytes | 3097 instructions | 3604 instructions | +507 |
| Unaligned `memcpy`, 2048 bytes | 20492 instructions | 20492 instructions | Same |

The new mapping wrapper validates the installed VMU lease and rejects destination overlap before calling the old mapping body. The SCI release additions read back restored SCI/GPIO/module-standby ownership. These operations add no timer writes or additional cache flush in acquire/release. Release occurs once per `read_sectors` requested run, not once per physical 512-byte SD block. These instruction/access deltas alone do not establish that they explain the reported FMV slowdown.

Current `memcpy` has fewer branches in its aligned loop despite its higher instruction count. Inferring a throughput regression from instruction count alone would therefore be unsound.

## SCEV compiler flag experiment (audit only)

The current VMU Makefile disables `tree-scev-cprop`; historical Standard did not. `git log -S` finds this flag already present at the earliest recovered source snapshot (`7d6f5ec`), with no retained rationale.

I rebuilt current Standard outside the repository with only `-fno-tree-scev-cprop` removed from compilation/link. All the selected reader functions above remained identical; aligned `memcpy` returned to the historical body. The ordinary production linker correctly refused this candidate because it overlaps the guarded service stack.

| Boundary | Current | SCEV enabled |
|---|---|---|
| Initialized binary end | `8c006fbc` | `8c006fc4` |
| BSS end | `8c007800` | `8c007820` |

The SCEV-enabled ELF was produced only after replacing the boundary assertion in an **audit-only external linker script**, allowing static inspection. It overlaps the stack by 32 bytes and must never be packaged or run. This is not a deployable optimization or a demonstrated FMV fix.

## Other behavior and runtime variables

The current ordinary dispatch includes logical drive-status/disc-check handling added after the older package. A separate provenance/game-call audit found no demonstrated Evolution 2 FMV branch depending on the altered status. The main game callers treat status 0 and 1 alike. Rolling it back is not established as a fix and risks known Skies/Time Stalkers regressions.

The current VMU resident reserves and intercepts Maple memory/traffic that EDF ordinary loading did not have. Existing optimized hooks may still impose runtime cache/interrupt or storage contention. That requires controlled observation; no causal claim is made here.

The supplied Evolution 2 startup bundle proves that supplied corpus image uses raw 2352-byte sectors. It does not establish which image variant the user launched in either the historical or current comparison. Those selected image paths could not be recovered. The current grouped UI defaults to Original, while the older UI presented variants separately. This is a meaningful unresolved comparison variable, not an explanation established from history.

A synthetic measurement using production `retail_image.c` gives 147 physical SD blocks for 32 sequential raw Mode-1 sectors, versus 128 for cooked 2048-byte sectors: **14.84% extra block traffic** for the same 65,536 user bytes. Request chunks of 1, 2, 4 and 8 sectors give the same counts. Raw also performs sector-header checks. Source: `raw-cooked-count.c` using the project's existing synthetic retail-image fixture.

## Evidence files

- `rebuilt/resident-sci.elf`, `.bin`, `.map`: exact historical Standard reconstruction.
- `standard-hotpaths-v3.json`: reachable-function normalized comparison.
- `compare_hotpaths.py`: comparison method.
- `leaf_instruction_counts.py`, `.json`: dynamic leaf instruction/access counts.
- `scev-on-comparison-v2.json`: selected data functions unchanged with flag enabled.
- `scev-on/`: **audit-only overlapping artifacts; do not run**.
- `raw-cooked-count.c`: synthetic image format traffic measurement.

No production source, active checkpoint or deployable package was modified by this audit. The investigation found no proven root cause for FMV slowdown and no verified FMV performance fix.
