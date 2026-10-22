# Windows CE polling proposal review — 2026-10-02

The owner supplied Claude's *WinCE disc driver patch notes*, dated October 2,
2026, in `Untitled.docx`. Source attachment:
`/workspace/scratch/e58804339be4/upload/Untitled.docx`, SHA-256
`ebf97bae8b57d43ed89e8885899c34a1fc6b17f0a4e0230b5c0e9801fd2c7d1a`.
Its extracted text was first reviewed against K-UI commit
`f6083120bb3daae0b2214dc2989a32bccdafa458` and the primary references below.
The owner subsequently supplied `ARMADA-0WINCEOS.BIN`; the independent binary
review below uses K-UI `0d400a47160182fa27b07b13c7767588886765b3` as its code
baseline. These are static analyses, not console boots. No DreamShell/SWAT
implementation was copied, and no kernel or extracted section is committed.

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

ARMADA's identity is now independently verified; the other two identities
remain **reported, not independently verified here**. The report
says `wsegacd.dll` invokes the direct BIOS GD entry `0x8c0010f0`, with progress
and streaming threads driven by CE interrupt IDs 20 and 21. It proposes replacing
their waits with short polling timeouts. Module locations, original patch
bytes, complete control flow, compressed-section properties, import resolution,
IRQ identities, and the absence of other direct hardware dependencies required
independent examination. The ARMADA findings below narrow those open questions;
they do not validate the two other kernels. Searching literal
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

## Interrupt acknowledgement contract

Microsoft describes `InterruptDone` as notification that interrupt processing
has finished, leading to OEMInterruptDone, which should re-enable a previously
masked source. That generic description does not prove that the Dreamcast OEM
implementation is harmless when no interrupt fired. ARMADA's actual mapping
and `0x10000` operation are now decoded below: the calls consume pending state
and re-enable real interrupt sources. A polling adapter still needs a separate
path for synthetic timeouts. The other wait is now identified as overlapped
I/O completion; it should remain tied to request completion.

## Smallest useful next milestone

