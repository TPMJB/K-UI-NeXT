# Evolution: controller-poll optimization candidate

Evolution froze before the live-marker and video-progress diagnostics existed.
Their overhead cannot explain the original failure. Removing that overhead is
measurement hygiene, not an established fix.

Build `5aff8a276681` deliberately stopped after two seconds of disabled video
with `E004`. At that snapshot all 2,119 submitted Maple pairs had completed,
the game video counter had changed 120 times, and saved exception SPC was
`8c1f362c`. The original executable maps that address to a video-mode routine
polling the low ten bits of `a05f810c` for exactly scanline zero. Timing-register
writes follow the wait. This is not a VMU or SD wait. One sampled SPC does not
prove that execution stayed there throughout the blank interval.

Long interrupt callbacks could make the game miss that short synchronization
window. This remains a hypothesis. This candidate reduces pre-existing VMU
callback work without bypassing the game's video synchronization.

## Install and test

Extract into a fresh folder, then run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1
```

This installs directly; no backup/restore script is needed. Use the same
Evolution GDI, SCI/Standard, Auto encoding, and controller setup. Keep physical
A1 unplugged and CDDA, background loading, and RAM patches off for this test.
The prepared card files are preserved by personalization.

The primary test is a normal virtual-VMU launch: opening FMV, title/menu,
new-game/save, power-off, and reload. There is no live marker or automatic
video-blank stop in normal mode. A missing marker is expected. If it freezes,
report the last visible screen and whether the card-reader LED remains active.
There is no need to repeat the older diagnostic comparisons first.

## What changed

After the first full layout validation, a strict controller-only batch can use
a smaller callback path. It accepts one to four distinct main-unit GETCOND
requests, including port A, with the controller function and exact prepared RX
slots. Up to 32 source words, including optional SDK padding, are retained in
private RAM. The original SDK DMA list stays in use.

Completion compares every captured source word, validates every physical reply,
and checks the live manager alias, initializer inputs, used RX entries, lease,
hooks, frame, lock and DMA context before changing one byte in the port-A parent
header to retain virtual-A1 presence. No virtual payload or SD operation is
permitted by this path. A changed admitted pair stops before that store.

VMU enumeration, reads, writes, mixed traffic, unsupported batches and a cold
layout use the existing full path. Full storage admission and free-hook lease
protection are retained. An unrelated RX slot is not authority for the one-byte
controller path, but is still checked before the full VMU/storage path.

The four-controller host fixture uses 170 guest reads per pair, versus 360 for
the full path. These are bus-read counts, not hardware time or frame-rate
measurements. Evolution's actual proportion of eligible batches is unmeasured.

Exact Evolution tracing is now explicit with `--evolution-trace`. Normal,
raw-chain, hooks-only and runtime-profile modes do not install its four game
trace patches or perform its MMIO snapshots/VRAM marker writes. The allocation-
failure diagnostic remains available without changing successful allocations.
A disabled video output no longer triggers the artificial two-second E004 stop,
even in opt-in tracing.

## Included comparison modes

| Selection | Hooks | DMA list | Virtual card | Evolution live trace |
| --- | --- | --- | --- | --- |
| Normal, no flag | Active; controller optimization eligible | Original for eligible controller polls, otherwise filtered | Yes | No |
| `--raw-chain` | Full comparison path | Original | No | No |
| `--hooks-only` | Full comparison path | Filtered | No | No |
| `--lease-only` | Lease protection only | Original | No | No |
| `--control` | No VMU hooks or worker reservation | Original | No | No |
| `--profile-runtime` | Full timing path | Filtered | Yes | No |
| `--evolution-trace` | Full diagnostic path | Filtered | Yes | Exact image only |

Flags are exclusive. Runtime profiling currently measures the full route and
must not be used as a timing measurement of optimized normal controller polls.
The trace mode retains the bounded event ring, terminal guard details and exact
instrumented timer-loop watchdog; it does not capture an arbitrary stalled PC.
It changes timing and is not the primary compatibility test.

## Validation and limits

Sanitizer tests cover the eligible port subsets and banks, both prepared RX
layouts, live manager aliases, unchanged source DMA, byte-exact parent-only
publication, absent non-A controllers, physical A1 conflicts, every source-word
mutation and completion-read failure, mixed/full fallback, durable save/read
switching, and attempted release of the worker lease. Existing persistence and
recovery tests remain required. Trace tests require zero sampler I/O in ordinary
modes and allow long legal blanking without an artificial stop.

Linked audits bind the new controller context helper and private state, the
existing storage guards and gateway register/stack behavior, all seven immutable
launch modes, relocation, cache publication and source provenance. The full
archive contains a cloneable source checkpoint. No game executable is included.

An Evolution fix is not established until this candidate succeeds on the console.
This package adds no SDK family, CDDA coexistence, SCIF or physical-disc support.
