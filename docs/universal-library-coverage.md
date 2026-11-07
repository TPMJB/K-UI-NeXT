# Owner library coverage on `9e3295bcfaec`

The November 6 startup manifests represent 42 game products and 43 startup
identities after excluding incomplete Soul Calibur at the owner's request.
Shenmue Passport has a separate startup under Shenmue's product. There are
72 usable descriptor paths, including raw/cooked and disc variants. The
October 28 [inventory](evidence/native-library-inventory-2026-10-28.json)
represented approximately 39 games. Shenmue, Shenmue II EU and Test Drive Le
Mans extend it. Power Stone was already CUE-listed and now has its executable.
All 21 earlier collected startup identities match the latest manifest.
See the [ordered implementation roadmap](universal-loader-roadmap.md).

The audit uses the exact 28 software members of the `9e3295b` manual ZIP.
Console observations below retain the build and option scope of the original
test. A matching executable classifier or module admission is host evidence.
The existing `make test-universal-loader` suite passed on the audited source.

## Shared code in the delivered ZIP

| Installed path | Shared coverage | Open work |
| --- | --- | --- |
| Native SD Standard, standalone New, and ZSO/LZO | Ordinary checked cold reads and verification on `f8807cb16561`. Standard virtual VMU is `9e3295bcfaec`. | Native VMU and Background require an admitted GDI and SCI path. ZSO has its own experimental native SCI Standard mode. CSO and CHD need import. |
| Compact native Background A1/A2 and physical-disc VMU A1/A2 | `9e3295bcfaec` includes the faster extent and allocation scans in all five ordinary virtual-VMU readers, counting Standard. | Background VMU Off still reserves the 284 KiB Background region. Standalone New covers a narrower demand-only path. Physical-disc VMU has separate Maple and allocator admission. |
| Native CD music | Toy Commander raw/cooked DATA and MDK2 raw on the checked `f8807cb16561` adapters. | Other native CDDA runtimes lack qualified audio adapters. Independent virtual VMU, read-ahead and CDDA combinations still need coverage. |
| WinCE | Four `eeed38bd540e` SCI GDI CD music adapters have the VRAM-safe sink and folded CRC16. | The ordinary CD-music-Off path still opens `ce-probe.kui`, absent from this ZIP. It depends on an earlier installation, which may supply the personalized Armada VMU adapter. The generic module was host-qualified separately. Separating the two routes requires explicit compatibility admission and a real package check. |
| Smash Pack | The exact accepted `cc81e63ca4d2` modules and helpers use their own checked collection handoff on the original SCI GDI route. | VC2 load latency remains a separate observation. Collection acceptance applies to the retained bytes and verified source. |
| GD Play and image formats | Physical native GD Play without virtual VMU uses the real drive. Ordinary SD GDI, BIN/CUE and supported CDI layouts can use the appropriate native reader. | Optical boot and an SD rip are separate paths. A1/A2 virtual VMU, CD music, compressed formats and transport combinations have narrower admission than plain image loading. |

See [native launch coverage](shared-launch-coverage-restoration.md),
[current CE delivery](review-06nov2026.md), [image formats](games-formats.md)
and [physical-disc scope](physical-disc-vmu-test.md).

## Native GDI titles

The 21 exact original startups from the October corpus were checked against
current production extent, native resolution and standalone SDK classifiers.
All 21 passed the extent check. Sixteen passed immutable combined resolution.
Four of the five combined refusals passed standalone SDK recognition. Blue
Stinger refused both readers' SDK admission. Runtime lease checks, slot
selection, live callback binding and console gameplay are separate gates.
Standard with physical cards is a separate route for testing base playability
on the existing image when optional native services refuse.

| Titles | Best established result and remaining gap |
| --- | --- |
| Evolution, Evolution 2 | Evolution has earlier New + virtual A2 boot/load evidence. Evolution 2 supplies the current SCI Background New, VMU On timing checkpoint. |
| Rayman 2, Record of Lodoss War | Earlier smooth FMV/gameplay and virtual save/load. Rayman also has a power-cycle reload report. Lodoss CD music is a separate gap. |
| Resident Evil Code: Veronica | Earlier US save, power-cycle reload and Disc 2 success. |
| Crazy Taxi 2, Jet Grind Radio | Earlier standalone boot/FMVs/play and subsequent positive game-card/virtual A2 reports. |
| Sonic Adventure, Sonic Adventure 2 | SA1 previously played and its exact original now binds plain and nesting SCI callbacks in the linked `f8807cb16561` standalone host model. The old reader refuses nesting. Current console callback activity and SA1 combined VMU behavior remain open. SA2 has an earlier positive New gameplay report. |
| Power Stone 2, Elemental Gimmick Gear, Time Stalkers | Earlier image launches worked. EGG has a reported save/load. Power Stone 2 also has distinct physical-disc SCI virtual save/reload evidence. Time Stalkers still has a separate physical-disc startup-clear issue. |
| Skies of Arcadia | Earlier raw and cooked boots and paced Standard virtual A2 save/reload. The documented opening Cupil hang is configuration-specific and remains open. |
| The House of the Dead 2 | Earlier virtual A2 save/reload. Lag and a reported boss freeze remain open. Current original resolves the exact compact arena. |
| Sword of the Berserk | Original passes immutable combined resolution. Its save path targets A1. Earlier physical-disc play on A1 is separate from image virtual-VMU persistence. |
| Silver | Historical exact SCI Standard adapter saved and reloaded after power-off. Current original passes extent and combined immutable discovery. Shared live VMU/exit behavior still needs console evidence. |
| Phantasy Star Online | Earlier boot/play report with poor intro FMV. Current original passes combined immutable resolution. Current runtime and virtual save behavior still need evidence. |
| Dead or Alive 2, Crazy Taxi, Zombie Revenge | Current standalone SDK discovery passes and combined resolution refuses. DOA2 played well on the earlier standalone New build. Crazy Taxi needs a proved retained arena. Zombie Revenge needs its Maple layout and reservation proved. The Japan DOA2 BIN/CUE variant has separate coverage from the tested original. |
| Spawn: In the Demon's Hand | Earlier standalone boot with poor intro and distorted audio. Current standalone SDK discovery passes and combined resolution refuses. |
| Blue Stinger | Current original refuses combined and standalone recognition. The early interrupt-table initializer differs from the admitted family. Standard physical-card gameplay remains a separate question. |

