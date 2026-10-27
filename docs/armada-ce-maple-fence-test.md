<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE completion-spanning DMA gate test

The preceding traffic build 2c0fd69e4be7 passed: 4,875 accepted observations,
zero refusals, 22 subunit read-command observations and one write-command
observation. The photographed last command was 10 hex, with an ACK; it was
in OTHER, not the counted write. Payloads and write durability were not
captured. See evidence/armada-ce-maple-console-2c0fd6.json.

This test holds automatic Maple DMA disabled across the original CE reply
handler, then restores it before the original worker's InterruptDone call.
It tests the missing consumption window needed for a future virtual A2
adapter. Virtual VMU remains OFF. This module does not inject responses or
change VMU save data. It writes only the Maple DMA enable register as described
below, its normal bounded CE wrapper frame, and private observer memory.

## Initial console test instructions (historical)

1. Power off and back up `KUI/apps/games/ce-probe.kui` to the PC.
2. Replace only that file with the package's version.
3. Keep a physical VMU in A1 and A2 empty. Cold boot original Armada using
   the same original raw GDI: **SCI SD, Background On, CD music Off,
   virtual VMU Off**. Keep the controller connected throughout this test.
4. Open the load/save screen and load an existing physical save if available.
   Play for about one minute. Creating or overwriting a save is not required.
5. Press **A+B+X+Y+Start**. Photograph all four rotating pages:
   **TRAFFIC, FENCE, MAPPING and DRIVER**, including BUILD and bottom rows.
6. Power off, then restore your backed-up module before the next cold boot.

If it stops early, photograph the last screen, power off and restore the
backup. No virtual-card recreation is needed. No exact rollback module from
your SD is included in the archive.

## Expected result

FENCE magic is `31464c4d` (MLF1). PRE and POST should advance, with many held
windows and confirmed restores. In a normally completed window, enable is
1 before, 0 while held, then 1 after restoration; DMA state is 0 when admitted
and when the original consumer returns. Source stays equal to the admitted
TX bus address and trigger remains 1. Initially busy transfers are skipped
without a hardware write. Historical refusal flags/faults should remain zero.

| FENCE row | Meaning |
| --- | --- |
| MAGIC PRE POST LEASES RESTORES | MLF1, PRE/POST calls, admitted held windows, confirmed enable restores |
| BUSY REFUSED POLL PEAK ACTIVE FLAGS | Initial busy skips, refused phases, maximum idle polls, current unresolved active state, historical reason bits |
| BEFORE ENABLE TRIGGER STATE SOURCE EXPECT | Initial MDEN/MDTSEL/MDST/MDSTAR and expected TX address |
| HELD ENABLE STATE POST ENABLE STATE SOURCE | Enable/state after disable and enable/state/source on POST |
| RESTORE ENABLE STATE PRE MODE POST MODE EXT | Restored enable/state, observed previous modes, latest extension |
| FENCE FAULT WHY AT VALUE EXPECT DETAIL | Most recent refused phase, preserved across successes |

A restore following a failed PRE is also counted; restores can exceed admitted
windows. The ACTIVE field means a disable was attempted and restoration has
not yet been confirmed, not that an acceptable window was proved. A normal
restore may immediately permit another hardware transfer, so restored MDST
may be 1. Restore success is checked from MDEN readback, not MDST.

ABXY terminal return can interrupt a different CE thread while Maple is between
PRE and POST. One final open window or PRE=POST+1 can therefore be the capture
boundary. Assess the many preceding confirmed restores and historical flags;
this capture is not proof of recovery from asynchronous termination. Power off
after the photos.

MAPPING now expects the original completion-entry CRC `7bd7ff55`, because the
hook moved to the worker callsite and leaves that entry unchanged. TRAFFIC
retains its previous whole-session command counts and header observations.
It is sampled before the gate; no new payload reads are added.

## Source-specific wrapper and admission

The exact original kernel/IP/track checks and pre-NK ROM metadata protections
remain in place. The text tail is admitted and widened to its existing stored
size as before. The source worker word at ROM `8c0b3db0` changes from `400bd013`
to `b4f70009`: NOP plus a relative call to the mapped wrapper, with the original
`mov r8,r4` delay slot. Worker 32-byte CRC changes `d48fad4a` to `004742ae`;
the completion prologue remains original. All other worker bytes repeat.
The worker branch is published last with caches disabled, before NK runs.

