# Raw G1 probe access, Rev A

The frontend component contract now includes bare probe pads for all **28 raw G1 ATA lines**, the actual buffer enables/direction and their FPGA requests, plus three local ground returns. This closes the dedicated debug-pad requirement in [Claude's pinned review](https://github.com/TPMJB/K-UI-NeXT/blob/b207dd659565826ffd757ccf7abe1998f63c9202/docs/g1-bridge-review-2026-10-09.md). The existing FPGA pin allocation and buffer topology are unchanged.

These are fabricated copper, **not purchased SMT parts or fitted headers**. Each uses `RawProbe_Pad_D1.0mm_NoPaste`, `PCB_TEST_PAD`, `in_bom=false`, `on_board=true`, `populate=true` and zero added component height. The project-defined pad has copper/mask only, no paste, silk or fitted-part courtyard, and an explicit per-footprint missing-courtyard allowance; copper clearance still applies. The source JSON includes each exact existing endpoint under `probe_access`. Regenerating the native schematic and PCB must carry these references and nets into the final design; final placement/routing qualification remains part of the carrier review.

| Probe | Net | Existing endpoint | Local return |
|---|---|---|---|
| TP100 | `ATA_DD0` | U35 pin 2 | TP136 |
| TP101 | `ATA_DD1` | U35 pin 3 | TP136 |
| TP102 | `ATA_DD2` | U35 pin 5 | TP136 |
| TP103 | `ATA_DD3` | U35 pin 6 | TP136 |
| TP104 | `ATA_DD4` | U35 pin 8 | TP136 |
| TP105 | `ATA_DD5` | U35 pin 9 | TP136 |
| TP106 | `ATA_DD6` | U35 pin 11 | TP136 |
| TP107 | `ATA_DD7` | U35 pin 12 | TP136 |
| TP108 | `ATA_DD8` | U35 pin 13 | TP136 |
| TP109 | `ATA_DD9` | U35 pin 14 | TP136 |
| TP110 | `ATA_DD10` | U35 pin 16 | TP136 |
| TP111 | `ATA_DD11` | U35 pin 17 | TP136 |
| TP112 | `ATA_DD12` | U35 pin 19 | TP136 |
| TP113 | `ATA_DD13` | U35 pin 20 | TP136 |
| TP114 | `ATA_DD14` | U35 pin 22 | TP136 |
| TP115 | `ATA_DD15` | U35 pin 23 | TP136 |
| TP116 | `ATA_DA0` | U36 pin 2 | TP137 |
| TP117 | `ATA_DA1` | U36 pin 4 | TP137 |
| TP118 | `ATA_DA2` | U36 pin 6 | TP137 |
| TP119 | `ATA_CS0n` | U36 pin 8 | TP137 |
| TP120 | `ATA_CS1n` | U36 pin 11 | TP137 |
| TP121 | `ATA_DIORn` | U36 pin 13 | TP137 |
| TP122 | `ATA_DIOWn` | U36 pin 15 | TP137 |
| TP123 | `ATA_RESETn` | U37 pin 2 | TP137 |
| TP124 | `ATA_DMACKn` | U36 pin 17 | TP137 |
| TP125 | `ATA_DMARQ` | U39 pin 4 | TP138 |
| TP126 | `ATA_IORDY` | U70 pin 4 | TP138 |
| TP127 | `ATA_INTRQ` | U38 pin 4 | TP138 |
| TP128 | `DATA_DIR` | U35 pin 1 | TP136 |
| TP129 | `INTRQ_OEn` | U38 pin 1 | TP138 |
| TP130 | `DMARQ_OEn` | U39 pin 1 | TP138 |
| TP131 | `IORDY_RELEASE` | U70 pin 2 | TP138 |
| TP132 | `REQ_DATA_OEn` | U32 pin 2 | TP138 |
| TP133 | `REQ_INTRQ_OEn` | U72 pin 2 | TP138 |
| TP134 | `REQ_DMARQ_OEn` | U73 pin 2 | TP138 |
| TP135 | `REQ_IORDY_LOW` | U33 pin 2 | TP138 |
| TP136 | `GND` | U35 pin 10 | GND plane |
| TP137 | `GND` | U36 pin 10 | GND plane |
| TP138 | `GND` | U38 pin 3 | GND plane |

`TP22` already probes `BUS_SAFE`; `TP23` already probes the final `DATA_OEn`. The extra direction and enable pads distinguish FPGA requests from hardware-gated ownership. `INTRQ_OEn`, `DMARQ_OEn`, `DATA_OEn` and their request signals enable when low. `DATA_DIR=1` selects device-to-host data; `DATA_DIR=0` selects host-to-device data. `REQ_IORDY_LOW=1` requests a low ready line. There is no separate `IORDY_OEn` net in this implementation: the actual U70 open-drain gate is `IORDY_RELEASE`, where high releases the line and low pulls it down.

Place the data pads at U35's **host-side** ATA pads, the raw controls at U36/U37 inputs, and the raw response lines at U38/U39/U70 outputs. Probe points on the FPGA-side `BUS_*` nets do not replace raw G1 access. Keep the added branch from the existing route to each probe at or below the **3 mm layout target**, preferably as an inline enlargement or immediate endpoint tap. Stagger the 1 mm probe pads near the 0.5 mm TSSOP pin pitch to satisfy copper clearance; route a nearby tap rather than a long centralized debug strip. Each local ground pad needs a short connection/via to the continuous ground plane. Dense pads may use bare bottom copper without adding bottom-side assembly. Bottom probes require removing the carrier for bench access. Insulate exposed underside copper for installation; probe access and loading must be checked on the actual routed board.

The 3 mm value is a routing target, not signal-integrity qualification. The final native-layout review must measure added stub length, check clearance, confirm all 28 raw nets are exposed, and verify the three ground returns. Short probe returns and input-only probing are required during bring-up; attaching a cable or analyzer changes bus loading and must be accounted for in timing measurements. No permanent header or debug cable is part of the installed assembly.