See [native console checkpoints](evidence/native-console-known.json),
[native VMU history](universal-vmu-review-2026-11-01.md),
[early SCI family](native-zr-family.md) and
[PSO and Spawn reports](evidence/native-pso-spawn-console-c2e6f07-2026-10-28.json).

## CD audio, Windows CE and other images

| Titles | Best established result and remaining gap |
| --- | --- |
| Toy Commander, MDK2 | Their installed native CDDA adapters have earlier positive music reports. Toy's FMV stutter remains. MDK2 had an occasional audio cut. The older Toy virtual-VMU pass is a separate configuration. |
| Grandia II, Record of Lodoss War, Quake III Arena | Earlier gameplay reports. Quake III's boot result is separate from CD music, which remains deferred. Grandia II and Lodoss play results do not establish CDDA support. |
| Frogger 2, Generator 2, Omikron, Rush 2049, Soul Reaver, Tony Hawk's Pro Skater 2 | Current as-is console play and music evidence is incomplete. The November originals permit a current comparison. Tony Hawk 2 now passes immutable combined resolution, superseding its earlier structural refusal. The remaining games need their actual first blocking gate resolved. |
| Sega Smash Pack Volume 1 | Accepted session covered VC2, Shining Force, Phantasy Star II, Sega Swirl, all other games, and return to VC2. Use the exact retained components and original GDI route. |
| Armada, Bust-A-Move 4, Worms Armageddon, Worms World Party | Four distinct SCI GDI music adapters are installed. Historical hardware evidence ranges from successful play/music on Armada, BAM4 and Worms Armageddon to a scoped WWP result. Armada FMV/audio quality and WWP full play/audio continuity remain open. Armada's historical writable VMU pass used its separate personalized adapter. |
| Nightmare Creatures II | Inventory entry with no verified console result found. Music On has no matching adapter in this package. The ordinary SCI WinCE path depends on the separately installed `ce-probe.kui`. |
| Power Stone | Earlier SD gameplay and separate original-disc direct boot successes. Its newly supplied CUE startup passes SCI and Maple driver recognition, with Maple layout still refused. It enables direct investigation of the known physical-disc VMU blocker. CUE/New selection is a separate route requirement. |
| Quake III Arena, Test Drive Le Mans | The later supplement adds Quake's intro/main executables and Le Mans' movie/main executables. Their true load addresses and service lifetime across transfers need proof before a primary-startup VMU result can cover the game. |
| Shenmue I, Passport, PAL Shenmue II, PlanetWeb | Three Shenmue startups are now supplied. Current shared native family classifiers do not recognize them. PAL Shenmue II optical boot and SD rip have separate successful reports. PlanetWeb is recalled from the owner's list and remains outside this census. |

See [native expanded sweep](evidence/native-expanded-library-review-2026-10-28.json),
[Tony Hawk 2 original-backed evidence](evidence/native-tony-hawk2-r1-discovery-2026-10-28.json),
[CD music and formats](release-v2.0-notes.md),
[Smash acceptance](accepted-progress.md) and [Shenmue II](shenmue-ii-disc-test.md).

## Remaining launch work

The latest Evolution 2 timing suggests another two to three seconds saved.
The owner revises COLD 03 to seven to eight seconds from approximately 21
seconds in the earlier recording. COLD 03 is the extent proof. The latest
change targets the earlier ownership/recovery phase. These estimates do not
isolate the exact savings of either phase on the latest run.

Warm launch still reads the owner executable and performs a fresh detached
allocation walk before giving the VMU worker write authority. The remaining
opportunity is a verified ownership transfer after the final GUI writer, with
recovery and invalidation across resets or card changes. Cold cache misses also
repeat some executable hashing in the GUI. That is a separate first-launch
cost. Neither requires dropping extent, allocation or integrity checks.

The first deployment gap is a separate ordinary WinCE module. The historical
path can hold an Armada-specific module with the same CE envelope, so an
absence-only fallback misses a card that already has that adapter. Give
ordinary CE a separate path and retain the inherited adapter route for the
exact matching Armada source. The installed private adapter's bytes were
unavailable for this audit. Qualify both routes in the actual package before
asking for a Nightmare Creatures II or Armada comparison.

Blue Stinger's original has the same early handler and stubs as an admitted
family, with a different VBR table initializer. A source-backed initializer
and live slot proof would extend standalone coverage. The combined Maple and
memory requirements remain separate. For already booting titles, streaming
tests need the same scene, exact reader and active audio state.
