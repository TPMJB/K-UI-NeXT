# Profile 13 page-1 console result — 2026-10-07

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
[replacement instructions](../cdda-preflight-test.md) require a new run and
photographs of all six pages. The replacement runtime has no confirmed console
result yet. It does not include, rebuild or replace the existing test 14 observer
at build `324c330bdb6c`.
