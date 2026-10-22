# Sonic SDK mount observation test

## Purpose

The owner hardware result for `7f49aec7ba1b` completed the initial 2,048-byte read at FAD45166, with CHECK result2 and zero error words. See [the GD-call evidence](sonic-gd-trace-2026-10-06.md) for the exact photograph transcription and scope of that result. BIOS INIT, INIT24 and GET_VERS40 also succeeded. No native reader contract mismatch is established.

The next unresolved boundary is the SDK filesystem mount. The first PVD result returns to code that checks the full `CD001` identifier, then reads the path table and root directory through the same DMA helper. Static analysis of the exact supplied owner executable identifies a common mount-exit epilogue. A RAM-only checkpoint at `0x8c603d4c` intercepts that epilogue before return: original R4 contains the actual mount result, zero for success or a signed error. Original R0 there contains saved interrupt-mask state and is not the mount result. The 12-byte patch is aligned, stays within this epilogue, and has no independently decoded direct branch targets or 32-bit address literals entering its interior. The original owner instructions are saved from verified RAM; no proprietary instructions or sectors are bundled.

## Observation mode

`KUI_RETAIL_STARTUP_TRACE=3` adds the terminal mount checkpoint to the same executable-size/IP-CRC/executable-CRC gate. Mode1 keeps its early startup stop. Mode2 keeps its first-read stop. Ordinary native builds and Windows CE exclude these diagnostics.

Mode3 returns the first PVD completion to the owner and continues observing delegated native GD calls. It records accepted/completed read counts and the latest accepted read and CHECK outputs. A rejected submission cannot replace an in-flight read's token or metadata. The first completed sector's CRC, full descriptor identifier/version, logical block size, path-table LBA/length and root-directory LBA/length are captured once; later reads do not replace that evidence. PVD metadata is observed, not substituted or used to change the native reader.

The mount checkpoint reports the real R4 outcome and stops. An earlier rejected command, negative GD result, invalid observation parameter/frame, or 4,096-call budget also stops with the latest evidence. A handler that never returns or an owner wait that makes no further observed calls remains outside this diagnostic's coverage. A mount success in this build does not establish a successful gameplay launch.

The GD observer preserves original integer registers except actual ABI return R0, original SR/CCR, original native stack, unchanged FPU state, and handler behavior. Bookkeeping remains in uncached high-stage RAM on its dedicated stack. Only terminal reporting changes video. The terminal checkpoint saves the original CPU frame before the already verified wrapper masks interrupts and disables caches for reporting.

## Validation

All 114 focused UBSan host executions pass: native relay9, CE relay11, startup trace19, existing GD mode2 cases32, mount mode3 cases43. Mount fixtures execute the production GD dispatcher against an independent baseline and verify first-sector silent continuation, multiple read metadata, full caller-frame restoration, physical/cached/uncached aliases, rejected in-flight requests, CHECK completion followed by NOT_FOUND and a new accepted read, first-PVD retention, identifier/type/version observations, call limits and R4 mount success/failure terminal reports. Eleven workflow hygiene checks, YAML parsing and git diff whitespace validation pass.

Strict SH-4 C/assembly compilation, complete mode3 stage link, allocated-section relocation/byte isolation and instruction audit pass. The linked stage contains8,816 audited instructions, with only the explicitly permitted bootstrap FPSCR setup. Observer before/after/report C frames are16/40/116bytes within the4KiB private stack. The stack occupies P1 `0x8ce10b80..0x8ce11b80`, observer state starts `0x8ce109e0`, and stage BSS ends `0x8ce3ece8`, below the supplied owner's heap at `0x8cf00000`. All allocated C/assembly sections and their relocations match the exact parent source for native modes0,1,2 and CE mode3. The delivered GitHub build is recorded in the package; its full native layout/stack/instruction/package checks must pass before delivery.

Test sectors and executable fixtures are synthetic; the owner files and photo remain outside source and deliverables. The new mode needs an owner console test.


## Owner hardware result: `c3fc640f1291`

The owner supplied the expected terminal photograph on 2026-10-06. All displayed numbers are hexadecimal:

| Field | Value |
| --- | --- |
| Calls, function, command/token, result, caller PR | `0000005c`, `00000001`, `00000017`, `00000002`, `8c648e38` |
| Mount result, accepted reads, completed reads | `00000000`, `00000015`, `00000015` |
| Path-table LBA, bytes | `0000afda`, `0000001a` |
| First PVD CRC32, complete descriptor identifier/version, sector size | `ba3adfae`, `00000001`, `00000800` |
| Root-directory LBA, bytes | `0000afdc`, `0000a000` |
| Last read command, FAD, count, physical destination, token | `00000011`, `0000b085`, `00000001`, `0c698460`, `00000017` |
| Last CHECK result, error1, error2, bytes, ATA | `00000002`, `00000000`, `00000000`, `00000800`, `00000000` |

The SDK mount returned success (R4=0), with all 21 accepted reads completed and no observed failure. The full primary descriptor identifier/version and 2,048-byte block size were valid. Root directory metadata was accepted by the SDK. The photographed PVD CRC agrees with the previous instrumented run, although the supplied startup bundle still contains no original sector bytes for independent disc comparison.

This confirms SDK filesystem setup completes in this instrumented run. It does not establish correct asset reads after mounting or a successful gameplay launch. The next diagnostic boundary should cover subsequent startup/file access and hardware initialization, rather than changing mount or sector handling without evidence.
