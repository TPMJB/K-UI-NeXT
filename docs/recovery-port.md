# Recovery port: first backend gate

**2026-09-23 implementation update:** the independent first-pass/repair worker,
FatFs durable journal, explicit UI actions and PC verifier are now implemented.
See [salvage-worker.md](salvage-worker.md) for the shipped behavior, bounded
limits, host fault tests and pending hardware acceptance. The earlier design
and provenance below remain the rationale; they are not a statement that the
worker is still missing. Normal capture and its accepted reader remain unchanged.

Status: the checksum/sector helpers and their host tests are implemented.
The Mode 1 checker now powers the explicit [Advanced CRC saved-file scan](advanced-crc-scan.md),
which checks an existing completed job and writes a separate suspect-sector report.
This is diagnosis, not optical recovery, zero-fill, or sector reconstruction.
The CRC replacement helper remains a tested backend for the future salvage worker.
Healthy acquisition and its current hash/EDC fast path are unchanged.
Automated pass/fail comes from the associated CI run; console scan acceptance
and hardware recovery acceptance remain separate later gates.

## Selected original code and provenance

Source repository: [TPMJB/K-UI_DS](https://github.com/TPMJB/K-UI_DS), pinned at
[2a5309298dde8fb100da1e2e4e10517695c9780f](https://github.com/TPMJB/K-UI_DS/tree/2a5309298dde8fb100da1e2e4e10517695c9780f).
The maintainer authorized reuse of independently authored contributions.

| New file | Selected source | Origin |
| --- | --- | --- |
| `src/core/recovery_crc.c` | `matrix_times` and `gd_crc_replace` from `applications/gd_ripper/modules/checksum.c` | Added by TPMJB in [102a59f5](https://github.com/TPMJB/K-UI_DS/commit/102a59f523c684c55c88fc64f3eb1d5bf2263178), the targeted-recovery contribution. |
| `src/core/recovery_sector.c` | EDC/GF table initialization, `parity_ok`, BCD address checking, `gd_check_sector_edc` and `gd_check_sector` from the same file | The separate checksum files were added by TPMJB in [77186c20](https://github.com/TPMJB/K-UI_DS/commit/77186c2043a2ee8a8c33903f3d9f3ed6a3c8e306); validation was split/refined in [b90e8ff4](https://github.com/TPMJB/K-UI_DS/commit/b90e8ff4d17d686c0b306f71e58acf6a587757ca). |

The GitHub file history and introduction diffs were inspected. This is a
selection of the original helpers, not an import of the mixed `module.c`,
DreamShell drive wrappers, GUI, filesystem integration, or contributor
agreement. The selected code is carried under K-UI-NeXT's GPL-3.0-only license
with the source identification retained in each new file.

The port changes names and includes, adds an explicit byte-count/null guard,
and refuses expected FADs whose minute representation would wrap. It also
explicitly rejects nonzero Mode 1 reserved bytes, even with regenerated valid
parity, as required by
[ECMA-130 section 14.4](https://www.ecma-international.org/wp-content/uploads/ECMA-130_2nd_edition_june_1996.pdf). It keeps
GD-ROM's extended minute representation (for example, FAD 549150 uses minute
byte `0xc2`); it does not impose a 99-minute CD limit. The accepted input range
is FAD 150 through 719999, the last representable minute/second/frame tuple
used by this helper. This range is a format guard, not a claim about drive
capacity. The original bounded lookup tables remain lazily initialized for
one serialized worker.

No `ds.h`, zlib, KOS, filesystem or optical-call dependency remains in either
helper. The public declarations are in `include/kui/recovery_checks.h`.

## Contracts

**CRC replacement** updates a finalized IEEE CRC32 when an equal-length region
changes and the number of bytes after it is known. It supports a 64-bit suffix
length. The caller must have a trustworthy original whole-track CRC and
old/new region CRCs. The algebra does not prove that the baseline is authentic,
that non-target data stayed unchanged, or that a write reached the card.

**Sector validation** checks an exactly 2352-byte raw Mode 1 sector's sync,
expected FAD, EDC, the zero-reserved field and P/Q parity. The returned flags
distinguish sync, address, EDC, ECC, reserved-field errors, unsupported mode and
invalid input. Mode 2 is unsupported. The caller must supply data-track
context; arbitrary audio cannot be classified from content alone and is not
validated here. The helper reads bytes without changing them; it does not synthesize
ECC or reconstruct lost data. Audio recovery will need its separate agreement
and confirmation-record policy.

## Tests

Run `make test-recovery`; it is also included in `make test`.
The Makefile generates `build/recovery-vectors.inc` from
`tests/make_recovery_vectors.py`. No game sectors are committed or required.

The vector generator is independent of the production helpers:

- Synthetic payloads cover low and high-density addresses, extended minutes
  and the representable upper boundary. Their EDC uses bit-at-a-time arithmetic.
- P/Q bytes use polynomial division with two remainder registers for
  `g(x)=x^2+3*x+2` over GF(256), with field polynomial `0x11d`.
  Production validation instead uses the original syndrome recurrence and
  inverse lookup table.
- Python's standard-library zlib supplies full-sector CRCs; generated comments
  also record each synthetic sector's SHA-256.
- Large-suffix CRC vectors use normal-polynomial modular multiplication for
  `0x104c11db7`, independently of production's reflected zero-byte matrices.
  The vectors cover distances above 4 GiB, bit 63 and UINT64_MAX.

The C tests compare repeated, overlapping, first/middle/final, whole-message
and empty replacements with a direct bitwise CRC recomputed over all bytes.
They also check compatibility with the existing `kui_crc32`, no-op changes,
large suffixes, exact length/FAD bounds, unsupported modes, altered addresses,
payload/EDC corruption and every byte outside EDC coverage in the reserved/P/Q
region. A parity-only defect must not pass just because EDC is unchanged.
An additional synthetic fixture has a nonzero reserved byte with regenerated
valid P/Q and unchanged valid EDC; only the new reserved-field flag may reject it.

These tests establish the pure helpers' behavior. They do not establish
durable recovery, corrected-sector accounting, optical recovery effectiveness,
or a complete damage-recovery app.

## Next dependencies

Follow [salvage-plan.md](salvage-plan.md). The read-only saved-file scanner is now implemented separately, including
FAT32/exFAT fault injection and small synthetic console fixtures. The next
salvage gate is the state/record codec and interruption model, then the separate
first-pass and recovery worker, FatFs adapters, verifier integration and UI.

The legacy targeted worker requires full-sized raw tracks and an immutable
full-track CRC baseline. Today's truncated normal captures cannot be passed
directly to it. A first-pass producer and explicit unresolved-target metadata
must exist first. Normal checkpoint formats do not encode holes.

Every optical request must continue to use the existing bounded adapter.
A fatal abort/reset requirement stays fatal; the old automatic drive-reset
loop must not bypass that guard. Healthy capture stays unchanged.
