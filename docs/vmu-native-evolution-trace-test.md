# Evolution: consolidated diagnostics

This package contains the remaining planned Evolution diagnostics in one
binary. It is an investigation build; an Evolution fix is not established.
Your last `--hooks-only` result still froze. That mode still redirects DMA to
a copied command list, so it did not isolate the game's original DMA list.
The new `--raw-chain` comparison does.

## First run

Extract into a fresh folder and run from that folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1
```

This installs the personalized loader directly. No backup/restore installer
is needed. Use the same Evolution GDI, SCI/Standard, Auto encoding and
controller setup as before. Keep physical A1 unplugged; keep CDDA,
background loading and RAM patches off for the comparison.

Look for eight small hexadecimal digits at the top left of the game screen.
If it freezes, allow about a minute for a possible watchdog page, then
photograph the screen, including those digits. If blue trace pages appear,
photograph all three; they cycle automatically roughly every five seconds.
Power off afterward. A short video that captures the last changing digits
is also useful, but a photo is enough to start.

The watchdog has an iteration budget, not a guaranteed wall-clock deadline.
It can report an excessive wait in the instrumented game loop. It cannot
rescue every freeze or capture an arbitrary fault PC. If the game clears the
framebuffer after its last checkpoint, the marker may disappear.

## Second run, if normal mode freezes

Use the same package and command with `--raw-chain`:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1 --raw-chain
```

The startup banner says `ACTIVE HOOKS / ORIGINAL MAPLE DMA LIST`. This keeps
the active hooks, RAM reservation and ownership/context guards, but leaves
MDSTAR and the original command-list bytes unchanged. It supplies no virtual
card and calls no in-game VMU storage backend. A missing-VMU message is
expected. Check whether the opening movie and title/menu appear; capture a
freeze or trace pages as above. This comparison does not test virtual saves.

| Selection | Reservation | Active hooks | Physical DMA list | Virtual card |
| --- | --- | --- | --- | --- |
| Normal, no mode flag | Yes | Yes | Copied/filtered | Yes |
| `--raw-chain` | Yes | Yes | Original | No |
| `--hooks-only` | Yes | Yes | Copied/filtered | No |
| `--lease-only` | Yes | No | Original | No |
| `--control` | No VMU worker | No VMU hooks | Original | No |
| `--profile-runtime` | Yes | Yes | Copied/filtered | Yes, timing capture |

All six selections are in this binary. Flags are mutually exclusive. The
same command without a mode flag restores normal virtual-VMU operation.
The personalizer leaves prepared card files unchanged. Normal startup
validation/recovery can still repair a journal before the game starts.

## Reading the capture

The live digits are `HHGGSSSS`: last hook code, last game checkpoint, and
the low 16 bits of the event sequence. They are observation points, not
instruction addresses. Sequence rollover is normal.

| Code | Last completed observation |
| --- | --- |
| `10` | Entered submit C body |
| `11` | Submit boundary passed validation |
| `12` | Chain/context built, before possible MDSTAR redirection |
| `1F` | Submit C return boundary, before the assembly DMA-start store |
| `20` | Entered completion C body |
| `21` | Completion chain/context passed validation |
| `22` | Original MDSTAR restored or retained; context valid |
| `23` | Before completion/virtual-response publication |
| `2F` | Completion C return boundary |
| `30`, `31` | Entered/returned from the free check |
| `41` | Entered the game's frame-pump function |
| `42` | Sampled the game's timer-wait loop |
| `43` | About to call the game's external wait function |
| `44` | Returned from that external wait |

Trace page 1 shows the reason, last checkpoints, saved PR/SR/SP, Maple
registers, TMU0 state, the game's wait flag, counters and CCR. Pages 2 and 3
show DMAC0 state, first/last wait-loop timer counts, PVR scan status and the
last 16 sampled events, oldest first. Each history row is code, saved PR,
Maple start state, timer count and wait flag. These are checkpoint snapshots;
they may precede the final stall. `E002` means the instrumented wait reached
1,048,576 iterations; it does not by itself prove the timer stopped.

The trace only activates for the exact reviewed Evolution executable:
2,143,652 bytes, SHA-256
`ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35`.
The game windows and immutable trace exports are checked before the first
trace patch. Other executable variants do not receive these trace patches.

## Validation and limits

The build passed the persistent bridge host suite, sanitizer tests for the
trace sampler and exact-image installer, and compiled-instruction audits.
The four new gateways were executed against the original game instructions
in 144 register/status cases, with eight negative mutations rejected.
Installer tests rejected 49 changed witnesses before any trace patch.
The compiled artifact audit checks relocation, private stacks, cache
publication, mode routing and clean source provenance. The full archive
also contains the exact source checkpoint. No game executable is included.

The live trace adds runtime work and changes timing. Use it to locate the
failure, not to judge final performance. Sampling reads the existing TMU;
it does not program a timer, change cache mode or write a trace log to SD.
There is no new SDK family, CDDA coexistence, SCIF or physical-disc support
in this diagnostic package.
