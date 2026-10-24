# Native SCI hybrid test — 1920659698ae

The candidate keeps the Background DMA pipeline and caps foreground catch-up.
REQUEST/CHECK/reader IRQ deliver at most one SD block; EXEC at most eight.
DMA waits share 1,024 polls per entry. PIO fallback remains one block per entry.
Card command/token/recovery waits retain their legacy timeouts.

Console performance is unverified. This is a native Evolution 2 comparison,
not a new Windows CE or CDDA/VMU build. Do not merge it over main as a release.

Download `K-UI-Native-Hybrid-1920659698ae.zip`, extract, and follow
README-FIRST.md. Install using the existing prepared profile, leave real A2
empty, and launch with SCI / Background On / Normal speed. Compare FMVs,
gameplay hangs and A2 save → full power-off → reload.

The build passed 42,274 hybrid host checks, native/CE regression suites,
linked memory/stack/mutation checks and 15 extracted-installer cases preserving
the prepared card/profile/journal. TEST-EVIDENCE.json and probe-report.json
record the limits and results. These tests do not measure console frame timing.

## Exact source and history

This branch retains the complete earlier checkpoint files and original source
history. From the repository root, run:

```sh
python3 hybrid/restore-hybrid.py
```

The script restores the original 74958b387827 checkpoint, imports the small
incremental bundle, and verifies the exact final commit and tree. It refuses
a dirty source checkout. Original source authors/dates/commit IDs are retained.
No user VMU image, disk profile, save, game executable or game track is included.
