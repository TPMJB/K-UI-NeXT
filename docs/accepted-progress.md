# K-UI accepted progress

## Standing delivery rule — 2026-11-06

The owner uses manual-copy ZIPs as the default delivery workflow. This persists
across sessions and supersedes historical wording that treats ZIP delivery as
a one-off exception or instructs future tests to use KUPs. KUP delivery or
updater-engine repair requires a separate explicit request. A GUI ZIP includes
both identical physical aliases; normal ZIP delivery excludes activation
records, banks and user data. The current Games/boot implementation is unchanged
by this documentation correction.

## Current console checkpoint — f8807cb16561

The delivered `KUI-Shared-Game-Launch-06NOV2026-2210-UTC-f8807cb.zip`
(SHA256 `dc607c3e89ab6bac5c718b738bca9990ffd85ec39abb308140de280c4a7503e9`)
remains the unchanged native timing candidate. Host/linked qualification is
recorded under `evidence/native-shared-f8807cb`; no Dreamcast timing or Sonic
live-IRQ acceptance has been supplied. Compare two or three normal library
games with the preceding measured behavior before another runtime change.
Capture GUI/module identity and both launch phases with usual matching options;
retain the automatically generated launch record. Existing operation-count
improvements must not be presented as measured console speed-ups.

## Sega Smash Pack accepted: cc81e63ca4d2, 2026-11-06

The owner accepts the collection integration as complete after this exact
sequence succeeded in one uninterrupted console session:
`VC2 -> Shining Force -> PS2 -> Sega Swirl -> VC2 -> ALL OTHER GAMES -> VC2`.
This includes repeated VC2 launches after both the saving games and the rest
of the collection, the condition that previously failed. This acceptance
supersedes the pending/rejected status of the earlier candidates below; their
individual failures remain historical evidence.

The accepted software is local source
`cc81e63ca4d29f51b02bdd49e95d8cedbe689d7d`, identical in source tree
`6f4814f643f5a39fd4728e79d3703167bb40e741` to published commit
`6d489a90276776a4d62e4f68b9efdc3161c6c1c0`. The tested archive is
`K-UI-Smash-cc81e63ca4d2-SCI-DAMAGED-BLOCK-CANDIDATE.zip`, SHA256
`eb469340a2a6732a356a77536ca8a04a261740c3bca0620e84266f06c204ca6b`.
Its embedded/standalone KUP SHA256 is
`3c85ac9927a7ade59f69001babbb33a88175c54b592589b86e59a9c13e476209`.
Retain these exact tested bytes; a documentation or merge commit is not a
new tested binary, and no replacement package is required for this acceptance.

Preserve the current VC2/Swirl helper components, the native72KiB worker,
independent background-reader ownership, original Genesis options/Save Game,
working returns, shared-card admission and loading improvements. The shared
SCI lost-byte quarantine, bounded same-LBA retry, normal CMD18 batching and
full block CRC checks are now part of this accepted baseline. Do not restore
an earlier helper/decoder, bypass validation or retune individual games to
recreate it. This collection acceptance does not qualify untested loader
families or turn the separate generic CE audit into an installed global rollout.

VC2 still takes a long time to load; the owner supplied no new duration or
VMU-on/off comparison in this acceptance. Record that as an optional future
performance investigation, not a remaining Smash completion blocker. Do not
start another VC2 change or request another console run without a new request.
If revisited, preserve this accepted baseline and measure the complete phases
before applying the existing shared batching/card-admission lessons. Work
counts alone do not establish a user-visible speedup. See
[the shared policy](global-launch-policy.md) and
[the accepted read-recovery repair](sci-damaged-block-reread.md).

## Universal loader integration started: 2026-11-06

The owner requested an ordered roadmap and implementation toward one broadly
compatible maintained build. Start from Main `d393b859a9ee`; the roadmap is
`universal-loader-roadmap.md`. Shared correctness and reproducible deployment
come before new service memory layouts, native SDK extensions, coordinated
CDDA streaming and broader lifecycle/format/transport coverage.

The first integration targets generic CE payload-write width, a recorded
ordinary/CE/CDDA build plan, fail-closed cold-rollout output and actual native
service geometry reporting. It preserves the accepted collection components
and existing worker reservations. The already merged CE completion fixes and
SCI lost-byte guard remain required in affected rebuilt readers.

This entry records authorized scope, not console acceptance, completed global
deployment, new virtual-card support for DOA2 or a CDDA/FMVs performance result.
Record build/package outcomes and any refused family before offering a new
installation. Keep accepted Smash gameplay and return results above intact.

### Shared pre-bootstrap follow-up

The owner reports almost twenty seconds before bootstrap in Power Stone 2 on
the first consolidated test and explicitly rejects per-game performance fixes.
The follow-up is in `shared-gui-launch-preparation.md`: common GUI card
admission and snapshot batching, plus launch-scoped reuse of a freshly verified
frontend recovery checkpoint. It preserves all twenty non-GUI files from the
72db614 test, the accepted collection and game-time worker geometry.

Existing executable/history batching and aligned GUI decoders are preservation
requirements, not new achievements. New tests bind those actual defaults and
compare the new shared paths with single-read/full-checkpoint controls.
The owner now reports Power Stone 2 reaches bootstrap in about four seconds
on 9d6680a, versus almost twenty seconds before. Record this as the reported
preparation improvement, separately from bootstrap and save/return coverage.

### Native bootstrap services: 7fca3e3 candidate

The owner asks to carry the faster Smash bootstrap lessons into shared native
stages while retaining VMU hooks, and recalls about two seconds through Smash
bootstrap. Exact dbadd source/binary comparison established three later changes:
cached whole-executable CRC, fresh metadata/snapshot batching and removal of
native informational holds. Older cached extent/replay/FAT work was already
installed and must not be claimed again as missing.

`shared-native-bootstrap-rollout.md` records the new 26-file candidate,
five-service coverage, preserved workers/geometry/Smash bytes and qualification.
The GUI's exact Standard and retained native-CDDA manifests are promoted to
the default resource paths without rebuilding these binaries. The owner now
reports no noticeable improvement with the new build ID. The exact final
reader was not captured, so this neither establishes a bootstrap speedup nor
diagnoses an updater failure. Treat the bootstrap performance change as
unaccepted. No universal two-second target or new compatibility claim follows
from the owner's Smash comparison.

