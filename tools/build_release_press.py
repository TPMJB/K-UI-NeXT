#!/usr/bin/env python3
"""Build K-UI 2.0.0 press images from accepted artwork and the real UI renderer.

Requires Pillow and the normal host C build dependencies. No console captures,
retail cover artwork or benchmark measurements are synthesized by this tool.
"""
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import argparse
from io import BytesIO
from pathlib import Path
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "resources/release-v2.0"
BG = "#080b13"
PANEL = "#111827"
WHITE = "#fff4df"
MUTED = "#b2b9c8"
ORANGE = "#ff8b35"
LINE = "#34303a"
FONTS = Path("/usr/share/fonts/truetype/dejavu")
FONT_FILES = {
    "regular": "DejaVuSans.ttf", "bold": "DejaVuSans-Bold.ttf",
    "mono": "DejaVuSansMono.ttf",
}

# All are sample states passed through src/dreamcast/shell_draw.c. The press-
# prefix adds an explicit UI SAMPLE build label and sorted abstract covers.
UI = {
    "home": "home-games", "games-library": "games-list-art",
    "games-gallery": "games-gallery", "games-variants": "games-variants",
    "games-options": "games-options", "wince-cd-music": "games-ce-options",
    "disc-ripper": "ripper-settings", "ripper-tools": "advanced",
    "vmu-manager": "vmu", "vmu-image": "vmu-image",
    "vmu-import": "vmu-import-preview", "network": "network",
    "wifi": "wifi-online", "file-manager": "files-root",
    "system-tools": "system-tools", "audio-cd": "audio-cd",
}


def font(size: int, weight: str = "regular") -> ImageFont.FreeTypeFont:
    path = FONTS / FONT_FILES[weight]
    if not path.exists():
        raise SystemExit(f"Missing font {path}; install DejaVu fonts to reproduce the press kit")
    return ImageFont.truetype(str(path), size)


def text(im: Image.Image, xy: tuple[int, int], value: str, size: int,
         fill: str = WHITE, weight: str = "regular", spacing: int = 8) -> None:
    ImageDraw.Draw(im).multiline_text(xy, value, font=font(size, weight),
                                    fill=fill, spacing=spacing)


def wrap(value: str, width: int, size: int, weight: str = "regular") -> str:
    f = font(size, weight)
    lines = []
    for para in value.split("\n"):
        line = ""
        for word in para.split():
            candidate = (line + " " + word).strip()
            if f.getlength(candidate) > width and line:
                lines.append(line)
                line = word
            else:
                line = candidate
        lines.append(line)
    return "\n".join(lines)


def save(im: Image.Image, name: str) -> None:
    path = OUT / name
    encoded = BytesIO()
    im.save(encoded, format="PNG", optimize=True)
    staging = path.with_suffix(".png.tmp")
    staging.write_bytes(encoded.getvalue())
    staging.replace(path)
    with Image.open(path) as check:
        check.load()
    print(path.relative_to(ROOT))


def canvas(size: tuple[int, int] = (1920, 1080)) -> Image.Image:
    im = Image.new("RGB", size, BG)
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, size[0], 8), fill=ORANGE)
    d.line((72, 120, size[0] - 72, 120), fill=LINE, width=2)
    text(im, (72, 42), "K-UI", 40, weight="bold")
    text(im, (230, 55), "INDEPENDENT DREAMCAST SHELL", 22, MUTED, "mono")
    label = "2.0.0 / TYRFING"
    x = size[0] - 72 - int(font(25, "bold").getlength(label))
    text(im, (x, 53), label, 25, ORANGE, "bold")
    return im


def screenshot(im: Image.Image, name: str, xy: tuple[int, int],
               size: tuple[int, int]) -> None:
    x, y = xy
    d = ImageDraw.Draw(im)
    d.rectangle((x - 9, y - 9, x + size[0] + 9, y + size[1] + 9), fill="#29252b")
    ui = Image.open(OUT / "ui" / (name + ".png")).convert("RGB")
    im.paste(ui.resize(size, Image.Resampling.NEAREST), xy)


def footer(im: Image.Image, note: str = "Software-rendered UI · sample data") -> None:
    w, h = im.size
    d = ImageDraw.Draw(im)
    d.line((72, h - 70, w - 72, h - 70), fill=LINE, width=2)
    text(im, (72, h - 50), "github.com/TPMJB/K-UI-NeXT", 23, WHITE, "mono")
    x = w - 72 - int(font(22).getlength(note))
    text(im, (x, h - 50), note, 22, MUTED)


def feature(filename: str, kicker: str, title: str,
            bullets: list[tuple[str, str]], ui: str, note: str = "") -> None:
    im = canvas()
    text(im, (72, 174), kicker, 24, ORANGE, "mono")
    text(im, (72, 227), title, 69, WHITE, "bold", spacing=3)
    y = 440
    for heading, body in bullets:
        ImageDraw.Draw(im).rectangle((73, y + 12, 82, y + 21), fill=ORANGE)
        text(im, (105, y), heading, 31, WHITE, "bold")
        body = wrap(body, 590, 27)
        text(im, (105, y + 48), body, 27, MUTED, spacing=7)
        y += 48 + 34 * len(body.splitlines()) + 29
    if note:
        text(im, (72, 938), wrap(note, 650, 22), 22, ORANGE, spacing=5)
    screenshot(im, ui, (808, 183), (1024, 768))
    footer(im)
    save(im, filename)


