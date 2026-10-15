<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Games launch options

Tyrfing preparation adds two one-time native boot patches, detected defaults
and a durable pre-handoff report. This is source preparation, not a published
2.0 release. Console acceptance of these additions is still required.

## Two boot patches

Open a game, press **Y** for Options, then choose **Advanced → RAM patches**.
Each of the two slots has an Enabled toggle, Address and Value. Select an
address/value and press **A** to edit eight hexadecimal digits. Left/Right
selects a digit; Up/Down changes it; A saves; B discards the current edit.
A second B returns to Advanced. START never launches from the editor.

Enter patches established for the exact game revision. These are one-time
32-bit writes to the loaded boot executable, after its original CRC and the
loader's integrity checks pass. They are not continuously enforced cheats.
Both complete four-byte destinations must fit inside the actual executable
bytes, excluding final-sector padding. The editor displays that range.
Physical, cached P1 and uncached P2 RAM aliases identify the same destination;
two enabled slots cannot target the same word. Zero is a valid patch value.

The first implementation supports ordinary native launches with CD music Off,
including Standard and Background readers. Windows CE, CD music packages and
diagnostic stages do not accept these patches. Choosing CD music On requires
turning patches Off first. An old package without the new capability refuses
an enabled patch request before handoff; update runtime and Games payloads
together. With patches Off, the executable and resident behavior stay as before.
The source game and loader files on the card are not patched.

Patch settings belong to the exact selected image path. Original and converted
2048-byte images keep separate choices. New saves use settings format v2;
existing v1 settings load with both patches Off. Preserve the two settings
files when backing up an installation; older runtimes cannot read v2 records.

## Restore detected defaults

In **Advanced**, select **Restore defaults** and press A. This saves defaults
for the selected image: Standard for native games, Background for Windows CE,
CD music Off, Automatic boot encoding and both RAM patches Off. Automatic
encoding follows the inspected image. Other images retain their saved choices.
A failed save is shown in the existing notice; the choices remain in memory.

## Launch reports

The normal shell launch path checks the source executable CRC and prepares a
report while the filesystem is still available. Checking a raw GDI executable
adds a complete read of that file before handoff. Reports contain the build,
selected image, actual reader/storage/encoding, chosen package/CD music profile,
source IP/executable sizes and CRCs, and canonical enabled patch addresses.
They contain no game payload bytes and explicitly say that game start is
unconfirmed. The report describes successful preparation, not gameplay success.

Reports alternate between:

- `/KUI/apps/games/last-launch-a.txt`
- `/KUI/apps/games/last-launch-b.txt`

The highest valid sequence is newest. Each bounded text record includes a
CRC32 footer; writes are synced, closed and fully reread. The previous valid
slot is retained if the other write fails. On a full or write-protected card,
launch preparation logs that a new report is unavailable and can still boot.
A cancelled launch can therefore leave preparation evidence without a handoff.
Images stored in `/KUI/apps/games` skip reports, so a report filename or FAT
short-name alias can never overwrite a sibling source track during launch.
The independent read-test/probe actions do not write these launch reports.

The legacy comparison and remaining runtime work are recorded in
[audit-legacy-games.md](audit-legacy-games.md) and
[games-parity-roadmap.md](games-parity-roadmap.md).
