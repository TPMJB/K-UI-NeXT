<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native controller poll timing test @BUILD@

This candidate reduces work in the shared controller/VMU polling path. Ordinary
heap and installed-hook checks take a small leaf gate before the existing
terminal-dispatch normalizer. Only the exact dispatch word in the matching
allocator family enters the full proof. All live identity, hook, context,
packet, response and storage predicates remain required. The game video wait
and the reader scheduling policy are unchanged.

The graphics-budget and standard VMU product-class corrections that passed in
Rayman 2 and Lodoss remain included. The bottom reservation is retained.
The complete **384 KiB reservation**, including the
**128 KiB worker and 192 KiB read-ahead cache**, remains available.

The original SDK allocator obtains an upper placeholder, allocates the exact
remaining bottom chunk, then frees the placeholder. Every transition is
checked before the worker is installed. The placeholder is not used as an
allocation buffer. The original allocator/free bodies and their pointer
literals must match the reviewed structural family. Full heap ownership,
release-overlap, reinitialization, DMA and storage guards remain required.

The New reader's SCI priority, checked PIO pair scheduling, read-ahead
producer and cache size are unchanged. This ZIP installs a matching Games UI
and native New/Legacy module. CE, CD music and compressed-image routes retain
their existing behavior. The standard virtual card streams from SD and
continues using virtual **A2**, leaving physical **A1** available. Card contents,
geometry, storage-only function bits and ownership identity are unchanged.

## Why this candidate

Evolution 1 still failed with diagnostics disabled on f158. Its complete
recording locates the repeated wait in original video-mode code that polls for
exact scanline zero; the SDK video counter continues to advance. Earlier
reservation-only/active-hook results and the NORMAL-mode failure make shared
runtime timing the current focus. They do not establish a hardware fix.

The full normalizer previously ran on every heap/hook identity read, including
unrelated addresses and families. Its outlined frame work was paid before
those calls could return unchanged. The new leaf makes the same applicability
decision before entering the full routine. This is a shared-path optimization.
Console confirmation remains required.

The owner confirmed that the previous bottom-placement candidate lets Rayman 2
pass Ubisoft with New, but the game reports no VMU. Its original device
classifier requires the padded product name **Visual Memory**. The old
**K-UI Virtual Memory** name decodes as another storage device and fails the
game's VMU selection gate. The common emulated reply now uses the standard
product class, while the producer/version still identify K-UI. Original
instruction replay accepts the corrected A2 reply and rejects the old name.
The replay also accepts A2 with physical A1 absent.

On the previous candidate, Lodoss showed black video with both readers. Its startup allocates a
private game pool and then requests **920,752 bytes** for graphics setup.
The original SDK heap has **921,472 bytes** left for that request. Our retained
allocation left only **528,224 bytes**, so the original allocator returned
NULL. The original caller then published `0x20000000` as the work pointer.

For the structurally verified remaining-RAM pool-split caller, this candidate
accounts for the reservation's **393,248-byte physical cost** in that existing
split calculation. The private pool becomes smaller by that amount, and SDK
graphics capacity returns to its original value. The allocation and child-pool
initializer receive the same adjusted size. The original graphics allocation
then succeeds in instruction replay. The correction changes one guarded live
reserve word; it does not modify the disc image or use a title-name whitelist.
Unrelated callers retain the existing bottom placement.

The owner has now tested delivered build **f158ba5e24f9**: Lodoss FMVs and
gameplay are reported flawless, and the game shows the virtual VMU. Rayman 2
also has flawless FMVs/gameplay. A follow-up confirms saving and loading work
in both Rayman 2 and Lodoss. Power-cycle timing was not separately described.
Continue ordinary gameplay to check later memory demand; a full-game pass is
not claimed. See the [console result](evidence/native-console-followup-f158-2026-10-26.json).

Evolution 2 loading worked on the previous candidate. Evolution 1 still loads
briefly after the Diff'rent logo disappears, then shows a black screen on this
candidate. Its next check uses the existing PROFILE diagnostics: keep New (X),
hold both triggers+B for one second at the failure, release, wait about 20
seconds, and capture the complete report cycle including CLOCK/VIDEO and all
PHASE HISTORY pages. The exact Evolution 1 image retains phase history instead
of the generic RAW OWNER STACK pages. No new build is needed for this capture.

