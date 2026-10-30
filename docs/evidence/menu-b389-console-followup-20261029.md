# Menu follow-up after b389e77445ad

The owner reports the b389 GUI looks more organized and feels somewhat faster,
but Virtual VMUs still shows unhelpful names and slight menu lag remains. The
menu reports about 8.8 MB used out of 16 MB. This is qualitative console evidence;
no input-to-display timing or current heap breakdown was supplied.

## Changes

Virtual-card list admission now derives display context from exact product
assignments and existing checked Games title-cache headers. Only relevant
512-byte headers are read, with a bounded directory scan per requested page.
Missing, stale, malformed or conflicting title metadata cannot establish a
game title. Save descriptions are read from the already admitted logical image,
following its FAT and header offset. They describe card contents and never
create assignments. Explicit friendly names remain. The selected row displays
assigned games and save contents without opening a card; row navigation does
not access storage.

Routine UI music-status publication previously waited for the audio mutex on
every input-loop iteration. The audio worker holds this mutex during stream
refill and Ogg decoding. Routine display snapshots now try the mutex once and
retain the previous coherent status if it is busy. Worker/control paths needing
authoritative status keep their existing blocking snapshots. Decoder ownership,
stream lifecycle and audio buffer sizes are unchanged.

The idle redraw timestamp now records frame completion. Slow drawing and
vertical-blank waiting no longer make the next background redraw immediately
due. An idle loop that just drew yields and samples controls again, instead of
adding the normal 8 ms sleep. Busy redraw limits and operation pacing retain
their prior behavior.

## Memory scope

The gauge includes the GUI image and BSS, firmware/main-stack reserves and live
heap, including thread stacks and cached music. Free allocator blocks and
unclaimed RAM count as available. VRAM/audio RAM are separate. The baseline
b389 packaged GUI spans 6,624,816 bytes (about 6.32 MiB) before its heap use;
the optional cached drawing canvas adds 614,400 bytes while in the shell.

The amount occupied by idle scratch does not itself establish CPU work or RAM
pressure. A separate GUI memory pass could lazily allocate benchmark and
storage-test scratch (roughly 1.54 MiB combined), but that work is not included.
The native reader/VMU's 331,776-byte (324 KiB) in-game reservation and toggle-Off
memory layout are unchanged. This update does not broaden game compatibility.

## Validation limits

Focused host checks exercise card-title/contents handling on FAT32/exFAT,
rendered text, existing save operations, UI cadence and real-C audio-lock
contention. Console responsiveness and the owner's actual card naming results
still require the next hardware run. The change removes known wait paths; it
does not claim a measured input-to-display latency or complete resolution of
all remaining lag.
