# Sonic scoped stack correction test

## Evidence

The owner hardware test `c3fc640f1291` returned SDK filesystem mount success, with all 21 reads completed and no observed read error. The full primary descriptor identifier/version and 2,048-byte block size were valid. See [the mount evidence](sonic-mount-trace-2026-10-06.md) for the photographed values.

Static analysis then found a concrete later stack collision in the supplied MK-51000 V1.004 executable. The owner Bootstrap2 loads its final entry SP as `0x8c00f400`; its CRT leaves that SP in place. A startup routine at `0x8c094c88` allocates `0x4004` bytes plus 24 bytes of saved registers. Accounting for the verified calling chain predicts SP `0x8c00b388`, with nested saved-register writes reaching `0x8c00b330`. This intersects the loader's protected resident reservation. The existing SCIF map places the resident manifest in this span, including active extent records. Merely invoking the nested SDK helper writes through the allocated stack; an eventual large-buffer fill is not required for an overlap.

This establishes a latent incompatibility. The photograph stops before this routine, so it does not prove that the original hardware stall reaches it. Late audio G2 waits precede the routine and remain possible earlier failure points. No assumption about a persistent free high-RAM region is needed: later owner scratch and heap usage makes permanently moving its stack inappropriate without a separate reservation audit.

## Test behavior

The separate opt-in `KUI_RETAIL_SONIC_STACK_TEST` flag is off in ordinary builds and excluded from Windows CE. Only the exact supplied executable-size/IP-CRC/executable-CRC combination activates the RAM-only hooks. No original game file, Bootstrap2 stack literal or VBR literal is changed.

Two one-shot late audio helpers, `0x8c110b00` and `0x8c1107cc`, are observed for G2 FIFO bit `0x20` and bit 0 becoming clear. Each bounded check restores the original instructions before resuming the original helper. A timeout stops with a diagnostic. A successful check preserves the owner CPU/cache context and does not redraw video. These checks cover the first invocation of each helper, not every subsequent bus wait.

A pre-read hook at `0x8c095b20` checks the first asset load before it can write into the high-stage reservation. It requires a successful size query, a nonzero handle and a nonzero size whose sector-rounded destination remains below `0x8ce00000`. The scoped routine requires this guard to have passed first; an unexpected startup order stops visibly.

The scoped hook intercepts the large-buffer routine before its original prologue or allocation. It restores every remaining hook window and calls the actual owner routine on a dedicated 32 KiB temporary stack, separate from the 4 KiB diagnostic bookkeeping stack. The owner caller's original stack and saved frames stay in place. The return trampoline captures the actual returned CPU frame and restores the original SP and return address; actual returned registers and SR are retained, and the wrappers do not touch FPU state. The routine clears the public SDK work-buffer pointer before its epilogue. Internal flash state retains a scratch pointer, as it also does with the original local stack, so the dedicated backing array is not reused for unrelated diagnostic work after return. A subsequent flash-operation initializer replaces that state.

RAM from `0x8c008000` through the selected resident limit is snapshotted before the scope and compared after it; the service stack is excluded. A difference is reported as an observed change during the scope; it is not automatically attributed to stack damage, since an unexpected legitimate mutation could also trigger this diagnostic. The audited owner scope uses flash/profile and sound commands, not the GD service. Firmware-private scratch behavior outside the supplied owner code is not established from static analysis.

After successful return, the normal game startup continues. This is a candidate correction test, not an intentional successful-mount stop. Only an observed fault or bounded wait failure claims the display. A handler or firmware operation that never returns, or a later wait outside these hooks, cannot be caught by this test. Other images and games retain their existing behavior.

## Validation

All 150 focused UBSan executions pass: 36 scoped-stack cases, native relay 9, CE relay 11, startup trace 19, GD mode 2 cases 32 and mount mode 3 cases 43. The new fixtures verify the exact image gate, four restored hook windows, bounded G2 waits, asset-query ABI and destination limits, P1/P2 original stack aliases, actual returned CPU state, private stack bounds/canary and resident mismatch reporting. Eleven workflow hygiene checks, YAML parsing and whitespace validation pass. Independent C/assembly review passes.

Strict SH-4 compilation and complete stage linking pass. All allocated sections and relocations match the parent source for ordinary native modes 0/1/2/3 and CE modes 0/3; the CE comparison enables the new flag to verify exclusion. The candidate has 8,360 audited instructions with only the permitted bootstrap FPSCR setup. C checkpoint/return/report frames are 56/40/124 bytes. The bookkeeping stack occupies `0x8ce14440..0x8ce15440`, the owner stack occupies `0x8ce15440..0x8ce1d440`, and stage BSS ends at `0x8ce4a5a8`, below the owner's later heap at `0x8cf00000`. Successful paths preserve FPU state and video; terminal report rows fit without wrapping over the CPU evidence.

The delivered package records the exact GitHub build and checksum verification. Fixtures are synthetic. Proprietary owner bytes, instruction windows, disc sectors and images are excluded from source and deliverables. Hardware gameplay remains pending the owner test.
