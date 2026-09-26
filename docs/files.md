# File Manager

The File Manager is on Home, below Games, Disc Ripper and VMU Manager. It
browses every folder and file on the SD card, opens games, music and
pictures, and copies, moves, renames and deletes with a check and a
confirmation first. It is new since K-UI 1.5.1 and has not yet been tried on
a console.

It is independent K-UI code, written for this project. The earlier K-UI_DS
File Manager is DreamShell's own File Manager with a K-UI layout on top: its
file list widget and its copy, move and delete code are DreamShell code, so
none of it is used here (see [prior work](prior-work-reuse.md)).

## Using it

Open **File Manager** from Home. It starts at the card's root.

- **D-pad** up and down selects; left and right turn the page. Eight items
  are shown per page, folders first, then files, each in name order without
  regard to capitals. The line under the list shows which items are on the
  page, and the selected item's date and read-only or hidden marks.
- **A** opens the selected item:
  - a folder opens;
  - a `.gdi` opens its Games image details, where A launches it as usual;
    **B** comes back to the File Manager;
  - a `.wav` or `.ogg` file plays through the Music Player in the background;
  - a `.png`, `.jpg`, `.jpeg` or `.pvr` picture opens in a picture view;
  - anything else shows its details.
- **B** goes up one folder, with the folder you left at the top of the page.
  At the root it returns Home. **START** returns Home from anywhere.
- **X** opens the actions for the selected item: **Open**, **Copy to another
  folder**, **Move to another folder**, **Rename**, **Delete**, **Details**
  and **New folder here**.
- **Y** creates a new folder here. **R** lists the folder again.

### Copy and move

Choose **Copy** or **Move**, then choose the destination folder: **A** opens a
folder, **B** goes up, and **Y** chooses the folder shown (**START** cancels).
The File Manager then checks the item, the destination and the free space, and
shows what it will do before anything is written. **A** confirms.

- Nothing is ever replaced. If the name is taken in the destination, the
  confirmation says so and the copy or move takes the next free numbered
  name, such as `track03 (2).bin` or `Crazy Taxi (2)`.
- A copy is first written as `KUI-copy-<n>.kui-part` in the destination.
  Each file is then read back and compared with what was read from the
  original (size and CRC32). Only then does the copy take its real name. Files
  and folders keep their dates.
- **B** stops a copy safely: the partial copy is removed. If the card fills
  up or a read-back does not match, the partial copy is removed as well and
  the reason is shown.
- A move on the same card only renames, so it is instant, whatever its size.
- A folder cannot be copied or moved into itself.

Copying large game files is limited by the serial SD interface and takes
time; a computer is quicker for big copies.

### Delete

**Delete** first counts everything the item holds and shows it, for example
"Holds 7 files and 1 folder, 1.1 GB", with a reminder that it cannot be
undone. **A** confirms. Read-only files and folders are cleared and deleted.
**B** stops part way: whatever is already deleted stays deleted, the rest
remains, and the result says how many were deleted.

### Rename and new folder

Both use the on-screen keyboard. **START** or **DONE** applies the name; **B**
cancels. A name cannot contain `/` or end with a space or a dot. The renamed
or new item is selected at the top of the page afterwards.

### What is protected

K-UI needs two files to start: `KUI/runtime.kui` and
`KUI/apps/games/retail-boot.kui`. These files, and the `KUI`, `KUI/apps` and
`KUI/apps/games` folders that hold them, cannot be moved, renamed or deleted
from the File Manager. Copying them is allowed. Everything else can be
changed, including box art (`KUI/covers`) and saved preferences.

### Pictures

The picture view shows PNG and JPEG pictures up to 3 MB and 1.2 megapixels,
and 16-bit `.PVR` textures such as a disc's `0GDTEX.PVR`, fitted into a
square with their format, size and folder. It uses the same decoders as Games
box art.

## Limits

- Names longer than 127 bytes are listed as `[Name too long]` and cannot be
  opened or acted on individually; a folder holding one can still be copied or
  deleted as a whole.
- Folders with more than 32,768 entries cannot be listed. Copy, delete and
  details descend at most 16 folder levels.
- Music paths must be shorter than 128 bytes.
- Everything runs on the storage worker while the screen shows progress; the
  menu does not accept other commands meanwhile.

## How it is built

- `src/core/files_path.c`: path rules (the same safe names the capture
  destination uses), the protected files, numbered names, sort order, and
  size and date text.
- `src/apps/files.c`: listing, checks and operations on the card through
  FatFs. A page is found in one pass over the folder, keeping the eight
  smallest names after the neighbouring row, so paging stays quick in large
  folders. Copies, deletes and counts walk the tree with one open folder per
  level.
- `src/apps/files_picture.c`: the picture view, using the pinned stb_image
  (PNG, JPEG) and K-UI's own `.PVR` decoder and scaler from box art.
- `src/core/shell.c` and `src/dreamcast/shell_draw.c`: the screens.
- FatFs's `f_chmod`/`f_utime` are now enabled (`config/ffconf.h`) to clear
  read-only marks before deleting and to keep dates on copies.

## Validation

- `test-files`: path rules, protection, numbered names, sort order, size and
  date text.
- `test-shell`: navigation, paging anchors, open-with, the actions menu,
  protected items, the copy, move, delete, rename and new-folder flows, stale
  results, and every screen's text.
- `test_files_images.py` on real FAT32 and exFAT images, with `fsck`
  afterwards: sorted paging forwards, backwards and from a missing anchor;
  file and folder copies read back and compared, dates kept, numbered names;
  Stop during a copy, a delete and a count; a failed write; a read-back that
  does not match; a full card; moves, including into itself and onto a taken
  name; deletes with read-only items; every protected path; rename (including
  a change of capitals); new folders; details; PNG, JPEG and PVR pictures and
  unusable files. Every operation must leave no file or folder open.

## Console test

Merge this build's `sd-update` `KUI` folder onto the card and keep the boot
CD. Use a card whose contents are backed up.

1. Open File Manager, browse into a game folder and back out, and turn a page
   in a large folder.
2. Open a `.gdi` (it should show Games image details), a song and a picture.
3. Make a new folder, copy a small file into it, and check the copy on a PC.
4. Rename the copy, move it back, then delete the folder.
5. Try to delete `KUI/runtime.kui`: it should refuse.
6. Optional: copy a whole game folder, note how long it takes, and launch the
   copy.

Photograph anything that looks wrong.
