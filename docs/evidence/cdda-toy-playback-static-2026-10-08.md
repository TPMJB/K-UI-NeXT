# Toy Commander playback revision: console evidence and audit

## Hardware result before this revision

The user reported audible playback on build `79fc8a4f2197`, with audio cuts
and glitchy video. Pressing Start during the intro produced the terminal
report. This establishes that the preceding startup correction reached audio
on this console; it does not establish correct playback quality or skip.

The five supplied photographs were readable. Their sequential words are:

| Page | Row 1 | Row 2 | Row 3 | Row 4 |
|---|---|---|---|---|
| 0 | `54595031 00000003 00000120 00000007` | `0000000D 00000009 00000009 00000001` | `00000014 0000000E 0000000E 00000000` | `0005BE09 0000000E 0005C24E 8CFD0000` |
| 1 | `8D000000 8CFD6C60 00000324 00000000` | `00005104 70CCEEB2 00000001 A09D3FE0` | `00020000 00000001 000013FF 00000000` | `000000BF 00003406 00000430 004B9E10` |
| 2 | `00000000 00004F82 0000002F 00000049` | `00000048 00000047 00000047 0000A5B2` | `00000030 00000003 00000000 00000000` | `00000000 00000000 00000000 00000000` |
| 3 | `00000030 00000047 00000000 00000001` | `0011C000 00124000 00000015 0000B360` | `00000000 00000000 00000000 00000000` | `00000000 00000000 00000629 00000629` |
| 4 | `000007AE 000007AA 000007A9 00000001` | `00000003 000007A9 00000101 00000002` | `00000000 00000000 00000000 00000000` | `00000000 00000000 00000000 00000000` |

The report records a verified driver, 128 KiB sound lease, 804-byte stack
watermark, no stack fault, and matching worker generations. It contains 72
START submissions, 71 finite retirements, 73 bank fills and 48 sampled active
starts. Raw, queue, bus and active-bank-write error counts are zero. These
are ownership/progress measurements, not measurements of audible frames.

Fault `0D` is the native-control fault. Detail `3` is the returning time or
attempt budget. PAUSE never replaced mailbox PLAY20. The cumulative counters
are 1,966 pause entries, 1,962 retries, 1,961 permitted pumps, one native owner
retirement and maximum 1,961 attempts in a retry sequence. Those totals rule
out persistent deferral by the native busy guard or an unrelated file owner
as the normal explanation: either deferral returns before counting a pump.

The longest reported worker visit is `3406` ticks, nominally 17.05 ms.
However, maximum service gap `BF` is only 191 ticks, nominally 0.244 ms. The
second value cannot normally coexist with that visit time during continuous
service. Independent review verified the original packaged worker bytes,
field offsets, compiled stores, timestamp preservation, snapshot copying and
renderer offsets. No timer reset or stale-T instruction sequence was
demonstrated. The discrepancy remains unexplained; this revision does not
change the clock frequency or reinterpret that field to hide it.

## Proven problems and corrections

The raw callback masks BL/interrupts across the whole card operation. The
worker previously requested up to two sectors in a visit. It now requests
exactly one, and the callback refuses other counts. Both visits around the
unchanged native updater remain. The private raw buffer decreases from 4,704
to 2,352 bytes. This reduces the bounded batch size; it does not prove a
universal card latency or eliminate all video contention.

The former finite handoff always waited a full programmed interval from
first observing START consumption, even if playback was already underway.
A fresh post-ACK observation now takes the smaller positive in-range cursor
from the same matching PCM16 finite templates as a conservative consumed
frame lower bound. Natural retirement waits the remaining duration plus the
existing 20 ms guard, and still requires inactive flags. Zero, malformed or
unpaired cursors retain the full interval. The initial service observation
cannot shorten the proof: it may precede START and belong to the prior bank.
STOP retains its independent full interval, and intentional clock fencing
resets both proofs to the full interval. Generation checks remain in force.

After eight setup packets, one immediate bounded ACK observation permits
same-visit template validation and START when ARM has consumed the final
packet. There is no polling loop. An unconsumed final packet defers normally.
This removes an unconditional extra visit without treating publication as
consumption. The two-port finite design retains periodic silence; seamless
streaming requires further protocol work.

Toy-only inline assembly that writes SR now declares the compiler's `t`
clobber. SR restoration also restores T, so this declaration is necessary.
The reviewed prior and revised binaries had no stale-T reader between an SR
restore and a fresh T setter or ABI boundary. This is a compiler contract
correction, not a demonstrated cause of the reported fault.

## Native failure audit and evidence

Native PAUSE rejects with -13 when the first record already owns the context,
or when GD REQUEST22 returns zero after record admission. The latter assigns
no new native owner. The native pump executes GD service, then CHECKs only
the current native owner. It cannot acknowledge an ownerless resident handle.
Native record subtype72 is not an admission flag; allocation76 is not a veto.

The exact normal CHECK translation is coherent with the authored GD service:
progress retains ownership, completion acknowledges the resident handle
before retiring native ownership, and subtype8 chains REQ_STAT36 onto the
same record only after consuming its previous handle. SDK reinitialization
clears GD ownership first. No legal scalar abandonment or additional direct
requester was demonstrated. An invalid token or denied CHECK scratch can
retire a native record while leaving a different resident handle owned, but
the previous report does not contain those arguments or results.

The new scalar native-lifecycle fixture models first-record admission,
owner-equality rejection, REQUEST-zero failure, ordinary PAUSE and the
subtype8 status chain against the real GD adapter/core. It reproduces
persistent PAUSE refusal after an explicitly ownerless terminal handle and
after a wrong native CHECK token. The earlier startup fixture hid that second
path by manually acknowledging the real handle after native retirement.
These are meaningful reproductions of mechanisms, not proof of the hardware
trigger. Native records and resident handles are never forcibly cleared.

Before the first native-control fault, validated native ownership and the
resident command/token/state are captured under IMASK15. The last GD CHECK
token, destination and exact base result are retained without an extra CHECK.
The 320-byte snapshot is version4; the preceding 72 word offsets are unchanged.
The new eight words occupy the former report padding on page4. Invalid native
contexts use sentinels, and capture restores the exact caller SR before the
terminal hook. This allows the next hardware result to distinguish an owner
that never retires from a lost/mismatched handle or rejected scratch output.

## Validation and remaining limits

Fourteen suites pass AddressSanitizer, UndefinedBehaviorSanitizer and strict
warnings. The timed playback fixture charges 8 ms per sector, nonzero sound
read cost, two worker visits within a 60 Hz frame budget and independent ARM
queue application. It verifies next-bank prefill during active playback,
partial setup, delayed ACK progress, stale entry cursors, slower paired-side
progress, early key-off fallbacks, malformed positions and supersession.
Capture tests check invalid contexts, SR.BL/IMASK, first-fault retention and
exact snapshot fields. A host fixture cannot establish hardware timing.

The package includes the actual linked images, maps, compiler stack reports,
strict instruction audits and complete corresponding source. GD dispatch
remains within its 512-byte cap and original 28-byte frame. The snapshot
remains a frame-zero leaf. The only new scalar-adapter literals are the exact
linked diagnostic BSS objects; they cannot become indirect call targets.
The ordinary reader and 1.8.5 runtime source are unchanged. Private game
instructions, retail binaries and SWAT implementation are excluded.

The revision has not run on the Dreamcast. The Start failure's specific trigger,
the inconsistent service-gap counter, driver early key-off behavior and
seamless audio remain unproven or unresolved. The build metadata contains the
final measured footprint/stack totals and the report's exact word legend.
