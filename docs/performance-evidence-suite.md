# Console performance evidence suite

The target is a global demand reader and emulated VMU that preserve normal
gameplay, movies, sound and saves, with performance close to the same game on
disc. These tests separate reader work, reserved memory, hook overhead and VMU
traffic. They are console experiments; host checks do not establish a speedup.

Start with Toy Commander and Armada movies. Run later stages when the earlier
results give a reason to continue. A phone recording and the displayed report
are sufficient; the optional computer analyzer needs no third-party packages.

## Copy and identify each run

With the console off, keep a copy of the current `KUI` folder, then copy the
selected test folder's `KUI` onto the card. Install one profile at a time. Use
the package's copy instructions and existing prepared VMU pair. No installer,
image conversion, VMU recreation or new card preparation is part of this suite.

1. Merge `00-control/KUI` into the existing card installation once, replacing
   matching files. This supplies the shared baseline modules and test menu.
2. For each candidate, merge only that numbered folder's `KUI` before a cold
   boot. Every folder includes its matching menu; `06-native-standalone` has
   the explicit worker-free comparison route.
3. Copy `00-control/KUI` again when returning to a control or changing test
   families, then apply the next selected overlay. Keep your backed-up folder
   for returning to your normal installation after testing.

Start with `00-control`. To change a test, copy that profile's `KUI` overlay
while powered off. Restore `00-control` before switching game families, then
apply the next overlay. Each folder includes its intended GUI routing; the
standalone control's GUI deliberately routes only New plus VMU Off to N0.

| Folder | Selected profile |
| --- | --- |
| `00-control` | T1, C1, N3 and ordinary Physical Standard baseline |
| `01-toy-ready-yield` | T2 |
| `02-toy-no-fingerprint` | T3 |
| `03-armada-measured` | C2 |
| `04-native-split-capture` | N1 |
| `05-native-full-submit` | N2 |
| `06-native-standalone` | N0, test GUI route |
| `07-native-standard-vmu` | S1, Standard paced72 with VMU On |
| `08-native-uninstrumented` | N3 |

Use the exact folder/profile and twelve-character build ID recorded in the
package manifest. IDs consist of the first ten source hash characters followed
by the suffix below. Record the actual displayed ID, not just the filename.
An ID mismatch makes that row an unidentified run. A zero activation counter
means the proposed path was not exercised; keep the observation, but do not
call it a failed or successful performance change.

| Profile | Suffix | Purpose | Activation evidence needed |
| --- | --- | --- | --- |
| T1 | `d0` | Toy measured AP control | DATA requests/completions greater than zero; correct AP profile |
| T2 | `d1` | Toy yields when a DATA DMA is still pending | T1 evidence plus pending-DMA yield count greater than zero |
| T3 | `d2` | Toy first-sector diagnostic fingerprint disabled | DATA completions and CRC32SKIPS both greater than zero |
| C1 | `e0` | Armada existing-counter terminal control | Existing DATA work greater than zero; MODE1 and hot GD/IRQ counts zero |
| C2 | `e1` | Armada aggregate DATA diagnostics | MODE2 and hot GD count greater than zero, plus DATA progress |
| N1 | `b0` | General New demand reader, 112 KiB worker / 324 KiB full reservation, capture, prefetch Off | Demand requests/bytes greater than zero; exact general-reader route |
| N2 | `b1` | N1 with full submit control | N1 evidence plus submit-control visits greater than zero |
| N0 | `b2` | Worker-free standalone New control | Standalone ID and SD blocks greater than zero; Virtual VMU Off and New selected |
| S1 | `b3` | Standard paced72 with virtual VMU On, A2 | Standard VMU ID, admitted A2 route and VMU progress |
| N3 | `b4` | General reader with the same 112 KiB worker / 324 KiB reservation, capture disabled | General-reader ID plus existing DATA/sector progress |
| S0 | `b5` | Ordinary Standard SD path with emulated VMU Off | Physical-VMU module ID plus DATA progress |