The owner's explicit scope is Games only. Freeze startup/splash, menu,
branding and updater implementations during this integration. Prioritize
shared preparation and bootstrap mechanisms across every applicable route;
record their actual installed coverage and final reader identity. Do not send
another performance candidate based only on a changed GUI label or model counts.

### Shared Games-only follow-up: eeed38bd540e

The branch now implements shared CE kernel-read reuse, indexed advisory native
hints, cached immutable discovery on a miss, same-invocation native role-proof
reuse, zero default cold photo holds and final-reader source reporting.
All13 cold build roles/16 aliases qualify. Five current native workers and nine
additional live components preserve their complete initialized code/geometry
against the retained 7fc cohort, apart from permitted build labels. Accepted
Smash files remain exact. No startup/menu/splash/updater source changed.

`shared-game-launch-performance.md` and `evidence/games-eeed38b` record scope
and implementation evidence. Default Standard/CDDA inputs now bind this exact
cohort. The manual test avoids KUP installation and explicitly backs up the
two saved selection records so copied physical files cannot be masked by a
bank. Those records, banks and user data are not overwritten by the archive.
Ordinary CE's `ce-probe.kui` path collision remains an explicit deployment gap.
The new binaries have no console speed or compatibility acceptance yet.

Delivered manual test: `KUI-Games-Manual-06NOV2026-1424-CST-eeed38b.zip`,
SHA256 `17ff15b9fc3ac08255e52549ade2b9936f70e427e99a0643bb09ed3777ae3d28`.
The archive contains28 software files and its README/BUILD/LICENSE, with no KUP,
activation records or user data. `evidence/games-eeed38b/delivery.json` records
the actual final source-reader fingerprints. Back up the two selection records
outside their active directory for this manual ROOT test; keep them for restore.

## 0a hardware report: damaged reconstructed block reaches the sector parser

The owner's three0a2ddd9bb9b6 photos show FN12 PIO_TRANSFER in the original VC2
driver, loading `VC2/BIN/COMMON/F_COMMON.BIN`. The sector parser rejects a header
after0x69800 bytes. The accepted source block retains a DMA lost-byte marker;
the mapper succeeded and the bus fault flag is clear. The blue pages are the
now-visible terminal report. This package retained91fa's actual current helper
and arbitration code; it did not roll them back. Fresh-first startup is still
not reliable.

An old-production negative control now demonstrates that CRC erasure repair
can accept a wrong block with an invalid raw-sector header. Shared SCI source
now rejects all reconstructed blocks and uses its existing bounded same-LBA
retry/programmed-I/O fallback before game delivery. Normal batching, CRC checks,
buffer geometry and current helper implementations remain required. This is
a supported repair to the observed failure path, not proof of the console's
exact corruption or a declaration that every freeze is solved. See
[the repair and evidence](sci-damaged-block-reread.md).

## 91fa hardware rejection: repeated VC2 fails before its VMU message

The owner tested the delivered91fa package and reported this exact sequence:
`VC2 -> Menu -> Play -> return to main -> Sega Swirl -> Shining Force -> PS2 -> Vectorman -> VC2 -> VC2 loading screen -> Dead`.
The first VC2 visit reached play and returned. The later visit froze after the
bottom-right Loading message disappeared, before the visible VMU message.
On a working run, that interval contains another approximately5–7 seconds of
SD activity before the VMU message and opening credits. No new LED behavior,
error code, framebuffer color or failing instruction was reported for the
failed run. Do not infer those details.

91fa is not accepted as a reliable VC2 fix. Its reproduced drive-status/VMU
loan defect remains a valid software correction; this report does not establish
that defect as the freeze cause. The latest failure is specifically before
the visible VMU check, which does not prove virtual-card code is inactive.
Trace the loading-to-VMU handoff and repeated-launch state against this exact
sequence. Do not request another console run merely to recheck a speculative
reset or a code path already excluded by actual compiled configuration.

The separate audit's generic CE completion fixes do not alter this native
foreground/MMU/PIO configuration. Shipping those alone would not constitute
a new VC2 test. Preserve current workers, options/Save Game, graphics policy,
loading improvements and working returns while obtaining new evidence.

## Broader audit follow-up; delivered91fa remains unchanged

The owner requested a systematic shared loader/VMU review after the repeated
VC2 regressions. The delivered91fa package fixes the verified VC2 GD-control
arbitration race and preserves its actual embedded/staged four-module quorum.
It is still awaiting console confirmation; no later audit fix is in that ZIP.

The separate audit source corrects stale GUI bundle-object selection, a shared
CE removed-callback flag that blocks VMU loans, and a missing DMA-end wake on
failure of an active CE transfer. Old-source controls reproduce all three;
new regressions are part of the normal test targets. Native GD preprocessing,
current workers and all collection gameplay/return code remain preserved.
Affected installed CE cohorts have not been rebuilt by this audit. See the
[findings, evidence and open gaps](shared-loader-audit-2026-11-06.md) before
assembling another update or claiming these source fixes have rolled out.

## Current hardware failure: VC2 also fails as the first selection

The owner now reports VC2 loading its VMU and then freezing on a cropped white
screen even as the first game after boot. Later selections can freeze black.
Repeated earlier successful launches do not establish reliable startup. The
native RAM-retirement candidate946 is not accepted as a solution; a
cleanup-only explanation cannot account for this fresh first-selection case.

An active-runtime defect has been identified independently of that cleanup:
while the virtual-card worker borrows the SD reader, the low GD gateway
returns -1 for non-request control calls. The original VC2 WSEGACD drive-status
call at return RVA23CE converts any nonzero result to C0000185 without retry.
The existing request-only wait gate therefore leaves a permanent-error race
in a normal, otherwise idle reader. Source-admitted control calls must wait
for the actual borrower to release ownership and then execute normally; they
must not receive invented results. CHECK's status4 already sleeps/retries in
this exact driver and EXEC retains its existing busy behavior.