The subsequent **63420.mp4** capture includes a complete report cycle. Its
validated interrupted contexts repeatedly land in the original exact
scanline-zero video-mode wait, while the SDK video counter advances. See the
[capture analysis](evidence/native-evolution1-f158-capture-2026-10-26.json).
PROFILE performs extra PVR/stack observations on every Evolution controller
hook even before the capture gesture. The owner subsequently tested the same
ZIP/profile/New reader in NORMAL mode, omitting `--profile-runtime`, and the
failure remained. PROFILE overhead alone is therefore ruled out. The next
engineering focus is the shared controller/VMU hook critical path inside the
original IRQ handler; added normal runtime latency remains a hypothesis.
No repeat of the PROFILE/NORMAL comparison is needed. Actual virtual requests
must still be filtered before DMA submission and their replies published
before the SDK consumer; save durability and ownership checks remain required.

## Install

From the extracted ZIP's root:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

Use your existing prepared profile; change that path only if your profile is
different. No new card preparation, game conversion or save reset is needed.
The prepared card and journal, game images, and saved launch choices remain
in place. The current profile uses the same virtual 200-block A2 card across
the tested games. Leave physical A2 empty; physical A1 can stay attached.

Safely eject and cold boot. Check the boot screen shows **@BUILD@**. Explicitly
select **SCI / Background On / New (X) / Auto encoding / CD music Off / RAM
patches Off** for these tests. Your last Legacy selection may still be saved.

New (X) and Legacy (Y) are reader labels in Advanced options, not new
controller hotkeys. The physical Y button opens Options and X inspects a
game. **Advanced → Background reader** offers New, Legacy and Off and saves
the selection for that image. An eligible original native GDI on SCI defaults
to New only when there is no explicit saved choice; an existing Off or Legacy
choice is preserved. Off uses the existing Standard route.

## Test New first

1. **Evolution 1:** use NORMAL mode as installed above. Check whether the game
   passes the logo/video-mode transition and starts the opening movie. If it
   does, continue into gameplay and check virtual A2 when the game permits it.
2. **Rayman 2 or Record of Lodoss War:** briefly repeat a familiar movie and
   load an existing A2 save as a control for the already working path.

The recorded f158 Rayman/Lodoss playback, gameplay and save/load passes are
preserved as baseline evidence. This candidate is a measured shared runtime
optimization, not a console-confirmed Evolution correction.

For a game that starts normally, check an existing A2 load. Use an unused
slot for any new save, wait for completion, then power off fully and reload.
For Skies, also check physical A1 coexistence. Its earlier normal boot result
alone did not establish these save/persistence checks for this candidate.

Legacy is available afterward as a comparison using the same reservation and
guarded pool-budget correction. The main acceptance target is New. Both use the same
384 KiB reservation; Legacy leaves the extra read-ahead space unused.

## If a stall remains

Report whether Evolution still stops at the same point. The first comparison
uses NORMAL mode; its capture gesture is disabled. A new recording is not
requested for this initial comparison.

If a PROFILE capture is subsequently requested, rerun the same installation
command with `--profile-runtime`, then hold both triggers+B for about one
second at the failure, release, and wait about 20 seconds. Record a complete
report cycle. Evolution uses PHASE HISTORY pages; other games use RAW OWNER
STACK pages. Stop when the starting heading repeats, then power off. PROFILE
adds measurement overhead and its timing is not assumed identical to NORMAL.

Raw stack pages are observations, not automatically fault PCs. They must be
matched to the original source frames. The exact Evolution 1 phase history
is retained. PVR samples measure timing and do not establish framebuffer
read-enable or decoded-frame publication.

The previous C29 and bottom-placement videos and Legacy results are recorded. A new
capture is useful only for a failure on this new build. If Rayman remains
broken and its resource metadata is requested, the optional read-only
collector is included; follow **RAYMAN-PROBE.md**. It exports only a 256-byte
FIX.DAT header as integers after verifying the exact IP/startup. It does not
follow pointers or export the full asset.

## Scope and source

The same eight of 26 supplied programs remain joint software candidates:
Evolution 1/2, Grandia II, Skies, Power Stone 2, Sonic Adventure 2, Rayman 2 and
Lodoss. Admission uses structural code and ownership checks, not title names.
A software pass does not override a console failure. Other revisions can be
tried, but an unsupported family can refuse at startup.

**DOA2 and Crazy Taxi 1/2 are not newly enabled.** Their allocator, source
provenance or SDK routing blockers remain separate. DOA2's proposed fixed
smaller arenas were rejected by original resource-load counterexamples.
Shrinking the advertised VMU capacity does not remove the reader/worker RAM
reservation. The DOA2 resource reports and Crazy Taxi startups are already
available and do not need to be uploaded again. Included collectors remain
available for reproducibility and future requested evidence.

RESULTS.csv retains the earlier console results. These compatibility corrections
are test candidates until hardware results are recorded. The full offline source
history, checksums and software evidence are included. See
**source/SOURCE-RECOVERY.md** to restore the exact source without a network or
an earlier checkout. No GitHub publication is part of this test delivery.
