# Standard-reader prelaunch CRC reuse

The owner reported roughly twenty seconds between pressing A and the first bootstrap screen while testing the small SCI virtual-A2 module. That delay precedes the detached loader, so its post-bootstrap lifetime scans cannot explain this particular interval.

## Source finding

The Games worker's action 58 calls `kui_games_retail_prepare_launch`, which enables launch reporting. Reporting requires the exact executable CRC even for an original raw GDI. The ordinary Standard path previously calculated that CRC in one full source pass, then a cache-bearing native module ran a second full source pass for SHA-256 and optional SDK preflight. Global VMU Off suppresses automatic card preparation; it does not suppress a cache-bearing module's source preflight. The test module's manually embedded card profile is separate from that preference.

For the reviewed 5,460,080-byte Skies Disc 1 executable, each pass requests 2,667 logical sectors through the existing one-sector image API. The IP checksum requests another sixteen. Before metadata and FAT mapping overhead, that is 5,350 sector-read requests; on a 2352-byte raw track the two boot passes read 12,545,568 physical source bytes. FatFs keeps the current track open during each pass, so this is not the older reopen-per-sector problem. The source file is reopened for the later preflight after mapping closes the initial reader.

## Change and boundaries

An eligible SCI/Standard native GDI module with one canonical cache seed now uses the source-read CRC already supported by the X/Y preflight path. Eligibility is shared with `native_cache_prepare` rather than approximated by title or filename. The CRC is published into the manifest only when the entire exact executable source read completed. Optional SDK mapping can still refuse while that complete-read CRC remains usable. If preflight cannot complete, the original complete `extent_crc` path runs; cancellation or a failed fallback never returns a launch image.

For an ordinary successful Skies preflight, the boot source pass count therefore falls from two to one: 2,667 boot-sector requests, or 2,683 including IP; 6,272,784 physical boot-source bytes on a raw track. SHA-256, exact-tail CRC, per-sector header checks, transport CRC, detached reread/checksum and runtime ownership checks remain. This predicts fewer reads, not a measured number of seconds saved.

This change introduces no read-ahead cache, buffer allocation, new filesystem write, manifest wire change or detached-loader change. One-sector read sizes and their order within the retained source pass remain unchanged. It does not revive the withdrawn `8f7ea7925344` batched checksum/read-ahead candidate, whose console failure is recorded in `prelaunch-checksum-batches-2026-10-06.md`.

## Verification

The production native-cache codec/preflight suite passes 39,135 checks under ASan/UBSan. Twenty new real FAT32/exFAT reporting-launch cases, with the actual global VMU Off preference saved and preserved byte for byte, verify raw/cooked SCI Standard reuse, a short read and an I/O error falling back to full CRC, cancellation and failed fallback refusing handoff, malformed/duplicate/absent cache seeds retaining ordinary CRC behavior, and SCIF retaining its ordinary CRC path. Independent Python zlib checks both the manifest and saved report against the exact 3,001-byte source rather than its padded sectors. Filesystem checks pass and source/package bytes remain unchanged. The tests retain one-sector I/O bounds and assert separate preflight/direct-CRC pass counts. All 46 existing real-filesystem X/Y regressions also pass on the final ASan/UBSan binary.

Hardware launch timing for this GUI change and the independent Skies runtime/save investigation remain to be tested. This finding does not explain a later in-game freeze or establish that a missing old save belongs to the currently selected card.
