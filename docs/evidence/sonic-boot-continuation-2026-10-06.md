# Sonic boot continuation test

## Evidence

The `37c7815d8c41` hardware result identified SCI with the standard reader. The scoped startup routine returned safely. Reader code/RO CRCs matched, while GD dispatch and video-pacing counters changed. The former whole-memory guard stopped on normal mutable state; see [the reader-state evidence](sonic-reader-state-2026-10-06.md).

The temporary stack remains necessary for the statically verified low-memory overlap. This candidate changes only the integrity policy surrounding that scope, preserving live reader counters and cache state.

## Guard behavior

The existing exact-owner gate, four one-shot RAM hooks, first asset-size check and bounded first audio waits remain. Only the large-buffer routine receives the private stack; the actual returned CPU state and original caller SP/PR are restored after success. Original game files are not written.

Before calling the scoped routine, the stage validates its selected resident's code and BSS bounds from the existing entry metadata. Within those BSS bounds it searches aligned candidates for the exact prepared manifest and requires exactly one match. The map address, shape and bounds are pinned before delegation. Standard readers use the complete manifest; background readers use the compact 64-slot shape, with validated slot counts.

After return, the guard compares the IP/code prefix and pinned manifest against the entry snapshot through both coherent P1 and uncached P2 aliases. It does not purge or restore reader data to make the comparison pass. A missing/ambiguous map, invalid range, changed code or changed map stops with a diagnostic and precise detail. Mutable service counters, pacing state and sector-cache contents may evolve and remain in their actual returned state.

Successful immutable checks allow Sonic to continue into normal startup. This is a boot candidate, with no deliberate stop merely because a mutable counter changed. A subsequent game hang or another owner stack collision outside this scope remains possible; console gameplay is pending.

The low reader ABI and assembly wrappers remain unchanged. Ordinary builds, unrelated images and Windows CE exclude the opt-in correction. Proprietary owner instruction bytes, executable files and disc sectors are excluded from source and deliverables.

## Console test

Replace the entire `KUI` folder on the SD card, keep the existing compatible boot disc, and restart. Launch the same original Sonic GDI with the standard reader. If it boots, check the opening, title/menu and briefly enter gameplay. Photograph any diagnostic; report bars or a hang if no diagnostic appears.

## Validation

All 72 focused UBSan cases pass. The fixtures cover mutable counter/cache changes continuing with actual returned CPU state; immutable code, IP or manifest changes stopping at the correct address; P2-only damage despite unchanged P1 CRCs; missing, duplicate or misaligned maps; standard and compact background shapes; BSS and final-candidate bounds; and shifted/shrunken metadata failing closed. Independent C/test review and whitespace validation pass.

Strict SH-4 compilation/linking and an 8,792-instruction audit pass. Ordinary native trace modes 0–3 and CE modes 0/3 match parent allocated bytes and relocations; scoped assembly remains identical. The bookkeeping stack occupies `0x8ce14920..0x8ce15920`, the owner stack occupies `0x8ce15920..0x8ce1d920`, and stage BSS ends `0x8ce4aa88`, below `0x8cf00000`. Checkpoint/return/report C frames are 56/36/124 bytes; the maximum report call path remains 304 bytes inside the 4 KiB bookkeeping stack. Standard and background map shapes are 160 and 64 slots respectively (`0x87c` and `0x3fc` bytes on SH-4).

The delivered archive records its exact GitHub build and package checksums. Console boot and gameplay remain pending the owner test.