Use the exact report labels and counter meanings included with each binary.
Do not substitute a menu ID for a resident ID. N0's test GUI route applies
only when New and Virtual VMU Off are selected. N0 and N1 differ in the
reserved memory and integration; N1 and N2 hold the reservation constant.
N1 versus N3 measures capture overhead while retaining the same reservation.
The full general reservation is `0x51000` bytes, including the 192 KiB cache
even with prefetch Off. Turning speculation off does not free that memory.
S1's Standard paced72 worker/layout is a separate comparison and is selected
only with Virtual VMU On. Standard plus VMU Off always selects S0, regardless
of whether S1's `retail-boot.kui` is installed.
S0 is an SD loader profile; the original game disc is a
separate baseline. Standard/New and any separately offered legacy Y reader
have different entry contracts, so their results are reader comparisons,
not a one-setting proof.

T3 removes the observer's first-sector fingerprint work; it does not remove
the transport's required CRC checking. C2 adds measurement; C1/C2 are a check
of DATA behavior and measurement cost, not a speculative refill change or an
audio mute experiment.
Toy's typed module internally runs its early AP DATA engine with the menu's
Background option Off. T1–T3 investigate pre-PLAY movie supply; these runs do
not combine native CDDA with an emulated VMU.

## Keep the workload fixed

Use one console, controller, cable, display mode, TV volume and card. Cold boot
every run. Keep the same game region/revision, image, image format, launch
options, initial save and input sequence within a pair. Record SCI/reader,
background loading, CD music, Virtual VMU and prefetch explicitly. Background
service of an already requested read and speculative prefetch are separate
features: keep speculative prefetch Off throughout the core matrix.

Use these scene definitions; give each a separate result row:

| Scene | Start marker | End marker / action |
| --- | --- | --- |
| Opening movie | First visible frame of the chosen movie | First stable menu/title frame after that movie; never skip it |
| Fixed movie excerpt, if the full movie is impractical | First frame of an agreed recognizable shot | Sixty seconds of phone time; record video/audio behavior, not time to a later scene |
| Quiet gameplay | First controllable frame of the same encounter | Sixty seconds with the same deliberately simple input sequence |
| Streaming/backtrack | First controllable frame at the same checkpoint | Same route to the next landmark and back; time to the final landmark |
| Save/load | Same prepared test save loaded, at the same location | One specified save, complete power off, reboot, load and verify its changed value |

Record Toy's first and second intro as different workloads. Record an opening
movie separately from a later scene that actually plays CDDA. A game-engine
intro, including SA2 Shadow's scene, must not be labeled an FMV. For a fixed
sixty-second excerpt, unchanged elapsed time is built into the protocol and
does not measure improvement: compare scene progress, stalls and sound.

For every movie, record five seconds before its first frame, the complete
chosen window and the report. Report video as smooth / repeated stalls /
slideshow; audio as clear / quiet / breakup / distortion / absent. Keep those
as observations rather than an invented FPS or loudness number. Time a full
scene from the phone footage, using the same two visible markers each time.

## Stage 1: movies and disc references

If the original disc is available, record the same Toy and Armada scenes on
disc first. Use the same game revision and display/sound settings. Without a
disc reference we can compare builds, but cannot establish near-stock speed.

| Workload | Runs, in order | Fixed options | What the comparison answers |
| --- | --- | --- | --- |
| Toy first troublesome movie | Disc, T1, T2, T3 | Same raw image; CD music On; Background Off; VMU Off; prefetch Off | Does pending-DMA yielding change stalls? How much does diagnostic fingerprinting cost? |
| Armada opening movie | Disc, C1, C2 | Same image; CD music On; background On; VMU Off; prefetch Off | Does the movie perform normally without CDDA playback? What DATA workload and loader cost accompany the symptom? |

Toy raw and cooked DATA adapters are already available. Finish the raw pair
first. Then repeat only the most useful control/candidate pair in the already
available cooked format. Keep raw audio tracks unchanged. Treat raw/cooked
as a separate format contrast; do not combine its delta with the scheduler
change. Do not convert an image just to fill the matrix.

