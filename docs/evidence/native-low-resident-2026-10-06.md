# Native resident below IP: isolated hardware test

## Observed conflict

Build `d6438f7022ac` still rejects Sonic Adventure's command `0x11` parameter array at `0x8c00b9f4`. The owner supplied Grandia II's actual R5 parameter array at `0x8c00b33c` in the same build. Both are inside the old native reader reservation, `0x8c008300..0x8c00c000`. Sonic's second one-shot scope did not cover the failed invocation. Neither photo identifies whether the hook was consumed earlier, skipped or overwritten.

This diagnostic moves the entire native reader and private service stack below the owner's IP image. Both Sonic-specific scopes and startup instruction patches are disabled. It changes no game instructions and writes no original game files. It is intended for initial testing on the same VA1 console and BIOS used in the reported failures. Hardware compatibility is pending; this is not a universal BIOS or multidisc claim.

## Exact layout

`KUI_RETAIL_LOW_RESIDENT=1` is opt-in. Ordinary builds remain at their original addresses.

| Region | Native ordinary | Native low test |
| --- | --- | --- |
| Resident entry | `0x8c008300` | `0x8c004000` |
| Standard code/data limit | `0x8c00bb00` | `0x8c007800` |
| Background code/data limit | `0x8c00bea0` | `0x8c007ba0` |
| Private stack top | `0x8c00c000` | `0x8c007d00` |
| Guest RAM floor | `0x8c008000` | `0x8c008000` |

The standard private stack remains 1,280 bytes; the background private stack remains 352 bytes. Moving the image changes its absolute references and may let the compiler simplify a redundant range comparison. Linker and call-graph checks must still establish the complete code/data/stack envelope for all four native transports.

The full owner IP image stays at `0x8c008000..0x8c010000`, bootstrap 2 at `0x8c00e000`, and boot SP/VBR at `0x8c00f400`. The GD/menu redirect slots and vector words below `0x8c004000` remain in place. Guest mapping still rejects all addresses below `0x8c008000`, including firmware and the entire relocated reader. It does not simply permit a game to overwrite the reader.

Windows CE keeps its complete legacy layout, including its native-ABI SCIF/IDE readers. These two readers are built separately rather than embedding the relocated native binaries. CE SCI standard and background readers retain their original kernels and private stacks.

The frontend and Python packager accept only the exact native tuples `(0x8c008300, 0x8c00bb00)` and `(0x8c004000, 0x8c007800)`; a CE envelope accepts only the old tuple. The ELF checker selects the corresponding exact stack/data limits and verifies embedded-code identity. A mixed or arbitrary header is rejected.

## Placement evidence and limits

DreamShell source pinned at `4a2b898cbc244b2fb9bd1698b45e5325056232fb` publishes the low loader base `0x8c004000`, syscall reservation boundary RAM+`0x4000`, and IP base RAM+`0x8000`:

- [Loader memory definitions](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/include/main.h)
- [Default low loader address](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/include/isoldr.h)
- [Syscall selection](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/main.c)
- [Native services and IP restoration](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/syscalls.c)
- [Full IP and syscall image loading](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/utils.c)

The published loader restoration ranges bracket this reservation: firmware restoration covers RAM+`0..0x4000`; full IP restoration covers RAM+`0x8000..0x10000`. This is evidence for a candidate placement policy, not proof that every BIOS service's scratch data avoids the interval. [Marcus Comstedt's syscall reference](https://mc.pp.se/dc/syscalls.html) labels the broader RAM+`0..0x8000` region firmware/default exceptions. Custom firmware, persistent multidisc data and every menu/GINSU path have not been established as compatible with this test.

## Admission before installation

The new checks run in the temporary high stage before the first resident copy. They do not consume scarce resident code space.

The original GD handler must be even-aligned, use an exact P0/P1/P2 main-RAM alias, and normalize into `0x0c000100..0x0c004000`. The retained SYSINFO, BIOFONT, FLASHROM and SYSTEM vector targets at `0x8c0000b0`, `0x8c0000b4`, `0x8c0000b8`, and `0x8c0000e0` must be even-aligned code pointers into the same lower RAM interval or a supported P0/P1/P2 alias of nonzero BIOS ROM below physical `0x00200000`. A target inside the planned reservation or an unsupported alias stops before copying. The screen identifies the unsupported vector and target.

Passing the vector checks confirms entry locations only. It cannot prove all dynamic scratch dependencies of retained firmware services. The reachable `NATIVE LOW RESIDENT` line identifies admission in this candidate. Existing GD replacement, read-only storage and destination checks remain active.

## Verification and console test

Local native compilation passed with `BUILD_ID=native-audit`: SCIF ends at `0x8c007610` (496 bytes spare), standard SCI at `0x8c0077f4` (12 bytes spare), IDE at `0x8c007500` (768 bytes spare), and background SCI at `0x8c007ba0` (exactly at its limit). Standard SCI's conservative stack bound is 1,184 of 1,232 available bytes after guards; background SCI's call-graph bound is 220 of 304. Compiled native code contains no remaining literal references to the old resident or private stack bounds. The native instruction audit covers 25,654 instructions; CE covers 28,632.

The complete CE entry payload, high stage and all four resident binaries are byte-identical to the previous ordinary source at the same build ID. Non-forced low-to-ordinary-to-low rebuilds reproduce the exact previous native/CE payloads and the initial relocated payloads. Compiled preflight control flow checks the GD target and all four retained vectors, displays the admission marker, then copies and initializes the resident.

Four focused host test groups pass, including 44,346 UBSan assertions across native standard/background and legacy/low configurations. The 26 package tests, 13 workflow checks and 23,766 caller-snapshot checks also pass locally. Focused tests cover actual compiled mapping and firmware admission predicates, alias/boundary failures, stopping before any copy, exact native/CE header tuples and selected ELF layout. Native compilation checks all four readers, stack budgets, entry/stage headers and embedded blobs. A matching ordinary build is compared against the previous source, and the complete CE payload is compared at a constant build ID. Cached builds must recompile when layout or diagnostic flags change.

Replace the whole `KUI` folder on the SD card and restart with the existing compatible boot disc. First launch the same original Sonic Adventure GDI with the standard reader, then Grandia II. Report whether each reaches its title/gameplay, or photograph the complete stop screen. No 2048 conversion is required. If both advance, try Power Stone 1/2 and the previously working Dead or Alive 2 as regression checks. These runtime tests remain pending until the owner reports results.

This change does not address BIN/CUE prelaunch CRC latency or the reported FMV read/completion stalls. Windows CE still defaults to its background reader.
