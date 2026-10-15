<!-- SPDX-License-Identifier: GPL-3.0-only -->
# K-UI V2.0 RC12 "Tyrfing" — preparation notes

**2.0 RC12 adds small independent maintenance tools to the assembled Tyrfing
candidate, retaining the tested SCI WinCE CD music and orange artwork.**
Tyrfing continues the cursed-sword naming theme.
This is a source integration candidate; it is not an announcement that final
2.0 has shipped. The existing stable download remains
[1.8.5](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.8.5).

Use the [installation guide](release-v2.0.md) and install the complete matching
runtime and Games payload folder. Existing exFAT cards, dumps and compatible
independent K-UI boot CDs remain usable.

## RC12 maintenance additions

- **VMU card backup:** Actions → Backup card image captures a verified raw
  128 KiB VMD, including an empty formatted card, into a new backup directory.
- **VMD browsing/extraction:** Backups opens standard raw 128 KiB images,
  including external images without a K-UI sidecar. Extract one/all saves into
  new verified `.vms`/`.dir`/`.crc` bundles, retaining names and metadata. These
  image operations need no connected VMU and never write to a physical card.
- **BIOS/settings-flash comparison:** System Tools selects an exact-size SD
  image and compares two complete stable reads with the current console source.
  Results show exact match or byte-difference count and first offset. Comparison
  writes neither the source nor the image and creates no output files.

These tools use independently authored K-UI code and upstream KallistiOS
interfaces. No DreamShell implementation code was copied. Full-card restore,
format/overwrite, broader VMS/VMI/DCI import and bulk VMU copy remain outside
this candidate. See [VMU controls and limits](vmu-restore.md) and
[system comparison](system-backups.md). Fault-injection and real FAT32/exFAT
host-image checks pass; the new integrated console workflows await acceptance.

## Orange Tyrfing artwork and preparation tools

RC11 replaces the previous release's splash with orange retro futuristic
artwork featuring the cursed blade Tyrfing. The runtime and CD share matching
television-safe artwork, with a new sword header, under-SEGA disc badge and
orange bootstrap accents. Runtime/boot image dimensions, splash timing and
memory bounds are retained. The V2.0 illustration is release artwork; the
live **RC12** label and build ID identify this candidate. Updating the card
changes runtime artwork; the optional matching CDI changes the burned disc.

Games keeps its alphabetical order when titles are added and restores the
last folder/highlight by name. Explicit LZ4/LZO1X selection, Check executable
and a temporary unsaved Baseline complement the saved launch choices. The
startup VMU LCD shows K-UI and the current version. New gameplay screenshot,
RAM-patch, alternate-executable, boot-method, region-override and VMU-emulation
features remain deferred.

Disc Ripper adds bounded retry preferences, optional strict Mode 1 acquisition,
verified cooked GDI export, repair-to-copy and saved-sector reread evidence.
It can also capture explicitly declared single-session CDs as BIN/CUE, or
write directly to the authenticated PC-volume destination. These additions
are implemented and host-tested; their integrated console paths still need
acceptance. See [ripper controls](ripper-controls.md), [PC capture](network-rip.md)
and the [remaining differences from DS-based 1.0.1](release-v2.0-ds-comparison.md).

## Integrated Games behavior

Launch options are saved per image. **Y Options** exposes Background loading
and optional CD music; Advanced holds encoding, background speed, image
information and developer tests. Original and converted2048 image paths can
retain different choices. CD music defaults to Off, and Off uses the ordinary
CE loader. Windows CE remains dependent on the background SCI reader.

The optional WinCE music engine has four exact admitted kernel/sound adapters,
established from US **Armada, Bust-A-Move 4, Worms Armageddon and Worms World
Party**. Other applications and track layouts can use a matching checked
kernel/sound contract; title strings alone cannot establish compatibility.
Native CDDA similarly broadens the existing Toy/MDK2 bindings to matching
executable/driver identities with their memory and lifecycle checks retained.
The menu offers CD music for inspected high-density AUDIO candidates; an
unrecognized runtime is refused when music is On. Unknown versions remain
Experimental, and music defaults to Off. See [admission boundaries](experimental-cdda.md).

For admitted profiles, raw2352 AUDIO is retained with either original raw or
supported converted2048 DATA. Conversion is optional and does not provide
new compatibility by itself. Game code, CE modules and audio are read from the
owner's selected image; retail bytes are not release assets.

## Console evidence carried into this candidate

| Area | Recorded result | Limit |
| --- | --- | --- |
| Native 1.8.5 reader | Owner confirmed Sonic Adventure, Grandia II and Skies of Arcadia on `5973741a6077`; Power Stone 1/2 worked after earlier command fixes | Scoped runs, not full playthroughs or an RC12 retest |
| Armada WinCE music | Established exact US profile and working full-stream baseline | Audio/FMV limitations remain; no new Armada test is inferred from Worms work |
| Bust-A-Move 4 | Owner reported successful music on `ed0a2c809bda`; diagnostics recorded ten starts, no refusal/HRESULT and one recovered underrun | Other revisions, every track and a long soak remain unverified |
| Worms audio supply | `8a22ec45229a` had no observed underruns in two submitted runs, with gameplay lag reported | Those runs do not establish zero-dropout playback everywhere |
| Worms responsiveness | Latest RAM-copy build `1aa16821209b` was reported to have almost completely eliminated lag; remaining delay was almost imperceptible | Subjective owner result; no measured frame-rate or zero-overhead claim |
| Worms converted2048 | Owner reported three successful cold boots of the tested converted2048 version | Does not certify other revisions or conversions |
| Worms World Party | Owner reported successful operation on `b08802f8945f`; diagnostic clip shows two native starts 33 seconds apart, no recorded gaps/recoveries and no guard/bind/audio fault | Clip contains diagnostic pages, not live gameplay or audible continuous music; image geometry is not established and general status remains Experimental |

