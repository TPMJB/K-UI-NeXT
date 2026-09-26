# Games: box art, titles and views

The Games app can now show box art, the real title of each game, and three
ways of browsing. Everything here is menu code. The game launcher, the
in-game reader and the pinned DOA2 behaviour are unchanged. It ships in
K-UI 1.5.1; on release candidate `cb508b2a4fe4` the owner reports "Box art
works". Scan time per game has not been measured.

## Using it

1. **Scan once.** In Games, press **START**, then choose **Scan box art**. The
   scan visits every game in `/Games`, including games inside category
   folders up to two levels deep, and shows its progress. **B** stops it
   safely; covers finished so far are kept. When it ends, the library opens
   with a summary line such as "Box art ready: 34 new, 0 unchanged, 5 without
   art".
2. **Choose a view.** **Y** switches between:
   - **List**: names, with the highlighted game's large cover on the right.
   - **Compact**: two columns of four, each with a small cover.
   - **Gallery**: four covers across, two rows.
   The choice is saved on the card and used next time. All three views show
   the same eight entries per page. In Compact and Gallery, the D-pad moves
   between covers and turns the page at the left and right edges.
3. **Image details** show the large cover beside the game's information.

Scan again after adding games or changing art. Only new or changed games are
read again; a scan of an unchanged library reads the folders and records and
writes nothing.

## Where the art comes from

Many retail discs carry their own cover picture, `0GDTEX.PVR`, in the root
of the high-density data track. The scan reads that file from the GDI and
decodes it. Games whose disc has no such file, or uses a texture format K-UI
does not decode (palettized, YUV or bump-map textures), get their title and a
disc placeholder.

**Your own images** fill those gaps or replace disc art. Save a PNG or JPEG
as `KUI/covers/<name>.png` (or `.jpg`, `.jpeg`) and scan again. `<name>` is
the game's folder name; a GDI that sits directly in `/Games`, or shares a
folder with other GDIs, uses its file name without `.gdi`. For example,
`/Games/Crazy Taxi/disc.gdi` uses `KUI/covers/Crazy Taxi.png`. Your image
always wins over the disc's art. Images must be at most 3 MB and 1.2
megapixels (for example 1000x1200); resize larger scans on a PC.

Two games with the same name (for example in two category folders) share
one cover name. The scan keeps the first, skips the other and says so; rename
one folder to give both covers.

## What is stored on the card

- `KUI/covers/<name>.kcv`: one record per game. It holds the disc title,
  product number, version and region, the GDI path and its size and date
  (and those of your image, if any), then the art in three prepared sizes
  (160, 104 and 56 pixels square). About 80 KB with art, 512 bytes without.
  A record is used only for the exact GDI path it names, and a new record
  replaces the old one only after it has been completely written.
- `KUI/apps/games/view.txt`: the chosen view.

Game folders and their files are only ever read. Deleting `KUI/covers`
removes all box art; the next scan rebuilds it.

Titles come from the disc header (`DEAD OR ALIVE 2`). A title the menu font
cannot show, such as a Japanese one, falls back to the folder name.

## Limits

- Up to 2000 games per scan; game folders at most two category levels below
  `/Games`.
- A page of box art reads eight cover records: about 400 KB in List view and
  less in the others. This is an estimate, not a console measurement.
- The first scan reads about 0.1–0.4 MB of each disc (header, directory and
  artwork file). Time per game on the serial SD interface has not been
  measured yet.

## How it is built

Independent K-UI code, written for this project:

- `src/core/pvr_texture.c` decodes the largest level of 16-bit `.PVR`
  textures: square and rectangular twiddled, stride and VQ layouts, with or
  without mipmaps and an optional GBIX header. The formats follow the
  KallistiOS texture tool at the pinned commit (`utils/pvrtex/file_pvr.c`,
  `utils/pvrtex/pvr_texture.c`); no code is copied.
- `src/core/game_metadata.c` finds `0GDTEX.PVR` with the same root-directory
  rules it already applies to the boot file. Each artwork sector's sync,
  mode and address must match its position before its data is used.
- `src/core/game_cover.c` scales art into the three sizes (area averaging
  when shrinking, bilinear when enlarging, transparency blended onto the
  menu's navy) and reads and writes the record header with a CRC32.
- `src/apps/games_covers.c` runs the scan, loads each page's covers and
  saves the view. `src/apps/cover_image.c` decodes your PNG and JPEG images
  with the pinned stb_image ([source record](../third_party/stb/README.md)).

No DreamShell code, cover scanner or decoder is used.

## Validation

- `test-pvr-texture`: every supported layout against a separate reference
  encoder that uses KallistiOS's incremental Morton method, all truncations
  of each file, and every rejected format.
- `test-game-cover`: header round trips, every single-byte corruption,
  structural rules, names and titles, exact scaling cases and in-place
  reduction.
- `test-cover-image`: PNG (RGB and RGBA) and JPEG fixtures, truncations,
  size limits checked before decoding.
- `test-game-metadata`: the artwork lookup beside the existing boot-file
  cases.
- `test-shell`: views, grid movement and page turns, the scan action and
  drawing.
- `test_games_covers_images.py` on real FAT32 and exFAT images: a scan of
  eight synthetic games (twiddled, VQ, mipmapped with GBIX, stride, none,
  your PNG, a category folder, two GDIs in one folder, one duplicate name);
  covers and titles on the page in all three views; image details; a
  second scan that writes nothing; a replaced and a removed image; Stop
  part way; write failures leaving no partial record; a missing `/Games`.
  `fsck` checks every image afterwards.

## Console test

Merge this build's `sd-update` `KUI` folder onto the card and keep the boot
CD.

1. Open Games, press **START**, choose **Scan box art**. Note roughly how
   long it takes and the summary line.
2. Press **Y** to see Compact and Gallery, move around, turn a page, and
   open one game's image details.
3. Leave Games and come back: the last view should return.
4. Optional: add `KUI/covers/<a game without art>.png` from a PC and scan
   again.

Photograph anything that looks wrong: missing, stretched or garbled art.
