<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native paced CHECK candidate

The owner reports the same movement stutter on 1baa80301a62 and identifies
VERIFYING EXECUTABLE LIFETIME EXTENT as the slowest startup step. The latest
capture has a 144.8448 ms maximum completed read, a 29.01376 ms controller
poll gap, and 88 SCI overruns. Of 62 timed controller windows, 51 expired
without delivery; 908 low-reader visits supplied only 11 card blocks.
Those numbers do not locate the exact visible pause or establish its cause.

The new candidate changes when the Background reader waits. It borrows the
Standard reader's close-poll idea while preserving the Background DMA path:

| Entry | Scheduling |
| --- | --- |
| Ordinary CHECK | Deliver arrived data; do not wait for unfinished DMA |
| Sixteenth consecutive close CHECK | Allow the existing full 20/25-block catch-up |
| EXEC | Keep the existing full 20/25-block catch-up |
| Real SCI interrupt | Keep the eight-block ceiling |
| PIO fallback | Keep the one-block allowance per entry |
| Physical controller completion | No experimental reader pump/window |

Close means the raw PVR scanline differs from the preceding CHECK by zero
or one. A backwards/wrapped or larger change restarts the count. EXEC and
new requests reset it. This compact policy uses two already reserved,
otherwise unused diagnostic words; it adds no resident RAM. Unlike the
Standard reader's full pacing object, it has no separate frame counter.
The actual card model verifies sparse checks, tight CHECK-only progress,
interleaved EXEC supply, scanline wrap, faults, DMA contention and exact
cooked/raw destination bytes. Console smoothness remains unverified.

## Install and compare

Use the existing prepared pair from this folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2
```

Use SCI, Background On, Normal speed, Auto/default encoding, CD music Off,
and RAM patches Off. Safely eject and cold boot. Compare intro pace/audio
and the same movement area. The ordinary mode has no intentional stop.

For a capture, add `--profile-runtime` to the same command. Hold both triggers
fully plus B for about one second, release, then continue through the problem
for roughly 20 seconds until the intentional stop. Record at least 25 seconds
of the cycling pages. The reader page now identifies the sixteen-poll boost
and disabled controller service. READ MAX still measures a complete accepted
request, not the duration of one masked GD call. AGE MAX is sampled pending
age. Neither is a frame-time trace or synchronized to a particular hitch.

## Startup verification

The full source CRC, resident comparison, bootstrap/suffix lifetime proof,
allocator/caller agreement, reference scans, exit proof and independent cache
SHA256 checks all remain. Only the pure executable-extent proof temporarily
enables the operand cache when entered with the exact relay CCR=0x100.
It calls the same C proof with the same arguments. Before returning, it
purges normal-index RAM, invalidates the temporary caches and restores
CCR=0x100. Other CCR profiles run the original call without cache changes.
The subsequent relay and gameplay cache policies remain as before.

The 140-byte linked P2 wrapper has an exact instruction/literal pin and
6,144 adversarial ABI model cases, including both proof results and all
interrupt-mask levels. Twenty unsafe mutations are rejected. Both CCR
writes have eight following NOPs; the model checks output/stack publication,
callee-preserved registers, return value, stack, PR and untouched FPU state.
This is correctness evidence, not a measurement of startup speed.

The low resident retains the exact 352-byte guarded stack. Its conservative
compiled call-graph requirement is 344 bytes, including the existing margin,
guard and top gap. No memory boundary or admission check was relaxed.

This remains an experimental native SCI Background build. The virtual A2 /
physical A1 protocol, persistent-save ownership and SDK-family admissions
are retained. Universal non-CE/CDDA compatibility is still unproven; the
separate Standard-reader open-loader investigation remains available.
