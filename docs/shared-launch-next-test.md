# Shared launch preparation and Smash next test

## Latest owner report after e134 delivery

K-UI-to-Smash startup is now 11 seconds; VC2 goes black; the pre-splash status
screen/delay remains. This candidate failed acceptance. Actual running root,
bank and component identities remain unconfirmed until the existing reports
are read. Archive/consumer checks prove the package contains different software;
they do not prove which software the console activated.

The current Main updater in e134 is a27da110f02c. The package changes bank
software, not the physical root dispatcher. Main's optimization reduces some
repeated reads but does not remove its unconditional startup screen. These
facts do not explain the separate 11-second game-launch measurement.

The new quiet CE admission path can hide its own refusal because it captures
the post-GIN blank scanout. Do not interpret a black screen as evidence that
the resumed original SAMPLE or WinCE itself was reached.

## Hardware result: latest candidate failed acceptance

The owner reported these results for the latest Smash test on 2026-11-05.
The running build ID was not included; do not infer it from package recency:

| Operation | Console result |
| --- | --- |
| K-UI to Smash Pack | 8 seconds; improved |
| Phantasy Star II selection to Start Game | 29 seconds; unresolved regression |
| Shining Force selection to Start Game | 30 seconds; unresolved regression |
| Virtua Cop 2 loading | Failed |
| Return to the collection menu | Failed |

These results supersede the pending-console language below. This is not an
accepted four-game package. Packaging the confirmed standalone VC2 helper did
not establish a working unified VC2 transition. Restoring the normal CE E0
diagnostic context did not implement menu return.

The new native warm-image restore performs a complete CRC, SHA-256 and copy
before the later native ownership checks. Its functional host tests did not
establish console latency. Do not attribute the 29–30 seconds to this operation
without a phase measurement. In particular, a direct SHA call is not evidence
that it ran uncached: the cold stage entry explicitly enables CCR 0x909 before
entering C. Neither the native delay nor the VC2 stop has a demonstrated cause.

## Reviewed CE GIN continuation candidate (not hardware accepted)

The next CE change removes the unverified fresh-SAMPLE/BOOT2 detour. It captures
original GIN's actual 21-word integer frame and CCR in a typed 160-byte launch
record at capsule offset 0x200, after whole-capsule CRC validation. CE entry
keeps GIN's actual RB1 rather than forcing the cold-loader RB0 state.

The high stage authenticates the original staged SAMPLE with full CRC and SHA,
restores it without a second executable SD read, installs the unchanged matched
foreground CE reader/helper, and resumes the actual GIN continuation via RTE.
The original live IP header/folder, BOOT2 and stack are retained. Original IP is
read into high temporary memory solely to verify and restore retired BOOT1 at
8300..c000, replacing the outer handoff that had reclaimed that interval. The
independently checked storage shim at 8100..8300 is the intentional exception.
Successful adoption does not repaint VRAM or change PVR registers. The linked
stage must end below the staged SAMPLE; its separate high stack remains above it.

Main updater a27da110f02c is integrated without changing its consumer or
producer. New delivery uses v2 with explicit GUI/four-stage/two-helper members,
so older pinned bank modules cannot take precedence over a newer embedded copy.
Standard VMU and Background workers remain the accepted installed components.

The unchanged CE helper retains its accepted component identity `ab1b145b33b4`.
It is reproduced from current helper source and must match the complete pinned
raw binary SHA, not just a normalized label. The previous A6 helper code was
identical except for that diagnostic label. A new GUI/stage ID must not change
helper string pooling, instruction addresses or body bytes merely to match a
cohort label. Independent component identity is part of Main's v2 contract.

This candidate addresses startup-state preservation, not a demonstrated
instruction responsible for the reported freeze. New linked-entry, frame,
publication, source-admission and refusal checks are host evidence only. It
must not be marked VC2 gameplay, native loading-speed or CE menu-return success.

## Historical candidate description

This candidate integrates the updater from Main
`758f62c43d76ce4c8550be5789874313a9a5551d` with the delivered
`26d61b435b48` GUI fixes. It retains all 26 installed module/helper files
from the authenticated `ab1b145b33b4` delivery unchanged. Main's updater
bindings independently identify the same Standard module and CDDA cohort.
Workers are not rebuilt from Main's older loader branch.

