# K-UI shell font sources

These are unmodified DejaVu Sans and DejaVu Sans Bold font files supplied by the
local Debian `fonts-dejavu-core` package. They are independent of DreamShell.
The font license is in `LICENSE.txt` and `../../LICENSES/DejaVu-fonts.txt`; the
latter is included automatically in both downloadable build packages.

The shell uses original rasterizations of ASCII 32–126 at 14 pixels regular
and 20 pixels bold. Four-bit coverage preserves antialiasing with a small static
atlas. Glyphs fit within individual advance cells; the renderer adds one pixel
between characters. The line boxes are 18 and 24 pixels high.

Normal builds compile the committed `src/dreamcast/shell_font_data.inc`; font
generation is not a build step. The font renderer requires no Pillow, FreeType
or filesystem access at runtime. To regenerate byte-identically, use Pillow
**12.3.0** with FreeType **2.14.3**:

```sh
python3 tools/generate_shell_font.py
python3 tools/generate_shell_font.py --check
```

The generator checks these source SHA-256 values and the rasterizer versions:

| Font | SHA-256 |
|---|---|
| `DejaVuSans.ttf` | `ae7b7855e115a5966d8b1b3f80f254ccc117ec86f9965e202ee2940453837280` |
| `DejaVuSans-Bold.ttf` | `5c1247acef7f2b8522a31742c76d6adcb5569bacc0be7ceaa4dc39dd252ce895` |

Upstream project: <https://dejavu-fonts.github.io/>.
