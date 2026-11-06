# Correction to the ab1 launch test

The ab1 console test reported a rejected Smash Pack menu loader, about 20 seconds
before Sonic reached bootstrap with Background On, and a brief pre-splash
message. The GUI correction preserves all 26 installed module/helper files.

## Reproduced Smash failure

The actual shipped outer has the collection magic pair in compiler comparison
literals as well as its real 224-byte record. The finder rejected the first
pair before checking the typed 16-byte header. Both CE child finders made the
same mistake. Discovery now filters the complete record identity. The actual
shipped ZIP passes the production outer and child module readers, including
both exact helpers; malformed, nonblank, duplicate and unaligned records remain
refused. No runtime/game worker changes are needed for this correction.

## Shared preparation work

The previous cold-stage policy did not establish that GUI preparation was fast.
Mounted verification used 4 KiB comparison reads; new 32 KiB windows preserve both
complete image passes, all hashes/CRCs, cancellation, and the durable publication
sequence. A 4 KiB stack fallback remains if temporary heap allocation fails.
On the 128 KiB exFAT fixture, unchanged session device reads fall from 1,133 to 657 and
changed-session reads from 1,663 to 1,047. Device read sectors, complete disk SHA, writes
and sync counts are identical. These are command-count measurements, not
console time estimates.

The GUI uses the existing byte CRC implementation for its public checksum
functions. Its two linked entry points are executed against an independent CRC
oracle. Game worker/resident implementations and memory reservations are kept.
Hot executable proofs reuse a freshly computed digest within the same operation;
Sonic hashes 6,751,168 rather than 13,502,336 executable bytes, and Skies 5,460,080
rather than 10,920,160. Shorter program prefixes receive their own exact hash.
Cache misses retain complete discovery and its independent checks.

## Persistent diagnostics

Native Standard, Background New and Background Legacy launches, with VMU On or
Off, write `/KUI/apps/games/launch-timing.txt`. It records requested mode, source
read/seek counts, verification bytes, SDK/cache work, VMU/history work, cleanup
and audio shutdown. The measured interval ends before report I/O. Failed native
preparations also produce a report when storage and the reserved-path checks
permit it.

`/KUI/startup.txt` captures GUI startup/version-selection results and logs before
the splash or a checked runtime chain. Earlier firmware/CD messages and later
storage-release errors are outside this record. The transient neutral screen
now says “K-UI / Starting.” Strict bank/replacement validation remains in force.

Console timings still need the replacement build's report. Native SF/PSII
return is retained, and the CE return witness remains diagnostic. This change
does not enable a new Background path for Smash Pack or implement prefetch-only
memory allocation.
