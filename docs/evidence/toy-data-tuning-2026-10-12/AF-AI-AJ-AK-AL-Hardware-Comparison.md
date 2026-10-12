# AF / AI / AJ / AK / AL hardware comparison — 2026-10-12

The larger DATA allowances changed the service exit limits, but the latest user report still describes no meaningful playback improvement: the second video advances three frames instead of two and remains very slow.

The exact retained 480-sector read completed in 1.222 seconds on AF and 1.211–1.221 seconds on the new builds, a reduction of 0.09–0.88%. AK and AL delivered the first credited sector later. One recording per build cannot separate that small difference from run variation.

| Build | Physical block cap | Timer ticks | Step cap | First credit (ms) | 480-sector completion (s) | Reduction vs AF |
|---|---:|---:|---:|---:|---:|---:|
| AF | 24 | 12000 | 1024 | 13.27360 | 1.221905 | reference |
| AI | 24 | 12000 | 2048 | 13.27232 | 1.220769 | 0.093% |
| AJ | 24 | 9000 | 1024 | 13.15328 | 1.219772 | 0.175% |
| AK | 32 | 12000 | 2048 | 16.98048 | 1.216600 | 0.434% |
| AL | 32 | 18000 | 2048 | 18.61632 | 1.211167 | 0.879% |

The block cap counts verified 512-byte physical card blocks. The matched request is LBA `00078091`, 480 logical sectors, destination `AC588B3C`. Timer frequency is 781250 Hz; timer and step limits govern admission between transport steps.

| Build | Service calls | Block exits | Time exits | Token exits | Step exits | Finished/inactive exits |
|---|---:|---:|---:|---:|---:|---:|
| AF | 2300 | 0 | 0 | 24 | 1437 | 839 |
| AI | 2241 | 1374 | 0 | 24 | 0 | 843 |
| AJ | 2489 | 0 | 1552 | 25 | 0 | 912 |
| AK | 2082 | 0 | 1244 | 25 | 0 | 813 |
| AL | 2041 | 918 | 0 | 24 | 0 | 1099 |

All accepted startup DATA requests completed and were acknowledged, with equal requested and delivered sector/byte counts and no recorded DATA failures. The startup outstanding fields are zero at the first accepted PLAY mailbox. After that boundary, the captured GD calls contain no accepted DATA requests. Those facts describe the instrumented phases; they do not locate the visible stall or establish controller inputs.

| Build | Startup DATA requests completed | Post-PLAY GD calls | CDDA raw errors / queue errors | Worker step max (ms) | Worker service gap max (ms) | TRACE words observed / 416 |
|---|---:|---:|---|---:|---:|---:|
| AF | 540 | 1747 | 0 / 0 | 22.403 | 78.004 | 336 / 416 |
| AI | 550 | 855 | 0 / 0 | 22.752 | 52.142 | 320 / 416 |
| AJ | 548 | 945 | 0 / 0 | 22.451 | 52.521 | 304 / 416 |
| AK | 534 | 826 | 0 / 0 | 22.711 | 50.126 | 304 / 416 |
| AL | 540 | 2368 | 0 / 0 | 22.580 | 82.040 | 320 / 416 |

Each capture has complete 192-word SCHED, 192-word DATA and 128-word PILOT reports. All 144 independently read critical words match the current primary transcripts. The later TRACE tail is incomplete and remains unknown; it is not filled with zeros. Zero audio transport counters cannot exclude an audible glitch.

Direct native-pixel QA resolves the first-sector CRC for LBA `0007829D`, count 1, as `D99DCACB` in AF and all four current captures. The fourth glyph is a hollow D. Current AI/AJ/AL transcripts were corrected with initial readings and frame hashes preserved; old AF files remain unchanged, and only the consolidated decoded view applies its documented glyph erratum. This does not verify the original GDI or other sectors.

[Full comparison and raw evidence](AF-AI-AJ-AK-AL-Hardware-Comparison.json) · [CRC pixel errata](crc-glyph-errata.json)
