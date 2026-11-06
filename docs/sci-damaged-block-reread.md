# Reject reconstructed SD blocks before game delivery

The owner's three photographs of build0a2ddd9bb9b6 capture a real read failure
in VC2's `wsegacd.dll` PIO_TRANSFER (FN12, return01CF1978). The request reads
`VC2/BIN/COMMON/F_COMMON.BIN`, beginning at sector509844. After0x69800 bytes,
the raw-sector parser refuses the next header. The failing block retains
`lost[0]=146`: DMA reception lost byte145 before the following sector header
at block offset176. The stream accepted its CRC and supplied it to the parser.
The pointer mapper succeeded; the snapshot has no active VMU loan or latched
card-bus fault. See the [transcribed hardware evidence](evidence/vc2-0a-repaired-block-hardware.json).

This identifies the failure path, not the exact electrical corruption. The
offending512-byte block itself was not captured. The whole raw track is also
not part of the supplied executable evidence.

The existing CRC erasure reconstruction is insufficient evidence for a damaged
reception. An independently authored counterexample loses byte145 and corrupts
header bytes176/177. The old production SCI code reconstructs a wrong byte145
and accepts the wrong block against the card's original CRC16. Its header then
fails the same parser check. The
[independent CRC vector](evidence/sci-lost-byte-crc-counterexample.json) and
`tests/test_sci_stream_repair_quarantine.c` establish that software defect;
they do not claim to reproduce the console's physical wire fault.

The shared SCI `take` guard now rejects every block with a lost-byte marker,
including a reconstructed block that matches CRC. It returns the existing CRC
failure result. The existing reader retries the same card LBA, switches to
programmed reception after repeated failures, and verifies the untouched
reception's full CRC before publishing bytes. No I/O is added inside `take`,
which may run while the next DMA reception is in flight. Normal CMD18 runs,
per-block CRC checks, scheduling limits, receive buffers and state ABI remain.
Only damaged receptions incur rereads. The policy is shared source rather than
a title-specific switch; a collection package does not install every other
loader in the library.

The guard grows the current collection CE reader's `take` by4bytes. Three raw
reader callback literals consequently advance by4bytes: offsets240
(`open_bus`),252 (`native_foreground_complete`) and256 (`memcpy`). The raw entry
AC008100,260-byte instruction/data prefix and252-byte zero tail retain their
geometry. The current full-SHA gate and canonical return image are explicitly
updated to those three reviewed addresses, and each is checked against the
fresh resident ELF. The raw instruction bytes and all other literals remain
unchanged. Return controllers carry this updated data so their restored reader
calls the current functions. Historical startup fixtures are not rewritten.

The current91fa VC2 helper implementation, Swirl helper implementation,
storage-arbitration fix, native72KiB worker reservation, Genesis options/Save
Game, return behavior, video-copy policy and updater remain outside the repair.
Helper transport suffixes and loader checksums must reflect the actual rebuilt
reader and canonical return data; they are not independent worker revisions.

Software checks cannot establish console acceptance. The owner must still
confirm fresh-first VC2 and the previously failing repeated-game sequence.