The correction is limited to VC2's high GD-wait helper and its component
identity. Swirl's helper, collection return controllers, native72KiB worker,
Genesis options/Save Game, graphics copy policy and read batching are outside
this change. The original driver error branch and the old linked wait gate
are required regression controls. A software reproduction of that race does
not establish that it caused every photographed console freeze.

## Later-reentry candidate: retire collection-native game RAM

The next candidate preserves075e's working initial sequence and cache fix.
Only the collection-native terminal return gains the existing CE return's
dead-RAM clear. The call occurs after successful worker retirement, original
argument capture and hardware/cache quiescence, before any new capsule read.
It never executes on BUSY/refusal and does not apply to generic session or
background-reader returns, which may retain live high-RAM objects.

The accepted profile's complete remaining reader/code/data/stack closure is
below AC010000. The old high game/worker frame is never resumed after this
boundary. This closes a retirement difference: SAMPLE leaves upper RAM
intact, and the CE fixed graphics allocator can return its prior contents
without clearing them. No actual bad graphics-buffer read was captured; the
change is a supported clean-retirement candidate, not a proven freeze cause.

CE controllers, immutable helpers, raw readers, native worker/reservation,
original options/Save Game and game read paths are preserved. The native
terminal controller grows only to add the clear; its low bounds/stack and
captured context must still pass linked checks. See
[the retirement note](smash-native-ram-retirement.md).

## 075e hardware result: initial sequence passes; later VC2 freezes after Start

After delivery of075e785e9308, the owner reports playing Sega Swirl, Shining
Force, Phantasy Star II and VC2 successfully, then every other game in the
collection successfully. On the subsequent VC2 run, pressing Start to enter
its main menu caused a small diagonal picture shift and a freeze.

Preserve the one-bit direct cache-entry writeback correction and the working
games, native options/Save Game, return paths, workers and helpers. The initial
Swirl → SF → PSII → VC2 sequence is now reported working. The remaining symptom
is a later VC2 title-to-menu transition; do not call it the same before-title
failure, infer an exact order for the other eight games, or claim a captured
PVR register fault from the picture shift alone. No new loading time was given.

The source package remains075e; no additional gameplay fix is established by
this report. Its current source is available for review in PR31; live Main
still has the preceding eddbb01 source at this checkpoint. Further changes
must address a supported later-reentry defect and retain this hardware progress.

## Warm-entry follow-up: correct terminal cache writeback

The current follow-up starts from the exact published Main eddbb01 tree. It
changes one bit of the terminal cache-array address: F4000008 to F4000000.
The former selects associative writes. CE calls this cleanup before disabling
its MMU, so writing a physical tag back through the associative interface can
miss or mismatch the game's virtual translation and silently do nothing.
The following CCR invalidation can then discard dirty data. Direct entry
writes avoid that translation dependency and retire every dirty cache entry.

This is a confirmed cleanup defect, not a captured cause of the console black
screen. The low64KiB firmware/IP area is deliberately preserved across returns
and is not all covered by the earlier bounded low-reader publication. Losing
dirty bytes there violates that preservation contract. Which live bytes were
affected in the reported console run remains unknown.

The source change preserves all instruction bytes, loop length and linked
controller geometry; only the literal's association bit changes. No worker,
helper, native options/save flow, game read path or return routing changes.
The new linked-instruction cache test models the documented SH7750 hardware
semantics and includes the old literal as a failing control. Console warm-order
acceptance still requires the exact Swirl → Shining Force → PSII → VC2 check.
See [the cache-retirement note](smash-cache-retirement.md).

## Current review handoff: VC2 fails after the multi-game sequence

The owner reports that Sega Swirl → collection → Shining Force → collection →
Phantasy Star II → collection → VC2 fails with a black screen **before VC2's
title** on the current ca96612c27fc candidate. This is a warm-start failure,
not the later VC2 Return action. The precise failing instruction and logo
boundary are unknown. Do not describe the current package as fully accepted.

The owner requested publishing the current work to Main for independent review.
No production game fix was made after ca966; preserve its Genesis options/Save
Game flow, the existing VC2 return work, current workers and all integrity
checks. Read [the review handoff](vc2-warm-launch-review.md) for established
results, exact preservation boundaries and limits of the read-only audits.

## e371 hardware result and required Genesis save menu

After the requested VC2-first/title/Return check, the owner reports that it
"actually worked" on e3714617ad28. Do not invent a separate after-Swirl result,
a new timing, or an all-game matrix. Preserve the CE controller/startup layout,
both helpers and the72KiB worker.

The owner identifies a functional regression: Shining Force and Phantasy Star
II need the original emulator options menu to commit cartridge SRAM to the
VMU. The direct A+B+X+Y+Start shortcut skips that menu, so instructions to finish
its Save Game action before that combo were unusable. The follow-up removes
the2-byte combo hijack. The original combo must open options with Save Game;
only explicit Return to Menu reaches the existing checked117F0 reset hook.
Retain the original dirty-save confirmation. Draining VMU I/O does not create
a cartridge save which the engine has not requested.

The RPG flow is: save within the RPG, A+B+X+Y+Start to open options, select Save
Game and wait for completion, then Return to Menu. Source/linked checks prove
those paths; durable progress after power loss remains a console check.

## cd3f hardware result: black screen; startup preservation failed

The owner reports that the next passive-reader candidate now gives a black
screen and rejects further startup regressions during return-only work. The
exact black-screen boundary was not supplied; do not silently claim it occurs
at a specific logo, on a fresh launch, or only after Return. This candidate is
not accepted. No successful startup or return result supersedes that report.

The earlier claim that the assembler installer matched eb6 was too narrow.
Its linked bounds changed. Compared with the console-booted3337 baseline,
the candidate writes456 more initialized/BSS bytes before NK, relocates its
saved previous-E0 cell and runtime, and changes the high copy entry. Identical
raw512 and helper41152 bodies do not establish equivalent startup behavior.
No concrete low-RAM owner conflict or failing instruction has been found;
the black-screen cause remains unproved. The bounded original NK replay stops
at its first unauthored device access and cannot certify later boot behavior.