The latest tested performance source is published as
`d9f861ba87a2957234d0af276a4cfeb573367258`. The preceding bounded-worker
candidate had already reduced lag without affecting music. These results
support retaining that baseline while preparing the release; they do not
certify every profile in the newly assembled RC12 package.

See [native placement evidence](evidence/native-low-resident-2026-10-06.md),
[BAM4 results](bam4-ce-cdda-test.md),
[Worms refill results](wince-cdda-refill-console-result.md),
[worker-budget changes](wince-cdda-worker-budget-test.md) and
[the latest RAM-copy result](wince-cdda-copy-test.md).

## Performance changes and preserved behavior

The optional music worker has a bounded physical-work budget while native
playback is active. It publishes checked partial PCM before yielding instead
of monopolizing long source turns. Aligned checked RAM copies use integer
word accesses with exact tails and an unaligned fallback; Worms retains its
short-copy exclusion and source-boundary checks.

These changes reduce optional music-path scheduling and copy cost. They do
not increase the physical SCI link rate. The established 1.8.5 native reader,
ordinary CE loading, physical capture engine and sound driver retain their
separate roles. Host regression and linked-layout checks complement console
reports; modeled timings are not console measurements.

## Wireless and Home integration

The wireless software adds checked adaptive SCI pacing, reconnects after an
accepted band change, listener-ready FTP startup and bounded FTP frame bursts.
Network's Wi-Fi page can check and install a compatible adapter firmware image
from the card after confirmation. It verifies the chip, image digest, inactive
slot write and restarted firmware. See [console updates](wifi-console-update.md)
and [wireless performance evidence](wifi-performance-test.md). Firmware 0.1.3 is
built separately. The final timing changes still need console throughput and
round-trip file-integrity acceptance; host transfer results are not radio speeds.

The [direct PC capture destination](network-rip.md) uses the bundled Python
helper and an authenticated FAT32/exFAT volume image, without a temporary
local-card dump. Local capture stays the default. Host loopback checks do not
establish console adapter compatibility or throughput.

Seven original icon pairs complete the ten-app Home menu, including the
television-legibility follow-up. The established Wi-Fi wording, Games options
and current branding remain in the merged renderer.

The [legacy Games audit](audit-legacy-games.md) and
[legacy GD Ripper audit](audit-legacy-ripper.md) compare reachable features in
the DreamShell-adapted project. They identify follow-up work; no legacy loader
code was imported. [The integration record](release-v2.0-integration.md) lists
the selected branches and exclusions.

## Remaining limits before final 2.0

- **WinCE music:** four exact kernel/sound adapters can admit other applications
  sharing their checked contracts. Other kernels, sound ABIs and memory layouts
  still need adapter discovery and validation. Unknown combinations and World
  Party's broader profile remain experimental; the option defaults to Off.
- **Native games:** Time Stalkers still returns to K-UI. Some FMVs stall or
  skip; Toy Commander FMV stutter is not fixed by this work. Universal CDDA,
  Mode 2 Form 2 and subchannel emulation are not provided. Quake III CD music is
  deferred: its GINSU transfer and different allocator need a separate adapter.
- **Storage:** SCI is required for CE. IDE/CF is untested synchronous PIO;
  ATA DMA and CE on IDE/SCIF remain future work. ext4 is bootstrap-only.
- **Formats:** standard LZ4 and explicitly selected historical LZO1X ZSO have
  separate experimental native SCI Standard readers; console acceptance is
  pending. CSO and CHD require PC import. These bounded ZSO readers do not
  supply general resident compressed decoding. See [ZSO scope](games-zso.md) and [CHD work](games-chd.md).
- **Release acceptance:** build the complete matching package, pass its host
  and console build/layout checks, and confirm the assembled candidate on
  hardware before calling it final 2.0. Earlier per-feature test packages and
  subjective reports do not replace that integration check.

The ten-app shell, original/2048 catalogue, verified/resumable ripping, VMU
backups, checked file operations, Wi-Fi/W5500 setup and independent boot and
recovery remain established features. No new networking or IDE throughput
figure is claimed. The [1.8.5 notes](release-v1.8.5-notes.md) retain the detailed
history of those features.

New K-UI shell code is GPL-3.0-only except where stated. Separate Wi-Fi
firmware and kwlink are MIT; dependencies retain their original licenses.
Corresponding source, dependency pins and notices must accompany a packaged
release. See [source and licensing records](../THIRD_PARTY.md).