The 92-byte wrapper saves r8/r9/r10, PR, MACH/MACL and GBR. PRE and POST each
enter kernel mode through the already reviewed SetKMode import and use the
P2 helper's private stack. Between them, the original completion routine runs
on the ordinary CE stack in its previous mode. The wrapper then returns to
the unchanged worker at 5db6 before InterruptDone. Its own CE frame is 28 bytes,
or 48 including the reviewed mode API wrapper. The private C stack is a
separate 1,536 bytes. The helper/workspace uses a 12 KiB reservation below the
unchanged OEM globals: `8cfcd000..8cfd0000`.

Before any MMIO write, runtime requires accepted live-extension/CPU/mapping
witnesses, shutdown zero, and a repeated uncached PTE mapping the extension's
MMIO pointer to physical `005f6c00`. Initial enable/trigger/source/state must
match the admitted configuration. An initial busy state is skipped. After
MDEN=0, enable is read back and idle is polled at most 64 times. Every refusal
after an attempted disable attempts MDEN=1 restoration. POST checks held
witnesses and restores enable even if those checks fail. No MDST, source,
trigger or interrupt-status register is written.

The owner audit finds only initialization MDEN writes, before worker creation;
ordinary completion and the second interrupt thread do not write MDEN. The
added wrapper has no new CE unwind record. Normal C ABI returns during steady
operation are the tested scope. Root-driver unload can request shutdown and
terminate the worker after a timeout, bypassing POST. Exceptions, forced thread
termination and concurrent root-driver unload are outside this test.

## Reproduction and limits

`make -f Makefile.armada-maple-test fence-tests` runs callback failure,
restoration, pairing, bounded polling, saturation, descriptor/cache witnesses,
reservation and actual-assembly bridge tests. With private owner inputs,
reproduce source evidence using:

```
python3 tools/armada_ce_maple_fence_owner_audit.py --startup-dir /path/to/startup --check docs/evidence/armada-ce-maple-fence-contracts.json
```

Source KOS, then build `make -f Makefile.armada-maple-fence
build/armada-ce-maple-fence/entry.elf`. Re-audit extracted native proofs with
`python3 tools/check_armada_ce_maple_fence_package.py /path/to/native-proof`.
The native audit pins the entire mapped bridge, private-stack shim, cache
array leaf and both narrow MMIO callbacks, and checks the retained private
C call graph and integer-only instructions. Private owner executable bytes
and disassembly are not distributed.

At release, this first DMA-gate console test was pending. It adds no virtual-device
advertisement, virtual reply publication, block emulation, SD save write or
journal commit. A hardware pass is a source-specific normal-operation
observation, not universal CE game compatibility or a general lifetime lease.


## Console result: build cbfe36952c04

The owner console run reports **4,151 PRE calls, POST calls, admitted held
windows and confirmed restores**, with every counter equal. Initial busy skips,
refused phases, unresolved active state, historical reason bits and fault rows
are all zero. Peak idle polling was one. The latest MDEN/MDST readbacks were
`1/0` before the hold, `0/0` after disabling, `0/0` after the original completion
returned, then `1/0` after restoring enable. Source stayed at the expected
`0cfd3000`; PRE/POST previous-mode values were both zero.

TRAFFIC also reports 4,151 accepted observations with zero refusals, including
41 block-read and 22 block-write command observations. These counts can include
repeated descriptors and do not establish unique blocks or write durability.
The latest retained request header is `0200010d`, followed by response header
`00010007`; no request or response payload was captured.

MAPPING matches the original completion CRC `7bd7ff55` and full mask `1f`;
DRIVER also retains full mask `1f`. Both fault rows are clear. This passes the
source-specific normal-operation gate test while physical-card traffic is
present. The original CE consumer ran between the paired PRE/POST phases and
all reported enable restorations were confirmed. It does not yet test virtual
A2 advertisement, synthetic replies, emulated reads/writes, SD persistence or
journal commits, or extend the exception/unload/termination limits above.

The complete four-page transcription, supported by the clearer duplicate
FENCE photos, is in
[evidence/armada-ce-maple-console-cbfe36.json](evidence/armada-ce-maple-console-cbfe36.json).