Return-only follow-ups must hold the actual3337 startup write-address trace,
copy entry, runtime window, initialized extent and BSS/previous-hook addresses
fixed. Verify actual linked state and forwarding behavior, not an assembler
source hash alone. Reclaiming space inside exit-only code is permitted only
with the same full IEEE CRC and all integrity predicates intact; gameplay
workers, immutable helpers and ordinary read implementations remain pinned.
An exact footprint is a containment boundary, not hardware acceptance or a
diagnosis of the reported black screen.

The bounded follow-up fits the complete current return repair inside the
accepted7340-byte VC2 and7500-byte Swirl images. Only the controller's exit-time
CRC changes to a complete bitwise IEEE implementation; ordinary reader/worker
and helper bytes stay unchanged. The linker pins copy/runtime/BSS addresses,
and the new actual-instruction startup contract compares cache publication,
write addresses, forwarding and preserved CPU state against3337. Both baseline
self-controls and candidates pass; the expanded cd3f image fails that gate.
This restores an explicit startup containment contract while retaining the
canonical raw repair, all13 guards and the full destination CRC. It is not a
rollback to older workers or an assertion that console startup is repaired.

## 02ec hardware result: white display during VC2 startup

The owner reports a white, reduced display area after a logo described as
"VM2" while testing the protected-reader candidate. The photo contains no
RET diagnostic. Whether this was VC2 first after power-on or after Swirl was
not supplied. Do not silently identify that logo or transfer the earlier3337
fresh-boot acceptance to this candidate. Neither raw-reader return repair nor
the after-Swirl transition is hardware accepted on02ec.

The bounded installer audit found no register/SR clobber or new graphics MMIO.
However,02ec added a512-byte startup write at B400..B600 and validated it after
helper loading. A full-game ownership contract for that extra saved copy was
not established. This is a changed startup behavior, not a proved explanation
of the photographed display.

The next candidate removes that capture and its startup CRC: its installer is
byte-exact eb6. The authenticated initialized return controller instead carries
the current reader's260-byte prefix; its252-byte zero tail is reconstructed.
Every build must match the fresh linked resident and raw512 file against that
asset and the reviewed full-image SHA. No old worker supplies these bytes.
Both validation boundaries check the complete logical512 bytes against the
original launch-sealed CRC, while the prefix also belongs to controller code
CRC. After freeze/quiesce/cache-off/MMU-off the controller restores all512
bytes at8100 and checks the actual destination before executing it. No new
B400 capture, memory lease or changed integrity expectation remains.

Both profiles pass linked recovery, refusal, late-install and original-source
checks. These establish software behavior, not console startup or return
acceptance. The next console check is full power-off, VC2 first through its
title and Return, then Swirl Return followed by VC2. Preserve the72KiB native
worker, immutable helpers, native/Swirl return fixes and all sector CRCs.

## eb6 hardware result: VC2 return fails the raw512 integrity check

The owner reports `RET CE010004`, detail `00000800` with the eb6 candidate.
The CE01 prefix confirms the revised controller executed. Only predicate11,
the full512-byte raw reader at8100, fails; the other12 predicates pass. The
refusal precedes helper retirement, hardware quiescence and outer-capsule I/O.
This identifies the failing integrity domain, not a bad SD card or a particular
writer. No result for the after-Swirl AM2 freeze was supplied for eb6.

The bootstrap raw reader was authenticated before NK and during helper
hydration, but its continued immutability through a complete CE game had no
separate preservation contract. The hardware result disproves that lifetime
assumption. Exact-source review found WSEGACD reads of8100 and the original
game's reset route, but no established writer; missing middleware and firmware
internals limit attribution. Do not claim a particular game write was proved.

The02ec follow-up captured the complete admitted512 bytes into presumed unused space in
the existing low return lease. It validates that reserved copy against the
original sealed CRC before publishing the return hook. Both return integrity
checks validate that copy. After quiesce/cache-off/MMU-off, it restores8100 and
checks the complete destination against the same sealed CRC before any reader
instruction can execute. The old execution check is moved to its execution
boundary, not removed or refreshed from changed live bytes. Changed saved
code or padding still refuses; a failed restore refuses before SD I/O.
That extra capture is superseded by the initialized-image design above.

## 3337 hardware result: Swirl returns; subsequent VC2 freezes

The owner confirms `3337e554b21c` returns from Sega Swirl to the collection
menu. Preserve that accepted private-callback dispatch. The owner then launched
Virtua Cop2 in the same session: its VMU loads, Sega and then AM2 logos appear,
then the display becomes white and freezes before the main screen. The owner
explicitly confirmed this was **after Swirl return**, not the first game after
a fresh boot. A subsequent full-power-cycle comparison on this same build
**successfully boots VC2 first**. Preserve that accepted fresh boot and treat
the after-Swirl startup failure as a separate transition failure.

VC2's own return still freezes with `RET CE000004`, detail `00000000` on that
fresh run. Unlike the old d04 predicate, the3337 mask path should only call
that failure with a nonzero mask. Audit the executed controller, diagnostic
arguments/display and actual delivery selection before treating the displayed
zero as an identified failed predicate. No exact new loading time was supplied.

Actual linked replay passes the Swirl(owner4)→outer(owner1)→VC2(owner3) receipt
sequence. The
new VC2 helper and low reader independently clear their declared mutable
workspace before use. Those checks exclude the demonstrated stale receipt or
private-state hypotheses within their modeled boundaries; they do not prove
hardware, interrupt, audio or graphics state is safe after a CE return.

The source audit also found omitted PVR-interface/sort-DMA retirement steps in
the shared return quiesce. However, VC2's original SAMPLE executes the same
original cleanup again before loading CE. This is an independent ownership
gap, not an established explanation of the later white freeze. Do not ship a
speculative device reset as a diagnosed VC2 repair. See
[the bounded source audit](evidence/vc2-after-swirl-render-state-source-2026-11-06.json).
The same3337 fresh-first comparison is now accepted as described above. Do not
repeat that test or call the after-Swirl failure a fresh-start regression.

The follow-up restores original MAIGO's complete main-RAM retirement clear
after quiescence/MMU-off and before loading the outer capsule. It also checks
the previously omitted PVR-interface/sort DMA owners before RAM replacement.
This restores an original return contract; it is not yet a hardware-confirmed
white-screen repair. The exact packaged3337 diagnostic chain renders nonzero
failure masks in replay, and package/ROOT/helper checks found no supported
stale selection. A separate CE01 error prefix will identify the executing
controller revision without dropping any guard. VC2 return remains open.
See [the RAM lifetime record](smash-ce-return-ram-lifetime.md).

