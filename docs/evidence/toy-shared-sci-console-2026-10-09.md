# Shared SCI console regression: 2faa62067610

The console run was reported on October 8, 2026, at about 8:15 PM
America/Chicago (October 9 UTC). The user described the intro video as
"a lot worse than before". The submitted photos show the candidate build
`2faa62067610`; they are evidence from the preceding asynchronous revision,
not a hardware test of the foreground top-up revision.

Page 5 was legible. Values below follow its published hexadecimal field order.

| Counter | Hexadecimal | Decimal |
| --- | --- | ---: |
| Pump calls | `0005A26F` | 369,263 |
| SCI IRQ calls | `000012B6` | 4,790 |
| Blocks consumed in calls | `000020FE` | 8,446 |
| Blocks consumed in IRQs | `00000D3E` | 3,390 |
| Audio card claims | `00000627` | 1,575 |
| Audio claims deferred | `00000000` | 0 |
| Audio card releases | `00000627` | 1,575 |
| Data stream resumes | `00000088` | 136 |
| Transport errors | `00000000` | 0 |
| Retries | `00000426` | 1,062 |
| Token waits yielded | `00000025` | 37 |
| Blocks received by PIO fallback | `00000211` | 529 |
| Longest pump, TMU ticks | `00000743` | 1,859 |
| Longest IRQ, TMU ticks | `00000272` | 626 |
| Active data token at capture | `00000000` | 0 |
| Card owner at capture | `00000000` | 0 |

Together the two consume counters record 11,836 physical 512-byte blocks.
The IRQ path supplied 28.64% of those blocks; the foreground call path supplied
71.36%. There were about 43.72 pump entries for each block consumed in a call.
These ratios describe this capture, not transfer bandwidth or frame time.

The existing TMU profile was admitted. At its 1.28 microseconds per tick, the
observed longest pump was 2.37952 ms and longest IRQ was 0.80128 ms. Summing
entry counts and maxima would not produce total blocked time. The report has
no elapsed playback interval or accumulated shared-transport timing.

Page 6's existing recovery counters were all zero, and claims/releases were
paired. That records no counted CDDA recovery or failed transport request; it
does not independently establish inaudible audio gaps. Page 7's photo was too
blurred to verify raw-read timing. No raw-callback total, maximum or average
has been inferred from it. The attached recording and the user's direct
comparison establish the reported visual regression, not its physical cause.

The old engine advanced at most one checked block per entry and never waited
for active DMA in a game-data call. A game interrupt mask delaying SCI level 1
could therefore leave many CHECK/EXEC visits doing little useful work. Frequent
entry/exit and the measured retries could add cost. These are source-supported
hypotheses: the report does not count game SR masks, every SCI wire fault, or
the video decoder's work, so it cannot prove which dominates.

The next isolated candidate addresses that scheduling hypothesis. Owned data
CHECK/EXEC calls can fill a four-physical-block allowance, crediting background
progress first, under between-step timer, iteration and whole-entry token
limits. Submission, IRQ and audio paths retain nonblocking behavior. The
clean-audio `7b55156aafa2` runtime and unchanged `6c4a9915ba33` launcher remain
the rollback pair. Console video and music must be compared again.
