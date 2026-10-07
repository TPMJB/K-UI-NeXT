# Profile 13 preflight: native memory and independent validation

Historical audit of the initial profile 13 build. The console later reported
six completed stages, zero failures and a 32,600-byte stack watermark, but
the report remained on page 1. That build sampled TMU1 without starting it;
its automatic page rotation and 180-second hardware deadline were therefore
not established. The original host adapter rendered all six pages directly
and did not exercise the timed loop. See
[the clock correction](cdda-preflight-clock-fix-2026-10-07.md) and
[the page 1 hardware evidence](cdda-preflight-hardware-2026-10-07.md).

Audit date: 2026-10-07. Scope: the separate `Makefile.cdda_preflight`
target, `cdda_preflight_main.c`, its read-only storage adapter, and the
linked profile 13 image. Profile 14 has a separate retail observation audit.
These results establish build and host properties; profile 13 console pages
and a measured console stack watermark remain pending.

## Native layout

The audited provisional build uses `BUILD_ID=000000000000` in
`build/cdda-preflight`. Final package identities and publication status belong in `build.json`;
changing the twelve-character identifier changes package bytes.

| Item | Measured value |
| --- | ---: |
| Entry / reservation start | `0x8c010000` |
| Reservation end, exclusive | `0x8c210000` |
| Runtime memory reservation | 2,097,152 bytes (`0x200000`) |
| Initialized payload | 70,552 bytes |
| Runtime file including header | 70,616 bytes |
| `.entry` / `.text` / `.rodata` | 140 / 48,640 / 21,728 bytes |
| `.data` / live `.bss` | 24 / 78,912 bytes |
| Dedicated NOLOAD stack | 65,536 bytes |
| Live BSS end | `0x8c0347e0` |
| Transport AUTO marker | One, payload offset `0x11380` |
| Undefined ELF symbols | Zero |

The three actual `PT_LOAD` ranges are disjoint:

| Permissions | Memory interval, end exclusive | Initialized / memory bytes |
| --- | --- | ---: |
| RX | `0x8c010000..0x8c021380` | 70,528 / 70,528 |
| RW | `0x8c021380..0x8c0347e0` | 24 / 78,944 |
| RW, NOLOAD | `0x8c200000..0x8c210000` | 0 / 65,536 |

The provisional runtime CRC32 is `48f15038`; its SHA-256 is
`bad9ff0632f286d1bbf4ce15076f99f9d31ff4ae757ce9a38c7c3850276e0a10`.
Package checks also require the exact writable/allocated NOBITS stack,
coverage and permissions for every allocated ELF section, initialized bytes
matching their load segments, and executable coverage of retained entry
symbols. A forged initialized or unloaded stack is refused.

## Stack and instruction audit

Fresh SH compilations with the target flags, `-fstack-usage`, and
`-fcallgraph-info=su` supplied the stack graph. Indirect storage, metadata,
progress/cancellation, block-reader and SCI callbacks were bound to the
actual adapter functions. The reachable graph has no recursive path.

| Worst reachable chain | Frame bytes |
| --- | ---: |
| `cdda_main` (test body inlined) | 360 |
| `kui_cdda_preflight_read` | 16,252 |
| `kui_cdda_disc_from_image` | 15,908 |
| `kui_cdda_disc_validate` | 56 |
| `valid_track` | 24 |
| Chain total | 32,600 |
| Total with conservative helper allowance | 32,856 |
| Stack usable after guards | 65,472 |
| Remaining static margin | 32,616 |

The largest retained individual C frame is direct `kui_game_image_open`
(17,140 bytes). Sequential metadata, extent and hashing operations are not
summed as though they were nested. Actual linked unsigned 64-bit division
and remainder helpers use 20 and 28 bytes respectively; their quotient
leaf uses no stack. The 256-byte helper allowance covers these paths.
The allocating named-image opener and selected-audio builder are discarded
by section GC. This is a static bound, not a hardware watermark or proof
about arbitrary interrupts. The host watermark hook is a test stub.