## d04 hardware result: both CE returns fail

The owner reports `d04dd36fdc7b`: selecting Return in Sega Swirl reboots the
entire console, rather than returning to the collection menu. The Virtua Cop2
combo freezes with `RET CE000004`, detail `00000000`. That is the controller's
initial runtime/integrity validation refusal, before idle-state retirement or
outer-capsule I/O. It currently combines several checks and does not identify
which one failed. Do not call this a working CE-return package or infer that
Swirl completed the same path. Preserve the native-return/loading baseline
and the restored CE PIO helper-loading policy while investigating.

The original Swirl frontend now reproduces d04's reboot in37 executed SH
instructions: its privileged wrapper calls the private MAIGO callback at8300
with argument0 / return address419a6. The controller incorrectly treated that
private argument as BIOS function0 and forwarded it to real firmware. The
follow-up recognizes the source-owned private callback separately, including
the original fallback thunk, while unrelated firmware commands still forward.
This repairs the demonstrated dispatch error; the later3337 console result is
recorded above.

The actual frozen helper initializer and three original raw config reads were
also composed with d04's controller validation. All controller/runtime, raw512
and helper41152 bytes remained unchanged, and validation passed. That does not
locate VC2's later hardware refusal. The follow-up preserves all13 predicates
and reports their failure bitmask on the second RET line. It does not bypass
integrity validation or claim a VC2 repair without the resulting live evidence.

The owner's current scope is to finish the four demonstrated save paths and
their loading/return behavior, then close Smash Pack. The owner reports that
Google lists no save support for the other eight Genesis titles. This is an
owner report, not an independent source/runtime verification; it is not a
request to continue title-by-title save integration. Reopen that work only if
contrary evidence appears. Preserve the lessons in the shared loader policies
and reusable checks; the eventual target remains a universal loader.

## d70 hardware result: native return works; CE regressions remain

The owner reports that `d70c4cf1acc0` works with the speed improvements
"more or less" retained. This accepts the previously failing native return
as reported; there is no new exact timing or phase-TXT capture. Preserve its
strict original-video transport and the shared allocation epoch.

Virtua Cop 2 is reported broken again. The failure point and whether this was
a fresh launch or a launch after another game were not supplied. Do not mark
VC2 accepted from helper hashes alone. The audit found a separate unqualified
change in the helper-hydration raw reader: it switched from the accepted
forced-PIO supply to DMA/poll. Ordinary CE reader and frozen helper bodies
match the accepted route, but that does not prove hardware equivalence of the
complete loader. The proposed narrow repair restores the accepted hydration
policy while retaining the outer loader's current batched DMA reads.

Swirl return briefly blacks the display, then shows a distorted frozen image.
The existing normal CE E0 exit enters a terminal diagnostic renderer; it does
not implement a menu reload. This is an unfinished return path, not evidence
that a completed CE return was tested successfully. A return controller must
prove complete helper/reader idle before freezing traffic and retiring CE.

Red RET flashes on otherwise successful returns are diagnostic progress drawn
by the loader, not game errors. The follow-up records these checkpoints in RAM
and retains visible fault reports only for failures. VMU Tools entry also
unconditionally reads SD preferences while blocking navigation; the follow-up
loads those settings when their rows are selected. Neither UI change is a
new native game-loading speed claim.

## 9f hardware result: 18-second selection; returned-outer video refusal

The owner reports about 18 seconds from native selection to the Start Game /
Resume menu on build `9f470e548ad2`. Native return still fails: the owner reports
`RET B4C2CD76`, detail `00000000`. This is a reported whole-selection time,
not a new phase-TXT total or acceptance of the loading/return goal.

The reason exactly matches `CRC32("SMASH TIMING ORIGINAL VIDEO REFUSED") |
0x80000000`. The returned outer reached its loader preparation and attempted a
KOS-only VID1 capture from the game's current PVR state. The source regression
was reproduced: the original KOS framebuffer-zero state passes, while nonzero,
blanked and 320×240 game states fail that guard. This identifies the 9f refusal;
it does not retroactively identify the individual stage behind 5ca's broad105.
See [the evidence record](evidence/smash-native-return-9f470e548ad2.json).

The d70 candidate carried the strict original 80-byte VID1 instead. Its
diagnostic return runtime grows from 516 to 596 bytes, all covered by its CRC;
the restored outer has a blank VID1 slot immediately after its 368-byte return
seed. The gate copies the validated record only after the outer capsule CRC
passes. Initial GUI capture remains unchanged; returned outer uses the carried
KOS record rather than recapturing game video. No video-mode guard is widened,
no timing is synthesized, and the worker/reader code remained unchanged. Its
later native console result is recorded above. The current follow-up makes
successful progress markers RAM-only.

## Historical 5ca result: broad return progress window

The owner tested build `5ca9697` with Shining Force and selected Return to Menu.
The photographed last display was `RET 00000105`, detail `00000000`. Startup
seemed marginally better, but no exact new elapsed time was reported. Native
return still fails; this build is not accepted as a return or loading solution.

`105` is a progress marker, not an error code. In 5ca its uninstrumented span
covers the raw-reader probe, outer-capsule reads, complete CRC checking and
transfer into the outer entry. The photo does not identify which individual
stage stopped or prove that all those operations completed.

