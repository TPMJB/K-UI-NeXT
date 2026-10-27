# Dedicated Toy Commander card — next console test

This test creates a Toy-only virtual card from your CURRENT p0004 pair,
including every new Toy profile. It keeps the original card intact. It is a
per-game card-preparation test; the normal Games automatic selector and broader
game support are still pending.

## 1. Create the new input on your Arch PC

After the Dreamcast is fully powered off, connect the SCI SD card to your PC.
Extract this ZIP into a local folder and open a terminal there. Run:

```sh
python3 prepare-toy-card.py /path/to/SD/KUI/vmu-test/p0004 toy-card-output
```

Replace `/path/to/SD` with the SD card's mount path. `toy-card-output` must be a
new local folder. The input p0004 folder must retain card.vmd, card.journal,
identity.bin and retail-persistent-profile-0004.bin.

The helper checks the current pair and journal, selects TOY_COMM.___ plus ALL
TOY_COMM.* profiles, and copies their complete payloads and metadata through
the production C constructor. It validates the resulting card and writes:

- toy-card-output/KUI/vmu-import/card.vmd
- toy-card-output/report.json

Check that the printed selected names include AAA and every new profile you
created. All other games are omitted only from this fresh image; their saves
remain in the original p0004. Used/free blocks are calculated from your actual
current files, so expect much more free space than the mixed card.

The helper checks the journal against the pair identity, capacity and maps.
A valid already-applied journal marker is accepted. Pending recovery, malformed
markers and a changed input refuse. If recovery is required, preserve the pair,
launch the existing p0004 Toy loader once, wait for normal startup, power off
without another save, and retry. Never clear a journal manually. Preparation
hashes in the original P2 describe the old import; legitimate game saves are
expected to have changed the current image.

A prebuilt Linux x86-64 host constructor is included. If executable permission
was lost when extracting, run `chmod +x vmu-image-subset`. To rebuild it using
your local C compiler:

```sh
cc -std=c11 -O2 -I source/include source/tools/vmu_image_subset_host.c source/src/core/vmu_image_subset.c source/src/core/vmu_image.c -o vmu-image-subset
```

## 2. Prepare a fresh pair on the Dreamcast

1. Copy toy-card-output/KUI onto the SD card's KUI folder, replacing only the
   earlier /KUI/vmu-import/card.vmd input. Keep every pNNNN folder intact.
2. Your installed Card tools build fdc4143a5625 already supports this input.
   The same audited tools.kui is included under Card-tools/KUI if needed;
   preserve your current tools.kui before installing that optional copy.
3. Safely eject, cold boot, open Card tools and press Start. Compare its file
   count and used/free blocks with report.json. The displayed names should be
   Toy Commander files.
4. Press A after verifying the preview and wait for READY. It creates a fresh
   p0005-or-later pair and its own identity, journal and allocation profile.
5. Send report.json and the newly generated retail-persistent-profile-NNNN.bin,
   together with a photograph of READY. Keep the new pair intact.

Do not put the generated input image into any existing pNNNN folder. This
package supplies no replacement game loader. Continue using your working
p0004 loader until a module is bound to the newly prepared pair.

## Validation

The production subset constructor passes its existing 17 ASAN/UBSAN host tests.
New helper tests cover zero and committed journals, pending recovery, identity,
capacity and map mismatches, marker/payload corruption, all Toy profiles and
options, existing-output refusal, and source preservation. An older verified
18-save import was also split successfully into its two original Toy files
(10 used/190 free), with exact retained bytes. That smoke test is evidence only;
no old card image is supplied as your new input. Run the helper on current p0004
so your latest saves are included. New console preparation is still pending.

