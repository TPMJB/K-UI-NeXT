<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native loader timeline capture

a12f32a63221 was slightly better, but the owner still reports about one second
of stopped movement every five seconds while music continues. Its capture
has no SCI entries, rearms or forwarded events. The largest controller-poll
gap is23.40992ms and largest complete read145.01248ms. These are not a
measurement of the visible one-second stop. Several consecutive reads or
game work after delivery could produce a longer movement pause.

The new diagnostic retains full CHECK/EXEC20/25, IRQ8 and PIO1 scheduling.
It records complete GD calls, including entry before reader hook changes
and exit after service progress. It also exposes the previously suppressed
VBR-change counter logic. No timer is configured and no runtime SD log is
written. Collection begins only at the existing capture gesture.

## Install and capture every run

From this folder, using the existing prepared pair:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2 --profile-runtime
```

Use SCI, Background On, Normal speed, Auto/default encoding, CD music Off
and RAM patches Off. Safely eject and cold boot. During the affected movement
area hold both triggers fully plus B for about one second, then release.
Continue playing through several pauses until the intentional stop after
roughly20seconds. Record **90seconds of the cycling pages**, enough for all
thirteen pages. No counters need to be typed manually.

## What is recorded

* Thirty-two0.625-second bins: loader-call time, call count and controller
  heartbeat count, with clock epoch and invalid/overflow accounting.
* Eight longest complete GD calls retained in RAM; the four longest printed
  with full before/after context and the preceding gap between GD calls.
* Sixteen recent request summaries retained; eight printed with token,
  LBA/count, wall duration, time inside/outside GD, call count, delivered
  bytes and flags. Requests within about33.3ms form a proximity chain.
* Original caller SR, entry/exit VBR and IPRB, CHCR/TCR/DMAOR/SCI status,
  active/hooked/armed state and GD command. RDR is never read.

The timer is the existing admitted781250Hz down-counter. An invalid clock
or observed profile break starts a separate epoch; no duration crosses it.
Unobserved timer stops/resets cannot be excluded solely by matching endpoint
profiles. All timing includes diagnostic overhead. Time outside GD is not
CPU idle time. Request proximity is not proof of a frame or rendering stall.

## Reading the added pages

Summary maxima can come from different calls/chains. The bins show whether
most of a slow interval was spent inside GD calls. A long request with little
inside-GD time suggests gaps in service opportunities; a chain of many reads
suggests a larger asset-load burst. Long inside-GD calls identify work to
budget or optimize without guessing at another CHECK throttle.

Each longest-call page uses a two-digit prefix: rank0–3 and row0–6. Five hex
words follow, in this order:

| Row | Fields |
| --- | --- |
| 0 | elapsed, preceding gap, epoch, entry tick, entry flags |
| 1 | entry function, previous status, token, LBA, sectors |
| 2 | entry completed bytes, original SR, VBR, IPRB, CHCR |
| 3 | entry TCR, DMAOR, SSR, packed state, exit tick |
| 4 | exit flags, function, actual GD result, token, LBA |
| 5 | exit sectors, completed bytes, original SR, VBR, IPRB |
| 6 | exit CHCR, TCR, DMAOR, SSR, packed state |

Packed state: low8bitsSCI state; bits8/9/10active/hooked/armed; bit11VBR
equals reader VBR; bits16–23GD command. Original SR is supplied by the
existing entry save, rather than sampling the resident's own masked SR.

Request rows:0token/LBA/count/total;1inside/outside/calls/bytes;
2epoch/flags/chain length/chain wall. Flags1partial,2failed,4replaced,
8bad clock. An active unfinished request is identified on the summary page.

## Memory and validation

The collector is3420bytes plus a64-byte static event and20-byte static
clock sample, all typed private worker BSS inside the existing lease.
No additional game allocation or low BSS is taken. The callback is published
only after retained-region binding, from a typed immutable descriptor,
and only for PROFILE mode. Bootstrap and normal-mode slots remain zero.

The low bridge is a nullable tail call. Its actual instructions and four
arguments are modelled; the cold binder's complete code is pinned/reviewed.
The full cross-image call graph retains the original352-byte low stack
budget, including32-byte margin,16-byte guard and32-byte top gap.
The contiguous font stores the same252 rows in158bytes; all rows and bounds
are independently compared. Guards and verification checks are retained.

Host tests cover timer wrap/epochs, split bins, consecutive reads, token reuse,
partial/replaced/failed requests, exact hardware context, gesture activation
and storage exclusions. These checks do not establish console performance.

## Design directions after the trace

Sega's Saturn CD library separates immediate-return requests from a recurring
server, and the Stream System separates disc buffering from host delivery.
Its resident streams avoid reading the same data repeatedly. PSn00bSDK's
CD-audio example likewise uses a main-RAM ring buffer. These suggest an owned
verified-data queue, a reliable producer opportunity and a small immutable
hot-sector cache when repeated LBAs justify it. A larger queue cannot fill
itself while its producer is unscheduled, and a transparent loader cannot
assume the game's asset layout or cooperation.

Primary references:

* https://antime.kapsi.fi/sega/files/ST-136-R2-093094.pdf (5.2 and pre-loading)
* https://antime.kapsi.fi/sega/files/ST-098-031194.pdf (4.3–4.4)
* https://github.com/Lameguy64/PSn00bSDK/releases (v0.24 sound/cdstream)
