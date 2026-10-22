# Armada CE performance isolation controls

These controls are prepared for a later targeted comparison. They are not a
performance fix, a writable VMU, or another required run of the completed 137
read-test pack. Hardware performance is pending.

All three use one helper implementation and the same reservation, sampler,
interrupt mask, DMA fence, checkpoint phase, and 1,200-completion limit.
Only the immutable four-byte diagnostic selector may differ in hook.bin.
The terminal report also prints MODE, FIRST and LAST proof counters.

| Folder | Mode | Extra work after the common sampler and fence |
| --- | --- | --- |
| 01-gate-only | 1 | None; no virtual A2 advertisement or reply |
| 02-proof-only | 2 | Initial and final live ownership proofs; no A2 advertisement, backend or guest RX stores |
| 03-full | 0 | Ownership proofs, read-only VMU backend and bounded guest RX publication |

All stop after the original CE handler returns. Expected AUTO reason 6, phase 2
means the completion limit was reached; reason 5 means an A2 fault was latched.
FIRST/LAST should stay zero in gate-only and be nonzero/equal in healthy
proof-only. A failed final proof is fault 99. Unknown selector values refuse
with fault 98. Keep the selector immutable throughout a cold boot.

If gate-only is slow, investigate the common sampler, mask duration and P2
execution first. If gate-only is smooth but proof-only is slow, the ordinary
controller ownership queries are expensive. If both are smooth while full is
slow, investigate publication and the extra traffic caused by advertising A2.
Proof-only does not create the full A2 request mix, so it is not a timing match
for every VMU command. No timing number has been measured on hardware yet.

The existing Armada image/settings and physical A1 must be held constant for
any later comparison. Each module replaces only KUI/apps/games/ce-probe.kui;
retain a backup of that module. These builds have no virtual save backing.

Reproduce with the pinned toolchain and clean source commit from BUILD.json:

```
source .deps/kos/environ.sh
make -j1 -f Makefile.armada-vmu-a2 ARMADA_A2_DIR=build/perf-gate ARMADA_A2_DIAGNOSTIC_MODE=1 ARMADA_A2_AUTO_STOP=5 ARMADA_A2_AUTO_PHASE=2 ARMADA_A2_AUTO_LIMIT=1200 build/perf-gate/entry.elf
make -j1 -f Makefile.armada-vmu-a2 ARMADA_A2_DIR=build/perf-proof ARMADA_A2_DIAGNOSTIC_MODE=2 ARMADA_A2_AUTO_STOP=5 ARMADA_A2_AUTO_PHASE=2 ARMADA_A2_AUTO_LIMIT=1200 build/perf-proof/entry.elf
make -j1 -f Makefile.armada-vmu-a2 ARMADA_A2_DIR=build/perf-full ARMADA_A2_DIAGNOSTIC_MODE=0 ARMADA_A2_AUTO_STOP=5 ARMADA_A2_AUTO_PHASE=2 ARMADA_A2_AUTO_LIMIT=1200 build/perf-full/entry.elf
```

The reconstructed baseline produces the same eight retained flat binaries as
137 after substituting only the twelve-character display build ID. That binary
equivalence does not make the recovered source tree historically identical.
See source-recovery-armada-137.md and evidence/armada-137-native-equivalence.json.
