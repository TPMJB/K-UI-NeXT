# Shared launch baseline: Evolution 2 on f8807cb16561

The owner reports approximately 20 seconds from K-UI to bootstrap, then another
35 seconds to the game: approximately 55 seconds total. The uploaded timing
file independently measures 21.704 seconds before the worker handoff is ready.
It identifies Background New, virtual VMU On, and the delivered combined A2
module `0:/KUI/apps/games/retail-native-dual.kui`, payload CRC `9B37B199`,
290,040 bytes. Both GUI and module report `f8807cb16561`. This attempt is not
evidence of a stale bank or selection of the standalone reader.

| Measured GUI work | Seconds |
| --- | ---: |
| Executable read and full-source hash phase | 8.788 |
| Executable lifetime proof | 4.721 |
| Mapping proof/replay phase | 2.733 |
| Shared card/history preparation | 3.641 |
| Other flat phases | 1.821 |
| Total before handoff-ready | 21.704 |

Nested detail intervals must not be added to those flat phases. The first
three phases total 16.242 seconds. `sdk_cache_hit: 1` means a saved exact
mapping was replayed after the full executable read and lifetime proof; it
does not mean the earlier advisory shortcut avoided that work. There were
145 source callbacks, 4,739,448 attempted source bytes, four source seeks,
and two GUI SHA operations over 8,081,344 bytes. The latter count is the full
4,057,056-byte source plus its independently proved 4,024,288-byte live prefix.
The report excludes its own final I/O and the later bootstrap.

## What the public loader code establishes

The shared advisory lookup uses a path index first. If no usable index exists,
it scans a bounded directory for a unique compatible candidate. Missing,
ambiguous, corrupt or unreadable configuration can conservatively return to
full GUI preparation. The uploaded log does not record which of those reasons
occurred. A successful full preparation can publish a direct index for the next
launch; the repeat's actual console report remains needed to establish that
this happened on the owner's card. No cache files from that card were supplied.

## What remains to unify

Batching and guarded native verification are already shared defaults. They do
not remove the duplicated GUI preparation when advisory lookup cannot select a
candidate. Normal native card preparation is currently coupled to a ready GUI
mapping even though its card geometry does not consume that mapping. The stage
supports an empty seed and performs fresh lifetime/mapping discovery
before installing hooks. This provides a shared avenue for deferring GUI work.

Blindly skipping GUI discovery would change compatibility: that discovery also
classifies combined-reader failures and selects the standalone New reader when
appropriate. A universal deferred path must preserve that automatic fallback,
mandatory cooked/patch/CDDA checks, exact collection owners, card assignment
refusals and accepted geometry. No title-specific eligibility list is proposed.
The collection's live allocation-authority transfer also remains distinct from
ordinary native startup's fresh filesystem admission; retaining a cache/profile
alone cannot replace that authority.

The additional 35 seconds are not attributed yet. The unchanged module's existing
progress captions can distinguish live ownership, owner load/BOOT2, executable
verification/discovery, original allocator work and final worker/hook setup.
A video of the existing build can locate that interval without a new diagnostic
build. One final bootstrap photo cannot time the earlier phases.

This audit changes no implementation or delivered ZIP. The original-boot issue
is provisionally resolved by the owner and is outside the next Games change.
The supplied console timing is preserved under
[`evidence/evolution2-f8807cb`](evidence/evolution2-f8807cb). Original-backed
local audit outputs are not part of this repository documentation update.
