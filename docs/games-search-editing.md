<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Finding games and editing the library

The library worker accepts a bounded query containing a title/name search
(up to 64 bytes), All/Favorites/Recent selection, region and disc number.
Search uses the displayed title or custom alias and the original row name;
ASCII letters are case insensitive. Region is Any, Japan, USA, Europe or
Unknown. Multi-region IP flags match each represented region. Missing region
metadata stays Unknown; folder names and title text never establish a region.
Disc is Any or 1–99, using a verified IP disc field when available and the
existing disc-group member number otherwise. A disc set remains one row when
any member matches. Its disc chooser applies the same query to the members.

Letter jumping selects the first matching displayed-title bucket: A–Z or #
for numbers, punctuation and non-ASCII initials. A missing bucket leaves the
requested offset unchanged. Filtering and jumping operate on the admitted RAM
catalogue, with bounded cached row indexes; they do not rescan game directories
or reread game images. View changes retain the existing view-save behavior.

Display-name aliases and explicit PNG/JPEG artwork assignments are separate
metadata. They change presentation, sorting and search without renaming game
folders, descriptors, tracks or launch paths. Empty alias restores the normal
title; empty artwork path restores automatic artwork. Aliases are bounded
printable ASCII supported by the menu font. Assigned images must fit the
existing image decoder limits and decode successfully before saving. A disc
set can have its own alias and artwork while retaining its real disc chooser.
The metadata writer alternates verified generations under
`KUI/games-library/metadata-a.bin` and `metadata-b.bin`; failed publication keeps
the previous metadata in memory and the prior valid disk slot.

New catalogue scans write version 4, retaining cached IP region fields from
validated exact-path artwork metadata and bounded disc-set evidence for
[collision-named captures](games-capture-disc-grouping.md). Version 2 and 3
catalogues remain readable; older missing region/evidence fields stay unknown
until an explicit scan. Scans still build and verify a replacement generation
before replacing the admitted catalogue.

## Explicit identity verification

Identity verification is an optional selected-game operation. It can take a
long time on serial storage: it reads every represented data and audio sector.
Normal catalogue scans, browsing, aliases and cover assignment never invoke it.
Cancellation, I/O or close failures leave the prior identity unchanged.

The SHA-256 identity includes track geometry and all decoded 2048-byte data
payloads plus 2352-byte audio main-channel payloads. Physical paths, filenames,
raw/cooked data stride, EDC/ECC and subchannels are excluded. Exact equivalent
Original/2048 copies therefore agree. Changed executable/content/audio, disc
number or revision bytes differ. This is represented-image identity, not
Redump verification or proof that an image contains every original track.
Compressed images are outside this first identity-verification slice.

A new path follows a prior identity only when exactly one prior verified
record matches, its old path is absent, and the destination has no independent
metadata record. The data representation must also match (raw, cooked2048 or
mixed), keeping Original/2048 launch settings independent even when their
canonical content hashes agree. The old activity/settings anchor is retained, preserving
favorites, recent position and settings through the resolver. Multiple copies,
ambiguous matches, existing destinations and changed verified content are not
silently combined. Titles and product names alone never establish identity.
If the new path already has its own favorite/recent record, rebinding is
refused instead of hiding that independently created activity.
Reusing a retained old anchor path for another image makes settings resolution
refuse that path until the ambiguity is explicitly resolved.

Verify important images before moving them. After a move, rescan the catalogue
and explicitly verify the new path. This first slice does not automatically
hash a whole library or infer moves from directory timestamps. Display edits
and ordinary path-based preferences remain usable without identity hashing.
