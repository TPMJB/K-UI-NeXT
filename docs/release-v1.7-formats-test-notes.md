# K-UI 1.7 — disc formats test notes

This is a development hardware test. It preserves the Original/2048 picker and
cached paging while adding generic image discovery, inspection and artwork.

- Direct layouts include GDI, ISO, BIN/CUE, CDI v2/v3/v3.5 and standalone
  BIN/IMG. Shared track files, bounded byte offsets, supported CD/GD sessions,
  pregaps and 2048/2336/2352/2448-byte backing are mapped into the detached
  readers. Mode 2 Form 1 payloads are supported; Form 2 is refused.
- CD/GD classification reaches the resident TOC service. Native CD launch has
  explicit Plain/Scrambled executable choices; CUE's `REM KUI SCRAMBLED 1`
  selects a default that Plain can override. ISO/raw session addresses and CDI
  pregap/container offsets are validated rather than guessed.
- Windows CE confirmation offers the background SCI reader. Oversized CE maps
  fail with an explanation instead of falling back to the broken regular
  reader. CE still requires SCI microSD and its supported boot layout.
- The computer importer supports CSO/ZSO expansion and CHD extraction through
  MAME `chdman`. Outputs are validated and published into a new destination;
  original images remain unchanged. Compressed resident decoding is absent.
- Batch GDI conversion now reports every conversion, skip and failure, with
  an optional JSON report. It still refuses unsafe or unsupported source
  layouts. Evolution's reported skips require its actual report/input to
  identify the cause.

There is no CDDA playback, subchannel emulation, Mode 2 Form 2 support,
WAV/MP3 CUE support, or NRG/MDS/MDF/CCD/archive reader. The legacy selected-image
probe remains a raw, zero-offset GDI diagnostic.

Generated host fixtures cover parsing, physical map reads through standard
and background cursor paths, full IP/exact boot CRCs, invalid encoding choices
and read-only FAT32/exFAT preparation. Native link/layout/stack checks enforce
reader memory budgets. These checks do not establish game compatibility.
The reported FMV stalls/skips remain unresolved; no speculative reader timing
change is included in this formats build.

Install matching runtime and Games files using
[the test instructions](release-v1.7-formats-test.md). Detailed supported
layouts, importer dependencies and evidence limits are in
[the formats guide](games-formats-test.md).
