<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Exact game setup recommendations

Game inspection can identify a historical tested setup and recommend its reader,
CD music and virtual-card slot. A setup recommendation never bypasses executable,
map, package, sound, VMU, memory, storage or runtime checks. It does not enable a
global Virtual VMU preference, create a card, install a missing module, or certify
the currently installed build.

## Labels and saved choices

- **Tested setup** means the exact release/executable and suggested choices have
  the scoped historical console evidence shown in the profile. The note keeps
  limitations and the need to test a rebuilt package visible.
- **Experimental** means there is no matching historical tested recommendation,
  or the recommendation came from a local profile pack. A compatible SDK family
  or a successful executable inspection does not upgrade this label.
- **Custom** means an explicit saved setup differs from the matching recommendation,
  or there is no matching recommendation. Matching an existing recommendation
  leaves the explicit saved entry in charge; it is never rewritten implicitly.

With no saved setup, an exact built-in recommendation may supply the in-memory
defaults. Existing saved **Standard**, **CD music Off**, **A1/A2**, encoding,
codec and RAM-patch choices are preserved. An explicit **Apply recommendation**
copies the recommended setup through the existing verified launch-settings save.
This disables prior manual RAM patches because none of the current recommendations
contains a patch. Imported Experimental setups are never applied automatically.

Normal folder listing does not read executable contents or profile packs. An
individual inspection hashes a complete executable only after bounded metadata
matches a candidate. Existing CD music candidates reuse their existing full-IP
CRC and executable SHA-256 pass. Unknown ordinary titles keep metadata-only
inspection. A local profile pack is at most 4,132 bytes and 16 records.

## First built-in catalogue

| Setup | Exact identity and evidence scope |
| --- | --- |
| Code Veronica / SCI New reader | US `T1204N`, `V1.000`, region `U`, native GDI, 3,915,142-byte boot SHA-256 `199b72af9bcd7fb80d401d4645c731e595970a0dcbcfd98e1bec187f4382fbbb`. [Owner evidence](evidence/native-code-veronica-console-2026-10-28.json) for build `2322fda6f92e`: play, save, full power cycle, reload and disc 2 passed. The collector observed the same executable for discs 1 and 2. No full-playthrough or in-session swapping claim. |
| Armada / SCI CD music | Existing exact-disc geometry candidate, 1,253,376-byte kernel SHA-256 and full-IP CRC `684f8040`. [Historical CD music evidence](experimental-cdda.md). |
| Bust-A-Move 4 / SCI CD music | Existing exact-disc geometry candidate, 1,830,912-byte kernel SHA-256 and full-IP CRC `8fb049c5`. [Historical CD music evidence](experimental-cdda.md). |
| Worms Armageddon / SCI CD music | Existing exact-disc geometry candidate, 1,110,016-byte kernel SHA-256 and full-IP CRC `86bbb334`. The existing candidate admits the recorded raw and consistently converted 2048-byte DATA layouts. [Historical CD music evidence](experimental-cdda.md). |
| Toy Commander / SCI CD music | Existing exact raw-disc geometry candidate, 748,444-byte executable SHA-256 and full-IP CRC `38ba2868`. Working music was reported; **FMV stalls remain**. Cooked Toy variants do not inherit this tested label. [Historical evidence and limitations](experimental-cdda.md). |

All built-ins require SCI storage and GDI. CD music recommendations do not
certify a virtual-VMU combination. Their historical evidence does not establish
the exact binary identity of a rebuilt module. MDK2, World Party, unknown
revisions and untested layouts are not added as Tested rows merely because an
adapter exists. Native Code Veronica is identified by its complete executable
SHA plus product/version/region and native boot kind; the audio rows additionally
require the existing full-IP CRC and exact-disc candidate.

## Local Experimental profile pack

An optional `KUI/profiles/game-profiles.bin` supplies up to 16 exact SCI/GDI
recommendations. Missing packs leave built-ins available. An invalid, oversized,
unreadable or corrupt pack is ignored with a log warning; it cannot replace a
built-in Tested row or turn a local assertion into Tested evidence.

The little-endian version-1 format has a 32-byte header, zero to sixteen 256-byte
rows, then a four-byte IEEE CRC32 of all preceding bytes. CRC detects corruption;
it does not authenticate the author or establish console compatibility.

| Header byte | Value |
| --- | --- |
| 0 | Eight ASCII bytes `KUIGPF01` |
| 8 | u32 version, exactly 1 |
| 12 | u32 row count, 0–16 |
| 16 | u32 row size, exactly 256 |
| 20 | u32 complete file size, including CRC footer |
| 24 | Eight reserved zero bytes |

| Row byte | Value |
| --- | --- |
| 0 | Product: 17-byte zero-padded ASCII field, nonempty and NUL-terminated |
| 17 | Version: 7-byte field with the same string rules |
| 24 | Name: 48-byte field with the same string rules |
| 72 | Optional note: 96-byte zero-padded ASCII field |
| 168 | u32 exact executable byte count, 1–12 MiB |
| 172 | u32 full 32 KiB IP CRC32, nonzero |
| 176 | u32 transport, exactly `KUI_STORAGE_SCI` |
| 180 | u32 format, exactly `KUI_GAME_IMAGE_GDI` |
| 184 | 32-byte complete unmodified executable SHA-256 |
| 216 | u8 reader: 0 Standard, 1 New, 2 Legacy; CE must use 1, native CD music must use 0 |
| 217 | u8 CD music, 0 or 1 |
| 218 | u8 boot encoding, exactly 0 Automatic for GDI |
| 219 | u8 codec, exactly 0; GDI has no compressed codec selection |
| 220 | u8 virtual slot: 0 A2, 1 A1 |
| 221 | u8 Windows CE boot kind, 0 or 1 |
| 222 | Region: 9-byte zero-padded ASCII field, nonempty and NUL-terminated |
| 231 | Twenty-five reserved zero bytes |

Duplicate complete identity keys, malformed/noncanonical strings, unknown
fields and unsafe or unsupported values are refused. Packs contain no RAM
addresses, executable content, patches, Tested flags or automatic-apply flags.
Reader and encoding combinations must already agree with the existing launcher:
native music-Off permits all three readers; native CD music requires Standard;
Windows CE requires Background; GDI uses Automatic encoding. Applying a pack
does not rely on silently normalizing its choices into a different setup.
The portable encode/decode API in `include/kui/game_profile.h` defines the format;
the mounted file adapter opens it read-only and always closes a successful open.

## Validation

The focused host test checks exact executable SHA, size, revision, region,
native/CE boot kind, format and transport; existing audio geometry/full-IP
requirements; saved Standard/Off/A1/manual-patch preservation; explicit Apply;
automatic built-ins versus opt-in imports; pack round trips, every truncation
and bit-corruption position, resealed malformed fields, duplicate keys and
capacity; missing/read/short-read/close/oversized/corrupt pack handling. Host
results do not establish a new console acceptance result.
