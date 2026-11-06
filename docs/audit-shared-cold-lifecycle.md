# Shared cold launch and lifecycle audit

Reviewed source: `91fa7cc6922b1e5274ac0e622fc9e9980763499c`.
The delivered VC2 candidate remains unchanged. This review introduces no
production change and does not qualify another console configuration.

## Confirmed secondary performance omission

`src/core/native_return.c` implements its own bit-at-a-time IEEE CRC32.
`kui_native_return_load()` applies it to every runtime payload byte, in addition
to its bounded header/plan checks. The actual caller is
`src/loader/native_session_worker.inc:native_session_return_transaction()`.
That caller's sector callback correctly starts one SD stream for each
contiguous extent; the omission is CPU verification work, not a missing
multi-block SD read.

This is confined to the explicitly optional `Makefile.vmu-native-session-controls`
family. That family retains its separate 112 KiB worker/324 KiB reservation.
It is not the current 72 KiB compact Background family or the Smash collection
return path. It does not explain VC2's reported white/black screen, and no
change to that optional worker belongs in the current test package.

An external pure-function fixture compiled the exact current private CRC and
existing `stage_fast_crc32.h` with the same integer-only SH `-Os` policy, then
executed their linked instructions with identical authored RAM. The fixture
built neither a historical worker nor an installable module. Both functions
returned the same IEEE CRC in all six bounded cases:

| Input bytes | Current private CRC instructions | Existing shared byte-table CRC instructions |
|---:|---:|---:|
| 0 | 11 | 28 |
| 1 | 85 | 39 |
| 4 | 307 | 64 |
| 16 | 1,195 | 172 |
| 512 | 37,899 | 4,636 |
| 4,096 | 303,115 | 36,892 |

These are actual emitted pure-CRC instruction counts, not elapsed console
measurements. The comparison excludes SD, cache timing and the surrounding
return path. A future isolated session-controls fix should reuse the existing
complete byte-table CRC, retain every payload byte/header/wire checksum and
SD block CRC, and separately prove its linked worker bounds and current
delivery. Do not rebuild or install that old optional ABI merely to close this
secondary audit item.

The source-hashed count report is retained in
[`evidence/audit-optional-session-return-crc-91fa.json`](evidence/audit-optional-session-return-crc-91fa.json).
The external fixture source/decoder inputs and ELF are retained in workspace
`shared-lifecycle-audit/`. They contain no retail game bytes.

## Reviewed shared paths and limits

The following checks found no further supported defect in this review:

- `retail_stage.c:load_sectors()` uses 16 logical-sector chunks and the shared
  `retail_image.c:file_read()` physical-run callback. Each chunk is bounded to
  the current track and uses its stored sector stride. `read_sectors()` stops
  and releases its storage ownership after each chunk, including error paths.
  This is bounded batching; it is not evidence of zero launch delay.
- `native_proof_cache_scope.inc` retains incoming CRC and seven-argument proof
  inputs, confines operand caching to the pure verification call, publishes
  its output/stack before restoring the prior profile, and avoids applying
  the native cache policy to live CE work. This source review does not replace
  its existing linked ABI tests or admit an unknown cache mode.
- The native, disc and CE preparation adapters invalidate both fresh FAT
  windows and their VMU validation/memo cache at identity, write and sync
  boundaries. Metadata read-ahead retains individually verified blocks, and
  uncertain stream teardown does not authorize a cached response. No cached
  receipt alone was found granting write ownership in these paths.
- The apparent missing DMA cache publication in the optional session return
  controller is not a defect: its static transmit/receive objects are linked
  into the worker's P2 mutable-state segment. It must not be “fixed” by
  applying a blanket cache reset before every controller poll.
- `native_return_hardware.c` drains/disarms the named Maple, G1, G2, PVR,
  sort and C2 producers before replacement, while the shared terminal assembly
  uses the current direct cache-array writeback. No additional generic device
  reset was justified by this review.

This is a bounded code/linked-function audit, not full SDK scheduling, physical
DMA/cache behavior, save durability, all-game acceptance or an installed global
rollout. In particular, no additional defect explaining current cold startup
or VC2 reentry was demonstrated here. The separately established VC2
control-call/VMU-loan race has its own source-bound fix and proof.
