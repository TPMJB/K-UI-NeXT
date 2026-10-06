# Prelaunch checksum batches

## Hardware rejection and recovery

Build `8f7ea7925344` was rejected on the console: the user reports that all tested games stop at `INVALID RETAIL MAP`, detail `00000001`, immediately after launcher shutdown. It is withdrawn as a working launch build. The failure precedes detached storage initialization. Detail 1 is the generic manifest-invalid result; it does not identify an IP/executable checksum mismatch.

The recovery restores `src/apps/games_retail.c` byte for byte to the previously working `606f74248d5c` source (blob `d521ce7236bd016debb8224e466b890f99027af5`). This includes its original one-sector checksum reads. The exact native corruption mechanism remains unproved. Both detached packages have identical machine code after their build IDs are normalized, and pinned KOS explicitly writes back the staged source before its cache-off handoff; claiming an unflushed map is not supported. The successful host tests below did not exercise that native handoff.

## Hardware evidence reported by the user

Build `606f74248d5c` enables Power Stone 1 and 2 with fluid gameplay. Games initially takes 5–7 seconds to load, then navigation is fluid. Game preparation before the first bootstrap screen and later bootstrap completion seem slower than older builds.

Sonic Adventure still fails with both original/2048 tracks and standard/background readers. The user sees corrupted graphics at the left; they did not wait to determine whether it reboots. A last visible loader checkpoint has not been established.

## Change

Prelaunch IP and executable checksums use up to 16 logical sectors (32 KiB) per call. A scoped 32 KiB backing-file read-ahead window combines raw-sector reads while the unchanged image reader validates each sector. Total additional temporary heap is 64 KiB, freed on all outcomes before physical mapping. Allocation failure uses the former one-sector path.

Read-ahead ends at the checksum's intersecting track range, including the full last physical sector needed to extract an odd logical tail. It does not read later tracks or a container footer. File changes/close/direct reads invalidate the cache. Cancellation is checked on cached reads, refills and completion; failed or stopped checksums are not published.

Executable/IP CRC semantics, source files, manifest wire format, detached loader, GD command fixes and game-time read pacing are unchanged. This targets preparation before handoff; physical timing still requires comparison on the console.

## Verification

The tests use a 66,537-byte synthetic executable and independent Python zlib CRCs. They exercise cooked GDI, raw GDI, ISO, raw BIN, shared BIN/CUE, 2336/2448-byte tracks and CDI, plus late invalid sector headers/Form2, short reads, I/O failures and cancellation on refills/cache hits. They assert checksum-range read bounds and complete file/mount cleanup. The 34 focused FAT32/exFAT cases and the full 214-scenario launch-preparation suite pass with ASan/UBSan. Successful fixtures pass filesystem-health checks; every active-operation whole-card hash is unchanged. A 66,537-byte executable uses three FatFs reads instead of the former 33; IP uses one cooked/two raw reads instead of 16. This measures call reduction, not physical elapsed speed. Real filesystem images retain identical whole-card SHA256 hashes. Strict SH-4 GCC 15.2 compilation passes; added call-chain stack use is about 100 bytes, with scratch memory on the heap.

## Separate Sonic hypothesis

The normal-success stage relay calls `retail_display_restore` after owner Bootstrap 2 reaches executable entry. That restores the pre-bootstrap K-UI scanout registers, clears 640 × 480 × 2 bytes at the start of VRAM and draws diagnostics. It does not restore the owner's video state afterward. This is a plausible common handoff hazard, not a proven explanation for Sonic or its left-side pattern. It is unchanged in this checksum comparison.