Use A+B+X+Y+Start to open the supplied terminal report. Record all pages; allow
40 seconds for Toy's five pages (30 seconds per cycle at 60Hz, 36 at 50Hz)
and at least 35 seconds for Armada's five pages. Record at least 30 seconds
of the native aggregate report, or longer if its supplied report has more
pages. A legacy Toy report containing SCHED/DATA/PILOT/TRACE takes over
two minutes per full cycle; record a complete cycle if that report is used.
Opening the report stops the game. A gameplay or second-intro report therefore
requires another cold boot, not continuing after the first report.
For useful last-request context, open it at the same recognizable point while
the movie is still running. Do not press Skip first: subsequent metadata reads
can replace the interesting last LBA. Label that capture separately from a
full-movie timing run that reaches the title/menu end marker.

The previous 63814 Toy capture was pre-PLAY DATA work, and the Armada A1
capture had no source bytes or native audio play attempt. Keep these as useful
observations. They do not demonstrate simultaneous CDDA and movie activity.
For a CDDA-specific follow-up, require both an accepted PLAY and positive raw
audio/source progress; a PLAY mailbox alone is not proof of audible output.

## Stage 2: general reader cost

Use the already admitted Power Stone 2 image, first with Virtual VMU Off.
Record its opening movie and one fixed quiet gameplay window. Use New with
N0/N1/N2; keep prefetch Off and all other options fixed. Use S0 for the
Standard plus VMU Off reference. S1 belongs to the VMU On comparisons below.

Leave native PERF capture **unarmed during movies**. Its L+R+B chord contains
B, which can skip a movie. Native movie rows are phone-only observations;
their templates deliberately block formal counter-based comparisons.
Record the launch identity and complete movie without that chord.

For an N1/N2 quiet gameplay or save window, settle at the same checkpoint,
then hold **L+R+B** to arm capture. Choose a location where B's game action
does not change the test route. Capture stops at its bounded duration or poll
fallback, after pending work is quiescent. Record all **four PERF pages for
at least 30 seconds**. N0/N3/S0/S1 have no such PERF capture; use their ordinary
report if available, plus the phone recording. A missing counter stays missing.

N0's compact SCI report uses these exact labels: `SD0` is DMA blocks, PIO
blocks, CMD18 starts and kept blocks; `SD4` is overruns, CRC errors, token
errors and foreign events; `SD8` is repaired, receive-ahead and deferred.
These are the existing eleven transport counters, not new CPU measurements.
A quiet window can serve no DATA. Retain real prior/cumulative served-demand
evidence plus actual admitted SDK/owner context and the matching module ID;
an SDK cookie alone does not prove the proposed path served a request.

| Pair | Hold constant | Question |
| --- | --- | --- |
| N0 versus N1, VMU Off | Game, New selection, image, scene and input | What is the combined reservation/integration cost of the general path? |
| N1 versus N2, VMU Off | The same general reservation and demand reader | What does full submit control add before VMU traffic is enabled? |
| N1 versus N3, VMU Off | The same general reservation and demand reader | How much does aggregate capture itself cost? |
| S0 versus N0 or N1 | Game, image, scene, VMU Off and prefetch Off | How do different reader implementations behave on this workload? |
| Disc versus best SD profile | Scene markers, input, display and game revision | How far is the complete SD path from the disc experience? |

Do not interpret Standard's pacing budget or a different ABI as an isolated
effect of speculative prefetch. If N0 and N1 are alike but N2 slows down, focus
on the submit path. If N0 already stalls, first inspect demand service and
CPU work. These are investigation directions, not conclusions from the labels.

## Stage 3: VMU cost and durable save

On the same N1 binary, repeat the fixed Power Stone 2 movie and gameplay with
Virtual VMU Off, then On. Use the supported, existing prepared VMU pair and
keep its initial save content constant. Record VMU activation/progress and
any reported backend traffic; On in the menu alone is insufficient evidence.
N1/N2 target virtual slot **A2**. Selecting A1 routes to an existing different
module and does not test these profiles. Record the target slot and displayed
resident ID. Keep physical accessories fixed within the pair.
Repeat N2 On only if its supplied control supports that same prepared pair.
Compare S1 On against N1 On as an observational reader comparison with the
same A2 VMU, save and workload; never request S1 with VMU Off.

