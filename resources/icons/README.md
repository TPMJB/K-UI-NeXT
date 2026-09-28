# K-UI Home icons

Seven original icons added for TPMJB on 2026-09-28, under GPL-3.0-only:
Games, VMU Manager, File Manager, Music Player, GD Play, Memory Test and Network.
They extend the existing Disc Ripper, Settings and Diagnostics SVG set. The
three original SVG/PNG pairs and their embedded pixels remain byte-for-byte
unchanged; their provenance is in `resources/branding/README.md`.

## Design and provenance

The new icons were authored directly as editable SVG paths with Codex assistance;
no image-generation model, downloaded artwork, game art or third-party icon
library was used. The approved brief was to complete the seven Home icons in the
existing neon style, with a 128×128 detail icon and a separately simplified
24×24 list icon. `docs/HANDOFF.md` contains the full design constraints.

Each large source is `<name>.svg`, with its 128×128 RGBA render `<name>.png`.
Each small source is `<name>-small.svg`, with its 24×24 RGBA render
`<name>-small.png`. Small versions remove detail and use heavier strokes; they
are not reductions of the large drawings. The RGB565 transparency key is never
used as a visible colour. Transparent corners and antialiased edges are retained.

The shapes use cyan `#65E8F2`, pink `#F07DDC`, muted `#ACBACD`, the existing
border `#A54F9D`, and panel `#121A31`. GD Play has its own disc-and-play icon;
Network combines wireless arcs with wired endpoints. No text or logos appear
inside the new icons.

## Rebuild

```sh
python3 tools/render_home_icons.py
python3 tools/generate_shell_art.py
python3 tools/generate_shell_art.py --check
make build/render-shell
python3 tools/render_app_previews.py --home-only --output build/home-previews
```

Only artwork editing requires Inkscape and Pillow. The committed renders were
made with Inkscape 1.2.2; PNG metadata is stripped by Pillow. Rendering updates
`home-icons.json`, which pins all 28 source and rendered files by SHA-256.
Ordinary Dreamcast builds consume the committed `shell_art.inc` without SVG
or PNG decoding, extra file reads, or image allocations on the console.

The brand and ten icon pairs occupy 355,584 bytes of embedded RGB565 data,
237,440 bytes more than the previous three-icon set. The artwork is part of the
SD runtime; a working existing boot CD can load it. Generated Home previews use
the production renderer and are not a separate mockup.

Preview: [all ten icons](../../docs/screenshots/home-icons.png) and
[Games selected on Home](../../docs/screenshots/home-games.png).
