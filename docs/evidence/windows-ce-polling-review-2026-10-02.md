# Windows CE polling proposal review — 2026-10-02

The owner supplied Claude's *WinCE disc driver patch notes*, dated October 2,
2026, in `Untitled.docx`. Source attachment:
`/workspace/scratch/e58804339be4/upload/Untitled.docx`, SHA-256
`ebf97bae8b57d43ed89e8885899c34a1fc6b17f0a4e0230b5c0e9801fd2c7d1a`.
Its extracted text was reviewed against K-UI commit
`f6083120bb3daae0b2214dc2989a32bccdafa458` and the primary references below.
The report describes static analysis, not a console boot. No original CE
kernel was attached, and no DreamShell/SWAT implementation was copied.

## Finding

The proposed interrupt-thread polling is a plausible compatibility experiment,
not an established four-halfword CE unlock. Current K-UI has a definite
exception/virtual-stack incompatibility in addition to the existing boot
placement and GD-service gaps. Keep normal CE Launch unavailable while these
contracts are established. Autonomous SCI DMA and CE completion adaptation are
separate milestones; either can inform the other without proving CE support.

## What the supplied report establishes, and what remains unverified

The report identifies three owner-extracted `0WINCEOS.BIN` inputs:

| Title | Reported bytes | Reported SHA-256 |
| --- | ---: | --- |
| ARMADA | 1,253,376 | `a7628c78090fce2c93012d5e4db6142096e138dceb7ebbf555142e902ca03c1f` |
| Bust-a-Move 4 | 1,830,912 | `2a8d24738bb5ba0687fa56ad33e814fe798158f9cb8cacfd4a8a20dea6bbb35d` |
| Worms Armageddon | 1,110,016 | `39b80e305947056551318bb65529cccc886021be60406c06f3110f7fe03bf571` |

These identities are **reported, not independently verified here**. The report
says `wsegacd.dll` invokes the direct BIOS GD entry `0x8c0010f0`, with progress
and streaming threads driven by CE interrupt IDs 20 and 21. It proposes replacing
their waits with short polling timeouts. Module locations, original patch
bytes, complete control flow, compressed-section properties, import resolution,
IRQ identities, and the absence of other direct hardware dependencies still
require independent examination of the owner kernels. Searching literal
register addresses alone cannot rule out computed hardware accesses.

Established in K-UI source:

- `retail_resident.c` redirects `0x8c0010f0` into the GD service.
- `retail_gd.c:guest()` accepts native RAM aliases and canonicalizes them;
  `retail_resident.c:map_guest()` purges a P1 range and uses P2. This is not
  general CE virtual-address mapping.
- The service lacks streaming commands 38/39, transfer/check functions 6/7 and
  12/13, and nonzero callback registration. A call-site list does not establish
  that a title exercises every path, nor that accepting a command as a no-op
  provides its expected semantics.
- The native stage overlaps the reported CE prefix destination, and the
  native relay assumes a particular initial stack and VBR. See the existing
  [CE loader audit](windows-ce-loader-audit-2026-10-01.md).

## Patch instruction review

The supplied branch arithmetic is correct: at driver offset `0x082e`, both
`bt +0x13` and `bra +0x13` target `0x0858`; the Worms pair at `0x07b2`, with
displacement `0x17`, targets `0x07e4`. However, **BRA introduces a delay slot**.
The following `0x6ad2` (`mov.l @r13,r10`) executes on the new path, whereas the
old taken BT skipped it. Its register and memory-access effects cannot be
declared harmless from the six supplied halfwords alone.

A candidate with fewer new effects is to replace the preceding `tst r0,r0`
(`0x2008`) with `sett` (`0x0018`) at `0x082c` / Worms `0x07b0`, retaining the
original BT. This reaches the same target without adding that memory read or
r10 assignment. Renesas documents the SETT encoding and non-delayed BT.
This is a **review candidate only**: independently verify original bytes,
incoming edges and downstream state before applying it.

Either unconditional-progress proposal also treats a failed wait like a normal
timeout and bypasses the original 15-second abort path. Preserve a meaningful
request deadline and handle wait errors; do not replace a bounded failure with
endless polling. A one-millisecond timeout is not a measured polling interval
or throughput guarantee. Scheduler resolution, two high-priority threads and
IRQ-masked read time all affect behavior.

