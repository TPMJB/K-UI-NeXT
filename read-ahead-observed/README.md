# Read-ahead console observation

Capture63352.mp4 of candidateafcf7e1524a9 confirms boot and cache delivery, but shows limited production:62 speculative512-byteblocks,86 cache feeds (about3.8% of demandfeeds), and8 unclassified abandonments. The longest whole request is139ms. The user reports no obvious harm and remains skeptical of benefit. This is not a controlled performance comparison.

The report has14pages, approximately62seconds percycle in the observed60Hz run. Record one fullcycle plus2seconds; repeated pages are frozen data.

CONSOLE-RESULTS.json contains the verified counters, eight per-request rows, arithmetic checks, evidence limits and next diagnostic target. CAPTURE-GUIDE.md has corrected instructions. Runtime code and previously published builds remain unchanged.

## Exact source history

This evidence-only source commit follows the afcf7e1524a9 read-ahead candidate. First recover that candidate using ../read-ahead/checkout-source.py, then run:

```sh
python3 read-ahead-observed/checkout-source.py
```

The incremental bundle imports the authored evidence and capture-guide change and verifies the complete resulting tree. Original retail executables, tracks, save data and user video are not included; the recording is identified by its SHA256.