def render() -> None:
    subprocess.run(["make", "build/render-shell"], cwd=ROOT, check=True)
    (OUT / "ui").mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kui-press-") as tmp:
        for name, mode in UI.items():
            ppm = Path(tmp) / (name + ".ppm")
            subprocess.run([str(ROOT / "build/render-shell"), "press-" + mode, str(ppm)],
                           cwd=ROOT, check=True)
            with Image.open(ppm) as im:
                im.save(OUT / "ui" / (name + ".png"), optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-render", action="store_true",
                        help="reuse already generated UI previews")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    if not args.skip_render:
        render()
    art = Image.open(ROOT / "resources/branding/release-v2.0-banner.jpg").convert("RGB")
    shutil.copyfile(ROOT / "resources/branding/release-v2.0-banner.jpg", OUT / "00-tyrfing-artwork.jpg")

    # The accepted pixel artwork stays intact, with an exact final-version label
    # on a separate editorial band. Its original embedded title says V2.0.
    im = Image.new("RGB", (1920, 1230), BG)
    im.paste(art.resize((1920, 1080), Image.Resampling.NEAREST), (0, 0))
    ImageDraw.Draw(im).rectangle((0, 1080, 1920, 1087), fill=ORANGE)
    text(im, (66, 1115), "K-UI 2.0.0", 54, WHITE, "bold")
    text(im, (520, 1132), "TYRFING", 30, ORANGE, "mono")
    text(im, (1100, 1140), "github.com/TPMJB/K-UI-NeXT", 27, MUTED, "mono")
    save(im, "01-release-hero.png")

    im = Image.new("RGB", (1600, 1600), BG)
    im.paste(art.resize((1600, 900), Image.Resampling.NEAREST), (0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 900, 1600, 908), fill=ORANGE)
    text(im, (84, 960), "K-UI 2.0.0", 96, WHITE, "bold")
    text(im, (88, 1092), "TYRFING", 39, ORANGE, "mono")
    text(im, (84, 1190), "Games. Saves. Discs. Network.", 44, WHITE, "bold")
    text(im, (84, 1264), "An independent shell for your Dreamcast.", 36, MUTED)
    d.line((84, 1390, 1516, 1390), fill=LINE, width=2)
    text(im, (84, 1441), "github.com/TPMJB/K-UI-NeXT", 35, MUTED, "mono")
    save(im, "02-release-square.png")

    im = canvas((1920, 1600))
    text(im, (72, 160), "A whole console toolbox.", 63, WHITE, "bold")
    panels = [("home", "01  HOME", (88, 300)),
              ("games-gallery", "02  GAMES", (1056, 300)),
              ("vmu-manager", "03  VMU MANAGER", (88, 940)),
              ("network", "04  NETWORK", (1056, 940))]
    for ui, label, (x, y) in panels:
        text(im, (x, y - 42), label, 24, ORANGE, "mono")
        screenshot(im, ui, (x, y), (768, 576))
    footer(im)
    save(im, "03-ui-overview.png")

    feature("04-games.png", "GAMES", "Less setup.\nMore playing.", [
        ("A library that stays sorted", "Alphabetical order, including games added later."),
        ("One title, two copies", "Choose Original or a 2048-byte copy where both exist."),
        ("Options that stay put", "Background loading and CD music save per image."),
    ], "games-variants")

    feature("05-wince-cd-music.png", "OPTIONAL CD AUDIO", "Windows CE.\nWith CD music.", [
        ("Opt in on SCI", "Playback for compatible kernel and sound profiles."),
        ("Keep the audio tracks", "Original raw2352 AUDIO; raw or supported 2048 DATA."),
        ("Choose it per image", "Music defaults to Off. Runtime checks apply."),
    ], "wince-cd-music", "Profile coverage varies. World Party remains experimental.")

    feature("06-disc-preservation.png", "DISC RIPPER", "Capture. Check.\nRecover.", [
        ("Resume an interrupted rip", "Checkpoints and bounded retry preferences."),
        ("Export and verify", "Cooked GDI export alongside retained formats."),
        ("Tools for difficult discs", "Repair-to-copy and saved-sector reread evidence."),
    ], "ripper-tools", "New ripper workflows: host-tested; console checks pending.")

    feature("07-vmu-saves.png", "VMU MANAGER", "Keep your\nsaves close.", [
        ("Back up whole cards", "Verified 128 KiB VMD card-image backups."),
        ("Open and extract", "Browse a VMD and extract saves without a VMU."),
        ("Bring saves together", "Import VMS, VMI or DCI; preview copy-all batches."),
    ], "vmu-image", "New VMU workflows: host-tested; console checks pending.")

    feature("08-network.png", "NETWORK", "Your Dreamcast.\nOn the network.", [
        ("Wired or wireless", "W5500 and XIAO ESP32-C5 adapter support."),
        ("Move files with FTP", "Transfer files between your computer and console."),
        ("Wi-Fi controls in K-UI", "Scan, join, choose a band and update adapter firmware."),
    ], "network", "Network adapters use SCI with SCIF storage.")

    # A portrait visual index makes the whole bundle easy to browse before use.
    names = ["01-release-hero.png", "02-release-square.png", "03-ui-overview.png",
             "04-games.png", "05-wince-cd-music.png", "06-disc-preservation.png",
             "07-vmu-saves.png", "08-network.png"]
    sheet = Image.new("RGB", (1600, 1800), BG)
    text(sheet, (60, 38), "K-UI 2.0.0 / Press image index", 42, WHITE, "bold")
    text(sheet, (60, 104), "Ready-to-post artwork and feature cards", 25, MUTED)
    for i, name in enumerate(names):
        x = 60 + (i % 2) * 770
        y = 170 + (i // 2) * 400
        source = Image.open(OUT / name).convert("RGB")
        source.thumbnail((710, 338), Image.Resampling.LANCZOS)
        sheet.paste(source, (x + (710 - source.width) // 2, y))
        text(sheet, (x, y + 352), name, 23, MUTED, "mono")
    save(sheet, "09-press-contact-sheet.png")


if __name__ == "__main__":
    main()
