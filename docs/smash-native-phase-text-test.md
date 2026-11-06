# Smash native phase TXT diagnostic

This test investigates the owner's measured 8-second VMU-Off / 30-second VMU-On
selection delay. It is not a speed fix and does not claim that lower command
counts shorten console loading.

Copy the manual ZIP's KUI folder over the existing KUI folder. The archive carries
both GUI aliases, both ROOT activation records and matching embedded/physical
Smash components. Leave banks and user data in place.

Launch Sega Smash Pack with VMU On and select **one** of Shining Force or
Phantasy Star II. Let it reach Start Game / Resume. Then power off normally and
return `KUI/apps/games/smash-native-phases.txt`. No VMU-Off repeat or photograph
is required. The existing `smash-launch-timing.txt` can also identify which
components actually ran if the phase report remains Pending or is missing.

The report starts at the first arrived MDE payload callback, excluding any initial
wait before that callback. It measures subsequent GIN work, capsule validation/load,
fresh child entry, filesystem ownership, journal recovery, two image-validation epochs,
BOOT2, SDK/allocator proof, original MDE CRT/allocator initialization, worker
relocation and hook publication. It ends at installed-worker completion, before
subsequent SGA/BUP/options work. A short total leaves initial pre-callback wait
and subsequent game work unresolved; it does not establish a whole-selection speedup.

The inherited performance counter gives nominal milliseconds only when modes
and deltas are usable. RTC is a separate coarse observation. Invalid, unavailable
or disagreeing clocks produce NA. An intervening reset cannot be excluded solely
from matching endpoint modes. Raw observations are retained for review.

The GUI preallocates a 4096-byte report. The detached stage maps that existing file
read-only with bounded filesystem traversal, validates allocation disjointness
and card identity, then overwrites only its eight data sectors. Every sector has
SD CRC validation and readback; no filesystem metadata is written by the stage.
Report I/O occurs after all measured intervals. The game continues after a
successful report; missing/refused report is best effort, while an uncertain
card I/O failure retains the existing fail-closed card contract.

Preservation: 72KiB native worker; unchanged accepted CE helper bodies; current
Main engine 3 updater; actual matching module bundle and physical modules. This
build does not implement Sega Swirl or Virtua Cop 2 return to the collection menu.