The retained Standard VMU service reserves 72 KiB. The retained combined
Background service has a 72 KiB worker and 284 KiB total reservation including
its 192 KiB cache. VMU Off retains the existing Background allocation policy.

## Changes under test

Raw native GDI launches may select one unambiguous, valid saved SDK mapping
without reading the executable a second time in the GUI. This is a hint,
never a claim that source bytes are unchanged. The existing detached stage
still reads its executable, proves its program extent, computes its own SHA,
and checks SDK coherence and live ownership. A mismatched hint takes ordinary
fresh discovery. Missing, malformed, ambiguous or ineligible hints use the
existing GUI preflight. The scan is bounded. VMU Off does not inspect the VMU
cache tree. Cold launches without a usable saved mapping retain full preflight.

Only the diagnostic-only GUI executable CRC is omitted on a successful raw
hint route. All storage block CRC checks remain. Cooked images, patches,
CDDA and special adapters retain their mandatory fresh checks. Smash keeps
its full fresh menu/engine SHA and CRC owner checks; saved hints cannot enter
its source-proof path. No persisted hash is passed as a fresh image digest.

Shared card allocation validation now memoizes at most 64 freshly read
metadata sectors within one read-only walk. It releases the temporary heap
before returning and invalidates on writes, media reset and errors. It still
visits every managed object and checks allocation, overlap and ownership.
Receipts, writes, synchronization, readback and both complete snapshot passes
are unchanged. Six real FatFs cases produce identical final disk hashes.
The exFAT allocation walk falls from 214 to 20 device read calls; that is a
host I/O count, not a predicted console duration.

## Focused console test

Use Sega Smash Pack with Standard / Background Off / virtual VMU On. Exercise
Shining Force, Phantasy Star II, Sega Swirl and Virtua Cop 2 from the original
collection menu. The native pair previously saved and returned to the menu;
the CE games were individually verified in their separate backends. The
new combined delivery still needs console confirmation. CE return to the
collection menu remains a safe terminal diagnostic, not an implemented return.

For the outstanding Sonic delay use Background New / virtual VMU On with
the existing saved mapping. Save the new launch timing report. A successful
hint route records a cache hit with zero GUI executable source bytes; no
new elapsed-time claim is made until this is measured on the console.

The current delivery is the standalone `.kup`. It uses Main's 26-role producer
profile: one GUI replacement and 25 inherited root roles. The optional-role
catalog compiled into a test GUI must not determine package compatibility.
The earlier 34-role container is incompatible with the released folder/restart
updaters, even though Main's newer namespace decoder accepts it. Regression
checks must exercise the actual released consumers as well as current Main.

The corrected container retains the exact `136234822d52` runtime bytes; it
does not rebuild the GUI or replace any worker. The 26-role receipt is accepted
by that GUI's updated loader. User cards, history, settings and root modules
remain in place. Installation kits are no longer the test-delivery format.


## Follow-up after the owner's regression report

The GUI-only136 delivery did not install or fingerprint the actual root Smash
module cohort. It therefore did not establish that the console used the four
stages tested by the host qualification. The next GUI carries a compressed,
SHA-checked four-stage bundle; each CE stage carries its own matched helper.
No root reader/helper is replaced. Explicit update-bank module replacements
take precedence, and a damaged replacement or bundle fails closed.

The update remains Main's26-role GUI package so an earlier26-role root GUI can
cold-boot it. A five-role bank receipt alone would not upgrade that old root
dispatcher. Normal native72KiB and Background284KiB/cache192KiB reservations
remain unchanged; native cold handoff uses temporary1,848,288-byte scratch at
8c400000 only during the retired-menu transition.

Changed behavior under test:32KiB capsule I/O with4KiB fallback; stage-owned
fresh complete MDE SHA admission allowing hot GUI cache reuse; and optional
reuse of the original GD-read MDE with fresh CRC/SHA checks. No measured
console-time improvement is claimed. Actual packet component identities are
recorded from already loaded module bytes in the launch timing report.

The confirmed10ef VC2 helper and ab1 helper differ only in their build-label
bytes. The virtual-VRAM halfword-copy fix remains. Normal CE children now use
the confirmed direct E0 diagnostic context, without the unqualified additional
8300..c000 observer. This is not an implementation of CE menu return, and the
cause of the owner's VC2 stop has not yet been demonstrated. The next console
run must establish whether the matched deployment and normal CE context work.
