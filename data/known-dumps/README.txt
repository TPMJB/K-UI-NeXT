Bundled known-dump database
===========================

redump.db contains game names, track numbers, sizes, and CRC32 values derived
from the Libretro Database file:

  metadat/redump/Sega - Dreamcast.dat, version 2026.08.01
  https://github.com/libretro/libretro-database/blob/master/metadat/redump/Sega%20-%20Dreamcast.dat

The source database is licensed under Creative Commons
Attribution-ShareAlike 4.0 International (CC BY-SA 4.0):

  https://creativecommons.org/licenses/by-sa/4.0/

This compact adaptation is distributed under the same license. It was produced
with host-tools/make_gd_redump_db.py and contains no game or disc payload data.

The bundled Libretro catalog normally lists one identifying data track per
game. Therefore GD Ripper reports IDENTIFIED BY DATA TRACK for a match. To use
a full Redump DAT with all tracks, run the included converter and replace this
redump.db file.

TOSEC GDI catalog
=================

tosec.db contains a compact factual index of retail GDI game names, track
numbers, lengths and CRC32 values from TOSEC's official 2025-03-13 DAT pack:
https://www.tosecdev.org/downloads/category/59-2025-03-13

Source DAT author: Maddog / TOSEC-ISO contributors. Regional source DATs:

TOSEC-ISO/Sega Dreamcast - Games - JP (TOSEC-v2025-02-16_CM).dat
SHA256 b3937cf6ef0fd13d97ef701202b8d06631c15f0de162a7364442e6f567157d88

TOSEC-ISO/Sega Dreamcast - Games - PAL (TOSEC-v2023-04-02_CM).dat
SHA256 4fb1cee56dfe84ae12003fd081a1b2758254ba16ce219bb0ccbfcdcd85323e9d

TOSEC-ISO/Sega Dreamcast - Games - US (TOSEC-v2025-02-09_CM).dat
SHA256 fdf463cee179196d1b089ee16bf8785511f0189f85e75e7d10c068860245f019

Generated using make_gd_redump_db.py: 1,245 game entries / 9,545 track entries.
Only names and checksum metadata are included; no games or disc payloads.
The Libretro CC BY-SA notice above applies to redump.db, not to TOSEC's work.
For updated metadata and corrections, visit https://www.tosecdev.org/.
