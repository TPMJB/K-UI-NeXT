# Evolution: measured worker overhead and optional exception capture

The `1b5d9eb6e924` mixed-port candidate still froze on the console as the MPEG
Sofdec screen disappeared. Its fast-path eligibility correction was real, but
it did not identify or resolve the failure. No new candidate is a confirmed
Evolution fix until it succeeds on the console.

## Install

Extract into a fresh directory, then run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1
```

This installs the module directly and preserves the prepared card pair. Use the
same Evolution image and launch settings as before. NORMAL has no game trace
patches, live counters, automatic diagnostic stop or exception-vector redirect.
Try the opening FMV and menu. If they work, save, power off and reload.

## What changed

The worker uses a cached private stack during runtime callbacks. Cold arm,
exported reservation bounds, persistent state and DMA buffers retain their
original uncached aliases. Every runtime gateway, including terminal and trace
entries, uses the same stack alias. Canary reads follow that alias; the linked
call-depth proof now reserves the entire 256-byte guard border.

The existing checked memory reader and RAM predicate are inlined. Their width,
alias, bounds and alignment checks remain. This removes repeated call frames
and lets the compiler fold constant widths and MMIO addresses. It does not
replace checked reads with unchecked ones.

A combined instruction replay now executes the actual linked C worker against
the original Evolution heap, SDK initializer, polling code and response
consumer, including the production virtual-VMU INFO response. It previously
exposed private-stack traffic that guest-read-only measurements had omitted.

| Steady polling pair | SH C instructions | Private P2 reads | Private P2 stores | Owner/MMIO reads |
|---|---:|---:|---:|---:|
| Failed mixed-port build | 9,455 | 1,097 | 764 | 146 |
| Cached runtime stack | 9,501 | 322 | 60 | 146 |
| Cached stack plus inlining | 4,791 | 318 | 57 | 146 |

These are executed instruction/access counts, not cycle estimates or measured
console latency. The revised path retains every owner read and the same sole
parent-presence-byte publication. Text grows by about 1.7 KiB from inlining.

## If the freeze remains

The same package includes PROFILE mode; no further build is needed:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1 --profile-runtime
```

Launch once. If an illegal instruction, illegal delay-slot instruction or CPU
address error occurs through the witnessed general vector, a terminal page
shows **CAPTURED CPU EXCEPTION** and actual **EXPEVT, SPC, SSR, TEA**. Photograph
that page. This does not depend on a later controller callback.

If it stays black, hold **B and both triggers fully down for two seconds**,
then release. If polling continues, the existing capture starts after 30 pairs
and cycles timing/phase pages after about 20 seconds or 1,200 pairs. Photograph
the pages. There are no automatic blank-screen deadlines or live VRAM markers.
The second page reports whether the optional CPU fault vector was armed.

The vector addition is exact Evolution revision only and PROFILE only. It
validates the existing vector, original handler chain and owned worker targets
before writing. It preserves VBR and the SDK dispatch table, leaves the TLB
vector unchanged, and forwards other exceptions and changed recovery callbacks.
An unsupported witness disables this optional capture without refusing VMU
admission. A bus hang, nested blocked exception, later vector replacement or
ordinary software deadlock can still remain black; no report alone does not
prove which one occurred. PROFILE sampling itself adds work.

## Evidence and limits

- The integrated replay passes 1,000 actual linked polling pairs, with old
  private-stack bytes overwritten between returned callbacks. Twelve malformed
  contexts refuse before any TX/RX write. Ordinary steady completion changes
  exactly one RX byte and the original SDK consumer accepts the responses.
- Cached-stack save/load, controller, ownership and refusal suites run with
  sanitizers. The emitted stack guards, reader, integer gateways and exception
  forwarding/publication code have independent instruction/mutation checks.
- The model has coherent RAM and synchronous DMA. It does not emulate caches,
  bus latency, scheduled interrupts, FMV playback or the game main loop.
- Earlier hardware captures showed a live CPU in an exact-zero scanline wait.
  The newest failed run was not localized. Evolution's late graphics setup
  differs from working comparison games, but timing starvation is still a
  hypothesis, not a proven fault or permission to bypass its video wait.

Reproduction commands and exact measurements are in
`docs/evidence/native-linked-worker-cost-2026-10-24.json` and the source bundle.
No original game executable is included. Other modes remain exclusive:
`--control`, `--lease-only`, `--hooks-only`, `--raw-chain`, `--evolution-trace`.
RAW retains full validation and is not an empty-gateway baseline.
