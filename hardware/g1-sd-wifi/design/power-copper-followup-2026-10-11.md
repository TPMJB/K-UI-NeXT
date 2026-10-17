# Supply-copper follow-up — 11 October 2026

Native DRC and the normal-current copper screen pass. The selected fault-current screen remains open: **138 failed track/via entries**, rather than 138 independent fault cases. These results do not prove physical fault overheating; they apply the unchanged steady-current IPC fit to a selected 2.5-A peak-current envelope.

The frozen board SHA-256 is `8c891b8b5a62dc336a8c3707fa846e28394412a2acb061c2c4e02a207ac74ed8`. `power-copper-check.json` identified that exact board; since 17 October it identifies the RP-JTAG board, with results unchanged apart from the board hash (no supply copper was touched). KiCad 10.0.7 native DRC with all severities, schematic parity and refilled zones reports **0 violations, 0 unconnected items and 0 parity issues** in `carrier-final-drc.json`.

## What changed

- The checker models separate via terminals on each copper layer and finite resistance between layers. It solves actual barrel currents, including the sum of load branches and unequal parallel paths, instead of taking the largest incident track current and dividing among merged vias.
- Filled plane islands are separate ideal electrical hubs. Logic-rail capacitor/IC/connector/test-point faults now include each local plane-to-pad stub; source-to-plane feed alone cannot qualify these paths. Every pad must be fed through modeled copper or stored filled plane geometry.
- Both TPS62162 outputs use a **2.5-A engineering screen**, rounding up the [TI TPS62160-family Rev E](https://www.ti.com/lit/ds/symlink/tps62160.pdf) 2.45-A maximum static high-side limit at VIN = 12 V, TJ = 25 °C. The typical dynamic comparator delay is 30 ns; actual inductor-current ramp can overshoot. This selected envelope is not a guaranteed maximum over all operating conditions.
- D21/F20/F21 received the manufacturer-recommended custom footprint pad geometry and graphics in place. Source provenance is `sources/input-protection-footprints.json`. Their positions, orientation, identity and pin nets were preserved. Four 1.00-mm FUSED_5V segments were trimmed to 0.95 mm to clear the larger F20 pad.
- One bounded width pass retained 1,298 widened logic segments and eight widened storage segments. One bounded via candidate tried 0.5-mm drill / 0.8-mm diameter on 92 fault-screened vias; native DRC rejected all 92 and their original geometry was restored. There was no reroute or temperature-limit relaxation.

## Screening result

The unchanged limits are 10 °C normal rise and 45 °C fault rise, with a factor of two only for via barrels and pad-neck runs no longer than 2 mm.

| Check | Result |
|---|---|
| Original routed board under corrected selected-current screen | 240 failed track/via entries |
| Follow-up normal-current screen | Pass on every selected rail |
| Worst normal long-track rise | 7.1 °C; limit 10 °C |
| Worst normal pad-neck rise | 5.4 °C; limit 20 °C |
| Worst normal via rise | 11.8 °C; limit 20 °C |
| Remaining storage fault-screen failures | 34 track entries, five vias |
| Remaining logic fault-screen failures | 12 track entries, 87 vias; 119 local fault cases evaluated |
| Selected fault screen / protection coordination | Fail / not qualified |

Exact geometry and UUIDs for each remaining entry are in `power-copper-check.json`. Baseline and bounded candidate evidence are `power-copper-before-followup.json`, `power-bounded-widths.json` and `power-bounded-vias.json`.

## Qualification still needed

The steady-current fit is deliberately stringent when applied to a peak envelope. Closing the fault item requires the converter's worst-case short-circuit waveform, including peak, RMS/current duty and duration, to be checked against copper transient survival and the unchanged thermal criteria. If that evidence cannot establish acceptable survival, the remaining fine-pitch escapes and vias need a later layout change with suitable parallel paths or wider copper. A passing normal-current screen does not waive this item.

Fuse cases are explicitly nameplate-current screens. Fuse ratings do not clamp current, and actual source current, time-current/clearing I²t and copper short-pulse survival remain unqualified. F20/F21 do not protect their own upstream entry copper. The documented 5-V tap-end fuse and console 3.3-V entry-short behavior require separate source/harness coordination.

Filled planes remain ideal hubs in this calculation; plane constrictions, ground return, installed thermal conditions and the actual supplier stack/plating need review. The report keeps `protection_coordination_qualified: false` and `pass: false`.

## Verification and reproduction

Four regression circuits pass in native KiCad Python, including a real in-memory filled-plane/local-capacitor-stub fixture. The other circuits verify branch-summed via current, unequal parallel-via sharing and a capacitor fault isolated to its own stub. `power-native-preservation-check.json` verifies all 12,322 routed track/via UUIDs, route coordinates/layers/net names and via geometry are retained, and all 351 footprint UUIDs/placements/orientations/pin nets are retained.

```sh
/usr/bin/python3.12 tools/test_power_copper.py
OPENBLAS_NUM_THREADS=1 /usr/bin/python3.12 tools/check_power_copper.py --report design/power-copper-check.json
kicad-cli pcb drc --format json --severity-all --schematic-parity --refill-zones -o design/carrier-final-drc.json controller/KUI-G1-Bridge-RevA.kicad_pcb
```

The checker intentionally exits 1 while selected fault entries fail. `update_native_power_footprints.py` matches the executed in-place method: it uses `RemoveNative` and new native graphics objects, avoiding the Python `Remove` ownership flip. Re-running it is unnecessary for this frozen board.
