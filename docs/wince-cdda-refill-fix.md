<!-- SPDX-License-Identifier: GPL-3.0-only -->
# WinCE CD music: cooked startup and urgent worker refill candidate

This follows console build `4f4201208a88`. Original Worms played music with
occasional cuts; the cooked copy failed before starting an audio worker.
The frozen original-image underrun and its limits are preserved in
[the evidence note](wince-cdda-diagnostic-result-62687.md).

## Changes

The playback model now receives the track's semantic audio/data control,
decoded with `kui_retail_track_control()`, rather than the manifest's packed
storage flags. Cooked DATA carries bit `0x80`; passing `0x84` as its control
made model initialization reject an otherwise admitted image. This happened
before worker startup, consistent with cooked recording `62686.mp4`: callback
failure, no worker event, no source jobs and no buffer. AUDIO still must be
the original raw track; this does not reconstruct discarded DATA headers.

The audio source worker now uses bounded polled reception while its existing
low-reserve urgency flag is active. Normal IRQ and DATA pumping retain DMA.
An IRQ can finish DMA and then receive the next block synchronously by PIO
when the guest channel is occupied or recovery requires PIO. That leaves no
future completion IRQ for that block; the source then waits for a worker
visit. Urgent worker reception avoids repeatedly introducing that dependency
while the reserve is draining. It only polls an already in-flight DMA and
never takes over the guest's DMA transfer.

The existing visit budget, cancellation, CRC checks, DATA arbitration and
Worms copy exclusion remain enforced. The worker restores interrupts between
512-byte receptions. No new priority, buffer-size or arbitration-quota change
is included. The ordinary CE/native/Toy/MDK2 loaders are not replaced.

The original report establishes slow delivered source progress, but does not
identify whether every delayed block used this DMA/PIO handoff. This is a
targeted candidate, not proof that all audible cuts are eliminated. Actual
polled reception cost and game pacing need hardware validation.

## Install and check

With the console off, merge the ZIP's `KUI` folder onto the SCI card, then
cold boot. It installs a matching runtime and the three separate Armada,
Bust-A-Move 4 and Worms CD-music modules. Keep existing images and settings.

Use Worms first with **Background loading On** and **CD music On**. Check the
cooked image in a match for audible music; its settings are separate from
the original image. Then use the original image and listen during normal
gameplay for the infrequent cuts. No title-screen wait is required.

If a cut occurs, the existing reset combination **A+B+X+Y+Start** and
20-second recording retain all four diagnostic pages. The preceding console
evidence has already been read; no repeat is needed to reconstruct it.

The source regressions check the cooked initialization boundary and a source
job that would otherwise need later DMA completion interrupts. They do not
measure console throughput or certify uninterrupted audio.