Checked CE load-header planning is implemented in `ce_load_plan.c`, with
4,269 synthetic checks passing ASan/UBSan locally (LeakSanitizer disabled for
the runner's `/proc` restriction). It accepts only the reported one-section
profile, checks EOF/rounding/entry/aliases and live-memory overlaps, and rejects
the present stage/prefix collision. It is not linked into the console runtime;
passing tests prove arithmetic against supplied reservations, not identity or
bootability. The supplied ARMADA kernel now verifies this header interpretation.

Normal launch eligibility is unchanged. The owner has now supplied the original
ARMADA kernel; identity, structure and relevant code are checked below. Keep
the original file unchanged.

Use a placement-only probe with an explicit CE package/profile and a separately
checked memory layout. Verify the prefix/body bytes and load ranges, report
checksums and candidate patch matches, and stop before entering CE. Any patch
dry run must be confined to the disposable loaded copy, reject unknown or
ambiguous input, and report readback. Resolve the entry/mapping/acknowledgement
contracts before an instrumented boot; then trace first GD calls and resident
guards. Title screen, gameplay, FMV/audio and VMU save/load remain distinct
console milestones.

## Follow-up: original ARMADA kernel verified

Input `/workspace/scratch/e58804339be4/upload/ARMADA-0WINCEOS.BIN` is exactly
1,253,376 bytes and matches the ARMADA SHA-256 above. Read-only structural
parsing follows Microsoft's ROMHDR/TOC/e32/o32 definitions; GNU Binutils objdump
2.44 decoded the uncompressed SH-4 little-endian code. Addresses of the form
`0x01de....` below are nominal module addresses, not a claim about the active
CE MMU/process-slot mapping.

The [structural metadata](armada-ce-structure-2026-10-02.json) preserves the
checked header and module/section records without storing kernel bytes.

### Placement and executable section

The one-section load header confirms destination `0x0c010000`, file offset
`0x800`, body length `0x131800` and entry `0x0c010000`. Prefix CRC32 is
`6200729c`; body CRC32 is `b9f755bc`. The checked planner rejects the current
stage/prefix collision and accepts the candidate stage at `0x8ce10000` for
pre-entry placement, with 611 body sectors.

The ECEC marker leads to ROMHDR `0x8c141420`, with 21 modules. ARMADA declares
RAM from `0x8c142000` to `0x8cfd0000`, followed by driver globals of `0x30000`
bytes at `0x8cfd0000`. **Those ranges cover the candidate high stage and stack
after CE starts.** A placement pass cannot establish stage survival during
bootstrap or a later relay. All live stage code, return paths and diagnostics
must retire before CE can reuse their storage, or preservation must be proven.

`wsegacd.dll` is module 12, TOC `0x8c1415f4`, e32 `0x8c099ac4`, o32
`0x8c099b28`, nominal virtual base `0x01de0000`. Its code section has RVA
`0x1000`, virtual bytes `0x4d73`, stored bytes `0x4e00`, data pointer
`0x8c0aa000`, flags `0x60000020`, and file offset `0x9a800`. It is uncompressed.
The o32 real-address field is `0x03de1000`, further reason to distinguish
nominal addresses from an observed CE runtime mapping.

### Four patch locations and loop behavior

All four reported old halfwords and surrounding sequences match ARMADA. There
is one code-section PC-relative MOV.W reference to the 15,000-ms literal, at
`0x01de1826`. The original branch at `0x01de182e` goes to `0x01de1858`. The
pending-request wait sends any nonzero return, including timeout or failure,
to BIOS abort function 8 at `0x01de1842`; the zero-return path acknowledges
and progresses. Thread A and Thread B's idle wait already ignore wait status.

The proposed BRA delay-slot read is from context `+0x124`. The complete Thread
B loop leaves its r10 value unused on the progress path; later abort use
reloads r10 first. This supports the report's dead-value claim for this ARMADA
thread. SETT plus the existing BT still avoids the extra memory access and
preserves the old taken-branch T state. Neither candidate is applied or enabled;
watchdog and wait-failure behavior remain design questions.

The report's **one EXEC per one-millisecond poll** description is incomplete.
Progress routine `0x01de1a14` loops around BIOS EXEC (`0x01de1a5e`) and CHECK
(`0x01de1a7c`). When CHECK returns 1, it tests the fourth result word against 1
at `0x01de1ab2`. If unequal, it calls **Sleep(5)** at `0x01de1ac6`, then loops
through `0x01de1c12`. K-UI currently returns fourth word 4 while pending. This
path would sleep and continue internally while pending; a one-millisecond
interrupt-thread timeout alone does not set its read cadence. Trace and
implement the CE CHECK contract before making performance predictions or
changing return fields.

Imports were resolved through this kernel's own coredll export directory:
ordinals `0x1f1` WaitForSingleObject, `0x1f0` Sleep, `0x276` SetKMode, `0x274`
InterruptDone and `0x22d` KernelIoControl. The formerly untraced wait at
`0x01de53d2` belongs to a DeviceIoControl wrapper: after GetLastError reports
`0x3e5` (ERROR_IO_PENDING), it waits on the request's overlapped event, supplied
or created with CreateEventW. This is not another directly bound interrupt wait
to replace blindly; successful request completion must signal that event.

### Actual OEM acknowledgement and interrupt mapping

Independent kernel tracing confirms these mappings:

| CE ID | Status register | Enable register | Bit | Kernel ISR |
| --- | --- | --- | --- | --- |
| 20 | `0xa05f6904` | `0xa05f6934` | `0x00000001` | `0x8c02d1f0` |
| 21 | `0xa05f6900` | `0xa05f6920` | `0x00004000` | `0x8c02d158` |

The ISR paths test their status/enable bits, mask the source and return the
respective CE ID. OEM dispatch at `0x8c02cccc` (file `0x1d4cc`) recognizes
IOCTL `0x10000`, reads the input ID and calls `0x8c02c928`. That routine uses
a 24-byte per-source table at `0x8c145ea8`; the ROM copy record maps its initial
contents to file `0x1f584`. If cached pending bits are zero, it reads status
AND enabled-mask, caches the result and writes captured bits to the status
address. It returns the cached result to the output pointer. It does not
manufacture an interrupt cause.

InterruptDone reaches kernel wrapper `0x8c02b670`, then dispatcher
`0x8c02c8fc`. IDs 20 and 21 share callback `0x8c02c5c8`, which ORs the source's
enabled-mask into its hardware enable register and clears its cached pending
bits. There is no matching-event test inside this callback. These observations
do not assume that writing the normal and external status registers has the
same hardware acknowledgement semantics.

Thus unconditional acknowledgement plus InterruptDone after a synthetic timeout
is not an established harmless no-op: it consumes shared software pending state
and rearms real interrupt masks. Keep synthetic progress separate from real
IRQ acknowledgement, or prove the relevant race/ownership behavior before a
live patch. The driver ignores the acknowledgement result on these loop paths;
that does not remove the OEM side effects. This narrows the remaining work to
an explicit completion adapter, CE-safe entry/mapping and checked handoff,
rather than simply applying the four reported halfwords.

## Follow-up: Claude's re-review and miscellaneous BIOS call

The owner's second report is Claude's `Found on re-review.docx`, local source
`/workspace/scratch/e58804339be4/upload/Found on re-review.docx`, SHA-256
`4ca5496cc0e895c888b78f69d855ac3aaea5084cad6c2111aa16eba76d48e828`.
Its miscellaneous-call findings were checked against the same unchanged ARMADA
kernel. This is additional CE prerequisite analysis; it does not alter the SCI
async diagnostic build or establish a CE console result.

### Verified disc-check path and exact memory ranges

ARMADA's helper at nominal `0x01de1290` loads the function pointer at
`0x8c0000e0` and calls it with r4=2 at `0x01de12aa`. Recovery routine
`0x01de1e68` calls that helper and retries negative results after **Sleep(16)**.
On zero, it copies 256 bytes from `0x8c008100` to driver context `+36`; on a
positive result, it zeroes that context region instead. It then reinitializes
the BIOS GD service. After CHECK returns -1, recovery is selected if its
second output word is `0x28`/`0x29` or its first output word is 6; these are
the precise tests, not a single interchangeable status field. The synchronous
INIT helper has a corresponding recovery path.

The 256-byte copy is a **read of `[0x8c008100, 0x8c008200)`**. That range does
not overlap the resident at `0x8c008300`. The same read also occurs during
driver initialization, at memcpy call `0x01de1400`; it is not exclusively an
error-recovery concern.

K-UI's `kui_retail_menu_hook` handles r4=1 and forwards other values, including
2, to the saved firmware entry. The separate overwrite hazard is the work
that forwarded firmware may perform. Flycast's independently retrieved BIOS
model, pinned commit `04669ebe4164bb589e6a2050aa1559724aa94291`,
`core/reios/reios.cpp` blob `0ecde7993bc546bd49f473c5ef40eabfe9b423ab`, models
miscellaneous function 2 by reloading **seven 2,048-byte sectors** beginning at
`0x8c008100`: write range `[0x8c008100, 0x8c00b900)`. That modeled operation
overlaps the resident. This is verified behavior of the HLE implementation,
not a disassembly or console trace proving the exact write extent of the
owner's real boot ROM. No reference implementation was copied.

A CE image service must intercept this request and provide image-consistent
disc status instead of passing it to the physical-drive firmware. Returning
zero with no writes is a candidate only after the boot-information contract
is established: CE reads the block at `0x8c008100`, while K-UI initially loads
IP.BIN verbatim at `0x8c008000`, and the HLE reload uses the disc's starting
sector at the shifted address. The owner bootstrap's final bytes at those
addresses have not been observed. Preserve or construct the correct small
metadata block explicitly without reloading bootstrap code over the resident;
do not assume an unconditional success no-op alone supplies valid metadata.
Validate this in the CE profile while retaining native behavior.

### Timer and DMA ownership checks

ARMADA's `timer.dll` maps physical `0x1fd80000` and writes TMU1: TCOR1 and
TCNT1 at nominal `0x01e31b40` / `0x01e31b48`, TCR1=`0x20` at
`0x01e31b4c`, and TSTR bit 1 at `0x01e31b56`–`0x01e31b58`. A caller scales
the interval by 12,500 at `0x01e31d46`–`0x01e31d50`. These establish TMU1
ownership and interval units, not an independently measured constant
one-millisecond kernel scheduler tick. K-UI's SCIF `native_begin()` resets
TCOR1/TCNT1, writes TCR1=0 and starts that channel; `native_end()` stops it.
That is a concrete resource conflict. A CE profile must keep SCIF out until
timing is moved to a compatible resource; SCI avoids this particular TMU1
conflict without proving the rest of the CE contract.

`ddhal.dll` maps physical `0x1fa00000`; verified transfer paths write CHCR2
`0x12c1` and shared DMAOR `0x8201` (for example nominal `0x01d6a224` and
`0x01d6a232`). One claimed detail is incorrect: kernel DMTE handlers at
`0x8c02d27c`/`0x8c02d28c`/`0x8c02d29c`/`0x8c02d2ac` apply
CHCR AND `0xfffffffb`, clearing **IE, bit 2**, while retaining TE. They do not
only clear TE. Separate DMA channels and a compatible DMAOR low-bit check are
useful facts, but do not by themselves prove safe concurrent ownership,
priority changes or completion handling.

### Other corrections carried forward

ARMADA's TLB refill at `0x8c0124f0` and translation routine at `0x8c01259c`
index their first-level tables using virtual address >>25. This independently
supports the re-review's process-slot hazard: `0x0c000000` selects slot 6
(full slot `[0x0c000000, 0x0e000000)`), whereas K-UI's native alias shortcut
unconditionally treats its lower 16 MiB as physical Dreamcast RAM. Address
interpretation must follow argument role and the active CE mapping. An actual
CE call passing a virtual buffer in that range remains unobserved.

Argument roles differ even within one call. ARMADA's MDL path resolves
IoAllocateMdl, MmProbeAndLockPages and KeFlushIoBuffers, then walks the page
list after the 24-byte MDL, forming/coalescing physical-address/length pairs
at nominal `0x01de2c5e`–`0x01de2c88`. Function 6 copies a pair onto the thread
stack before its calls at `0x01de1bac` and `0x01de1784`: r5 names a virtual
parameter block whose first word is a physical-page-derived destination. The
contiguous command-17 path also uses the first list address. This confirms a
static reason to distinguish argument roles; exact submitted values and the
active MMU mapping still need a console trace.

The report now acknowledges BL, but removing it only in C parameter accesses
does not address the earlier caller-stack pushes described above. The present
absence of a CE reset is not evidence that `guest()` refusal makes the path
safe: normal CE Launch is still disabled and no such boot was tested.

The boot-information readers are also conditional: ARMADA checks byte
`0x8c0080fc` against 1 before reading up to 12 characters at `0x8c0080f0` or
the signed number at `0x8c0080fd` (accepted range 4–99). The kernel debugger
signature check normally uses `0xac008000`, but its machine-class detection
also contains a `0xac004000` selection path. Neither the uploaded kernel,
whose body starts at `0x8c010000`, nor these reads establish actual boot-time
contents below that address. K-UI's stage clears bit `0x20` in IP byte `0xfc`;
that would turn a space into zero, but the original IP image was not supplied
here to verify that byte for all reported discs.

The re-review still describes one EXEC per one-millisecond poll. The verified
ARMADA **Sleep(5)** loop and K-UI CHECK fourth-word mismatch documented above
remain applicable. Timer ownership, masked-call duration and shared DMA state
require their own checks; none is resolved by the four polling halfwords.

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
- Microsoft, US6425125B1, Case 3 ROMHDR/TOC/e32/o32 structure definitions:
  <https://patents.google.com/patent/US6425125B1/en>.
- Microsoft, *PE Format*, export directory/ordinal structure definitions:
  <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>.
- Flycast, pinned commit `04669ebe4164bb589e6a2050aa1559724aa94291`,
  `core/reios/reios.cpp`, `reios_sys_misc` case 2 (BIOS behavior reference):
  <https://github.com/flyinghead/flycast/blob/04669ebe4164bb589e6a2050aa1559724aa94291/core/reios/reios.cpp>.

The generic Microsoft contracts are useful constraints, not independent
verification of the Dreamcast kernels' OEM implementations.
