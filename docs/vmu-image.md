# Virtual VMU image preparation

`src/core/vmu_image.c` prepares and checks plain 128 KiB virtual VMU images
through logical 512-byte block callbacks. It is independent K-UI code, separate
from the resident journal and Maple packet endpoint. It never opens a physical
VMU or chooses a file to overwrite.

The standard image contains 256 physical blocks, 200 user blocks, and an empty
directory when newly created. Stored file data are ordinary VMU bytes, without
word swapping or a K-UI wrapper.

| Blocks | New contents |
| --- | --- |
| 0–199 | Zeroed user data, with free FAT entries |
| 200–240 | Zeroed unused area, with free FAT entries |
| 241–253 | Zeroed directory; FAT chain 253 → 252 → … → 241 → end |
| 254 | Allocation table; its own entry is end-of-chain |
| 255 | Root block; its own entry is end-of-chain |

Free entries are `0xfffc` and chain terminators are `0xfffa`, stored as
little-endian 16-bit values. The root has sixteen `0x55` format bytes, the
standard root/FAT/directory locations, user-block limit 200, and conventional
VMU compatibility parameter 31. Its media information matches the prototype
Maple endpoint. There is no executable VMU processor capability.

## Creating a card

The owner first creates an exactly 131,072-byte, entirely zeroed new image.
`kui_vmu_image_format_blank` checks its independent containing-file length and
reads every block before writing anything. Any nonzero byte produces
`KUI_VMU_IMAGE_NOT_BLANK`; existing or imported cards are never automatically
reformatted.

Formatting writes all blocks 0–254, applies a durability/order barrier, installs
the valid root last, and applies a second barrier. It then rereads and compares
all 256 blocks with the expected new image. A successful return means every
block passed that comparison. The prepared file should become selectable only
after this succeeds. On an I/O error, the caller must discard or separately
inspect the newly prepared file; an incomplete format is never a success.

The timestamp uses the pinned KOS eight-byte BCD order: century, year, month,
day, hour, minute, second, and weekday with Monday 0. A NULL timestamp uses
2000-01-01 00:00:00, Saturday. Gregorian leap-day and field ranges are checked
before any I/O; a caller can supply the current validated timestamp instead.

## Admission of an existing image

`kui_vmu_image_validate` is read-only. It requires the exact containing-file
length and reads all 256 blocks. It then checks:

- Canonical 200-block root geometry and the expected system FAT chains.
- Valid data/game directory types, copy flags, names, sizes, and header offsets.
- Complete allocation chains of the declared lengths, without cycles,
  out-of-range links, shared blocks, or orphan allocations.
- Unique filenames, treating trailing spaces/NUL padding consistently.
- A single contiguous VMU game file starting at block 0, if one exists.

Free blocks may contain nonzero or stale contents. The native proof's raw
block 42 marker therefore remains a valid empty filesystem: no directory/FAT
allocation claims that block. This is a protocol/storage proof, not a game save.
The validator does not infer or repair the meaning of opaque game payloads and
does not replace each game's own save-data CRC validation.

The current admission policy is deliberately restricted to the canonical root
format generated here. It rejects 241-block, relocated-system, damaged/bad-block,
and noncanonical legacy media layouts. Some root fields are labelled unknown
in KOS's public struct but are described by the public storage-function
specification; this milestone checks their canonical values. Rejection never
modifies an imported image. Wider image compatibility needs explicit admission
rules and corresponding Maple media information, rather than silent conversion.

## Memory and callback requirements

The caller supplies a 3,720-byte workspace containing one data block, one FAT
block, a 200-byte ownership map, and bounded directory-name storage. Production
code allocates no heap memory and never puts a 128 KiB card on the stack.
Callbacks receive logical image block numbers 0–255. They must return zero only
for complete successful operations; `sync` must be a durable ordering barrier.
The owner must serialize access and prevent concurrent image changes. The
read-only validator needs only the read callback.

## Sources and tests

The byte layout follows the pinned
[KOS VMU root/directory declarations](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/vmufs.h),
[Marcus Comstedt's flash layout documentation](https://mc.pp.se/dc/vms/flashmem.html),
and the public
[FT1 Storage Function v0.80 hardware specification](https://github.com/Kochise/dreamcast-docs/blob/master/CONTROLR/DOCS/Ft1_080e.doc).
No KOS formatter or another emulator's formatting implementation was copied;
this is an independently authored constructor for the documented on-disk data.

`tests/test_vmu_image.c` checks literal root and system-FAT fixtures, exact new
contents of all blocks, blank-image protection at every physical block,
timestamp boundaries, full 200-file/200-block cards, malformed directory and
allocation cases, geometry rejection, and every read/write/barrier failure in
formatting and validation (1,040 injected I/O failures). Additional readback
corruption must return verification failure. It passes ASan and UBSan.

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
  src/core/vmu_image.c tests/test_vmu_image.c -o build/test-vmu-image
ASAN_OPTIONS=detect_leaks=0 ./build/test-vmu-image
```