The subsequent 9f visibility build added granular return progress and made outer-stage
progress visible on the current framebuffer. Return-only outer markers110→116 distinguish C entry through the BOOT2 relay.
Outer error reason is `CRC32(message) | 0x80000000`, with the original detail. It retains the 5ca allocation-epoch behavior and
accepted 72 KiB worker, CE helpers and ordinary readers. This is a visibility
diagnostic, not a return repair or an additional speed claim. Its later console
result is recorded above. See the [marker map](smash-card-session-build.md#return-progress-interpretation)
for the new105→10A boundaries.

## c375 hardware result — 2026-11-05: incomplete improvement; native return unresolved

The owner tested Shining Force only: selection to the next Start Game / Resume
screen was about22 seconds rather than30. Returning from Shining Force to the
Smash Pack menu was broken, so the proposed multi-game sequence was not run.
This candidate fails acceptance: native return is broken, and selection
latency remains excessive. The owner said the preceding ZIP may also have
frozen on return; its first affected build is uncertain. Earlier working
switching is a historical baseline, not proof that858 was good. Do not attribute
the failure specifically to the new GIN/session path.
Do not treat the linked transport proofs as console
return acceptance or the8-second reduction as completion of the speed target.

The attachment `smash-genesis-saves(1).txt` identifies buildc3758c3f826a and lists
ROM SRAM eligibility. It is not the phase trace and cannot establish which
runtime attach path executed or where the22 seconds were spent. The requested
file is `KUI/apps/games/smash-native-phases.txt` from this run. If absent, report
generation itself needs investigation. The owner then clarified the return
failure: the current screen freezes and the SD activity light stops; there is
no reported visible error. This does not locate the exact failing transition.

The read-only return audit found no demonstrated CRT overwrite of the low
return gate. Original return-parser helpers produce the same valid result
from old synthesized and actual GIN Shining Force contexts. Return checks1–4
can park silently before their stop-renderer pointer is trusted; never reaching
the reset hook is also unexcluded. Existing linked transport tests model worker
retirement/quiescence and do not execute gameplay through the real button/reset
sequence. That audit did not diagnose the c375 return failure. The later 5ca candidate
changed the exact native button edge but still failed return, as recorded above.

The correct4096-byte `smash-native-phases(1).txt` has now arrived. It identifies
c3758c3f826a / Shining Force and proves the new route executed: full_admissions0,
attaches1, receipt valid / sequence2, all semantic filesystem counters0,
native_gin_route1 and boot2_calls0. All23 raw endpoints have consistent clocks;
the17.437-second nominal total agrees with17 RTC seconds. See
`evidence/smash-native-phases-c3758c3f826a.json` for exact endpoints and hashes.

The old "Filesystem ownership" label is4.963s: about0.278s precedes witness
replay, and4.685s is fresh replay/identity validation. The costly semantic
traversal was bypassed, but its replacement still costs seconds. Existing
vmu_prep_* counts exclude witness/sidecar direct I/O;515 checked sectors must
not be called total attach traffic. Witness record/run counts were not exported.
Original MDE/GIN is5.678s; capsule SD1.713s; prefix verification/stage entry1.708s;
actual SD/card initialization only0.011s; SDK/allocator proof1.263s. The measured
total excludes pre-callback time, report I/O and later game work; it does not
assign every second of the owner's22-second observation to VMU.

## Pre-c375 comparison baseline — 2026-11-05

Before c375, the owner observed **about 8 seconds with VMU Off versus 30 seconds
with VMU On** from selecting Phantasy Star II or Shining Force to the Start Game /
Resume screen. The credits screen remains visible during the wait. The previous
manual72 build did not resolve this delay. Host read-command reductions are not
accepted as elapsed console improvement. The extra approximately 22 seconds is
the performance target. The later c375 capture and 5ca observation above
supersede this as the latest result; neither met the loading or return target.

Virtua Cop 2 now boots and works on the owner's manual-copy baseline. Preserve
that path and the accepted virtual-VRAM helper. Sega Swirl works, but returning
to the collection menu still crashes; Virtua Cop 2 menu return is also unresolved.
Native menu switching preserved the VMU before the failedc375 candidate above. Other Genesis games' high-score
persistence has not been demonstrated. See `smash-genesis-save-scope.md`.

In that comparison VMU Off used the physical route, while VMU On selected the unified native-child
route. The difference includes GIN/capsule transfer, fresh native cold admission,
card/journal validation, BOOT2, MDE initialization and worker installation, then
subsequent game VMU traffic. Do not equate the difference with a proven BUP-only
bottleneck. A retained credits image does not locate execution within that path.

Build 09a delivered **SMASH_PHASE_DIAGNOSTIC=2**, an automatic TXT diagnostic that
continues into the original game. It times the first MDE payload callback through completion
of native worker installation. Initial wait before that first callback is outside
the measurement. It does not time subsequent SGA/BUP/options work.
It writes `KUI/apps/games/smash-native-phases.txt` after the measured interval.
Unavailable or inconsistent clocks are reported as NA. This is not a speed fix;
its purpose is to distinguish cold-entry overhead from later game VMU work.
See `smash-native-phase-text-test.md` for the one-run procedure and exact scope.

## Current design retained by the video-provenance follow-up: v2 allocation epoch

The owner authorized one collection-lived card admission. The c375 witness
candidate bypassed the semantic traversal but still spent4.685s replaying
metadata/identity. The current source replaces that per-child replay with a
version2 allocation epoch: initial outer entry performs complete filesystem
admission, then a96-byte live receipt carries that admitted allocation authority
through checked owner transitions. Children use `kui_vmu_persist_stage_rebind`;
they do not load or replay the witness sidecar. Direct adoption of the original
GIN-loaded MDE remains, avoiding duplicate native MDE/BOOT2 loading. See
`smash-collection-card-lifetime.md` for the exact boundaries and failure policy.

The normal worker already retains its admitted image/journal extents and current
block CRCs during gameplay. What is new is retaining allocation authority across
the collection's owners. Each owner still builds a fresh backend, checks current
CID, recovers the real journal, validates current save data, checkpoints and
checks final identity before a new durable handoff. No active backend, stale
save snapshot or continuously acquired SCI lease is carried across owners.

The earlier native A+B+X+Y+Start shortcut went directly to the outer-return
gate, bypassing the engine Save Game menu. This policy is superseded by the
original-options restoration above. Complete the RPG save and options menu
Save Game action, then explicitly Return to Menu. The worker must quiesce and retire
before outer reload. The 5ca test failed at the broad105 window; 9f then exposed the returned-outer
video refusal. The carried-VID1 d70 follow-up now has the owner's native-return
acceptance reported above.

All four module roles carry the new configuration; legacy all-absent modules
retain the old path and mixed cohorts are refused. Accepted72KiB worker geometry,
canonical CE helpers and current Main2b4b8c673b7c updater code are preservation
requirements. The 9f test reported about18 seconds to Start Game / Resume and
failed return; d70's later result supersedes that native-return failure.
The owner reports retained speed improvements without a new exact timing.
CE menu return and other Genesis high-score persistence remain unresolved on
the last console-tested build. The new CE controller is a separate follow-up,
with hardware acceptance still pending.
The requested deliverable is a manual-copy ZIP with both identical GUI aliases,
both checked ROOT selectors and the four matched stages/two preserved helpers;
no generated session, save or game data belongs in that ZIP.

The updated TXT distinguishes prefix verification5→10, SD initialization10→11
and allocation-epoch validation12→13. A successful native rebind reports
THIS_OWNER full_admissions0, attaches1, allocation_epoch1, witness sectors/runs0,
native_gin_route1 and boot2_calls0. Receipt sequence advances across valid native
returns. SD/CPU timing remains NA when no SD callback timing was collected;
zero witness traffic is not a measured zero-duration I/O result.

## First automatic hardware timing capture — 2026-11-05

The owner returned the complete 4096-byte `smash-native-phases.txt` from build
`09a098074029`, selecting Shining Force. Worker installation completed. All 24
raw endpoints have consistent PMCR/FRQCR, monotonic cycle/RTC values and valid
clock flags. Nominal total 21.576 seconds agrees with the 22-second RTC span.
Evidence: `evidence/smash-native-phase-hardware-09a098074029.json`.

Largest measured costs: original MDE first payload callback to post-GIN 6.896s;
capsule load 2.091s plus validation 0.669s; filesystem ownership 5.667s;
MDE restore/load 1.377s; SDK/allocator proof 1.289s; base relay 1.125s.
The two journal/image-validation intervals total 0.427s. The original allocator
and CRT interval is 0.776s, and worker allocation/relocation/arm total 0.106s.

The real ownership traversal has 396503 chain steps/FAT entries and 2469 directory
sectors, unlike the tiny earlier fixture. Overall preparation used 2329 single
commands, 341 runs and 8048 CRC-checked sectors. Do not use the historical
1096-to29 fixture result as a representation of this card or elapsed proof.

Initial pre-callback wait, subsequent SGA/BUP/options work and report I/O remain
outside these timings. The 21.576s includes original-game work also present with
VMU Off; it is not a measurement of 22 seconds of VMU-only traffic. Fix the
measured setup costs while retaining original complete ownership/hash/CRC checks.

## 858 startup candidate — marginal hardware improvement, unresolved

The owner reports the858 package is at most marginally faster. The primary
internal-selection delay remains unresolved. No updated phase capture was
provided with that observation; the attached858 file is the Genesis save report.
Do not claim the startup fixes passed hardware acceptance.

The follow-up preserves the measured09 worker and addresses concrete work:
compact aligned outer copying within its existing guard; reuse of verified live
FAT windows without reducing the256-sector metadata cache; a strictly guarded
cached whole-executable CRC with bounded private-stack publication; and removal
of the two informational30-frame holds from normal native startup globally.
All ownership, complete executable CRC, per-block SD CRC and fresh image
validation epochs remain. None of this establishes elapsed console savings yet.
See `smash-measured-startup-next-test.md`. Keep the automatic phase report enabled
for the next single native selection; do not repeat the Off baseline or settled
VC2 compatibility matrix for these temporary-stage changes.

## Why retain collection allocation authority

The GUI selects one backing card/profile and patches it into native, Virtua Cop2
and Sega Swirl children. The858 path rebuilt filesystem ownership for each
child and reloaded MDE/BOOT2 for native selections. The c375 path retained a
metadata witness and GIN continuation, but fresh witness replay remained costly.
Neither candidate met the hardware performance goal.

The current v2 candidate retains admitted allocations through the authenticated
RAM receipt and explicitly rebinds each owner's adapter after quiescence. It
does not retain a mounted filesystem or backend. Do not copy active structs or
reuse the first sealed handoff after saves:
objects contain owner pointers, commits update block CRCs and invalidate the
initial seal. Retain all CRC/durability and identity checks. Do not enlarge the
72KiB worker or steal any stack reservation without an explicit resource design.

See `smash-collection-card-lifetime.md` for the source audit and implementation
boundaries. The historical858 package did not contain this architecture; its
hardware result is not evidence for or against the new v2 path.

The858 Genesis report now covers the actual ten ROMs: Phantasy StarII and Shining
Force each declare8192 SRAM bytes; the other eight declare0 through the original
engine's cartridge-save path. Controller settings remain separate. Evidence:
`evidence/smash-genesis-save-capabilities-858ca7ee3db1.json`. No additional header
extraction is required to answer that eligibility question.

## Source, worker and deployment preservation

Current Main was checked at `2b4b8c673b7c71a58c41e8a79252313940ea944e`.
All 14 production updater files in this test source match that Main byte for byte.
The test branch descends from the recovered 433 updater adoption and 338/e134
Smash integration; the prior 72 source commit itself is not recovered. Its cache
and save-report changes were reconstructed and checked against the accepted 72
behavior. Do not represent that reconstruction as an exact recovered source tree.

The linked native worker and low resident must reproduce accepted 72 code/data
apart from their actual twelve-byte source build label. Native VMU reservation
remains **73728 bytes**. Background and its 192KiB cache are separate existing
components; this test does not replace Standard, Background, disc or CDDA readers.
CE helpers retain their exact canonical SHA256 and actual `ab1b145b33b4` component
label. That unchanged helper is not an older replacement VMU worker.

Historically the owner requested a **manual ZIP** for this test; the standing
ZIP rule above now applies beyond that individual request. That historical
package required identical GUI bytes at `KUI/runtime.kui` and
`KUI/boot.kui`, BOTH valid ROOT activation slots, four matching Smash modules,
and the two exact accepted prepared CE helpers. Keep banks and all user data.
The GUI must embed those same four modules: an older embedded bundle can take
precedence over newer physical files. Validate actual packaged module bytes
through the production GUI admission path before delivery.

The GUI preserves the 338 removal of the separate pre-splash status painting and
shared mutable session/settings files, and uses current Main's engine 3 updater.
No new updater, worker-geometry, universal Background support or CE menu-return
fix is claimed by this diagnostic.

## Historical notes — superseded where contradicted above

The sections below record earlier work and failed candidates. They do not select
the current source, hardware acceptance result, updater engine or delivery recipe.

## Historical updater-only integration and preserved baseline

The source baseline is published main
`0045dd96937ee7485ecd4fe0432b28babea8646a`. This integration ports the updater
without merging the recovered ab1 loader/VMU/Smash development. The owner's
2026-11-04 instruction explicitly limits this merge to updater code.

| Area | Included change | Preservation boundary |
| --- | --- | --- |
| Installed versions | Checked banks, selection records, restart and rollback | Existing root readers and their absence remain shared |
| Package admission | Bounded namespace, sparse inheritance, old26/old34 compatibility | No exact-count gate and no new optional modules |
| Test selection | Folder browsing and one embedded .kup per ZIP | Legacy manual-copy ZIPs require conversion |
| GUI restart | Fully verify/reload selected GUI directly | Updates action120; restart action126 |
| Module paths | Three bank-aware existence checks in the existing launcher | No title-specific routing or new backend selection |
| Reader admission | Exact verified Standard/CDDA identities in the GUI | No reader code, build flags, memory geometry or binary changes |
| Root migration | Preserve system/optional boot override, then copy runtime | No timing-sensitive button required; all user files retained |

`docs/evidence/updater-preserved-main-source.json` records main's protected
source blobs and the four deliberately reviewed GUI glue paths. All other
protected sources and reader recipes must match main byte for byte. The four
binding files under resources/updater-bindings retain exact admission for the
already installed 233,964-byte Standard reader and native CDDA aliases.
These manifests are not permission to replace readers or infer an allowlist
from arbitrary SD files.

The owner confirmed that the earlier root repair GUI06cbd036bf0d booted and
resolved the reported cohort error. That binary remains their working repair;
this source-only main integration is a separate narrow port. No replacement
reader or new console test matrix is required. The owner reported a longer
than desired installed-version selection screen; this merge does not claim
to resolve its latency. A completed console CDDA-package round trip and
universal in-game return are not established by the root-boot confirmation.

The failed full-cohort updater substituted a wrong Standard reader and added
presence-sensitive modules. Its release/build recipes are not included here.
GUI-only packages and the manual root repair contain no KUI/apps files.
Readers, VMUs, journals, game metadata and settings are left in place.

The accepted compact 72-KiB VMU and combined284-KiB/cache192-KiB installations
remain distinct from VMU-only operation. This updater does not change any
service reservation, promote session-controls variants or add a universal
cacheless profile. Native/disc/CDDA/WinCE support remains route-specific.

## Current Smash test branch

The latest tested candidate failed hardware acceptance on 2026-11-05
(the running build ID was not provided): collection startup improved to 8 seconds, but Phantasy Star II
took 29 seconds and Shining Force 30 seconds to reach Start Game. Virtua Cop 2
loading and collection-menu return both failed. No unified four-game success,
native selection speedup, VC2 transition fix or CE menu return is accepted.
Host packaging, CRC and execution-model checks do not supersede these results.
See the hardware result at the top of `shared-launch-next-test.md` before using
the historical candidate description below.

Main remains758f62c43d76; the following is test-branch work, not a claim that
these game changes have been merged or accepted on the console. The owner
reported15-second collection startup,15–20-second native selection delays,
VC2 stopping and Swirl parking on return with the136 GUI-only delivery.
That delivery inherited root modules without establishing the actual cohort.

The next Main26 GUI update contains four compressed same-commit Smash stages;
each CE stage contains its matched raw helper. Root Standard/Background/disc
readers and raw helper files remain unchanged. Actual linked bundle references,
module/helper bodies and GUI routing must be checked, not just package headers.
The old26-role root dispatcher must still cold-boot the new GUI receipt.

Retain the confirmed10ef virtual-VRAM halfword-copy helper. Its ab1 runtime
body differs only in its build label. Normal CE children restore that direct
E0 terminal context and omit the later unqualified low return observer. CE
collection-menu return remains unimplemented; VC2 cold entry remains pending
console confirmation. Do not claim either was fixed by host admission tests.

Native cold transitions can reuse an original CRC-checked MDE read through
temporary1,848,288-byte scratch, followed by fresh complete source SHA admission.
Persistent72-KiB worker geometry is unchanged. Hot GUI mapping deferral requires
the new stage's complete-source-owner contract; old/missing/ambiguous proofs
retain fresh preflight. Capsule I/O now uses32KiB windows with4KiB fallback.
Refer to docs/shared-launch-next-test.md for the test scope and evidence limits.

## Robust updater v2 follow-up

The next updater-only change fixes the contract rather than adding another
catalog exception. The source remains the published-main updater; no reader,
stage, memory geometry or reader recipe is rebuilt or merged here.

Partial packages materialize a checked snapshot of the actual running ROOT or
BANK plus intended changes. FULL packages are independent of the prior bank,
can replace corrupt prior files/receipts, and preserve absence for omitted
software. Component builds are independent; configuration/content determines
package and snapshot identities. Raw helpers, official audio/assets and data
catalogues are supported. The current C consumer preflights every generated
KUP before delivery; default test/release producer paths create v2.

Root engine2 is a one-time setup requirement. Its startup floor skips older
GUI banks instead of reinstalling their decoder. Normal tests then install a
standalone KUP and restart directly. The current136 GUI adoption preserves
all its game/UI code and startup reporting, ports only Main updater code,
and uses the exact26 original module/helper binaries without rebuilding them.
That adoption is a test deliverable, not a merge of its game code into Main.

## Startup latency follow-up

The owner reported about five seconds at the pre-splash version-selection
screen. There is no fixed delay in that function. Current code could reread
one refused multi-MiB bank GUI for both activation slots, rescan the EMPTY
slot after validation, and remount an already pinned boot device for discovery.
The updater-only follow-up deduplicates refused GUI sources after each slot's
handoff attempt, reuses the checked slot offset, uses the existing equivalent
fast CRC only in updater paths, and opens the pinned transport directly.
KUI/updater-startup.txt records phase timings so another video is unnecessary.
Cold selection still reads and verifies a distinct bank GUI once; no console
timing improvement is asserted from host read-budget or integrity tests.