A literal-aware disassembly scan examined 22,064 executable nonliteral
instructions. The only floating-state setup is the existing startup
`lds r0,fpscr` at `0x8c01000c`; no floating arithmetic was linked. The image
has no linked AICA driver, BIOS vector hook, client bridge, heap allocator,
KOS or newlib. It uses pinned GCC 15.2.0 / binutils 2.45.1, the SH-4
freestanding flags, section GC and libgcc integer helpers.

## Runtime and storage contract

This target reads metadata and physical allocation; it launches no program
and never initializes or resets AICA. Its private FatFs configuration is
read-only, exFAT enabled, fast-seek enabled, with one serialized SCI owner.
Filesystem objects and bounded traversal state are private. Directory
discovery is iterative, bounded by depth 8, 128 directories and 4,096
entries. The generated-test subtree is excluded. Explicit configuration
must name a safe absolute path to the same matching complete image.

The descriptor is the supplied 451-byte GDI with SHA-256
`96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803`.
All fifteen backings are required. Physical blocks needed for each file's
bytes are mapped without omitting audio; unused allocation after EOF is
outside this claim. Cluster runs, file coverage, card/partition bounds and
overlap are checked. CLMT construction has bounded seeks and 160-run
capacity. The additional retail readiness gate accepts at most 64 combined
track/extent slots, refusing a valid larger map without writing past bounds.

The pure reader validates raw sync, Mode 1 and FAD headers, whole IP and
boot ranges, and boot nonoverlap before hashing. It hashes 32,768 logical
IP bytes and the exact ISO boot extent, excluding final-sector padding.
Cancellation or failed progress leaves the output unchanged. CRC/SHA report
the bytes present; they do not authenticate an unknown executable against
an external trusted digest. Metadata callbacks are bounded and immutable.

After six successful stages, all FIL/DIR state is closed and cancellation
callbacks are cleared. SCI remains initialized for the read-only report
pages until poweroff; there are no further filesystem/card operations.
Failure shuts storage down once. The 180-second deadline uses unsigned
elapsed ticks below one full wrap and the stated nominal SCI time reference.

## Independent host evidence

`test_cdda_preflight_integration.py --sanitize` passed 36 actual-main
adapter scenarios under strict warnings, ASan and UBSan. The independent
synthetic fifteen-file image retains the original descriptor geometry;
its generated file contents contain no supplied game executable or audio.
Normal execution records 6 stages / 0 failures, 15 stats, one descriptor
read, 22 source-sector reads (3 metadata + 19 hashing), 25 progress calls,
48 cancellation checks, 30 manifest slots and six rendered pages.

Coverage includes missing/truncated/overlapping maps, descriptor/source I/O,
raw sync/mode/FAD damage, IP/ISO/boot errors, early and final cancellation,
progress refusal, malformed physical runs, exact 64-slot acceptance,
65-slot refusal, run-capacity refusal, wrapped deadline and stack-guard
failure. Valid boot-payload corruption changes both reported digests;
padding corruption leaves them unchanged. All failure paths perform no
AICA operation and preserve transactional core output.

Independent Python `hashlib`/`zlib` references for this fixture are:

| Logical object | Bytes | CRC32 | SHA-256 |
| --- | ---: | --- | --- |
| IP | 32,768 | `d57c1f89` | `876039306cb69d5065c4b8497a8d93ae20b812cfa9755581840e69283ceda208` |
| Boot | 5,003 | `6b8ef64e` | `a96b1a6bb251855851081cbbc670a46f21731e70ecf0fb66ab98296511533471` |

`test_cdda_preflight_fatfs.py` passed seven optimized and ASan/UBSan cases
using actual upstream FatFs plus the actual storage adapters: 4 KiB and
128 KiB exFAT clusters, MBR partition, ambiguous discovery, explicit config,
excluded-fixture config and path traversal. An independent exFAT parser
matches every exported run and canonical extent digest, including a
fragmented track. All fifteen files, cluster crossings and partial EOFs
are read and checked; cached-map invalidation and cancellation recovery
are covered. Every whole-card SHA-256 remains unchanged.

The final pure preflight unit suite passed 22,437 checks; storage primitive
mocks passed 1,887 checks. Fresh fixed-ID profiles 11 and 12 are byte-identical
to their delivered packages, as recorded in
`cdda-preflight-preservation-2026-10-07.md`. This audit adds no new console,
listening, retail launch, IRQ sharing or physical cancellation claim.
