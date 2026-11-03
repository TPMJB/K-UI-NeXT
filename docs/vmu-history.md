<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Virtual VMU history

Per-game virtual cards now have dated restore points. Open a game's exact
virtual-card chooser, select the card, and press START for History. START
creates a manual checkpoint; A previews restoration and asks for confirmation;
X exports the selected version (or the current card when the list is empty).
Y refreshes. Dates use the Dreamcast's local RTC; an unavailable date is shown
as unknown rather than invented.

Restoring makes a **new, independently verified playable card** and selects it
for this game. The original card, its journal, other games' assignments, and the
history source remain intact. The former card remains in the chooser and can
be selected again. For an older dedicated card, its original identity and
verified game binding join the candidate catalogue in the same publication
that selects the restored card. No import marker is added to the original.
These controls do not operate on physical VMUs.

The launcher also checkpoints the actual card it resolves, including automatic
defaults and new cards, before its raw filesystem ownership scan or SD unmount.
Before creating a missing card, it also verifies the allocation of all managed
VMU/history files, so a damaged free-space bitmap cannot lend a history cluster
to the new card before the checkpoint runs.
VMU Off does not look up a card, checkpoint it, or publish a session receipt.
History or receipt verification failures refuse that virtual-card launch with a
reason; there is no success message for an unverified backup.
This automatic boundary applies to virtual-card launches through the Games
image launcher. Physical GD Play/disc-boot preparation and physical VMU tools
keep their existing workflows and do not acquire automatic history here.

After the pre-launch checkpoint, two alternating CRC-protected receipt records
pin the physical SD identity, partition, game product, pair and card identity.
On the next K-UI startup or return, the sole storage worker admits the previous
card again, checkpoints changed data, and publishes a cleared receipt only
after checkpoint verification or unchanged-data deduplication succeeds. The
label is **Previous session**: the receipt does not prove that gameplay ran,
that a return was clean, or that a game finished its save. A refused launch can
therefore leave a harmless pending receipt which deduplicates at the next boot.
Foreign/stale identities and receipts with no valid predecessor are retained
and reported, rather than being silently discarded.

History uses the existing virtual-card library admission. A complete committed
redo journal block is validated against the original card identity and mapping
and overlaid into RAM. The stored `card.vmd` and `card.journal` are never edited
or replayed by history. Torn/uncommitted journal markers follow the existing
store's recovery rules; valid markers with corrupt payloads or foreign identity
refuse. This is a recovered card boundary, **not an atomic whole-game save**.

Checkpoints live under `KUI/vmu/history/<product>/h<16-hex-sequence>/`, with a
131072-byte `card.vmd` and a 512-byte provenance record written last. The record
includes source identity, sequence, local timestamp, reason, image SHA-256 and
CRC32, plus its own CRC. Listing verifies the complete image, VMU structure and
record again. Incomplete or corrupt versions are omitted and counted. An old UI
selection pins the exact sequence and hash; it cannot restore a different row.

The newest eight complete versions per game are retained. Identical latest
data creates no new version. Older history-owned files are removed only after
the replacement is complete and verified, publication marker first. A failed
retention pass can temporarily leave a ninth complete version; retrying an
unchanged checkpoint finishes that pass. Failed/corrupt partial directories are
not reused or automatically deleted. A bounded 256-entry scan eventually
refuses further work if such debris accumulates; preserve the SD and inspect
the history directory before cleaning it manually. Unexpected extra files
prevent automatic deletion of that directory.

Explicit exports publish a verified plain VMD and readable metadata under
`KUI/exports/saves`. Temporary `.part` files never have a final VMD name. The
browser/HTTP export root does not point at active cards, journals or history.

Validation uses the real FatFs/card/journal code with ASan/UBSan: FAT32 with
512-byte clusters, exFAT with 4 KiB and 128 KiB clusters, interrupted snapshot,
restore and receipt writes, committed redo not present in the base image,
deduplication, corrupt records/images, stale identities, original-file
preservation, retention and explicit exports. The host runner uses a private
FatFs formatter; independent mkfs/fsck utilities were unavailable in the
executor. Physical Dreamcast timing and power-cut acceptance are still needed.
