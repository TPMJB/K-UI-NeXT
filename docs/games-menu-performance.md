<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Games menu speed: where the time goes

The owner reports a slow Games menu at startup and a jarring transition on
every page turn. This is a reading of the current code, not a console
measurement.

## What happens today

- **Every storage job starts the card from cold.** The storage worker calls
  `kui_sd_connect()` and `kui_mount()` before each job and
  `f_mount(NULL)`/`kui_sd_disconnect()` after it. Connect re-initialises the
  card (on SCI: port acquire, then the SD start-up command sequence) and, in
  Auto mode, `open_volume()` mounts and unmounts once to validate it before
  the caller mounts again. So each job pays a card start-up and two mounts.
  The first-page warm-up, each first visit to a page, each selected game's
  cover and title, and the saved selection are separate jobs.
- **A page turn blanks the list.** `turn_games_page()`/`list_games()` set
  `games_loading` and the message "Reading SD directory..." and wait for the
  worker, even when the catalogue already holds the page.
- **A page's first visit opens up to eight folders.** `entry_page()` resolves
  each unresolved folder row with `single_image()`, which opens that folder
  to find its one GDI. Only the first page is warmed in the background.
- **The first listing reads `/Games` three times**: `index_selectors()`,
  `index_variants()` and the sorting pass in `list_with()`.
- **Covers and titles load one at a time**, only for the selected game. A
  row shows its folder name until its cover job has run once.

## Plan

### Stage 1: same look, no waiting

1. Keep one storage session open while the owner browses Games: connect and
   mount once, release after a few idle seconds, on leaving Games, and
   before any job that needs the bus or the drive (launch, CD audio, VMU,
   network, writes). The worker already owns every storage job, so the
   session lives on that one thread.
2. Never blank a page: serve cached pages directly and keep the old page on
   screen until the next one is ready, with a small busy marker instead of
   the message.
3. After the first page, resolve the remaining folders and load titles and
   covers in idle time, nearest pages first, so later page turns need no I/O.
4. Read `/Games` once on the first listing instead of three times.

### Stage 2: a scrolling browser

- One continuous list over the in-RAM catalogue (up to 1024 sorted entries
  today) instead of fixed pages, with view modes: list only, list with the
  selected game's cover, and tiles.
- Sort by folder name, game title, region or recently played; jump by letter.
- A library index file on the card (title, region, GDI name, cover present)
  built in the background and refreshed when `/Games` changes, so startup
  shows the whole library at once.

DreamShell's browser is a reference for the experience only. Its code is
PolyForm Noncommercial and is not copied.
