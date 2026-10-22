# Pinned still-screen pacing build, 2026-09-26

The owner chose this build as the best Games launcher result and asked for it
to be pinned and committed to `main`. Further serial-SD (SCIF) transport tuning
is closed: the remaining slowdowns are the cost of bit-banged SD itself.

## Exact build

- Build: `2072b489c378` ("Bound the learned frame length used for read
  pacing"), on top of `6ca6719` (still-screen pacing, all-CMD18 streams).
- Pinned branch: `baseline/doa2-pacing-2072b489c378`.
- CI: [run 36209735323](https://github.com/TPMJB/K-UI-NeXT/actions/runs/36209735323),
  host suite and native build passed.
- `sd-update` artifact 10895048305, 4,348,249 bytes, artifact digest
  `sha256:2e524df1b245118ea0ff172f5cf648f99a56f6592f776aaa77b4bf50be6cb51c`.
  The artifact expires on 2026-10-26; keep the owner's downloaded copy.
- Resident end `0x8c00ba10` (240 bytes below the `0x8c00bb00` guard);
  conservative stack bound 1052/1232 bytes; linked instruction audit passed.
- Same DOA2 T3601N/V1.100/U raw GDI, serial SD adapter and boot CD as the
  earlier baselines. It also carries the Ogg menu music and Harbor Lights.

## Owner reports

| Build | Report |
| --- | --- |
| `2072b48` (this pin) | "This actually worked really well. Some longer load times between fights but really the only bad load time left was the first ten seconds of a fight were pretty laggy. Otherwise it was extremely playable." |
| `bc9926f` (in-play trial, not kept) | "Arguably, that made Dead or Alive 2 worse... There's a longer period in the beginnings of fights where the controls are like molasses." |
| Same round, Evolution (first game) | "Mostly responsive, just the few FMVs were horribly stuttery." |

These are approximate owner observations; no counter photos were taken.

The trial build alternated in-play `EXEC` steps between one and two sectors
every two seconds ([CI run 36213391285](https://github.com/TPMJB/K-UI-NeXT/actions/runs/36213391285)).
One-sector periods lengthened the fight-start streaming without making those
seconds smooth. That fits a DOA2 frame whose own work exceeds about 11 ms:
even a 5 ms read per frame then halves its speed. Early-fight lag and FMV
stutter are therefore the CPU cost of SCIF bit-banging at the data rate the
game requests. The trial code is not on `main`.

## Remaining options outside transport tuning

- Derived 2048-byte data tracks would remove about 13% of the bytes read
  (deferred by the owner).
- A faster SD interface (for example a hardware-clocked serial port) would
  need different hardware; the stock rear-port SCI is not revived.
