# Games scan fix and current main — @BUILD@

This includes `main` through `34e0ddce89b3`: visible All games/Favorites/Recently
played tabs, direct favorite toggling, screenshot Settings cleanup, and optional
native SCI session controls. It also fixes the Games scan crash reported on
`bbe142d4a278`. That build's Time Stalkers image boot and improved HOTD2 gameplay
are now owner-confirmed.

## Install and scan

Copy the ZIP's root **KUI folder** onto the microSD root and merge/overwrite.
Boot normally and check the menu build **@BUILD@**. The root update contains the
new menu and the five unchanged native modules from `bbe142d4a278`; their game
diagnostic pages continue to identify that module build. Saves, cards, journals
and existing activity records keep their paths.

In Games, open **START → Library options → Scan for changes**. **X now toggles
a favorite**, and **Y changes tabs**. Layout and rebuild are also in Library
options. Hold L+R and press Y to save a K-UI menu screenshot under
`KUI/screenshots`.

The scan reads actual disc metadata from existing rips. A proven empty or
manifest-only old rip folder is omitted from the saved launcher. Distinct real
copies remain available, and interrupted/failed scans retain the preceding
complete list. Existing uniquely matched disc-group favorites/history survive
the catalogue upgrade.

## Test order

| Order | Check | Settings |
|---|---|---|
| 1 | Scan for changes completes, then leave/reopen Games | Games / START / Library options |
| 2 | Deleted/re-ripped game has one live launcher entry | After the complete scan |
| 3 | Existing Shenmue rips have the correct disc order | Open the grouped row |
| 4 | Favorites, Recently played, layout and selection survive refresh | X favorite; Y tabs; layout in Options |
| 5 | Time Stalkers image still boots and can save/reload | Background / New (X) / VMU On / A1 |
| 6 | HOTD2 gameplay and EGG save/load regression | HOTD2 A2; EGG A1 |

Leave the selected physical VMU slot empty. Time Stalkers' image startup repair
and the HOTD2 optimizations remain in the normal modules. The latest HOTD2 photo
records a 7.13 ms maximum foreground delivery visit, versus 8.48 ms in the earlier
photo, with no recorded SD faults or PIO-window overruns. Different sessions do
not establish a controlled performance gain or that all remaining lag is gone.

## Optional disc switching and return to K-UI

The separate **OPTIONAL-CONTROLS/KUI** folder contains the new A2/A1 SCI session
modules. To test them, merge that folder's KUI contents onto the SD root after
the normal update, replacing its two Background native modules. These use a
112 KiB worker and 324 KiB complete lease, retaining the full 192 KiB cache.
They have linked/protocol test evidence; physical-console acceptance remains a
separate test. Restoring the root update's two modules restores the compact
72 KiB / 284 KiB profile.

Use SCI / Background On / New / VMU On / Auto encoding / CD music Off / RAM
patches Off. Start with a single-disc intentional return, then a matched two-disc
Skies or Code: Veronica set. The catalogue can snapshot at most four admitted
paths; actual game handoff compatibility still requires testing.

- Intentional return: hold L+R+A+B+X+Y without Start across 180 completed
  controller polls, photograph the diagnostic page, release, then press Start
  when **START RETURNS TO K-UI** is offered.
- Next disc: at the game's real disc request, hold L+R+Start with A/B/X/Y released
  across 90 completed polls, then release.

Read `details/NATIVE-SESSION-HARDWARE-TEST.md` for the complete sequence and
refusal/reporting rules. An unexpected fault page retains its diagnostic evidence.

## Remaining boundaries

Physical-disc regression cases remain Power Stone 2 A2/SCI and Berserk A1/SCI.
The persistent slot setting, insertion-readiness retry and preflight reuse are
retained. Time Stalkers' physical-disc VMU handoff remains unsupported; its
repaired continuation applies to image launches. Power Stone 1, Sonic Adventure
exit handling and the shared Silver lifetime/alias boundaries remain documented
separately; Silver's old Standard adapter is still the reference.

The complete historical eight-title audit remains blocked by missing original
Grandia II and Lodoss executables. No prebuilt discovery catalogue is installed.
Targeted filesystem, source and linked-code evidence accompanies this package;
this candidate still needs the console checks above.
