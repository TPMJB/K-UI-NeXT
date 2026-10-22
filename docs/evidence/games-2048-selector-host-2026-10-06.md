# Original/2048 Games selector host checks

2026-10-06, `milestone/2048-variants-test`. This records host validation, not
Dreamcast boot, speed or audio results.

- 53 Games scenarios across FAT32 and exFAT: 106 real filesystem image checks.
  Original and converted-first enumeration, logical pagination/counts, missing
  and ambiguous copies, track-layout mismatches, suffix chains, index overflow,
  descriptor faults, one-shot read/close failures and cancellation were tested.
  Card SHA-256 remained unchanged, fsck passed, and explicit FatFs handle and
  active-operation write assertions passed. Pairing reads descriptor bytes and
  file sizes, not full track contents or converter provenance hashes.
- Full shell controls/rendering suite passed with ASan/UBSan. Both versions
  dispatch the selected exact path; stale results, invalid alternatives and
  back/cancel behavior were checked. Native standard/background20/background25
  and existing Windows CE reader choices remain unchanged.
- All eight FAT32/exFAT cover scan/cancel/failure scenarios passed. The host
  renderer compiled with warnings as errors; selector, detail and confirmation
  previews were visually checked. The shortened `K-UI 2048 picker` header fits.
- 24 retail packaging/metadata tests and eight workflow hygiene/scope tests
  passed. The focused CI scope rejects resident reader changes.

Local sanitizer runs disabled LeakSanitizer because this execution environment
prevents its process inspection; AddressSanitizer and UndefinedBehaviorSanitizer
remained enabled. Filesystem handle closure is explicitly asserted by fixtures.

No resident reader, SCI/SCIF/ATA driver, transfer allowance or image-map format
was changed. GitHub must still compile/package the exact published commit.
