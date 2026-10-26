<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native graphics budget and VMU identity test @BUILD@

This candidate corrects a graphics allocation failure and a VMU product-class
rejection. It retains the bottom reservation from the previous candidate.
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

The owner confirmed that the previous bottom-placement candidate lets Rayman 2
pass Ubisoft with New, but the game reports no VMU. Its original device
classifier requires the padded product name **Visual Memory**. The old
**K-UI Virtual Memory** name decodes as another storage device and fails the
game's VMU selection gate. The common emulated reply now uses the standard
product class, while the producer/version still identify K-UI. Original
instruction replay accepts the corrected A2 reply and rejects the old name.
The replay also accepts A2 with physical A1 absent.

Lodoss still shows black video with both readers. Its startup allocates a
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

These are software results. **Rayman's card detection/save/load and Lodoss's
video, audio and later game memory demand still require console testing.**
Evolution 2 loading worked on the previous candidate. Evolution 1's black
screen after Sofdec remains unresolved.

## Install

From the extracted ZIP's root:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --profile-runtime
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

1. **Rayman 2:** check that it detects virtual A2, then load an existing save
   or create one in an unused slot. Power off fully and check that it reloads.
2. **Record of Lodoss War:** let the opening movie start without skipping it.
   Report whether video appears and whether audio plays normally or stutters.
3. **Evolution 2 or Grandia II:** briefly repeat a familiar movie/gameplay
   route as a control for the previously working New reader.
4. **Evolution 1:** recheck the point after Sofdec once the first tests are
   complete. Its previous failure remains recorded; do not assume it fixed.

For a game that starts normally, check an existing A2 load. Use an unused
slot for any new save, wait for completion, then power off fully and reload.
For Skies, also check physical A1 coexistence. Its earlier normal boot result
alone did not establish these save/persistence checks for this candidate.

Legacy is available afterward as a comparison using the same reservation and
guarded pool-budget correction. The main acceptance target is New. Both use the same
384 KiB reservation; Legacy leaves the extra read-ahead space unused.

## If a stall remains

Keep `--profile-runtime` installed. At the failure point, hold **both triggers
plus B** for about one second, release, and wait roughly 20 seconds for the
intentional profile stop. Record the complete repeating report cycle,
including every **RAW OWNER STACK** page. About two minutes after the report
appears is sufficient; stop when the starting heading repeats. If startup
stops before that gesture can work, photograph the complete refusal screen
or record its last cold checkpoint. Power off after the report.

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
