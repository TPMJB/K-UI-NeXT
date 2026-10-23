# Evolution: guard capture

The four photos from build `72f684374902` show a deliberate refusal with
reason `00000040` during the first submission: only checkpoints `10` and
`11` ran; submit/completion/free counters are zero. Maple DMA had not started.
This is an earlier refusal than the previously reported freeze before the
opening movie. It does not establish that both failures have the same cause.

The old report replaced its detailed guard witnesses with zeros. This update
preserves them and distinguishes target, builder, verifier and context failures.
It also captures the complete bounded packet-header batch. No admission check
has been loosened, and this is not a claimed Evolution compatibility fix.

## Install and run

Extract into a fresh folder and run there:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1 --raw-chain
```

This installs directly. No backup/restore installer is needed. Use the same
Evolution GDI, SCI/Standard, Auto encoding and controller setup as before.
Keep physical A1 unplugged, and CDDA, background loading and RAM patches off.

If blue pages appear, capture **EVOLUTION GUARD DETAILS**, which follows the
four existing trace pages. If any **EVOLUTION PACKETS FROM INDEX** pages follow,
capture those too. Pages cycle approximately every five seconds. A short video
of a full cycle is also sufficient. Power off afterward.

If the launch instead reaches the earlier movie freeze, photograph the trace
pages if they appear. The eight small game-screen digits are `HHGGSSSS`: hook,
game checkpoint and event sequence low16. The game can overwrite them. The
wait watchdog covers only the instrumented loop and cannot rescue every freeze.

## New guard details

The first trace page now also repeats the check, address and actual value.
The dedicated guard page retains the expected value and packet geometry:

| Check | Meaning |
| --- | --- |
| `00030001` | Private DMA target failed its P2/span/alignment checks |
| `00030002` | Filtered command-list builder rejected the captured batch |
| `00030003` | Built private command list failed verification |
| `000200xx` | Final context comparison `xx` failed |

`AT`, `VALUE`, `EXPECT` describe the failed comparison. Builder/verifier
results are signed integers printed as hexadecimal: `FFFFFFFB` is unsupported
physical command/function; `FFFFFFF9` is empty physical output; `FFFFFFF8` is
changed output. The source, byte count, descriptor/padding counts, target and
capacity are retained. Each packet row is index, control, RX address, header
and first payload word. Full write/save payloads are not captured.

The snapshot owns at most 32 packet headers and is collected only after a
terminal refusal. It does not poll more frequently or add SD trace writes.
The existing live trace function and the original game gateways are unchanged.

## Existing trace pages

Pages 1–3 contain the reason, saved PR/SR/SP, Maple/TMU0/CCR/DMAC0/PVR state,
counters and last 16 sampled events. These are checkpoints, not a fault PC.
Page 4 records the current and last accepted video surface.

`E003` deliberately captures an unsafe surface after a valid one was drawn:
1 disabled video, 2 24-bit depth, 3 unsupported pitch, 4 unsupported height,
5 both framebuffer spans outside VRAM. Initial unsupported video is skipped.
A legal video transition can cause this diagnostic stop.
`E002` captures 1,048,576 iterations of the instrumented timer-wait loop; it does
not by itself prove the timer stopped.

In the latest photos the surface was never accepted, video was disabled
(`CTRL=4`), and neither of the two trace calls wrote to the framebuffer.
The linked private chain is aligned and safely inside its 128 KiB reservation.
An all-A1 batch is rejected earlier by the mandatory parent check, reason 21,
so it cannot explain these reason 40 photos. The precise remaining cause
requires the new guard witness.

## Comparisons retained

| Selection | Reservation | Active hooks | Physical DMA list | Virtual card |
| --- | --- | --- | --- | --- |
| Normal, no mode flag | Yes | Yes | Copied/filtered | Yes |
| `--raw-chain` | Yes | Yes | Original | No |
| `--hooks-only` | Yes | Yes | Copied/filtered | No |
| `--lease-only` | Yes | No | Original | No |
| `--control` | No VMU worker | No VMU hooks | Original | No |
| `--profile-runtime` | Yes | Yes | Copied/filtered | Yes, timing capture |

Flags are mutually exclusive. Remove `--raw-chain` to restore normal virtual
VMU operation. The personalizer leaves prepared card files unchanged; normal
startup validation/recovery may still repair a journal. Raw-chain supplies no
virtual card and makes no in-game VMU storage call. A missing-VMU message is
expected. Its guard still validates the private candidate chain before leaving
the original MDSTAR and command bytes unchanged.

## Validation and limits

The real sampler and persistent bridge now run together in a sanitizer test,
including normal/raw transfers and real target/build/verify/context refusals.
The tests verify exact terminal witnesses and unchanged MDSTAR, RX slots and
storage on refusal. Separate tests cover bounded snapshot ownership, all five
surface captures, the event ring and loop watchdog. Exact-image installer tests
reject 49 changed witnesses before mutation; 144 original-instruction equivalence
cases and 8 gateway mutations are checked against the original executable.
Compiled audits cover private stacks, relocation, cache publication and source
provenance. The full archive contains a cloneable source checkpoint.

Trace patches require the reviewed 2,143,652-byte executable, SHA-256
`ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35`.
No game executable is included. Console execution of this update is unverified.
The trace perturbs timing; it is not a final performance build. There is no new
SDK family, CDDA coexistence, SCIF or physical-disc support in this package.
