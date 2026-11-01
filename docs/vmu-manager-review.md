# Physical VMU copy/delete review

The official black VMU used for the earlier copy/delete tests later produced
different block-0 payloads on successive read-only import attempts. Each read's
receive-buffer fingerprints before and after copying matched its copied-output
fingerprint. The source of the differences remains unresolved. This review
does not establish that the card is defective or that earlier writes caused
the changing reads.

## Corrected application defects

The manager formerly allocated a VMU mini-game (`0xcc`) into the first free
ascending blocks, even when block 0 was occupied or the prefix was fragmented.
Such a copy could be unusable by the VMU and rejected by the whole-card image
validator. Admission now requires a single mini-game contiguous from block 0.
Restore, copy and copy-all refuse before writing when the destination cannot
provide that free prefix. Existing saves are never moved to make room.

The manager also formerly called the pinned SDK's `vmu_block_read` directly.
That implementation copies `(data_len - 2) * 4` bytes without requiring the
exact 130-word response. The manager's surrounding canaries cannot protect
against an underflow inside the SDK copy. Manager reads now use the existing
bounded BREAD helper, retaining duplicate reads, device checks and canaries.
The helper validates framing before copying, keeps frame ownership through
the copy, and quarantines sent operations that time out.

## Bounded read retries and logical import follow-up

The November 1 review found a separate BREAD retry defect in the adapter's
request storage. KOS `maple_frame_init` aliases the send and receive buffers;
an AGAIN reply overwrites the request before KOS marks it UNSENT and resends
from `send_buf`. The reader now retains two independent request words per
physical slot. An owner-frame guard remains held across a SENT timeout and
late AGAIN retries, so another device cannot reuse a pending request. It is
released only when that request is no longer queued or in flight. A private
512-byte copy must have the same CRC32 as the receive payload before and
after copying, before any caller bytes are published.

The application retains two identical reads for healthy blocks. If complete
replies differ, at most eight reads are allowed and three consecutive whole
512-byte samples must agree. No byte voting, padding repair or synthesized
allocated data is used. Identity, cancellation, canary and transport failures
stop immediately; later independent payload and metadata comparisons remain.
Refusals log block/save context, first differing offset and observed CRCs.

The current import wizard captures all admitted saves into a separate
canonical card without reading unused physical flash. An exact raw VMD
backup still reads all 256 blocks twice and refuses disagreement, including
unstable free blocks. These are intentionally different operations. The
retry defect can explain refused reads or timeouts; it is not proof of the
cause of the black VMU's earlier changing allocated block 128. Console
recovery remains unconfirmed until the owner retries this build.

## Delete and copy write paths

Delete clears the selected directory entry, verifies the directory, frees only
the selected chain in the FAT, then compares the complete root/FAT/directory
snapshot. It does not erase the freed payload blocks. Copy/restore writes new
payload only to selected free blocks, verifies it, then publishes FAT and
directory changes and compares metadata. A failed copy can leave new bytes in
still-free blocks; those bytes must nevertheless read consistently later.
No out-of-range write or deliberate overwrite of unrelated allocated payload
was found in these application paths.

## Remaining SDK write concerns

The pinned official KOS revision is
`fcfa7d869471591ca1c777543261a7bfea7cb726`. Its
`kernel/arch/dreamcast/hardware/maple/vmu.c` write path does not check the final
BSYNC response, reuses `UNSENT` for completion and an IRQ retry state, and marks
timed-out frames vacant without cancelling queued or in-flight work. The
public block-write wrapper makes up to four complete-write attempts.

These conditional transport defects remain outside this change. Application
readback detects many failures but does not prove correct SDK write completion
under every timeout or retry. The observed hardware reports do not include a
write timeout, FILEERR or AGAIN trace tying these conditions to the black VMU.

A separate [bounded-write prototype](vmu-bounded-write-prototype.md) now
exists behind an explicit compile gate. It is absent from the manager,
importer, read probe and retail loader. Its stronger host fault oracle and
pinned-SDK compilation pass, but no physical write has been performed and
disposable-card acceptance is still required. It does not change the
completion guarantees of the manager's current SDK write calls.

## Verification scope

Manager host tests use real FAT32/exFAT SD images with ASan/UBSan and filesystem
checks, but simulate physical VMU reads and writes. They cover mini-game
placement and refusal without writes, metadata preservation, staged backups,
and injected application I/O failures. The separate bounded-reader tests run
the actual reader against a Maple frame/cache model, including malformed
responses, busy acquisition, timeout cleanup and late completion. Hardware
retesting is still needed for the physical-card behavior.
