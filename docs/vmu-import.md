# External VMU save import

VMU Manager → **SD backups → Y Import** opens the SD file picker. Select a
`.vms`, `.vmi` or `.dci` file, review its preview, then confirm to stage a new
K-UI backup bundle. Import does not require a connected VMU and never calls
physical VMU APIs. Restoring the staged save is a separate action in SD backups,
with the existing destination preview, filename-conflict refusal and confirmation.

The preview shows the internal VMU filename (including its exact 12 raw bytes in
hex), name provenance, data/mini-game type, copy-protection flag, block count,
header offset and timestamp. High-bit filename bytes are preserved; the menu
substitutes `?` when it cannot draw them. The hex line remains exact.

## Accepted formats

| Input | Payload and metadata rules |
| --- | --- |
| Bare VMS | A CRC16-valid data package with its header at byte zero. Its SD filename stem must be 1–12 printable ASCII bytes; that exact stem becomes the VMU filename and the preview explicitly identifies this reconstruction. The timestamp is unknown and reconstructed as eight zero bytes; it is not presented as an original date. |
| VMI + VMS | Select the exact 108-byte VMI record. Its safe 1–8-character resource selects `<resource>.vms` in the same folder, independently of the selected VMI filename. The VMI's explicit internal VMU filename, type, protection and timestamp are preserved. The companion's exact byte length must match the VMI. |
| DCI | Exactly 32 bytes of original directory metadata followed by the recorded number of 512-byte blocks. The directory is not byte-swapped; each four-byte payload group is reversed. Original filename, type, protection, timestamp and header offset are preserved. The original first-block field is ignored for allocation, retained in the staged metadata and replaced by the separate restore transaction when it allocates actual VMU blocks. |

Data packages accept either the exact logical package length or its complete
512-byte allocation padding. CRC16 covers the 128-byte header, icon images,
eyecatch and declared data, with the CRC field zeroed. It excludes allocation
padding; existing padding bytes are retained and only missing allocation padding
is zero-filled.

Mini-games require VMI or DCI metadata declaring the game type and header block
one. Bare mini-game VMS files cannot supply an authoritative type or original
filename. Game header CRC/data-length fields are unspecified by the format, so
import checks the header/icon/eyecatch bounds and complete source stability;
it does not claim an embedded mini-game payload checksum.

## Limits and refusal rules

- Local SD paths use the full Files limit: at most 383 bytes beginning with `/`,
  optionally preceded by `0:`. Parent traversal and other drives are refused.
- A VMI resource uses ASCII letters, digits, underscore or hyphen only. This is a
  K-UI path-safety policy, narrower than the external format. Rename the VMS
  companion and update its VMI resource/checksum on a computer when an external
  resource uses a different encoding or punctuation; an unsafe resource is never
  interpreted as a path.
- Internal names from VMI/DCI preserve non-control high-bit bytes and printable
  punctuation. They must contain a meaningful name, occupy at most 12 bytes and
  have only zero padding after a NUL. They are never used directly as SD paths.
- VMI version must be zero, file count one, reserved fields zero and mode bits
  limited to game/protected. VMI timestamps require a valid Gregorian date in
  years 1–9999, valid time and weekday. Weekday conversion follows the documented
  Sunday-based VMI and Monday-based VMU directory conventions.
- DCI directory reserved/dirty bytes must be zero, type data or mini-game,
  protection zero or `ff`, and header offset zero for data or one for games.
  Valid BCD timestamps are preserved. An all-zero DCI timestamp is accepted as
  unknown for compatibility; this is a K-UI policy rather than a documented
  claim that the original date is known.
- Headers require 1–3 icons, eyecatch mode 0–3, zero reserved bytes and complete
  in-bounds graphics/data. Each file is bounded to 255 blocks (130,560 bytes),
  plus the DCI's 32-byte directory. Actual restore capacity is checked separately.
- Truncated, inconsistent, unsupported and CRC-invalid inputs are refused.
  Import is not a forensic repair or format-conversion tool.

## Verification and storage

Preview opens all inputs read-only and reads them twice, checking exact length,
byte-for-byte equality, SHA256 and stable FAT metadata. Confirmation requires the
same normalized path and complete input snapshot, reopens and rechecks all
inputs twice, then stages the bytes that were actually checked. A source changed
after preview requires a fresh preview. Stop before staging leaves no output.

Every import creates the next unused organizational `A1-NNNN` folder under
`/KUI/backups/vmu`; `A1` does not imply a VMU was connected or identify the
source card. Existing backups and input files are never overwritten. The shared
K-UI publisher writes a block-padded `.vms`, the 32-byte `.dir` and the existing
`.crc` proof using new temporary files, sync/close, complete readback and rename.
The resulting bundle is accepted by the existing restore preview unchanged.

A separately verified `import.txt` records the input paths, SHA256 snapshot and
whether names/timestamps were preserved or reconstructed. Its checksum describes
the bytes read during this import, not historical authenticity or compatibility
with a particular game. A failure can leave a new partial folder; a completed
bundle is retained if its later provenance write fails. Original inputs remain
read-only. Keep the complete folder, and preview any later restore again.

## Independent implementation

This implementation was written from our existing backup/restore code and the
published byte layouts. No DreamShell implementation source was read or reused.
The documented upstream KallistiOS structures and our existing CRC16/SHA256
routines provide the platform/data primitives.

Format references:

- [Marcus Comstedt: VMS file header](https://mc.pp.se/dc/vms/fileheader.html)
- [Marcus Comstedt: VMI records](https://mc.pp.se/dc/vms/vmi.html)
- [Marcus Comstedt: VMU flash memory and DCI](https://mc.pp.se/dc/vms/flashmem.html)
- [Pinned KallistiOS VMU package declarations](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/vmu_pkg.h)
- [Pinned KallistiOS VMU directory declarations](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/vmufs.h)

Host verification uses independently constructed format fixtures, ASan/UBSan,
real FAT32 and exFAT images, exact source-preservation checks, assertions that
import never touches Maple, and the real bundle publisher followed by the
existing physical restore preview. Console timing and external-save/game
interoperability still require hardware acceptance testing.