Then perform one named save change on N1 On, wait for the game's save-complete
indication, power off fully, reboot with that profile and load it. State the
before/after value that proves the new save survived. Use a test save rather
than overwriting the only copy of a valued save. If a format prompt appears,
keep its photo and stop that save row without formatting.

Use physical VMU save/load on disc as the end-to-end reference when available.
That comparison changes the complete storage system and cannot attribute a
delta to one hook. Preserve the same logical initial save and action. Report
save completion latency and reboot/load correctness independently of movie
and gameplay smoothness.

Toy and Armada get a VMU On row only when the package explicitly admits that
game/SDK family. Earlier Armada A2 proof/full controls are read-only and do
not provide writable-save validation. Mark unsupported combinations as such;
Power Stone 2's persistence result does not establish every SDK or WinCE save.

## Stage 4: targeted compatibility canaries

Proceed after a concrete profile wins or explains a cost in the earlier stages.
Use accepted control versus that profile, first Off and then On only when
supported. One fixed run per case is enough to find a regression; repeat any
apparent improvement or failure on a fresh cold boot before broadening tests.

| Game / window | Why it is useful | Required evidence |
| --- | --- | --- |
| Toy, later native CDDA scene | Native audio and demanded DATA coexist | Accepted PLAY plus raw bytes, DATA progress, no new audible gaps |
| Armada, later CE CDDA scene | CE binding/refill under DATA activity | Source bytes plus native play/worker evidence and DATA progress |
| DOA2, quiet fight | CPU/hook cost with little streaming | Same fighter/stage/input and demand/hook activation |
| Evolution, fixed route and backtrack | Sequential demand, seeks and repeated reads | Same checkpoint/route; demand progress and seek/reuse counters if exposed |
| Rayman 2 or Power Stone 2, save/reboot/load | Persistent VMU activity and game speed | VMU progress, save acknowledgment and changed value after reboot |
| House of the Dead 2, fixed early encounter | Earlier SDK compatibility | Exact admitted SDK route, demand activation, same encounter/input |

Raw/cooked, legacy Y, CDDA and SDK expansion are follow-ups to a specific
question, not a requirement to test every Cartesian combination. Retain a
working accepted profile and repeat its matching run if a new failure appears.

Confirm an apparent winner with three matched cold-boot pairs, alternating
which profile runs first. Keep repeat1 matched to repeat1, and so on. Report
the individual scene times and visible/audible behavior; a single maximum
counter or one unusually quick boot does not establish a general improvement.

## Results to send

One phone clip per row can carry all the evidence. Name it with profile,
game/scene, VMU state and repeat number. Include this short text:

```text
Case: T2-toy-intro1-off-1
Expected profile/build: [from package manifest]
Displayed profile/build: [from terminal screen]
Game region/revision and image format:
Reader / background / prefetch / CD music / VMU / target slot:
Scene start -> end markers; repeat number:
Video / audio / input observations:
Engagement label(s) and exact displayed values:
Phone scene time, or “fixed 60-second excerpt”:
Save changed value and reboot result, when applicable:
```

Copy report values exactly, including leading zeros and page labels. They are
hexadecimal unless a report explicitly says otherwise. These T1/T2/T3 demand
reports show raw ticks at 781250 Hz (divide by 781.25 for milliseconds). The
older B7 FMV report used hex milliseconds, so retain the actual report title.
Armada VISIT-MS/JOB-MS are not frame times. Zero valid samples means an
unavailable timing; invalid clock flags also prevent a trustworthy time delta.
Store phase0 and phase1 metrics separately. First PLAY is a command boundary,
not a movie marker. All-zero phase1 must not be reported as instantaneous I/O.
In the Toy demand report, `PROFILE CRC32SKIPS PLAYS SAT` identifies profiles
1/2/3. T2 needs positive `YIELDS` in `VISITS STEPS WAITPOLLS YIELDS`; T3 needs
positive `CRC32SKIPS`; T1 needs positive visits/steps and DATA completions.
Nonzero `SAT` invalidates aggregate timing. The displayed `BADCLK BADTIME`
row is phase0: do not reuse its zeros as phase1 clock evidence. `ORIGINAL
CALLER SR` describes the inherited caller mask, not an unmasked transport.

