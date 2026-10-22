# Sonic Adventure startup checkpoint diagnostic

## Owner image and observations

The owner supplied IP.BIN, 1ST_READ.BIN and a three-track GDI descriptor. The image identifies as Sonic Adventure US MK-51000 V1.004. IP.BIN is 32,768 bytes, CRC32 `22de24d8`, SHA256 `970ae87bdf5d921c3861f1ead4ce1613d5ca44e3fdc81a8fed2f6445203ec491`. The executable is 6,751,168 bytes, CRC32 `73f4277b`, SHA256 `8a65a81fb0f862da473c0e3489c063defb4dac25b9ee382d13108cd59635b86e`. Supplied proprietary files and disassembly are not included in source or deliverables. The bundle's JSON purpose text names an earlier DOA2 investigation; it is not a Sonic hardware observation.

Video-handoff build `df8d99f4f7c8` still produces left-edge white bars that fade to black. The final loader text precedes owner Bootstrap 2, but the subsequent native relay is silent. Those lines do not locate the stall. The owner corrected the launch-delay report: regular raw GDI starts smoothly; the earlier slow image was BIN/CUE.

## Static audit

The supplied Bootstrap 2 establishes CPU state, clears `0xac00fc00..0xac00ffff`, and calls the executable. It contains no GD or PVR accesses. Sonic's stack fill covers `0x8c00c000..0x8c00f3ff`, above the current native resident and protected stack ending at `0x8c00c000`. Its BSS clears are also outside the resident. No early memory collision was demonstrated. A later main-exit path loads an executable at `0x8c008300`; its existence does not establish that early startup takes that path.

Before SDK disc initialization, video setup includes a scan-counter zero-to-nonzero wait, G2 FIFO bit `0x20` clear wait, and vertical-blank event wait. These are concrete bounded-probe candidates. The first SDK GD initialization and PVD read use commands supported by the current native service; unsupported streaming or interrupt completion is not established as their failure cause.

## Diagnostic implementation

`KUI_RETAIL_STARTUP_TRACE=1` enables the diagnostic only in the native high stage. The default is zero, and CE excludes it. All three executable-size/IP-CRC/executable-CRC gates must match. Four one-shot 12-byte stubs are installed in the loaded RAM image only after original-entry restoration, complete executable CRC comparison, and resident verification. Original instructions are saved from verified RAM rather than bundled.

| Point | Owner address | Observation |
| --- | --- | --- |
| 0 | `0x8c6082d4` | SPG_STATUS at `0xa05f810c`, masked by `0x1ff`, reaches zero then nonzero |
| 1 | `0x8c6083b0` | G2 FIFO at `0xa05f688c`, bit `0x20` clears |
| 2 | `0x8c6085bc` | ISTNRM at `0xa05f6900`, bit `0x08` asserts |
| 3 | `0x8c603d78` | Entry to the SDK GD initializer; intentional terminal stop |

The G2 checkpoint is at the common poll entry, avoiding an existing branch target inside an earlier possible stub location. The checkpoint sequence must be 0, 1, 2, 3. Each first-three checkpoint restores its original instructions before probing and continuing. Each condition has a budget of 3,000,000 register samples, not a calibrated time duration. Original owner code resumes and performs its own wait afterward, so probes can affect timing and do not establish an unperturbed failure location conclusively.

Wrappers preserve integer CPU state, use no floating-point instructions, publish dirty RAM before disabling caches, and restore the owner's cache mode and frame. A dedicated 4 KiB stack remains in the high stage below Sonic's heap starting at `0x8cf00000`. Successful intermediate checkpoints perform no scanout-register or VRAM writes. A timeout or final GD checkpoint snapshots register evidence before restoring the diagnostic display. The terminal screen reports point, passed mask, CPU/cache state, first/last values, sample counts, and current scan/G2/ISTNRM values. It intentionally stops rather than launching gameplay.

## Validation and limits

The production C implementation passes 19 targeted startup cases under UBSan, plus 9 native and 11 CE relay cases. Tests use a wholly synthetic executable independently adjusted to the target CRC, shared fixed-address P1/P2 aliases, and exact patch/restoration checks; they contain no owner instructions. Tests cover independent gates, prepatch failures, stack guards, ordered checkpoints, bounded timeouts, terminal reports, and unchanged remaining executable/resident bytes. Eleven workflow hygiene checks pass.

Strict SH-4 C/assembly compilation, a temporary native trace-stage link, and its instruction audit pass. That link places the trace stack at `0x8ce10020..0x8ce11020` and the stage BSS end at `0x8ce3e188`, below the heap. Individual trace C frames are at most 68 bytes. Tracing-disabled native and CE allocated sections match the previous source byte-for-byte. GitHub must still complete the full native build, layout, stack, instruction and package checks before delivery. Physical-console behavior is not yet verified.

The workflow enables a clearly suffixed startup-trace artifact for the diagnostic commit or explicit dispatch flag and suppresses its release-asset promotion. The delivered test should use the original raw GDI from the supplied bundle with the standard reader. Photograph the final diagnostic; if the bars return without it, report that outcome and waiting time. This is a localization test, not a Sonic gameplay fix or a BIN/CUE launch-speed change.

## Hardware result for `39cf23167a4e`

The owner supplied a photograph of `FIRST SDK GD INIT REACHED`, point `3`, passed mask `7`. In this instrumented run, owner Bootstrap 2, executable startup, and all three observed hardware waits completed. This deliberately stops before the SDK initializer executes its GD calls; it does not demonstrate that disc initialization or a sector read succeeded.

| Field | Photograph value (hexadecimal) |
| --- | --- |
| SR / VBR / CCR | `60000100` / `8c00f400` / `00000105` |
| Scan first / last / reads | `00003d16` / `00002601` / `0000102e` |
| G2 first / last / reads | `0000000e` / `0000000e` / `00000001` |
| PVR first / last / reads | `00000038` / `00000038` / `00000001` |
| Current scan / G2 / ISTNRM | `00003955` / `0000000e` / `00001038` |

SR's interrupt mask is zero at this checkpoint. The supplied executable clears IMASK immediately before SDK initialization; its first BIOS INIT call precedes the SDK's later interrupt-masking block. Any subsequent call observer must therefore restore the exact original interrupt state before forwarding the real handler, rather than assuming all first-mount calls arrive masked. Static review of INIT, command initialization, drive status, version response, and the first PVD read found no proven native-service contract mismatch. Further observation of actual call results is required.
