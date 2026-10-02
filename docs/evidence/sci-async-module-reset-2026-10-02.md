# SCI async probe: first receive wait fails after verified DMA

The owner tested `c25c1f6c2190` and again reported "framing bus fault".
The exact uploaded report is preserved as
[`sci-async-console-c25c1f6c2190.json`](sci-async-console-c25c1f6c2190.json).
SHA-256: `b91ff4d839077828ad10b358910eea41171008172bb9efd64f057766a5d6c7ed`.

## Console evidence

All 16 slow trials and the first fast trial passed CRC, baseline and guard
checks and delivered a DMA completion interrupt. The second fast trial failed
before another DMA began. Fast reception took 340 us for 514 bytes; this is
not filesystem throughput. The fast completion recorded trailing ORER. All
handoff checks passed without retry. Maximum measured IRQ-masked and handler
intervals were 27 and 5 us; independent timer IRQ progress is uninstrumented.

The new first-fault snapshot pinpoints the failure:

| Field | Value | Meaning |
| --- | --- | --- |
| Framing operation / index | idle clock / 0 | First dummy byte with CS high |
| Wait flag / polls | `40` / 10,000 | RDRF never observed within the bound |
| Pre-stop SSR / SCR | `86` / `30` | TDRE/TEND set, no RDRF or error flags; TE+RE enabled |
| SMR / BRR / SCMR | `80` / `00` / `00` | Expected synchronous full-speed settings |
| SPTR / PDTR | `05` / `e3e3` | CS bit `80` high |

These are hexadecimal register values. Transmission flags alone do not prove
that eight clock edges occurred. However, the next selected-card ready poll,
CMD17 and token wait had not begun, so those card response delays cannot
explain this failure. A cached-speed mismatch is not demonstrated.

Recovery reinitialized the card and verified an ordinary CRC-checked CMD17
against the baseline. Handlers/registers restored; no foreign DMA or quarantine.
The final aggregate CRC/data/guard flags were cleared at the beginning of the
failed second fast trial; they do not invalidate the 17 completed trials.

## Bounded experiment

The existing handoff already disables TE/RE, clears receive flags and
reinitializes synchronous framing in the documented order. No missing mandatory
visible-register write has been identified. A retained receiver-state problem
following autonomous receive and trailing overrun is a hypothesis.

Renesas specifies that module standby initializes the SCI registers except
the serial port register. STBCR.MSTP0 controls only SCI. The next runtime-probe
candidate uses this documented reset mechanism after a fully verified transfer
with trailing ORER. It does not reset the card between trials.

Requirements:

- Completed, owned DMA with count zero and TE, followed by CRC/data/guards pass.
- SCI stopped, CS high, no prior bus fault, no foreign DMA or quarantine.
- Recheck ownership under a short IRQ mask; preserve all other STBCR bits.
- Confirm module stop and resume with bounded readbacks. No SCI MMIO while
  stopped, and no normal SCI callbacks or register restoration if resume cannot
  be confirmed. Such failure requires restart and blocks storage/report writes.
- Verify SCR=0, SMR=0, BRR=ff, SCMR=0, SSR masked by fc equals 84, and retained
  SPTR control bits. Do not compare sampled input bits as output latches.
- Restore mode, current baud and smart-card settings, allow the bit-time settle,
  restore normal SPTR controls, then enable TE+RE together.
- Keep slow trials without overrun on the previous handoff. Preserve sticky
  faults, ownership checks, quarantine and original first-fault snapshots.

The JSON records reset attempts, successes, failures, state and STBCR before,
stopped and resumed. State 0=not attempted, 1=OK, 2=precondition, 3=stop not
confirmed, 4=resume not confirmed. Signature errors use `0x100` plus bits
SCR=1, SMR=2, BRR=4, SCMR=8, SSR=16, SPTR=32.

Host tests can exercise this policy and a modeled receiver stall, but cannot
prove that it explains the physical console. Console acceptance remains 16
slow and 64 fast verified reads, DMA completion and CPU overlap, followed by
verified normal storage recovery. This is not an asynchronous game reader or
Windows CE support. Ordinary game reads and native resident limits remain.

Primary reference: [Renesas SH7750 hardware manual Rev. 7.02](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
sections 9.2.1, 9.6, 15.1.4, 15.3.4 and 15.5; SCI reset values in table 15.2
and smart-card register initialization in section 17.2.1.

## Validation and delivery

The probe and runtime-wrapper strict builds and ASan/UBSan tests pass.
Independent review also compiled and ran the probe tests. Local LeakSanitizer
is disabled because this runner cannot inspect `/proc`; CI retains its normal
configuration. The model exercises 64 fast reset cycles and 16 slow controls,
bounded stop/resume failures, all six reset-signature mismatches, invalid
CRC/data/guards, preexisting bus faults, foreign ownership and quarantine.
Assertions forbid SCI MMIO and bus callbacks while the module is stopped.
A hostile enabled SCR signature is shut down before IRQs are unmasked.

The actual JSON formatter fits 1,512 bytes per stage and 4,360 total with
full-width integer values and 40-character names. Buffers are now 2,048/6,144
bytes to retain margin. The production reset-row formatter passes strict
compilation with 128-trial and signature-failure examples. The failure-screen
preview was rendered and visually inspected. Full host and Dreamcast CI remain
pending.
The next console action is one SCI async probe and its JSON/full result photo.
If restart is required, photograph and reboot. No new soak, game timing or
boot CD is needed for this experiment.
