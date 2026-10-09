# Toy Commander reset callback: static evidence

Date: 2026-10-08. This records authored analysis and scalar contracts from
private inspection of the user-supplied boot file. It contains no game payload
or disassembly. The admitted boot is 748,444 bytes, SHA-256
`ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd`,
loaded at `0x8c010000`.

The latest hardware observation was approximately one second of recognizable
CD audio, followed by silence. The user's ABXY+Start reset attempt then froze
instead of producing the pilot report. The observation does not identify the
stalled program counter. The static route below establishes a reachable
unbounded retry before the previous BIOS-menu report hook; it does not prove
that the hardware stopped in that retry.

## Confirmed reset route

| Boundary | Address / contract | Consequence |
| --- | --- | --- |
| Main calls system initialization | Call at `0x8c027982` to `0x8c04e8d6` | The existing heap interposer installs the pilot before this call returns. |
| Native reset callback literal | `0x8c027a24`, value `0x8c027104` | This is the source pointer registered immediately after system initialization. |
| Reset registration | Load at `0x8c02798a`; store at `0x8c02798e` to `0x8c0fc0c8` | Registration occurs after the installer returns, so publishing the literal during installation takes effect before registration. |
| Reset callback invocation | `0x8c04f1ee` inside `0x8c04f1d2` | The original reset decision and pending-reset latch run before the callback. The integer argument is 1. |
| Native callback | `0x8c027104` | Closes stream objects, then calls the global sound stop/shutdown path. |
| Global sound stop retry | `0x8c049f98`, retry at `0x8c049fa6`, original stop entry `0x8c068a96` | A zero stop result loops without a deadline or intervening sound service. |
| Later system exit | `0x8c0bade0` | Depending on IP flags, exit can request BIOS menu command 1, request command 3, or jump to the boot entry. This occurs after the problematic callback. |

The previous pilot all-stop adapter revokes pilot state and then tail-calls the
original SDK stop entry. That does not make the caller's retry bounded: a
failed or occupied original sound queue can keep the reset path from ever
reaching the BIOS menu hook. Adding another check after that native callback
would leave this blockage in place.

## Implemented early terminal callback

The exact-title installer now replaces the registered callback's source
literal with the already linked low-resident pilot return hook. It changes no
controller polling, reset decision, or original retry body. Once the game
decides to reset and invokes its callback, the pilot enters its terminal
numerical report before native stream, sound, filesystem or cache teardown.

Admission requires the complete unmodified boot digest and the full existing
patch transaction. The new callback literal, original registration store, and
original callback call are checked before the initial heap hook and checked
again before installing the sound/reset patches. The terminal target comes
from the generated resident symbol binding; it must be even and lie in
`[0x8c004000, 0x8c007800)`, outside the guarded report stack and the heap worker.
The package's linked-symbol audit checks that this binding is the actual
authored code entry. A mismatched contract stops installation before that
transaction changes title words.

The one aligned four-byte literal is written through P1 and published with
the existing authored cache bridge: affected 32-byte lines are written back,
execution changes to P2, and instruction-cache invalidation preserves the
other CCR mode bits. The installer runs masked, and callback registration
cannot run until the initialization call returns.

The terminal assembly masks BL and IMASK before switching to the known low
report stack. It captures the original PR and stack pointer as integer
arguments and jumps into the report; it never returns to the discarded game
frames. The report copies scalar pilot telemetry before mailbox reset and
uses the captured display configuration. This route performs no SDK shutdown,
ARM reset, lease free, sound packet publication or new card operation. The
ordinary reader and other titles keep their original reset behavior.

The early callback cannot recover an original game or SDK instruction that
has already stopped returning before the reset decision is evaluated. The
separate authored worker-fault report entry addresses faults observed at a
completed worker boundary. Neither route is an interrupt-independent
watchdog, and this reset fix alone does not resolve the audio stop or slow
FMV playback.

## Verification

The host admission regression rejects a changed native callback pointer,
changed registration/call contract, odd target, target below the low resident,
target at the guarded-stack boundary, heap-worker target, and null target.
The valid exact contract passes. This command completed without address or
undefined-behavior sanitizer findings:

```sh
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Iinclude -Isrc/loader \
  tests/test_toy_pilot_admission.c -o /tmp/toy-reset-admission-test
ASAN_OPTIONS=detect_leaks=0 /tmp/toy-reset-admission-test
```

Independent private review confirmed the callback source, delayed
registration store, invocation argument, installation order and unbounded
native retry. Cross-compiled resident fit, stack totals and source/package
identity are checked by the normal pilot build and packager; their final
results belong to the release record. No successful console reset report is
claimed by this static evidence.
