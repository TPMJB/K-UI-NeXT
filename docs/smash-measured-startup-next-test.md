# Measured Smash startup follow-up

The owner's Shining Force timing file from09a098074029 is a valid complete
hardware capture. It measured21.576 seconds from the first MDE payload callback
to the installed native worker; RTC independently advanced22 seconds.

| Measured phase | Baseline |
| --- | ---: |
| First MDE payload callback through original GIN | 6.896 s |
| Child capsule loading and validation | 2.760 s |
| Filesystem ownership | 5.667 s |
| Base relay validation | 1.125 s |
| Two journal/image-validation intervals combined | 0.427 s |
| Fresh child entry through installed worker | 11.893 s |

The first interval includes original-game transfer/copy/scanning also present
with VMU Off. These21.576 seconds must not be called22 seconds of added VMU
traffic. Initial pre-callback wait and later SGA/BUP/options work remain outside
the report; the owner separately observed approximately8 seconds Off /30 On.

The next candidate changes concrete work in those paths:

* The outer resident uses a compact aligned32-bit RAM copy with exact byte
  fallback/tails. Its46-byte leaf fits the original low guard. Existing VRAM
  halfword handling and CE helper bodies remain unchanged.
* Native/disc admission retains verified FAT windows within its existing
  temporary scratch. It retains the complete256-sector metadata cache and
  all semantic ownership walks. CID, write, sync and validation epoch changes
  invalidate reuse. CRC failures and uncertain teardown never grant a suffix.
* Compatible native image relays cache the pure whole-executable CRC. Exact
  CPU/cache/MMU/RAM bounds guard the scope; unsupported states call the original
  leaf. Every CRC byte remains checked. Only its proven private stack span is
  published before restoring the exact operand-cache-off relay profile.
* Normal native stages omit the two informational30-frame display holds.
  They are an explicit diagnostic opt-in; CE's existing policy and fatal
  diagnostics remain intact. This is a shared default, not a game-name rule.

Native worker/resident remain the measured72KiB implementation. No new worker
reservation, game patch, reduced card validation, cache crossing a fresh image
validation epoch, or Background-reader replacement is part of this test.

The manual ZIP has matching GUI-embedded and physical Smash modules, identical
GUI aliases and both checked ROOT activation records. Current Main updater
sources remain present. No KUP is required for this test.

Merge its KUI folder into the existing one. Launch ONE of Shining Force or
Phantasy Star II with VMU On, allow Start Game / Resume to appear, power off,
and return `KUI/apps/games/smash-native-phases.txt` plus the observed selection
time. No VMU-Off repeat, photograph, VC2 regression matrix or video is required.

Host parser/copy/cache/ABI checks qualify the candidate; they do not establish
console elapsed savings. Compare the new total and individual phases against
the capture above. Sega Swirl and VC2 return to the collection menu, other
Genesis persistent high scores and the complete22-second gap remain unresolved.
