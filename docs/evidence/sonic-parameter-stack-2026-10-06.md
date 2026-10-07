# Sonic parameter-stack collision

## Hardware evidence

The owner supplied a photograph of build `a74e53af030c` on 2026-10-06. The standard native reader stopped with `GD REQUEST REJECTED` and rejection branch `3`.

| Current field | Hex value |
| --- | --- |
| Function / command | `00000000` / `00000011` |
| Rejection branch | `00000003` (parameter array cannot be mapped) |
| Sector size / guest bounds | `00000800` / `8c008000..8d000000` |
| Track count / live track pointer | `00000003` / `8c00b2fc` |
| Previous card result / blocks read | `00000000` / `00000370` |
| Original native caller SP / PR | `8c00b9f0` / `8c648d7a` |

Function, command, rejection, SP and PR describe the current call. The displayed LBA `82f22`, count `69` and destination `0cd00000` are retained from an earlier READ: parameter mapping failed before those fields could be updated. FLAGS was freshly cleared before mapping; it is not a successfully read fourth parameter.

## Confirmed collision

The recorded PR identifies the supplied owner's SDK wrapper at `0x8c648d36`. Its 20 local bytes and 12 saved bytes place the 16-byte parameter array at original SP plus four. The firmware stub tail-jumps without changing SP. Consequently this call's parameters occupy `[0x8c00b9f4, 0x8c00ba04)`.

Those addresses exceed the guest floor, but lie inside the protected resident reservation. The delivered SCI reader's manifest occupies `0x8c00b200..0x8c00ba7c`; its track pointer matches the photograph. The game wrapper has written its four parameters there before submitting the request. The reader's assembly entry also saves 36 bytes below the game's current SP, beginning at `0x8c00b9cc`.

Allowing parameter reads from this reservation would not repair the preceding memory writes. Both guest-mapping guards remain unchanged.

## Ancestor investigation

Static analysis identifies a reachable candidate chain:

`0x8c09b3f0 -> 0x8c09b45c -> 0x8c604c5e -> 0x8c604dc4 -> 0x8c648d36`

Its frame sizes total `0x3994` bytes (`0x3818 + 0x120 + 0x20 + 0x1c + 0x20`). This fits the photograph if the outer routine entered at `0x8c00f384`. However, the SDK wrapper is shared by multiple game paths; its PR and stack depth alone do not uniquely establish that ancestor. No additional game routine is patched on this inference.

The next diagnostic captures saved stack words before writing its cache snapshot or restoring video:

| Offset from captured SP | Address in this run | Expected value for the candidate chain |
| --- | --- | --- |
| `20` bytes | `0x8c00ba04` | `0x8c604e50` |
| `36` bytes | `0x8c00ba14` | `0x8c604c98` |
| `60` bytes | `0x8c00ba2c` | `0x8c09b4de` |

These are raw bounded stack words, not an automatic backtrace. A complete scan of the supplied executable found only one direct caller of `0x8c09b45c`, at `0x8c09b420` inside `0x8c09b3f0`, and no embedded pointer to it. Matching all three words therefore identifies that candidate through its static call graph. All three slots lie below the reader's private stack in this run. The deeper saved word at `0x8c00bb54` could already have been overwritten by the reader's private stack and is deliberately omitted. The parameter address is derived only for this identified wrapper; it does not come from accepting the unmappable parameter array.

Supplied executable bytes, filenames and disassembly stay outside source and deliverables. The current scoped correction for the earlier startup routine remains in place.

## Console test

Replace the entire `KUI` folder on the SD card. Restart with the existing compatible boot disc, launch the same original Sonic GDI with the standard reader, and photograph the complete `SONIC CALLER STACK` report. This candidate identifies the enclosing routine for a targeted stack correction; it does not yet claim to repair the collision.

The report has two legend/value pairs:

| Legend | Values in order |
| --- | --- |
| `FN CMD REJECT SP PARAM` | Current function, command, rejection branch, original caller SP, derived parameter address |
| `PR S20 S36 S60` | Current caller PR, raw stack words at SP plus 20, 36 and 60 bytes |

Stack words and PARAM remain zero unless PR is `0x8c648d7a`, rejection is `3`, and SP is word-aligned inside the P1 interval `0x8c008000..0x8cffffc0`. The upper bound leaves a full 64-byte read window within RAM. An unsigned interval check rejects P0/P2 aliases, addresses below the IP floor and wrapped/high values. The three volatile alias-qualified word loads occur before a compiler memory barrier and every cache snapshot store. No owner memory is written and no SD I/O occurs.

The native report intentionally omits stale LBA/count/destination and the previous report's optional metadata to fit the existing reservation. CE/background fault reports and all menu-return counters retain their previous behavior. Both diagnostic marker phrases share one display line.

## Validation

The freestanding native/CE resident, stage and entry builds passed layout, instruction and stack checks with zero unresolved symbols and no floating-point instructions. The standard SCI resident ends at `0x8c00baf4`, 12 bytes below the unchanged `0x8c00bb00` limit. Its conservative private-stack bound remains 1,184 of 1,232 bytes. Guest validation, core GD service, native entry assembly and scoped-stack code are unchanged from the prior validated candidate.

The production-derived caller fixture passed 14,066 checks with UBSan, `-O2`, LTO and strict aliasing. It covers the photographed SP, range endpoints, alignment, aliases, wrapped values, PR/rejection guards and a cache snapshot overlapping its input words. An independent reviewer reran the fixture and checked the nine-field mapping. Actual SH LTO disassembly confirms all three owner-word loads precede the first cache write and video restoration.

Native background, CE standard and CE background resident payloads match the parent byte for byte. Ordinary native stage modes 0–3 and CE modes 0/3 retain allocated bytes and relocations; scoped assembly is unchanged. The workflow runs the new fixture for Sonic-scoped diagnostics; its existing console-only path gate now admits that fixture. All 11 workflow-hygiene checks and whitespace checks passed.

The delivered archive records the exact GitHub CI run and source commit. Physical-console confirmation of the enclosing routine is pending.
