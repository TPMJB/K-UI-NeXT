# GD Play: larger native executables

This update removes the old 4 MiB direct-boot ceiling that rejected Skies of
Arcadia. Its known native boot executable is 5,460,080 bytes, about 5.2 MiB.
Native GD Play now admits executables up to the existing 12 MiB physical-disc
format limit, subject to available contiguous staging RAM and checked handoff
geometry. It still refuses a launch that cannot be staged safely.

## Install all four files

With the Dreamcast powered off, back up these four existing SD files, then
merge the ZIP's `KUI` folder into the existing `KUI` folder and replace:

- `KUI/runtime.kui`
- `KUI/apps/disc/native-disc.kui`
- `KUI/apps/disc/native-disc-vmu.kui`
- `KUI/apps/disc/native-disc-vmu-a1.kui`

Keep the rest of the installation and your cards/save files. No installer is
needed. The modules and runtime in this package form one matching update;
copying only the runtime leaves an old disc module's 4 MiB limit in place.
To revert, restore all four backed-up files together. Installation does not
change the saved Virtual VMU preference or slot.

This runtime also contains the earlier physical-VMU backup recovery, clearer
save labels and details, remembered import matches, and **GD Play → START:
Cards** management. No game executable, disc image, or personal VMU image is
included in this package.

## Console test

1. Insert Skies of Arcadia and open **Disc tools → GD Play**. Start the disc
   normally. The old "up to 4 MiB" refusal should be gone.
2. Verify the introduction and an existing save with Virtual VMU Off, using a
   physical card if available.
3. Enable Virtual VMU in **GD Play → Y Options**, choose the intended A1/A2
   slot, and verify the linked card loads. Make a save and check it after
   restarting. The native VMU driver and allocator checks still apply.
4. Check one smaller native disc as a regression test. Save Diagnostics if a
   launch fails; note the selected VMU mode and slot.

The game creates its own internal save files when you save during play. The
Cards menu can link an imported card or create a blank one without a rip on
SD. Supported native launches also retain their existing blank-card creation
when the release has no assigned card.

## Scope and evidence

Larger executable handling applies to native physical GD launches with Virtual
VMU Off or On. It does not add a Windows CE or CD/MIL-CD direct-boot path, alter
the original GD firmware service, or increase the GUI runtime-file limit.
The 12 MiB value is an admission ceiling, not a guarantee that a running GUI
can allocate a buffer of that size. Allocation, stack margins, stage overlap,
exact executable fingerprints, and existing native VMU admission checks remain
in force. Older 4 MiB modules remain usable with smaller executables; larger
games need the matching modules supplied here.

Host tests use synthetic sectors, including the exact known Skies executable
length, and exercise large-file staging, partial final sectors, fingerprints,
legacy modules, and refusal paths. Build/audit logs are included in `evidence/`.
Host tests and linked-code audits do not establish console acceptance of this
new path. Skies' larger native-disc boot still needs the console test above.

`source/HEAD.tar.gz` contains the complete committed source tree, and
`source/delta.patch` contains the change from the manifest's local base.
`build/` contains the linked runtime/module artifacts and their clean-source
compile records. `manifest.json` lists each ZIP member's size and SHA-256.
