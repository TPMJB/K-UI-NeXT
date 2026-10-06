# Games selector hardware feedback and correction

The user tested GitHub build `ce7a24a6ed6f` on 2026-10-06 over SCI:

- Games navigation felt much slower.
- Armada and other Windows CE games offered Original/2048-byte copy. Native
  games did not, despite batch conversion of the collection.
- Armada felt slightly better with the converted image; only the background
  reader launched. The standard reader produced a black screen.

The photographed GAME MENU RETURN report is from the background CE reader:
guard=0, sectors=0x5928, IRQ blocks=0x119d8, call blocks=0x4ad4,
DMA blocks=0x11a03, polled/overruns=0x4aae, repaired=0x86, CRC retries=5,
token/foreign/ahead=0. This records reception/recovery counters, not elapsed
time or five confirmed bad game sectors. It does not locate the standard
reader's failure. Historical standard Armada playback exists, so the new
failure must not be described as expected or already diagnosed.

Code inspection found discovery/validation repeated for the entire converted
collection on every page/view request. Pairing also replaced the original
folder name with `f_stat().fname` and then matched directory rows exactly;
case/short-name differences could omit the Original's version association.
These are concrete defects corrected in this follow-up. Their connection to
every missing native pair on the user's card still requires console evidence.

The correction caches bounded logical rows, resolves ordinary folders lazily,
preserves enumerated names, and polls idle Games input independently of redraws.
Fresh listings log bounded candidate/pair counts and failure reasons to help
identify any remaining folder-specific issue. Reader binaries and transport
algorithms are unchanged. Host validation results are recorded after checks;
this is not a claim that the follow-up has already passed on console.

## Correction validation

- 76 scenarios on FAT32 and exFAT: 152 real image checks passed under ASan/UBSan,
  with unchanged whole-card SHA-256, clean fsck, no writes and closed handles.
  Covers original/cooked order, nested and multi-track layouts, exact Unicode
  names, ASCII case, stat-name aliases, unmatched aliases, deterministic layout
  mismatches, transient pairing/discovery failures, cancellation and both index
  limits. Unsupported Unicode-case/SFN-base aliases remain separate entries.
- Twelve repeated cached page requests add no connect, media, directory, stat
  or descriptor work. Mounted artwork callbacks still receive a mounted card;
  this does not claim that cover pixels need no reads.
- Full shell controls/rendering and the Games input/redraw cadence checks
  passed. Explicit refresh/first entry requests fresh discovery; page/view
  changes request reuse. Busy redraw limits remain covered by the original
  finite-rate and zero-rate tests.
- Independent reviews of runtime integration and final backend found no
  remaining blocker. Native runtime compilation and packaging are performed
  by GitHub for the exact published source tree.
- Eight cover integration scenarios, 24 retail packaging checks and nine
  workflow hygiene/scope checks passed. Reader changes remain outside the
  focused Games validation scope.

LeakSanitizer is disabled in local runs because this environment prevents its
process inspection; AddressSanitizer/UndefinedBehaviorSanitizer stay enabled.
File/directory handles are explicitly asserted by the filesystem fixtures.
