# Collection-native terminal RAM retirement

The075e owner report establishes a working first Swirl → SF → PSII → VC2
sequence and successful play in every other collection game. A later VC2 run
freezes with a small diagonal picture shift after Start. Preserve that progress;
the display change alone does not identify a framebuffer register or writer.

The native and CE terminal paths had different RAM retirement. CE clears
AC010000..AD000000 before loading its fresh outer capsule; native left the
old game RAM intact. Original SAMPLE clears below8CE00000. The exact NK fixed
allocator can return its FD0000 reserved graphics payload without clearing it.
NK does initialize the cached pointers in its own BSS; no stale pointer or
actual payload read-before-write explaining the freeze has been found.

The candidate makes collection-native retirement consistent at an existing
ownership boundary. Its worker has returned1, its three original pure helpers
have completed, ROOT/video/receipt are captured low, and hardware/cache
quiescence has completed. No old callback or owner frame resumes. The clear
finishes before the first new outer-capsule read; all sector CRC checks remain.

In the accepted075e profile, the complete raw-reader callback closure is low:
manifest8C00734C, service8C007280, card8C00768C, resident BSS ends8C007760,
raw readerAC008100. The native controller, its596-byte runtime, captured seed,
probe and private stack also lie belowAC010000. Generated candidate addresses
must remain within that bound. Fresh outer loading and original GIN recreate
the next menu/MDE/SAMPLE and context; no high-RAM source hint is required.

This applies only to `KUI_SMASH_CARD_SESSION` in the native collection return.
Generic background/session returns may keep high-RAM state and are excluded.
The shared integer clear leaf is compiled through a native-symbol wrapper.
The existing CE leaf/source and all CE controller bytes remain unchanged.
Workers, their72KiB reservation, helpers, read implementations and original
Genesis options/Save Game routing are not modified.

Qualification must execute the linked clear leaf over the entire interval,
prove low-memory/caller preservation and corrupt-bound refusal, and compose
native retirement with actual capture/quiescence/raw loading. BUSY and refusal
paths must retain the old RAM. An old075e control must show retained poisoned
upper RAM; the candidate must remove it before loading the new owner.
The complete real outer prefix must still reach its original C entry with
ROOT/video/receipt intact. These are software ownership proofs, not a physical
VC2 or PVR trace. Console confirmation of the later Start transition remains
necessary, and this candidate must not be labeled fully accepted in advance.