Armada C1/C2's first row is `VERSION MODE PROFILE-ID GD IRQ`. C1 is MODE1
with hot GD/IRQ counts zero; its existing terminal DATA/sector counters still
show that the game read data. C2 is MODE2 and needs hot GD plus DATA progress.
`GD-MOD/IRQ-MOD` are TMU modulo lower-bound counts, not exact long interrupt
mask durations. Do not convert them into a CPU-use percentage or full elapsed
time. `GD-CYC/IRQ-CYC` use elapsed CPU cycles only when the retained hardware
configuration is admitted at both ends; unavailable/bad or clipped cycle
observations do not invalidate the logical read counters. Keep such report
times unavailable while retaining the phone elapsed time and DATA counts.

IRQ blocks divided by all blocks describes where progress was credited. It
does not measure CPU use, time spent with interrupts masked, bandwidth saved,
or a particular movie's frame rate. No such conversion is made by the analyzer.
Do not turn an unsigned counter wrap or saturation into an ordinary delta.

## Optional typed analysis on a computer

The package includes `performance-results-template.csv`,
`performance-results-template.jsonl` and `performance-case-guide.md`.
Their 35 staged plans contain actual expected module IDs from `build.json`;
observed IDs, clocks, counters and times are blank. Fill UNKNOWN revision,
scene markers, save seed and display mode consistently before comparing rows.
Native unarmed movie rows use `observation_only: true` and remain ineligible
for a formal comparison even after the phone observations are entered.
Do not replace a missing native movie capture with an assumed activation count.

`tools/analyze_performance_results.py` accepts JSON arrays, JSONL or CSV.
It performs no installation and opens no console/card device. Example:

```sh
python3 tools/analyze_performance_results.py results.jsonl
python3 tools/analyze_performance_results.py results.csv --json
python3 -m unittest discover -s tests -p test_analyze_performance_results.py
```

Every row needs these workload fields: `game`, `revision`, `workload_id`,
`scene`, `start_marker`, `end_marker`, `save_seed`, `video_mode`, `repeat`.
Use `none` for a genuine no-save workload, rather than leaving its seed unknown.
Also provide `schema_version: 1`, unique `case_id`, and settings `media`,
`format`, `reader`, `profile`, `prefetch`, `background`, `vmu`, `cdda`.
The supplied rows also fix `storage` to SCI or GD-ROM; changing that source
within a reader/profile pair is refused.
Media is `disc` or `sd`; option values are `on`, `off` or `n/a`, and VMU can
also be `physical` or `unsupported`. SD rows additionally require `build_id`,
`expected_build_id`, and `expected_profile` from the manifest. The observed
and expected identities must match exactly apart from letter case, and build
IDs must contain exactly twelve hexadecimal characters. Active physical or
emulated VMU rows also require `vmu_slot`, for example `A1` or `A2`.

An example row is below. Replace the placeholder identity and workload details
with actual observations; it is a schema example, not captured evidence.

```json
{
  "schema_version": 1,
  "case_id": "T1-toy-intro1-off-1",
  "game": "Toy Commander", "revision": "actual region and revision",
  "workload_id": "toy-opening-1", "scene": "intro1",
  "start_marker": "first movie frame", "end_marker": "first stable title frame",
  "save_seed": "none", "video_mode": "actual display mode", "repeat": 1,
  "media": "sd", "format": "raw", "reader": "toy-ap", "profile": "T1",
  "expected_profile": "T1", "expected_build_id": "actual manifest ID",
  "build_id": "actual displayed ID",
  "prefetch": "off", "background": "off", "vmu": "off", "cdda": "on",
  "engagement": {
    "loader": {"counter": "ZERO EXEC DONE FAIL ACK / DONE", "observed": "hex:000005EE"}
  },
  "clocks": {
    "phase0": {"valid_samples": "hex:000005EE", "bad_calls": "hex:00000000", "bad_intervals": "hex:00000000"}
  },
  "metrics": {
    "scene_elapsed_ms": {"value": "dec:55000", "unit": "ms", "source": "phone"},
    "phase0_call_max_ticks": {"value": "hex:00002204", "unit": "ticks", "clock": "phase0"}
  }
}
```

