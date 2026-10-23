# Evolution: continue across video blanking

The owner's four photos from build `4995b8016518` show `E003`, reason 1:
video control changed from 5 to 4 with the same size, pitch and frame addresses.
The last checkpoint was completion entry 20, with 2,021 submissions and 2,020
completions. This was an intentional diagnostic stop; that completion had
not yet returned to the original SDK consumer.

The original executable deliberately disables video at `8c1cb9ce` and
`8c1cb9f2`, then waits for counter `8c358d18` to change. Its video handler
increments that counter; other SDK code can also reset it. Another routine
briefly disables/restores video at `8c1f144a`/`8c1f1456`. Therefore this capture
does not prove a VMU fault or identify the earlier movie freeze. The exact
original instructions and literals are checked by the artifact audit.

This build lets those video transitions resume. After its first loss of a
valid surface it permanently stops drawing the live marker: a safe VRAM
address span alone does not establish ownership during movie playback.
The event ring and failure reporting continue. The disappearing marker is
expected in this build.

## Install and run

Extract into a fresh folder and run there:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1 --raw-chain
```

This installs directly. Use the same Evolution GDI, SCI/Standard, Auto encoding
and controller setup. Keep physical A1 unplugged; keep CDDA, background loading
and RAM patches off. Raw-chain leaves the original DMA list unchanged and
supplies no virtual VMU, so a missing-VMU message is expected.

If it reaches the title/menu, use the same command **without `--raw-chain`**
to test normal virtual-VMU operation and save/reload. Both modes are in this
build; no additional download is needed. The personalizer leaves prepared
card files unchanged. Normal startup validation/recovery may repair a journal.

If blue pages appear, capture **EXCEPTION AND VIDEO PROGRESS**, page 1, and any
**EVOLUTION GUARD DETAILS** or packet pages that follow. Pages cycle about
every five seconds; a video of one full cycle is sufficient. Power off afterward.

If it remains black without pages, report that and roughly how long you
waited. This capture cannot rescue a stall that prevents every instrumented
hook from running. No additional timer, vector, interrupt source or SD trace
log is installed.

## What the new capture means

`E003` is no longer emitted for an ordinary lost surface. The sampler retires
drawing and lets the original code continue.

`E004` deliberately captures a **continued video-disabled interval while
instrumented callbacks still run**. It requires 1,562,500 observed TMU0 ticks
(about two seconds at the existing 781,250 Hz configuration), a running timer,
prescaler 2, reload `FFFFFFFF`, and no ambiguous large inter-sample counter jump.
An observed clock change or ambiguous timer jump invalidates that interval's deadline;
normal timer wrap is supported. A long legal load may also trigger E004.
It is a progress snapshot, not a crash assertion or a guaranteed wall-clock
watchdog.

The new page preserves raw saved exception SPC/SSR/VBR and INTEVT, the video
counter, cached display control and blanking flag, first/current counter,
observed changes, stable timer duration and drawing retirement. Saved exception
registers may reflect SDK handling or nesting; they are not an arbitrary fault
PC. Counter changes are not asserted to be monotonically increasing frames.

`E002` still captures 1,048,576 iterations of the exact instrumented timer-wait
loop; it does not by itself prove a stopped timer. The original game gateways
and every VMU admission check are unchanged.

## Guard details retained

If reason `40` reappears, the guard page identifies the exact stage:

| Check | Meaning |
| --- | --- |
| `00030001` | Private DMA target validation |
| `00030002` | Filtered command-list builder |
| `00030003` | Built command-list verification |
| `000200xx` | Final context comparison `xx` |

`AT`, `VALUE`, `EXPECT` retain the failed comparison. Builder/verifier results
are signed integers printed as hex; `FFFFFFFB` means unsupported command/function,
`FFFFFFF9` empty physical output, and `FFFFFFF8` changed output. Each packet row
is index, control, RX address, header and first payload word. At most 32 private
packet metadata records are copied after refusal; full save payloads are not
dumped. All-A1 batches fail earlier at reason 21 because a parent is required.

## Other modes retained

| Selection | Reservation | Active hooks | DMA list | Virtual card |
| --- | --- | --- | --- | --- |
| Normal, no flag | Yes | Yes | Filtered | Yes |
| `--raw-chain` | Yes | Yes | Original | No |
| `--hooks-only` | Yes | Yes | Filtered | No |
| `--lease-only` | Yes | No | Original | No |
| `--control` | No VMU worker | No VMU hooks | Original | No |
| `--profile-runtime` | Yes | Yes | Filtered | Yes, timing capture |

Flags are mutually exclusive. The raw-chain candidate still passes the same
private-chain/context checks before the original command list is used.

## Validation and limits

ASan/UBSan tests cover brief blanking and resumption, all unsafe surface cases,
permanent drawing retirement, stable-clock continued-blank capture, counter
progress/freeze, timer wrap/reset/configuration changes, actual sampler with
normal/raw bridge transfers and normal save/read operations. Refusal tests
retain all four detailed witnesses and verify unchanged DMA/RX/storage.

The actual terminal renderer is checked for bounded rows and complete packet
pagination through 32 records. Compiled audits check all original game gateways,
the exact seven-instruction read-only exception-register leaf, zero stack use
in that leaf, relocation, private stacks, cache publication and source provenance.
The full archive includes a cloneable source checkpoint.

Exact trace image: 2,143,652 bytes, SHA-256
`ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35`.
No game executable is included. This diagnostic changes timing; console
compatibility and an Evolution fix remain unverified. There is no new SDK
family, CDDA coexistence, SCIF or physical-disc support in this package.
