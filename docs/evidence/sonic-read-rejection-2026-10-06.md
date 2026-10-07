# Sonic read-request rejection diagnostic

## Hardware evidence

The owner supplied a terminal photograph for build `943a0489c75e` on 2026-10-06. It identifies the standard SCI reader and reports `GD REQUEST REJECTED`. All displayed values are hexadecimal.

| Field | Value |
| --- | --- |
| GD function / command | `00000000` / `00000011` |
| Logical LBA | `00082f22` |
| Sector count | `00000069` (105) |
| Destination | `0cd00000` |
| Last card result | `00000053` |
| Blocks read | `00000370` (880) |

Command `0x11` is DMAREAD (17). Native `guest()` already accepts physical `0x0c`, cached `0x8c` and uncached `0xac` RAM aliases and normalizes them before the resident mapper. This destination is aligned and 105 cooked sectors fit in RAM. The logical sector range is inside the generic GD read limit; the photo does not show the actual track/extent check result or the fourth request parameter.

REQUEST validates parameters and the image range without reading storage. Its rejection does not establish a current SD transport failure. The last-card field comes from an earlier acquire/read/stop operation. `0x53` is outside the complete SD-result enumeration (`0..9`), so damaged reader state is a concrete concern. The photo does not identify the overwrite or display the scoped routine's completion flag.

The previous hardware run's first asset query succeeded with `0x8280` bytes (17 sectors), whereas this request is 105 sectors. Static examination identifies that first asset as a sound driver. This supports a different later asset request but does not by itself prove the precise executed call chain.

## Static investigation and limits

Additional large stack frames exist in the supplied executable. A texture-bank loader has a `0x3810` local area and nested filename formatting; a texture/mipmap helper has a `0x4058` local area plus saved registers. With the original low startup SP, these can overlap the resident reservation. Their execution at the photographed request is not established. The latter helper's top caller has no identified direct incoming branch or embedded pointer. This candidate does not patch either routine speculatively.

Supplied proprietary executable bytes, strings and disassembly remain outside source and deliverables.

## Candidate and console test

The next candidate retains the current scoped temporary stack, exact-owner gate and immutable reader/map checks. It adds rejection-specific evidence to the native fault report: the exact REQUEST validation branch, fourth read parameter, sector size, track pointer/count, guest bounds, last card result, block count and native caller SP/PR. Values are captured before diagnostic video restoration. Request bounds, reserved-word validation and command completion behavior remain in place.

The grouped report has four legend/value pairs:

| Legend | Values in order |
| --- | --- |
| `FN CMD LBA CNT DST` | GD function, command, logical LBA (GETSCD: format), sector count (GETSCD: byte count), destination |
| `REJECT FLAGS BPS LO HI` | Rejection branch, fourth read parameter, configured bytes per sector, guest lower bound, guest upper bound |
| `TRACKS MAP LASTCARD BLOCKS` | Track count, live track pointer, previous card result, blocks read |
| `SP PR` | Original native caller stack pointer and return address |

These are captured values, not a claim that corrupted pointers remain usable.

| REJECT value (hex) | Submission validation branch |
| --- | --- |
| `0` | Accepted; no request rejection |
| `1` | Previous command still owns its handle, or service is executing |
| `2` | Unsupported command/backend |
| `3` | Parameter array cannot be mapped |
| `4` | Read FAD outside allowed range |
| `5` | Zero/excessive sector count or byte-size bound |
| `6` | Nonzero reserved fourth read parameter |
| `7` | Destination cannot be mapped |
| `8` | Track/image range check failed |
| `9` | Other command parameters invalid |
| `a` | Invalid function argument |

Reason `8` identifies the existing image-validation callback's `0/-1` failure. This report does not capture its underlying image-result enum or recompute the manifest CRC.

Rejection metadata is independent of the command's status/error/completion state. REQUEST still performs no SD I/O, and the native assembly records the actual owner PR/SP before switching to the private reader stack. The standard native terminal snapshot reuses the aligned sector cache only after entering a terminal path, which never resumes game I/O. The renderer reads this word-aligned byte storage through a GCC alias-qualified word type. Native menu-return diagnostics group the same nine existing counters into two rows. Background and CE reader layouts and fault screens retain their prior ABI and layout; the high-stage renderer also retains its original implementation.

Replace the entire `KUI` folder on the SD card, keep the compatible existing boot disc and restart. Launch the same original Sonic GDI with the standard reader and photograph the complete `REQUEST REJECTION DETAILS` screen. This is a diagnostic for the new failure, with no claim that Sonic gameplay is repaired.

## Validation

The freestanding native and CE resident/stage/entry builds passed instruction, layout and stack audits with zero unresolved symbols and no floating-point instructions. The standard SCI resident ends at `0x8c00baf4`, 12 bytes below the unchanged `0x8c00bb00` limit; its conservative private-stack bound is 1,184 of 1,232 bytes. No memory reservation or request bound was widened.

Native background, CE standard and CE background resident payloads match the parent candidate byte for byte. Ordinary native stage objects for trace modes 0–3 and CE stage objects for modes 0/3 retain the parent's allocated bytes and relocations; scoped assembly is unchanged. These object checks exclude the intentionally updated embedded native resident payloads.

The five focused GD native/background/CE, background integration and CE background integration targets passed with address/undefined sanitizers (leak checking disabled for the ptraced container). The scoped-stack fixture's 72 synthetic cases passed with undefined-behavior checks. The tests include the exact 105-sector request through all three native RAM aliases, no I/O during submission, full standard/CE completion data, and unchanged rejection/handle ownership behavior. The actual formatter was separately checked at `-O2 -flto -fstrict-aliasing` against expected hexadecimal rows for resident, ordinary stage, CE and background builds.

The GitHub CI run and exact delivered commit are recorded with the candidate archive. Physical-console diagnosis remains pending.
