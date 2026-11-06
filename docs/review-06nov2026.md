# Delivery and review checkpoint — 6 November 2026

The `f8807cb16561` ZIP remains unchanged, SHA256
`dc607c3e89ab6bac5c718b738bca9990ffd85ec39abb308140de280c4a7503e9`.
This review changes standing instructions and records read-only audits. It
adds no runtime change, replacement build or Dreamcast timing result.

## CE delivery

The statement that both CE changes exist only in source is too broad. The
four delivered `ce-cdda-{armada,bam4,worms,wwp}.kui` files contain both the
VRAM-safe halfword payload sink and the folded 128-entry CRC16 table.
The audit binds exact ZIP bytes through entry, stage and both SCI residents,
then executes the actual compiled caller/mapper/width checks. It includes an
unsafe-copy mutation control; helper presence alone was not the criterion.

The generic `ce-probe.kui` replacement was compiled and qualified but omitted
to preserve the existing personalized writable-VMU adapter. Its current
installed ROOT bytes are unavailable for inspection. Do not describe that
generic path as updated by this ZIP.

The smaller table is a space tradeoff, not a speed improvement. It saves
256 bytes and adds a CRC-dependent branch per byte; the emitted music loops
execute 19 or 22 instructions per byte. These are instruction counts, not
console cycles or movie timings. Full-table candidates still violate the
existing CDDA memory boundary; the last narrow size trial remained 64 bytes
over. No such candidate was shipped. Keep the native timing candidate fixed;
qualify a separate CE comparison for correctness/space before measuring both
on hardware or drawing an Armada FMV conclusion.

## Cold boot and Restart are different paths

Cold CD/root entry uses the copied GUI and ROOT resolver. Restart K-UI reads
`KUI/system/active0.bin` and `active1.bin` and prefers the highest valid
activation sequence. A valid compatible older bank can therefore win over a
newer physical ZIP. Neither GUI build strings nor filenames order the slots.
The activation's `previous` field is restore metadata, not a direct boot
fallback pointer. Bank identity and the GUI build may differ.

The physical SD card is not connected here; this audit cannot establish
whether that card currently has any selected bank. It changes no selectors,
banks or user data. For the current comparison, cold boot after copying the
ZIP and confirm GUI `f8807cb16561`. Keep the newest valid alternating launch
receipt and native `KUI/apps/games/launch-timing.txt`; their final source-module
path/build/CRC identifies the selected reader. Accepted Smash components keep
their prior identities and may come from the exact embedded bundle. A GUI
label alone cannot establish the complete component set or live SCI activity.
`startup.txt` and `updater-startup.txt` may be old, since cold ROOT writes neither.

Restart selection is source-traced and supported by existing host fixture
assertions. It is not presented as a new current-card or Dreamcast reproduction.

## Compatibility, measurement and originals

Most `f8807cb` changes reduce launch/preparation work. They do not establish
support for previously unsupported SDK, VMU, memory or CDDA/WinCE combinations.
The early SDK nesting-handler correction is a distinct compatibility repair.
No new console speed, gameplay or saving acceptance follows from host checks.

The owner supplied Claude's report that the original Sonic Adventure executable
passes the old/new linked test: the old reader refuses nesting mode, the new
reader binds both modes and all six refusal cases remain. Record that as
externally reported original-backed validation of `0d1691d2`, not a local SA1
run or console result. No input hash accompanied that report. Local original-
backed EGG/HOTD2/Time Stalkers and Evolution 2 tests remain separately recorded.

Manual-copy ZIP is now the standing rule in `AGENTS.md` and accepted progress,
including future sessions. Keep this candidate while comparing two or three
games' first/repeat launch phases with matching options and source identities.
Future runtime changes are isolated by commit and console checkpoint. Available
original executables are required for relevant family tests; hashes/fixtures
alone do not replace them. Private game bytes remain excluded from deliveries.

The source- and artifact-bound audits are in
[`evidence/review-06nov2026/review.json`](evidence/review-06nov2026/review.json).