Use explicit `hex:`/`0x` and `dec:` prefixes, JSON numeric values, or a row-wide
`radix: 16` for unprefixed console strings. Bare digit strings without a radix
are refused instead of guessing whether `10` means ten or sixteen. Missing
values are `null`, `unavailable`, `n/a` or `-`; a recorded zero remains zero.
`FFFFFFFF` is not universally missing: declare a metric's documented sentinel
in `unavailable_values`, for example `["hex:FFFFFFFF"]`. Raw tick metrics may
be retained with `unit: "ticks"`; the tool does not invent a clock frequency.

For a pair, add `reference_case` naming its baseline row and `contrast`:

| Contrast | Permitted difference | Interpretation |
| --- | --- | --- |
| `profile` | Profile | Same workload/options, different binary profile |
| `vmu` | VMU and its target slot | Same profile, Off versus On |
| `format` | Image format | Same revision/content/workload, already available raw versus cooked |
| `reader` | Reader, profile, background | Explicit observational reader/ABI comparison |
| `media` | Media/storage, format, reader, profile, background | Disc versus SD with VMU state held constant |
| `system` | Media/storage, format, reader, profile, background, VMU and slot | Disc/physical VMU versus complete SD/emulated VMU system |

All other workload/settings fields, including repeat number, must match.
Each named primary contrast must actually change; `system` requires a
disc/SD pair. A profile pair also requires an engaged `candidate` evidence
entry with its actual feature counter; all SD rows require `loader`, and VMU
On requires `vmu`. Add `required_activity: ["cdda"]` and matching `cdda`
engagement for an explicitly CDDA-active workload. Extra required activities
can distinguish data-read, save-write and readback cases. Each engagement
entry has a named `counter`, typed `observed` value and optional `minimum`
(default1). A missing or zero mandatory counter blocks the comparison.

CSV uses the same field names, with dotted columns for nested values:
`engagement.loader.counter`, `engagement.loader.observed`,
`metrics.scene_elapsed_ms.value`, `metrics.scene_elapsed_ms.unit`,
`metrics.scene_elapsed_ms.source`, `clocks.phase0.valid_samples`, and so on.
Separate CSV `required_activity` names with semicolons. No special workbook
or Python library is needed. A spreadsheet may also be used just to type the
rows; phone recordings remain the primary perceptual evidence.

The tool refuses unmatched workloads/identities and reports unengaged or
unavailable evidence. It omits time deltas when clock samples are absent,
invalid or saturated, and compares only equal units and sources. Phone elapsed
time can remain usable when a loader clock is unavailable. Positive percentage
change means a larger value; it is not automatically a speedup or regression.
An output marked eligible means the row has enough identity/activity evidence
for its declared comparison, not that the game passed or reached stock speed.

Clock entries normally require `valid_samples`, `bad_calls` and
`bad_intervals`, plus `saturated` when the report provides it. A legacy trace
may instead supply `clock_epoch`, `unmatched_ends` and `nested_begins` together;
all must be zero. Missing clock evidence remains unavailable. Enter only fields
actually displayed or recovered from that report's documented ABI.

For Toy's `phase0_data_ticks_max`, use the DATA timing row's own sample count,
its phase0 BADCLK/BADTIME values and SAT flag. Do not substitute CALL samples
or phase0 validity for a different metric/phase. For C2's GD maximum cycle
time, use `TIMED-GD`, not TIMED-IRQ, as valid samples; retain BAD-CYC, unpaired
and clip evidence from the full report. Request completion cycles include
time between game calls and are not masked-body CPU time. For native PERF,
each `N OK BAD SUM MAX` row supplies its own OK/BAD counts; retain WALL BAD
and METRIC FLAGS as well. The tool conservatively withholds uncertain times
while still allowing meaningful logical counters and phone observations.
