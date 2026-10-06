# Sonic GD-call observation test

## Evidence and purpose

The previous startup trace `39cf23167a4e` reached point 3 with passed mask 7 on the owner's console. Owner Bootstrap 2 and early executable/video startup therefore completed in that instrumented run. The terminal screen preceded the SDK's GD initializer, so it provided no actual GD-call result. See [the startup trace evidence](sonic-startup-trace-2026-10-06.md) for the supplied image fingerprints and photographed register values.

Static review finds this first-mount sequence compatible with the native reader: BIOS INIT function 3, command INIT24 and EXEC/CHECK, DRIVE function 4, GET_VERS40 and EXEC/CHECK, then a one-sector DMA17 PVD read at FAD45166 into physical address `0x0c698460`. No reader contract fix is established from that review. The SDK reloads its GD dispatcher from `0x8c0000bc` for each of these calls.

The next optional diagnostic records the actual delegated calls and results through first-read completion. It is an observation build that intentionally stops; it makes no gameplay or launch-speed claim.

## Implementation constraints

Mode `KUI_RETAIL_STARTUP_TRACE=2` retains the exact executable-size/IP-CRC/executable-CRC gate and the verified one-shot startup checkpoints. At point 3 it restores the original owner instructions, checks the immutable resident reader again against its selected blob after owner startup, validates the currently installed native GD dispatcher, and installs a high-stage observer through the GD pointer's uncached RAM alias. Resident BSS is legitimately mutable and is not compared. Mode 1 retains its original pre-GD terminal stop. Ordinary native and Windows CE builds exclude the observer.

The observer forwards the real installed handler with the original SR, CCR, stack pointer and R4–R7. Its necessary return trampoline changes the handler's PR; the native handler uses that PR only to return. The separately recorded caller PR remains the owner's actual SDK return address. Mutable observer state and a dedicated private stack use uncached high-stage RAM below the owner's heap. The observer temporarily masks interrupts for bookkeeping, preserving register-bank selection, then restores the caller's exact interrupt state before delegation and return. It does not disable caches or purge the full RAM on each GD call. Parameters and output buffers retain their P1/P2 RAM aliases under the current cache configuration; physical P0 addresses are normalized to P1 for safe CPU observation.

Recording uses each command's defined parameter length. INIT24 has no parameter array; GET_VERS40 has one defined pointer; a PIO16/DMA17 request has four words. DRIVE output comes from R4, while CHECK output comes from R5. Nested calls while bookkeeping is active bypass recording and delegate to the actual handler so its normal lock behavior applies. A native call's actual R0 result is returned unchanged. Rejected later requests do not overwrite an earlier accepted read's token or metadata.

A matching first-read CHECK completion or failure produces a terminal report. A bounded GD-call count can identify repeated polling. It cannot catch an original handler that never returns, nor a later owner wait that makes no further observed calls. These limits must remain explicit in the delivered instructions. Terminal rendering happens only after evidence is captured; intermediate observation does not redraw scanout or VRAM.

## Validation

The tests use synthetic RAM and PVD bytes; proprietary owner files and instructions are excluded from source and deliverables. The observation fixture executes the real GD dispatcher alongside an independent baseline service, checking actual results, output bytes, full caller-frame restoration except R0, both photographed unmasked and subsequent masked SR states, physical/cached/uncached aliases, defined parameter lengths, in-flight token preservation, failure reports, nested busy behavior, and the 4,096-call bound. Additional cases verify code corruption is caught before vector installation.

All 71 focused host cases pass under UBSan: 32 GD observer, 19 startup trace, 9 native relay and 11 CE relay. Eleven workflow hygiene checks and YAML parsing pass. Strict SH-4 C/assembly compile, complete mode-2 stage link, and no-FPU instruction audit pass. Observer state, its private stack and the high-stage BSS end remain below the owner heap at `0x8cf00000`. Largest observer C frame is 116 bytes within its 4 KiB stack. Allocated code/data/BSS sections match the previous source byte-for-byte for native mode 0, native mode 1, and CE mode 2. The delivered GitHub build is recorded in the final package; full native layout/stack/instruction/package checks must pass before delivery. Hardware behavior of this new observer remains unverified until the owner tests it.
