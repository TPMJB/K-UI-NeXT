# K-UI NeXT handoff (2026-09-28)

Where the project stands, what the hardware needs next, and a brief for the
artwork. It is written for whoever picks the project up, including another
AI assistant helping with the art. The disc reader has its own handoff:
[HANDOFF-disc-reader.md](HANDOFF-disc-reader.md).

## The project in one paragraph

K-UI ("Katana User Interface", after the Dreamcast's codename) is TPMJB's
independent Dreamcast environment, built directly on upstream KallistiOS. A
reusable boot CD loads the SD runtime (`/KUI/runtime.kui`) from an SD card
in the serial port's SD adapter. Home has ten apps, in this order: Games,
Disc Ripper, VMU Manager, File Manager, Music Player, GD Play, Memory Test,
Network, Diagnostics and Settings. Version 1.5.1 "Dáinsleif" is released
from `main`. The applications are in good shape. What is left is bringing
up new hardware (a W5500 network chip, a Wi-Fi board, later an IDE/CF
drive) and artwork.

## Branches

| Branch | What is on it | State |
| --- | --- | --- |
| `main` | The 1.5.1 release | Released |
| `claude/modest-galileo-hpjv79` | 1.5.1 plus the File Manager, Games first on Home, the W5500 driver and FTP server, the SCI connector plan, the CF board design, and this handoff | CI green (host tests and Dreamcast build). The W5500 and FTP work on the owner's console (2026-09-29), at about 370 KiB/s each way |
| `claude/wifi-esp32c5-firmware` | Everything above, plus the Wi-Fi board's firmware (`firmware/kui-wifi`) and K-UI's side of it: the Wi-Fi page, and FTP over Wi-Fi | CI green (host tests, both boards' firmware builds, Dreamcast build). Not tried on hardware; the boards have not arrived |

The Wi-Fi branch is meant to go into the W5500 branch once the board works
on a console, and that branch into `main` for the next release. The
`milestone/*` and `baseline/*` branches are pinned history; leave them.

## Next on the hardware

### The W5500 (works on the console)

The owner reports the W5500 working on the console (2026-09-29). It takes
its power from the Robot Retro power supply's 5 V instead of 3.3 V at
CE113, which saved two solder points; that suits a W5500 module with its
own 3.3 V regulator (a 5V pin).

FTP works too. With the SPI link at 12.5 MHz and a 100 Mbit/s full-duplex
cable link, uploads to the SD card ran at about 304 KiB/s and downloads at
260 to 320 KiB/s, in bursts. The server slept about 8 ms (a scheduler
tick) each time a transfer waited for the network; commit `765f8d4` keeps
transfers moving instead, and with it both directions run at about
370 KiB/s. Downloads still come in bursts, because the network waits while
each 16 KB is read from the card.

What limits each direction now: an upload spends about two-thirds of its
time reading the W5500 through KallistiOS's SCI read, which waits after
every byte; the card write is the rest. A download spends most of its time
reading the card (about 700 KB/s), with the network idle meanwhile.
KallistiOS's DMA mode for the SCI (DMA channel 1) could stream those reads
and, for downloads, send while the card is read. The first step, DMA for
reads, is on this branch for the owner to test: the FTP screen says
"12.5 MHz with DMA" when it is in use. It is not yet on the Wi-Fi branch;
if it works, it goes into that branch's shared SCI layer. Sending while the
card is read comes after that.

- **Build:** the Diagnostic build run
  [36635569185](https://github.com/TPMJB/K-UI-NeXT/actions/runs/36635569185)
  (`claude/modest-galileo-hpjv79`, commit `a911dc9`, with the transfer fix).
  Download `sd-update`, merge its `KUI` folder onto the card and keep the
  boot CD.
- **Wiring:** [the FTP server's hardware section](ftp.md#the-hardware) and
  [the SCI connector plan](sci-connector.md). The chip select goes on RA101
  (GPIO7), MISO on R115, MOSI on R122, SCLK on R140, 3.3 V and ground at
  CE113. Check every point with a multimeter first.
- **Test:** [the FTP console test](ftp.md#console-test). Run Network → A
  (Inspect), X (Test network) and Y (FTP server), copy a large file both
  ways, and photograph each screen with its speed.

### The Wi-Fi board (Seeed XIAO ESP32-C5, arriving in a few weeks)

1. Load the firmware from a computer and try it on the bench over USB:
   [firmware README](https://github.com/TPMJB/K-UI-NeXT/blob/claude/wifi-esp32c5-firmware/firmware/kui-wifi/README.md).
2. Wire it per the SCI connector plan. For 5 V, use the drive connector's
   5 V, or the Robot Retro power supply's 5 V fan header if these checks
   pass:
   - the supply is version 1.1 or later;
   - the header measures a steady 5 V with the console running;
   - it can supply about 500 mA (Wi-Fi bursts draw 300 to 400 mA);
   - its ground is shared with the console's.
   Keep a ~220 µF capacitor next to the XIAO, run a ground wire with the
   signals, and never use the console's original fan port. The W5500
   already runs from that supply's 5 V. The XIAO's 5V pin is also its USB
   power line, so put a Schottky diode (a 1N5817 or SS14, band toward the
   XIAO) in its 5 V lead, as Seeed advises for powering a XIAO through that
   pin; otherwise unplug the lead before connecting USB.
3. Console test:
   [the Wi-Fi guide](https://github.com/TPMJB/K-UI-NeXT/blob/claude/wifi-esp32c5-firmware/docs/wifi.md#console-test).

### The CF board (designed, not yet made)

The mainboard is a VA1, the card goes inside, and the owner's photos of
both sides are in. [hardware/cf-board](../hardware/cf-board/README.md) has
the rev 1 design: a 66.5 x 54 mm board with a CompactFlash socket, wired
as the slave beside the GD-ROM drive. It includes the schematic, the
routed board, Gerbers, the parts list, a wiring guide and a 1:1 fit
template. Its netlist and DRC checks are clean.

1. Print the fit template and find a spot inside the console for it.
2. Check the drive connector's orientation (the README's first wiring
   step).
3. Order the board and parts, then build and wire it.

K-UI cannot read the card until the storage rework adds a G1 ATA path.

### Waiting for a console check

The File Manager and the Games-first Home order (both on the W5500 branch)
have passed their computer tests but have not been reported on a console.

## Fixed on 2026-09-28 after a code review

A review of the code by ChatGPT raised three faults. Each was reproduced,
fixed, given a test, and passed in CI:

1. **FTP could lose a file it was replacing.** The old file was deleted
   before the upload took its name, so if that rename failed, both copies
   were lost. Now the old file waits under a spare name
   (`KUI-ftp-<n>.kui-old`) and gets its name back if anything fails. Fixed
   on both branches.
2. **The Wi-Fi firmware accepted an update too early.** It confirmed a new
   image before knowing its tasks and buffers had started. Now an update
   that fails to start rolls back at once, and one that starts is kept only
   after the Dreamcast has reached it over the link. Fixed on the Wi-Fi
   branch.
3. **A name lookup could outlive a link reset** and answer the next
   request with the old result. Each lookup now carries a ticket, and stale
   results are dropped. Fixed on the Wi-Fi branch.

## Art brief

This section is for the art. It says what K-UI looks like, where each
piece of artwork goes, the technical limits, and what would help most.

### What K-UI looks like

![Home screen icons today](screenshots/home-icons.png)

- **Screen:** 640×480, drawn straight into a 16-bit framebuffer (RGB565:
  32 levels of red and blue, 64 of green). There is no alpha blending at
  run time. Icons mark transparent pixels with a key colour (RGB565
  `0xF81F`, pure magenta), and their soft edges are blended in advance
  against the panel colour.
- **Layout:** the header has the 128×64 K-UI brand at the top left, the
  version, the song playing and the build. On Home, a 208-pixel-wide app
  list sits on the left, with a 24×24 icon on each row. The right pane
  shows the chosen app's name, a 128×128 icon, three lines of description
  and an "A Open app" button. The controls line sits at the bottom. All
  text stays at least 32 pixels from the left and right edges.
- **Fonts:** DejaVu Sans, 14 px regular and 20 px bold, ASCII only, with
  4-bit antialiasing. Accented letters (as in Dáinsleif) can only appear in
  artwork, not in on-screen text.
- **Palette:** the original K-UI launcher's colours:

  | Colour | Hex | Used for |
  | --- | --- | --- |
  | Navy | `#080F23` | screen background |
  | Panel | `#121A31` | panels; icons sit on this |
  | Violet | `#472958` | selected row, icon frames |
  | Pink | `#F07DDC` | selection edge, accents |
  | Cyan | `#65E8F2` | accents, the main button, highlights |
  | Divider | `#343354` | rules and edges |
  | White | `#F6F6FF` | titles, selected text |
  | Muted | `#ACBACD` | secondary text |
  | Green | `#8BDEB4` | passed |
  | Amber | `#FFB64A` | warnings |

- **Brand:** the startup splash ([startup.png](../resources/branding/startup.png))
  shows the K-UI character: a woman in a large futuristic visor, drawn in
  neon outlines, with chrome "K-UI" lettering and a perspective grid, in a
  1980s retro-futuristic style. For Dáinsleif it is crimson neon with
  restrained cyan. The request that produced it is kept word for word in
  [startup-dainsleif-prompt.txt](../resources/branding/startup-dainsleif-prompt.txt).
  The header brand and the boot-disc badge are older K-UI artwork in cyan
  and magenta.

### Where artwork goes

| Piece | Files | Size and format | How it gets into the build |
| --- | --- | --- | --- |
| Startup splash | `resources/branding/startup.png` | 640×480 RGB. The artwork fills 592×444, centred on `#030913`, with lettering inside TV-safe margins (7%). Keep the large source too | `tools/build_splash.py` converts it at build time. It checks the PNG's pinned Git blob, so a new splash updates `PNG_BLOB` and `startup-README.md` |
| Header brand | `resources/branding/launcher-brand.png` | 256×128 source, shown at 128×64, opaque on navy | `tools/generate_shell_art.py` writes `src/dreamcast/shell_art.inc`; it checks pinned SHA-256s |
| Home icons (3 drawn so far) | `resources/icons/<name>.svg` and `.png` | 64×64 PNG with transparency; shown at 128×128 and 24×24 | `tools/generate_shell_art.py` (the `SOURCES` and `ICONS` lists), and `home_apps[].art` in `src/dreamcast/shell_draw.c` |
| Home icons (7 placeholders) | none yet | see below | The same, once art exists |
| Boot-disc badge (under the SEGA licence screen) | `resources/branding/boot-disc-badge.*` | 320×90, at most 32 colours, MR format, at most 8,192 bytes | Packaging passes it to mkdcdisc. A new badge needs an MR encoder step, which the repo does not have yet |
| Startup sound | `resources/branding/startup-chime.ogg` | ≤ 2.65 s, 44.1 kHz mono Ogg Vorbis | `tools/build_splash.py --encode-chime` |
| Menu music | `resources/music/*.ogg` | Original synthesized songs, Ogg Vorbis | See `resources/music/README.md` |
| Game box art | the user's own, in `KUI/covers/` on the card | PNG or JPEG, any size | Not bundled. See [games-covers.md](games-covers.md) |

The runtime file must stay under 4 MiB. Each 128×128 icon costs 32 KiB in
it, and the splash about 600 KiB, so a full icon set is affordable.

### What would help most

1. **Icons for the seven apps that have placeholders.** The contact sheet
   above shows them as flat blocks. What each app does:
   - **Games:** launches games stored on the SD card, with box art.
   - **VMU Manager:** the Dreamcast memory card (the VMU, which has a small
     screen). Browses, backs up and restores game saves.
   - **File Manager:** every file and folder on the SD card.
   - **Music Player:** WAV and Ogg music from the card, and audio CDs.
   - **GD Play:** boots the disc in the drive through the console's own
     BIOS. It borrows the Disc Ripper's icon today, so it needs its own.
   - **Memory Test:** checks the console's RAM.
   - **Network:** the network adapter, Wi-Fi setup, and sharing the SD card
     over FTP.

   Match the three existing icons (a neon line drawing inside a rounded
   square with a violet edge), or propose one new set for all ten. The
   owner decides.
2. **A splash for the next release,** once the owner names it. Keep the
   character and composition, and restyle the colours and lettering as
   Dáinsleif did.
3. Later: a new boot-disc badge, which needs the MR encoder step first.

### Rules for the artwork

- **Size:** deliver icons as 128×128 PNGs with transparency, plus a
  separately simplified 24×24 version. At 24×24, a scaled-down detailed
  icon turns to mush. Today's pipeline takes 64×64 sources; accepting
  128×128 and 24×24 is a small change to `tools/generate_shell_art.py`.
- **Generated images:** image generators rarely produce exact sizes or
  clean transparency. Generate large (for example 1024×1024) on a flat
  background that can be keyed out, and scale down afterwards.
- **Lines and detail:** the picture goes to a television, often over
  composite. Use strokes of at least 2 pixels at final size, avoid
  one-pixel horizontal lines (they flicker on interlaced output), and keep
  shapes bold and simple.
- **Colour:** use flat colours from the palette. RGB565 bands smooth
  gradients, so dither any gradient that must stay. Never use pure magenta
  (`#FF00FF`) in an icon, because that is the transparency key. Avoid
  large areas of saturated pure red, which smear on composite.
- **Text:** none in icons. In the splash, keep lettering inside the TV-safe
  margins and large enough to read at 640×480.
- **Originality:** original work only. No SEGA or Dreamcast logos or
  trademarks, no characters or art from games, and nothing from
  DreamShell (the old DreamShell logo is deliberately unused).
- **Provenance:** for each piece, keep the source file, the date and, if it
  was generated, the exact prompt, as
  [resources/branding/README.md](../resources/branding/README.md) and
  [startup-README.md](../resources/branding/startup-README.md) do.
  Artwork in the repo is distributed under the project's GPL-3.0-only terms.

### Getting new art into the build (for whoever writes the code)

- Icons: add the sources to `resources/icons/`, extend `SOURCES` and
  `ICONS` in `tools/generate_shell_art.py`, run it to regenerate
  `src/dreamcast/shell_art.inc`, and point the apps' `art` fields in
  `src/dreamcast/shell_draw.c` at the new icons. Then record the hashes in
  `resources/branding/README.md`.
- Splash: replace `resources/branding/startup.png`, update `PNG_BLOB` in
  `tools/build_splash.py`, and record it in `startup-README.md`.
- Preview on a computer, without a console: `make build/render-shell`, then
  `python3 tools/render_app_previews.py --output previews`, which renders
  every screen to PNG with the real drawing code.

### Starting prompts

For an icon:

> Design a Home-screen icon for K-UI, a homebrew menu for the Sega
> Dreamcast shown on a TV at 640×480. Match the attached Disc Ripper,
> Diagnostics and Settings icons: a neon line drawing inside a rounded
> square with a violet (#472958) edge, on the dark panel colour #121A31.
> Subject: "VMU Manager", the Dreamcast memory card with its small screen,
> which backs up and restores game saves. Use flat colours only: cyan
> #65E8F2, pink #F07DDC, white #F6F6FF. Bold 2-pixel lines at 128×128. No
> text, no logos, no pure magenta. Square image on a plain flat background.

For a splash:

> Edit the attached K-UI startup splash for the next version, named
> "<name>". Keep the woman in the visor, the chrome K-UI lettering on the
> right, the perspective grid and the 4:3 composition. Restyle it in
> <colours>. Lines: "K-UI V<version>", "<name>", "(Katana User Interface)",
> "by TPMJB". Keep all art and lettering inside 7% TV-safe margins. No
> SEGA logo, no extra text.

## Ground rules for the code

- **No DreamShell code.** K-UI is independent, so its development credits
  no one else's code. Assets reused from the older K-UI_DS repository are
  only those TPMJB made, each recorded with its origin.
- **Licences:** K-UI is GPL-3.0-only. The Wi-Fi firmware and its link
  library (`firmware/kui-wifi`) are MIT, because the firmware links
  Espressif's closed Wi-Fi libraries. See `THIRD_PARTY.md` and `LICENSES/`.
- **Proven paths:** `src/core/capture.c`, `src/dreamcast/disc.c` and
  `src/core/command.c` are proven on hardware; do not change them without
  new console evidence. Per-block CRC checks stay, and the pinned baselines
  stay as they are.
- **Storage:** the SD card stays on the serial port's SCIF. The SCI port is
  for the internal connector (network boards, later a microSD card).
- **Never commit game dumps** or copyrighted game artwork. Tests use
  abstract stand-ins.

## Where things are

| Path | What it holds |
| --- | --- |
| `src/core` | Portable logic, tested on a computer: capture, filesystems, FTP protocol, W5500 driver, shell state |
| `src/apps` | The apps' jobs: Games, Files, FTP server, Network, Wi-Fi driver (Wi-Fi branch) |
| `src/dreamcast` | Console glue: drawing (`shell_draw.c`), main loop, disc, SCI port |
| `include/kui` | Headers |
| `tests` | Host tests, image tests (FAT32 and exFAT with fsck), simulated W5500 and Wi-Fi board |
| `tools` | Packaging, art and font generators, preview renderer, dependency fetcher |
| `resources` | Branding, icons, fonts, music, each with a provenance README |
| `firmware/kui-wifi` | The Wi-Fi board's ESP-IDF firmware (Wi-Fi branch) |
| `docs` | Guides, test plans, release notes, evidence from the console |

## Building and testing

- On a computer: `make test test-images`. This needs FatFs, which
  `python3 tools/fetch_deps.py --fatfs-only` downloads.
- Dreamcast builds come from GitHub Actions. On `claude/*` branches, run the
  **Diagnostic build** workflow by hand. Its artifacts are `sd-update` (the
  card's `KUI` folder), `bootstrap-cd`, and the release packages.
- The Wi-Fi firmware builds in the **Wi-Fi firmware** workflow whenever
  `firmware/kui-wifi` changes on its branch (or when run by hand): host
  tests, then images for the ESP32-C5 and C6.
