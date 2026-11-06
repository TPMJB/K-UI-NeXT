# Virtua Cop 2 virtual video RAM payload writes

The three hardware pages for build `46577020a054` record a failed read
delivery into a CPU virtual address backed by video RAM. The retained
pre-copy source matches the live source both immediately and later, while
the first destination byte is already wrong immediately: expected `02`,
read back `01`. `WHY=82` also records a later destination change. The
transcription preserves the uncertainty in one early hash digit.

The unique UTLB entry maps destination `4243E600` to physical `0433E600`,
with its cacheable bit clear. This is the 64-bit VRAM aperture. The reader's
old selector recognized only direct `A4/A5` aliases; the virtual address
therefore reached `memcpy`. This late-loading profile deliberately uses a
byte-only `memcpy`. Sega's Hardware Specification Outline, table 2-6 and
section 3.4, lists VRAM word/longword/burst access and prohibits byte access:
<https://segaretro.org/images/8/8b/Dreamcast_Hardware_Specification_Outline.pdf>.

Use the existing aligned volatile halfword writer for admitted CPU virtual
payloads. Retain the caller's virtual pointer, map access mode, permissions,
cache attributes and CE TLB handling. A virtual pointer can map RAM or VRAM;
no lookup of a transient TLB entry or conversion into a physical DMA pointer
is needed. Direct main RAM retains its existing copy path. The writer's
aligned read/modify/write preserves neighboring bytes at odd callback
boundaries, within the same mapped page.

The halfword loop uses explicit odd/even lanes and stays out of line to keep
its emitted bus accesses inspectable. Three terminal-only low-memory labels
are abbreviated; their page number, reason, address, value and full numeric
snapshot remain present. The existing memory reservation and guarded stack
remain the linker's limits.

`tests/test_native_virtual_vram_copy.py` executes the actual production
dispatch function with instrumented host sinks and separately exercises the
real halfword writer with odd offsets, lengths, split callbacks and guards.
The photographed address must select CPU mapping mode 1 and the halfword
sink. The old build fails that regression. These tests do not model the
Dreamcast bus, CE faults, graphics activity or hardware DMA timing.

The source remains unverified against independent golden disc bytes. This
capture establishes one concrete incorrect write path; it does not establish
that every texture issue has this cause. Console validation should first
check loading screens/gameplay, then virtual VMU save/load. The optional
three-page diagnostic uses the corrected copy for further evidence if
corruption persists.
