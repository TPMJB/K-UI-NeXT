# Native Maple submission/completion test

The previous relocated worker passed on Power Stone 2. This corrected build
fixes the buffer-layout refusal seen in the first passive test and removes a
repeated image scan/hash. Its console timing still needs confirmation.
It resumes the
game briefly, observes one real Maple submission and its completion, then
stops before the original consumer runs. Physical peripheral requests remain
unchanged. No virtual card is advertised and no saves are written.

Copy this ZIP's **KUI** folder onto the card, replacing the existing files.
No installer or backup/restore script is required. Safely eject the card.

Launch **original Power Stone 2 GDI**, with **SCI / Standard**, default/Auto
encoding, CD music off, background tasks off and RAM patches off. Use the
original tracks, rather than the `-2048` conversion.

Photograph the single final screen. Success says **PASSIVE PAIR VERIFIED**
and **PHASE 0000000B**. A refusal screen is useful too: photograph its phase,
detail and runtime result. The build intentionally stops; power off afterward.

This checks live SDK buffer layout, the allocated worker's continued ownership,
the original submission register context and the matching completion. It does
not yet prove save/load, long gameplay sessions, other games, CDDA coexistence
or SCIF VMU backing. Copy your normal KUI folder back before ordinary play.

The ZIP includes the complete source checkpoint and build audit.
