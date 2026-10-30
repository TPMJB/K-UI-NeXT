# Direct payload-only GDI capture

`GDI 2048 direct` (`capture_format=gdi_direct_2048`, format value 6) creates
2048-byte `.iso` data tracks and unchanged 2352-byte `.raw` audio tracks directly.
The existing `GDI 2048` format remains a separate post-capture derivative that
retains its raw tracks. Existing jobs retain their original format on Resume.

The direct path uses the accepted raw optical reader and its existing DMA/PIO
ownership rules. It validates raw sector framing, requested address and EDC,
then packs only the supported 2048-byte payload into the active chunk buffer
before writing. It does not acquire a full raw track or create whole-track raw
temporary files. Mode 1 and Mode 2 Form 1 are supported; mixed sector modes in
one track and unsupported forms fail without publishing a completed manifest.
Strict acquisition retains its Mode 1-only parity contract. CD capture still
uses its separate BIN/CUE profile; this direct GDI option is for GD media.

Checkpoint sector counts remain logical sector counts. The explicit format flag
binds data to 2048-byte storage strides and audio to 2352-byte strides. CRC32 and
optional SHA-256 cover the bytes actually saved. Prefix checks, Quick Resume,
sampling, buffer-mutation guards, truncation of uncommitted tails and full Verify
use those saved strides. A complete saved-byte readback is mandatory before
publishing the direct GDI/manifest, including CRC-only jobs with end readback
otherwise disabled. CRC-only Quick Resume may skip the initial prefix read,
but it still performs a complete saved-byte check before completion, including
when acquisition was already fully checkpointed. A failed or interrupted end
readback never publishes completed metadata; a later Quick Resume retries that
verification. Stop and storage/optical failures preserve resumable work.

The final manifest uses schema 3 and profile `gdi-direct2048-rawaudio-v1`; it
also names the optical layout profile, per-track sector sizes and saved hash
scope. This prevents older raw-only readers from interpreting cooked hashes as
2352-byte hashes. Raw Redump/TOSEC comparison is explicitly skipped because the
saved payload hashes do not describe raw sectors. A successful readback checks
saved bytes against capture; it does not establish an independent reference.
`tools/verify_dump.py` independently checks schema 3 sizes, CRC/SHA hashes and
the mixed-stride GDI descriptor. Schema 1/2 raw and derivative verification is
unchanged.

The image suite `tests/test_capture_direct_images.py` uses actual FatFs FAT32
and exFAT images with ASan/UBSan. It compares payload/audio files against a
separate raw-capture oracle, tests Mode 1/2 and the DMA path, read-only Verify,
changed saved-byte refusal, cancellation, write/sync/source-buffer/optical
faults and resume. Normal capture also refuses both active in-place-repair
marker names before reading checkpoints or changing files. Console throughput
and disc compatibility require hardware confirmation.
