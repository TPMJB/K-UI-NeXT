# Profile 13 console results — 2026-10-07

## Original build: page 1 only

The user's photographs confirm the first report page of build `324c330bdb6c`
from `K-UI-CDDA-Integration-Tests.zip`. The screen reports **PASS**, six completed
stages and zero failures. It did not advance to page 2 after the intended
15-second interval. Pages 2–6 have not been confirmed on console.

| Page-1 field | Photographed value |
|---|---|
| Build | `324c330bdb6c` |
| Completed stages / failures | `6 / 0` |
| Backed tracks / audio | `15 / 12` |
| Extent slots / 64-slot fit | `44 / 1` |
| Title | `TOY COMMANDER` |
| Product / version | `MK-57020 / V1.022` |
| Region | `U` |
| Boot file | `1GUTH.BIN` |
| Boot LBA / bytes | `548634 / 748444` |
| IP CRC32 | `38ba2868` |
| Boot CRC32 | `cdc493b3` |
| IP / boot hashed bytes | `32768 / 748444` |
| Private stack bytes | `32600` |
| Checked card blocks | `3042` |

These values establish the observed page-1 result for the user's card and
image. They do not supply the undisplayed IP/boot SHA-256 values, physical-map
hash, complete per-track rows or selected-path/scan page. No values from those
pages are inferred or filled in.

The subsequent source investigation found that the preflight sampled TMU1
without starting it. The same stopped counter was used for automatic page
changes and the intended 180-second read deadline, so both timing functions
were inactive in this original build. Its six completed stages remain an
observed result; the old deadline and paging behavior are not validated.

The replacement initializes the standalone diagnostic's timer before storage
reads and keeps it running through the report. The
[replacement instructions](../cdda-preflight-test.md) required a new run and
photographs of all six pages. At that point the replacement runtime had no
confirmed console result. It did not include, rebuild or replace the existing
test 14 observer at build `324c330bdb6c`.

## Corrected build: complete six-page report

The user supplied photographs of all six report pages from the corrected build
`9fb65a33f235`, delivered in `K-UI-CDDA-Preflight-Fix.zip`. The supplied sequence
is pages **2, 3, 4, 5, 6, then 1**. The final page 1 after page 6 confirms that
the report advances and wraps through the six-page cycle on this console.
The report shows **PASS**, six completed stages and zero failures.
The values below reproduce the reviewed photo transcription; the arithmetic
and original-descriptor checks are stated separately. The photographs are not
copied into this source record.

### Summary page

| Page-1 field | Photographed value |
|---|---|
| Build | `9fb65a33f235` |
| Completed stages / failures | `6 / 0` |
| Backed tracks / audio | `15 / 12` |
| Extent slots / 64-slot fit | `44 / 1` |
| Title | `TOY COMMANDER` |
| Product / version | `MK-57020 / V1.022` |
| Region | `U` |
| Boot file | `1GUTH.BIN` |
| Boot LBA / bytes | `548634 / 748444` |
| IP CRC32 | `38ba2868` |
| Boot CRC32 | `cdc493b3` |
| IP / boot hashed bytes | `32768 / 748444` |
| Private stack bytes | `32612` |
| Checked card blocks | `3042` |

### Hash page

Each displayed SHA-256 is joined from its two consecutive 32-character lines.

| Page-2 field | SHA-256 |
|---|---|
| IP | `dcb2b68f92456fac4261ab8e386875f8f337e40e6e26bd11368f873f55fbc5a6` |
| Boot | `ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd` |
| Physical map | `23e463efc5308deeec891dcb75240e275c3128fba80da1d1e7880ce295c44427` |
| GDI | `96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803` |

The GDI digest matches the pinned original 451-byte descriptor. The IP and boot
digests identify the reported 32,768 IP bytes and 748,444 boot-file bytes.
The physical-map digest identifies this card mapping; it is not a whole-disc
content hash.

### Per-track pages

Pages 3–5 show all fifteen backings. FAD ends are exclusive. D means data;
A means audio. The card value is the first physical card LBA, not a claim
that the whole backing occupies one contiguous run.

| Track / file | Type | FAD range | File bytes | Extent runs | First card LBA |
|---|---|---|---:|---:|---:|
| 1 — `track01.bin` | D | `[150,7257)` | 16715664 | 2 | 59017984 |
| 2 — `track02.raw` | A | `[7407,7903)` | 1166592 | 1 | 59051008 |
| 3 — `track03.bin` | D | `[45150,201900)` | 368676000 | 3 | 59053568 |
| 4 — `track04.raw` | A | `[202050,219862)` | 41893824 | 2 | 59780096 |
| 5 — `track05.raw` | A | `[219862,234241)` | 33819408 | 2 | 59862272 |
| 6 — `track06.raw` | A | `[234241,255749)` | 50586816 | 2 | 59928832 |
| 7 — `track07.raw` | A | `[255749,272167)` | 38615136 | 2 | 60027904 |
| 8 — `track08.raw` | A | `[272167,282949)` | 25359264 | 2 | 60103680 |
| 9 — `track09.raw` | A | `[282949,303242)` | 47729136 | 2 | 60153600 |
| 10 — `track10.raw` | A | `[303242,323502)` | 47651520 | 2 | 60247296 |
| 11 — `track11.raw` | A | `[323502,341464)` | 42246624 | 2 | 60340736 |
| 12 — `track12.raw` | A | `[341464,356840)` | 36164352 | 2 | 60423680 |
| 13 — `track13.raw` | A | `[356840,374351)` | 41185872 | 2 | 60494592 |
| 14 — `track14.raw` | A | `[374351,377422)` | 7222992 | 1 | 60575488 |
| 15 — `track15.bin` | D | `[377572,549300)` | 403904256 | 2 | 60590080 |

For every row, `(end FAD − start FAD) × 2352` equals the displayed file length.
Each start FAD equals the original descriptor's track LBA plus 150, and the
file names and data/audio types match that descriptor. The run counts sum to
29; adding fifteen track records gives the displayed 44 slots. Track 14 has
3,071 backed sectors and ends at FAD 377,422. The next data track starts at
377,572, preserving the 150-sector unmapped gap.

### Selected image and card geometry

| Page-6 field | Photographed value |
|---|---|
| Selected original descriptor | `0:/Games/TOY_COMMANDER/TOY_COMMANDER.gdi` |
| Scan directories / entries | `0 / 0` |
| Complete / incomplete matches | `1 / 0` |
| Explicit path configured | `1` |
| Card sectors | `249999360` |
| Partition start / sectors | `2048 / 249997312` |

The configured-path result explains the zero discovery-scan counts; it does
not bypass the completed image and backing checks. The partition ends at card
sector 249,999,360, matching the reported card limit.

All three per-track pages, covering tracks 1–15, were supplied. This corrected
run confirms the complete displayed report and its automatic cycling. It does
not establish a precisely calibrated 15-second interval or exercise an
injected 180-second timeout. It launches no game and establishes no retail
timer or sound-resource ownership. Test 14 remains the previously supplied
observer at build `324c330bdb6c`; these photographs do not report a test 14 run.
