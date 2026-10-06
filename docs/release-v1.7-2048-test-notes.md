# K-UI 1.7 — 2048-byte test notes

This development test is based on K-UI 1.7. It is not a new public release or
a hardware compatibility claim. The accepted SCI transfer allowance and
resident memory boundaries remain unchanged.

- GDI preparation and resident readers accept mixed 2048-byte Mode 1 data and
  2352-byte raw data/audio tracks with zero file offsets.
- Cooked tracks read their payload directly. Raw tracks retain address/header
  validation. Full raw-data reads from cooked tracks fail before backing I/O;
  this test does not synthesize raw headers or ECC.
- Cooked boot executables receive an exact-byte checksum during preparation
  and loading, including the Windows CE prefix when present. This adds an
  initial launch pass, not an in-game read pass.
- The host converter streams into a separate new folder, validates raw Mode 1
  sync/mode/address fields, preserves audio, and records per-track identities.
  It refuses unsupported sectors, offsets, overlaps and incomplete tracks.
- Track format fits existing manifest/TOC storage. Raw-only wire output remains
  compatible, and native/CE memory layout limits remain fixed.
- Matching `Game` and `Game-2048` single-GDI sibling folders are grouped into
  one Games entry after bounded track-layout checks. The version picker chooses
  the exact backing path before inspection; detail and confirmation show the
  selected version. This does not change any resident reader or storage driver.
- A converted copy without a matching original remains accessible. Ambiguous,
  mismatched or unsupported layouts remain ordinary entries rather than being
  silently hidden. Pairing checks do not hash complete tracks.

Conversion does not guarantee faster loads, Windows CE boot, complete raw-sector
requests or CD audio playback. Keep the raw image for comparison and fallback.
Automated checks cover parsing, conversion, mixed reads, error paths and
filesystem preparation; only physical-console tests can establish performance
and audio/video behavior.

Install the matching runtime and Apps as described in
[the test instructions](release-v1.7-2048-test.md).
