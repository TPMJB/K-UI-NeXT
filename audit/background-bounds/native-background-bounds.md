# Native Background fallback work limit

The native Background reader can use DMA between game calls, while the CPU
checks and copies an earlier block. If the game owns DMA channel 1, reception
falls back to programmed I/O (PIO). That fallback must return control promptly.

Before this change, `deliver(wait)` checked its wait target only when a DMA
transfer was pending. PIO blocks were ready immediately, so the same loop could
read an entire request inside one masked game call or reader interrupt, even
when called as `deliver(0)`.

The regression test uses the production reader, cursor and GD service with the
existing SCI/card model. A 64-sector cooked request with guest DMA1 occupied
previously consumed all 256 SD data blocks inside REQUEST. An interrupt-time
takeover reproduced the same behavior. Data could be correct while the game
lost CPU time for the whole transfer.

The native reader now permits one atomic PIO data block per GD entry, shared
by its before/after processing, and one per reader interrupt. Once consumed,
further work yields until a later entry. The newly received PIO block can remain
buffered until that next entry. The request token, cursor, buffered data and guest DMA ownership are
retained; a paused PIO path resumes through subsequent GD calls without
requiring an SCI interrupt.

Actual DMA-only entries retain their existing catch-up targets and overlap.
This combines the Background pipeline with a bounded PIO fallback; it does not
swap separately linked readers or classify movies from framebuffer activity.
Windows CE's separate scheduling remains unchanged.

The limit counts atomic PIO data blocks, not elapsed time. Existing command,
token-search and recovery timeouts remain. Host tests establish the work bound,
eventual byte-exact completion, and ownership behavior; they do not measure
Dreamcast frame timing or prove that every observed gameplay stall has the
same cause.

The user's test of build `9fa65fc1cae0` reported smooth Evolution 2 FMVs with
Background and occasional gameplay stalls of roughly three or four frames.
Standard gameplay was nearly smooth, with rare single-frame interruptions.
Their earlier report of flawless FMVs at `edf20f0f11a3` used regular loading;
this change does not establish why that earlier build differed.

For a console test, use the new Background package, select the existing
prepared profile, and leave Background On. Check the opening FMV, the same
gameplay section, and save/load. Compare against the preserved `9fa65fc1cae0`
package using the same game image and settings.