## Critical entry and virtual-memory prerequisites

`retail_resident.S` uses `.Lirq_mask = 0x100000f0`. It therefore sets **SR.BL**
as well as IMASK. SH7750 hardware manual section 5.5.3 specifies a manual reset
when a general exception occurs with BL set. A CE TLB miss would not simply
wait until the resident returns.

The wrapper also pushes PR and r8–r14 on the caller's stack **after setting BL**
and before switching to the private stack. A virtual-stack page boundary or
missing translation can therefore fail before parameter decoding. Changing
only `guest()` is insufficient. Three contracts need explicit validation:

1. **Entry, exit and exceptions:** caller-stack accesses, original SR and
   register-bank preservation, CE VBR/TLB-fault behavior and private-stack
   capacity. Clearing BL alone does not establish that CE's exception handlers
   can safely run while using the small resident stack or while interrupts are
   masked. Keep the native wrapper's established behavior separate.
2. **Mapping and lifetime:** parameters versus destinations, every crossed
   page, permissions, residency and physical contiguity. Submission and later
   EXEC/check/callback calls need a defined address-space context and ownership
   lifetime. Page locking in a driver is not proof that every supplied pointer
   is a physical alias or that its pages are contiguous.
3. **Cache and callback context:** validate coherence of CE virtual aliases
   with any physical/P2 access; protect resident ranges after translation.
   A callback address must execute in its expected CE process/thread context
   with a reentrancy contract. Native masking and a single private stack do not
   establish this contract for faulting or callback paths.

## Interrupt acknowledgement remains open

Microsoft describes `InterruptDone` as notification that interrupt processing
has finished, leading to OEMInterruptDone, which should re-enable a previously
masked source. That generic description does not prove that the Dreamcast OEM
implementation is harmless when no interrupt fired. `KernelIoControl` forwards
parameters to OEMIoControl, so the report's `0x10000` operation must be decoded
from the actual kernel. Confirm which Holly sources map to IDs 20/21 and what
both acknowledgement calls touch before polling through them. The report's
other untraced wait remains a separate possible blocker.

## Smallest useful next milestone

Checked CE load-header planning is implemented in `ce_load_plan.c`, with
4,269 synthetic checks passing ASan/UBSan locally (LeakSanitizer disabled for
the runner's `/proc` restriction). It accepts only the reported one-section
profile, checks EOF/rounding/entry/aliases and live-memory overlaps, and rejects
the present stage/prefix collision. It is not linked into the console runtime;
passing tests prove arithmetic against supplied reservations, not identity or
bootability. Header interpretation still needs verification against a kernel.

Normal launch eligibility is unchanged. Next obtain the original ARMADA
`0WINCEOS.BIN` already extracted by the owner, verify the hash above, then
independently inspect its load header, ROM/module tables, executable section,
patch contexts and relevant OEM routines. Keep the original file unchanged.

Use a placement-only probe with an explicit CE package/profile and a separately
checked memory layout. Verify the prefix/body bytes and load ranges, report
checksums and candidate patch matches, and stop before entering CE. Any patch
dry run must be confined to the disposable loaded copy, reject unknown or
ambiguous input, and report readback. Resolve the entry/mapping/acknowledgement
contracts before an instrumented boot; then trace first GD calls and resident
guards. Title screen, gameplay, FMV/audio and VMU save/load remain distinct
console milestones.

## Primary references

- Renesas, *SH7750, SH7750S, SH7750R Group User's Manual: Hardware*,
  R01UH0456EJ0702, Rev. 7.02, September 24, 2013, sections 3.6 and 5.5.3:
  <https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware>.
- Renesas, *SH-4 Software Manual*, RJJ09B0346-0600, Rev. 6.00,
  September 7, 2006, BRA/BT definitions and section 9.82 SETT:
  <https://www.renesas.com/ja/document/mas/sh-4>.
- Microsoft, *OEMInterruptDone (Windows CE 5.0)*, updated September 14, 2012;
  documentation lists applicability to CE 2.10 and later:
  <https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms904912(v=msdn.10)>.
- Microsoft, *OEMIoControl (Windows CE 5.0)*, updated September 13, 2012:
  <https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms904917(v=msdn.10)>.

The generic Microsoft contracts are useful constraints, not independent
verification of the Dreamcast kernels' OEM implementations.
