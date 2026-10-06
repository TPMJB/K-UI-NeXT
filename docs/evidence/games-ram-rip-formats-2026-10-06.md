# Games RAM catalogue, GD command compatibility and ripper formats

User hardware observations before this change:
- Power Stone BIN/CUE blackscreens with the standard reader; its GDI also reportedly failed previously.
- Sonic Adventure previously glitched then reset; no new format-only causal evidence.
- An optimized Japanese Dead or Alive 2 BIN/CUE boots on the user's stock-region console. This confirms one native CUE layout on hardware, not general title compatibility.
- The uploaded Power Stone CUE has separate Mode1/2352 data tracks and audio, both GD density markers, and track3 starts at45000 under the current parser. Payload BINs were not supplied.

This change publishes rows before selected artwork, preserves visible rows during page requests, warms `/Games` through the idle storage worker and retains four RAM catalogues. Catalogue allocation is capped at2048 entries, about1MiB; art pixels at512KiB plus32 records. Selected UI/staging cover buffers total100KiB, replacing400KiB page buffers.

Real FAT32/exFAT tests cover196 Games scenarios and22 covers scenarios, including zero-I/O warm page/root returns, cached positive/negative art, cancellation, retry, bounded eviction and conservative capture payload filtering. Shell tests cover stale generation/root/page/view/selection results and navigation during art loads. Same-port physical card replacement requires explicit X Refresh.

Three GD command ABI corrections are corroborated by pinned DreamShell4a2b898cbc244b2fb9bd1698b45e5325056232fb and redreamffb7302245ff40515cb9f0f0b0e233a4b39342d3:
- Mode command CHECK reports10 transferred bytes, independently of its16-byte RAM output.
- Invalid CHECK handles return-1 and status{5,0,0,0}; they do not consume the valid command.
- Completed/failed commands keep their queue slot until CHECK acknowledges them. The resident's busy-request rejection predicate uses command occupancy.

All five GD/async sanitizer suites and six native reader links pass, with unchanged memory/stack/integer/FPU guards. Native SCI has44 bytes remaining; SCI background ends at its existing boundary. No physical storage timing or optical recovery policy changes were made. Hardware retests are required before claiming either failing title is fixed.

Ripper output choices are GDI, BIN/CUE, CSO, DreamShell-LZO ZSO and compressed CHDv4. Compressed outputs retain verified raw source files and an internal fixed `.capture.gdi`; CSO/ZSO represent a cooked high-density data track, while CHD retains captured data/audio mainchannel tracks with declared synthetic padding. No subchannels or excluded physical gaps are claimed captured. Native compressed game boot remains a separate reader/memory feature.

Export validation includes 934 sanitizer checks, real FAT32/exFAT jobs in all three compressed formats, full decoded-source comparison, cancellation/Resume, existing-file reuse and damage preservation, prewrite layout rejection, Mode2 payloads and zero-write Verify. Independent zlib/LZO decoders and actual MAME 0.264 plus pinned libchdr validate containers. CHDv4 uses PAD-only metadata with zero virtual PREGAP, preserving every original track start; the importer normalizes legacy GD audio after GDI extraction. All five export/codec translation units compile and assemble under strict SH-4 GCC 15.2. Native KOS runtime linkage is checked by GitHub CI.
