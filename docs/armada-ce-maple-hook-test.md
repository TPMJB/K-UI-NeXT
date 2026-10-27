<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE Maple completion-entry test

The header test passed on the console: four descriptors, 32 reads, 52 list
bytes and both passes matched. DRIVER and header fault rows were clear. That
GD-triggered snapshot contained controller polling and main-device queries,
not VMU save traffic. Its transcription is in
[evidence/armada-ce-maple-console-e98e4a.json](evidence/armada-ce-maple-console-e98e4a.json).

This test installs a small completion-entry observer in RAM before CE starts.
It counts actual executions and records CPU/call context. It reserves 4 KiB
below the existing OEM driver area. Virtual A2 remains off; no save operation
is required and no memory-card recreation is needed.

## Install and test

1. With the console off, back up `KUI/apps/games/ce-probe.kui` to your PC.
2. Install only the package's replacement `KUI/apps/games/ce-probe.kui`.
3. Cold boot **Original Armada**, using the same original raw GDI and
   **SCI SD, Background loading On, CD music Off, virtual VMU Off**.
4. Stay at the menu for about 30 seconds, then play for about one minute.
   Check controls, sound effects and loading. A physical VMU is optional for
   this entry-count test.
5. Press **A+B+X+Y+Start**. Photograph all three rotating pages:
   **HOOK**, **MAPPING** and **DRIVER**, including the BUILD and bottom rows.
6. Power off and restore the backed-up module before the next cold boot.

If it stops before the menu, photograph the last screen, power off and
restore the backup. The ZIP contains no exact rollback module from your SD.

## Expected report

The HOOK magic is `314b484d` (MHK1), visits should be nonzero, and samples stop
at 32. Visits continue increasing and saturate at `ffffffff`. The first and
last rows describe the first and last of those bounded samples; they do not
cover every frame in the session.

| HOOK row | Meaning |
| --- | --- |
| MAGIC VISITS SAMPLES LIMIT OLD MODE | MHK1, execution count, captured samples, cap 32 and previous mode |
| FIRST EXT SP PR P2 SR OLD MODE | First extension argument, incoming stack/return address, recorder SR and previous mode |
| FIRST TTB PTEH CCR PROCESS RESERVED | First CPU/page-table context and one reserved zero |
| LAST EXT SP PR P2 SR OLD MODE | Most recent captured call context |
| LAST TTB PTEH CCR PROCESS RESERVED | Most recent captured CPU/page-table context |
| RESERVED ZERO FIELDS | Five reserved words; these are not a runtime failure detector |

The regular worker's previous mode is expected to be zero. The recorder SR
has MD set because the bridge explicitly enters kernel mode, then restores
the returned previous mode before resuming the unchanged function body.
MAPPING now expects the deliberately patched entry CRC `576e75e7`, full mask
`1f` and clear faults. DRIVER likewise needs status 1 and mask `1f`.

## Why the bridge is needed

The exact owner's MAPLE worker uses ordinary CreateThread. Source analysis
shows its initial SR.MD is zero; it does not enter kernel mode before calling
completion. A direct P2 detour would fault. The authored 92-byte bridge stays
inside the CE-mapped text section, calls the existing SetKMode import, invokes
the stackless 136-byte recorder, restores the previous mode and replays the
two displaced register pushes. Source evidence proves incoming T is dead
before the original body's first condition-consuming branch.

The bridge explicitly saves volatile GPRs, PR, MACH, MACL and GBR. Its maximum
own stack use is 52 bytes; the reviewed COREDLL mode wrapper brings the maximum
combined use to 68 bytes. No floating-point instruction is emitted. The
recorder follows no driver, stack or DMA pointer and calls no CE, Maple or SD
API. It writes only its fixed 120-byte private log; the bridge uses its own
bounded caller-stack frame.

## Pre-boot placement and publication

Only the exact original kernel/IP/track profile is admitted. The whole kernel
SHA passes before reservation. The stage checks ROMHDR, original 32-byte entry,
O32 text metadata, zero padding, linked stage bounds and both embedded blobs.

- ROMHDR RAMend changes from `8cfd0000` to `8cfcf000` before NK allocates pages.
  OEM globals at `8cfd0000..8d000000` remain outside the reservation.
- Explicit GD physical aliases into the reserved page are refused. Ordinary
  CE allocations exclude it through RAMend; this is not a new general CE
  virtual-to-physical destination validator.
- After bootstrap 2, the relay verifies the full executable CRC, original
  metadata, zero log page and copied recorder again with caches disabled.
- The text virtual size grows from `57a2` to its existing stored size `5800`.
  This exposes the checked 92-byte zero tail without adding a mapped page or
  crossing the next section. Source scans found no HIGHLOW overlap, absolute
  reference, direct branch or existing function coverage in that tail.
- The bridge is copied and checked. The four-byte entry branch is published
  last. Existing relay assembly invalidates caches before restoring boot state.

There is no live CE code patch, arbitrary runtime allocation or native worker
lease in the OEM region. Removing this test module and cold booting restores
the original loading path; owner files on SD are unchanged by the observer.

## Verification and limits

Run `make -f Makefile.armada-maple-test hook-tests`. Host verification includes
40,930 reservation/guard checks under ASan/UBSan, the actual CMP5 production
wrapper, complete byte mutation checks, and 480 closed SH CPU-model cases
across three module bases and both previous modes. The model deliberately
clobbers volatile API registers to check restoration; the exact mode API
semantics are separately supported by hashed owner-source contracts.

```sh
python3 tools/armada_ce_maple_hook_owner_audit.py \
  --startup-dir /path/to/private/Armada-startup \
  --check docs/evidence/armada-ce-maple-hook-contracts.json
make -f Makefile.armada-maple-hook build/armada-ce-maple-hook/entry.elf
python3 tools/check_armada_ce_maple_hook_package.py build/armada-ce-maple-hook
```

Native checks pin every authored bridge/recorder byte and validate both ELF
segments, fixed write domains, embedded blobs, low controller, cache reader,
integer-only instruction policy and CE dispatch stack. Source metadata and
patches are shipped without owner executable bytes or disassembly.

At release, this first console test was pending. A successful count/context capture does
not yet establish DMA ownership, response lifetime or CE virtual-save support.
The next adapter step needs actual completion-time request/response handling;
this build makes no virtual device or save-data changes.

## Console result: build `00cec9b6ee9a`

The owner supplied all three pages. The bridge recorded **6,800 completion-entry
visits** (`1a90`) and reached its 32-sample cap, with the expected `314b484d`
magic. First and last retained samples both show old mode zero and recorder
SR `40008000` (MD set), matching the explicit mode transition. The original
return address `01dd5db6` matches the reviewed completion call site.

The raw extension argument `00030a60` matches DRIVER's raw extension in this
run. Its resolved physical address is `8cf93a60`; this relocation is a reason
to retain runtime mapping checks rather than pin the previous run's address.
MAPPING accepted patched CRC `576e75e7`, and both MAPPING and DRIVER completed
with status 1, mask `1f` and zero fault rows. HOOK's reserved zero fields
remain reserved fields, not a separate runtime failure detector.

The complete transcription is
[evidence/armada-ce-maple-console-00cec9.json](evidence/armada-ce-maple-console-00cec9.json).
This passes the completion-entry/context milestone. It does not yet prove
request ownership or DMA lifetime, capture a VMU transaction, or enable CE
virtual saves. No virtual-card recreation is required.
