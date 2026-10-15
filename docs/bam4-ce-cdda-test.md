<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Bust-A-Move 4 Windows CE CD music test

This is the first experimental console candidate for the original US
**Bust-A-Move 4**, product **T-8117N**, with its original **26-track GDI**.
It has not yet been tested on Dreamcast hardware. Host checks establish build,
layout and guarded code behavior; they do not establish audible playback.

## Install and launch

With the console off, merge the ZIP's `KUI` folder onto the SCI microSD card.
The install contains exactly these two files:

- `KUI/runtime.kui` — matching menu and title selection.
- `KUI/apps/games/ce-cdda-bam4.kui` — separate BAM4 CD music module.

Keep the existing `ce-cdda-armada.kui` from Armada RC9. This package neither
includes nor replaces it, or the ordinary CE/native/MDK2/Toy loaders. Keep your
existing game files and saved launch options. Cold boot after copying.

Select the original US Bust-A-Move 4 image, open **Y Options**, set
**Background loading On** and **CD music On**, and launch with **START**.
These options save automatically for the selected image. A repacked image or
another region/revision is outside this exact profile.

## Console test

1. Start a real puzzle stage and play for **at least three minutes**. Include
   a stage change or the next round's load so the test exercises a music
   transition. There is no required wait for music at the title or main menu.
2. Listen for music during play and after the next stage loads. Check that
   sound effects remain audible. Note silence, repeated short audio, delayed
   music starts, interruptions, distorted audio or a lockup.
3. After a failure, or after the completed test, press **A+B+X+Y+Start**
   together to return to diagnostics. Record **15 seconds** so all three pages
   are visible; they cycle automatically every five seconds. Then power-cycle.

Send that diagnostic recording and a short description of what you heard,
including whether music returned after the stage change. If boot stops at an
error screen, photograph that screen instead. A diagnostic return is required
to distinguish successful startup from sustained music delivery.

## Scope and recovery

The BAM4 profile reuses the Armada full-stream transport, progressive checked
PCM publication, native ring protection and cached restart path. Admission,
kernel/module identities, owner/process addresses and sound method guards are
specific to BAM4. Native driver code and game audio come from the selected
image at runtime.

**CD music Off** selects the ordinary CE loader. To restore the previous menu,
copy `KUI/runtime.kui` from your Armada RC9 ZIP with the console off and cold
boot; the preserved Armada audio module does not need replacing.

**Worms is not enabled.** Its different audio consumer/locking contract needs
separate work. This candidate does not claim universal Windows CE CDDA
support, smoother FMVs or that the remaining timing issues are resolved.

## Build and source evidence

Both installed payloads must match clean compiled ELFs, the same committed
HEAD, and their displayed/envelope build identifiers. Packaging verifies the
BAM4 linked layout, bounded stacks, post-relay bootstrap attachment, complete
owner reader guards and blank CEX2 source map. No private executable, CE
kernel, ARM firmware or PCM sample is included.

The ZIP includes complete current source as a tar archive, verified dependency
source archives and licenses from the pinned Armada RC9
delivery, build/audit records, and complete member SHA256/CRC manifests.
It does not replay the inherited historical corruption-test suites or ship
an Armada rebuild. The first hardware result remains the acceptance test.
