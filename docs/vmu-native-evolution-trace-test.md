# Evolution: consolidated diagnostics

The owner's video from build `62b24dc35d60`, reported in `--raw-chain`
mode, shows the marker still updating through approximately 48.63 seconds,
then disappearing around 48.67 seconds. The confidently legible fields are
`1F44`; the final sequence digits are not clear enough to report exactly.
`1F` precedes the original DMA-start instruction replay, and `44` follows
the instrumented external wait. This is a checkpoint, not a fault PC or proof
that DMA itself stalled. Raw-chain failing shows the copied DMA list and
virtual replies are not necessary for the observed failure.

This update closes a capture blind spot: after drawing on a valid surface,
the old sampler silently skipped disabled video, 24-bit video, unsupported
pitch/height or two out-of-range framebuffer addresses. It now stops with
`E003` and preserves the current and last accepted video registers. A legal
video transition can trigger this deliberate diagnostic stop. It is not a
claim that the transition caused the underlying freeze or that Evolution is
fixed. The original game hook/replay and virtual-card behavior are unchanged.

## Surface-loss capture after the video result

Extract into a fresh folder and run from that folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1 --raw-chain
```

This installs the personalized loader directly. No backup/restore installer
is needed. Use the same Evolution GDI, SCI/Standard, Auto encoding and
controller setup as before. Keep physical A1 unplugged; keep CDDA,
background loading and RAM patches off for the comparison.

Look for eight small hexadecimal digits at the top left of the game screen.
If it freezes, allow about a minute for a possible watchdog page, then
photograph the screen, including those digits. If blue trace pages appear,
photograph all four; they cycle automatically roughly every five seconds.
Power off afterward. A short video that captures the last changing digits
is also useful, but a photo is enough to start.

The watchdog has an iteration budget, not a guaranteed wall-clock deadline.
It can report an excessive wait in the instrumented game loop. It cannot
rescue every freeze or capture an arbitrary fault PC. If the game clears the
framebuffer after its last checkpoint, the marker may disappear.

## Other comparisons in the same build

The command above uses the same original-DMA comparison as the video:

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
they may precede the final stall. Page 4 shows the current video control, size, pitch and framebuffer addresses,
plus the last accepted surface. For `E003`, reason 1 is disabled video; 2 is
24-bit depth; 3 is unsupported pitch; 4 is unsupported height; 5 means both
framebuffers fail the VRAM bounds check. If only one field is safe, the sampler
continues drawing there. Unsafe video before the first valid surface is still
skipped, preserving initial startup behavior. If the screen remains black
without an `E003` page, no later hook may have reached that condition, or the
framebuffer was cleared while retaining a supported mode. This is not a
general crash watchdog.

`E002` means the instrumented wait reached
1,048,576 iterations; it does not by itself prove the timer stopped.

The trace only activates for the exact reviewed Evolution executable:
2,143,652 bytes, SHA-256
`ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35`.
The game windows and immutable trace exports are checked before the first
trace patch. Other executable variants do not receive these trace patches.

## Validation and limits

The build passed the persistent bridge host suite, sanitizer tests for the
trace sampler (including all five lost-surface captures) and exact-image installer, and compiled-instruction audits.
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
