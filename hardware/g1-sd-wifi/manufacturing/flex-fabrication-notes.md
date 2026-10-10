# Split-arm flex fabrication notes, Rev A prototype

Status: native routing complete; prototype assembly, physical continuity, installed route, rail budget, signal integrity and supplier production-file approval are not complete. Separate passive flexible PCBs connect to the carrier's J50/J51 ZIF sockets. This is not integrated rigid-flex and there is no flex component assembly.

| Item | Prototype CAD specification |
| --- | --- |
| Arms | A/BIOS-facing and B/board-edge-facing, each 20 conductors |
| Materials | Two-layer polyimide, 25 µm inner PI, nominal 0.12 mm covered base thickness |
| Copper | 0.5 oz per side; 0.15 mm signal traces, 0.25 mm return traces, 0.28 mm optional rail trunks |
| Finish | ENIG |
| Coverlay | Yellow; F.Mask and B.Mask represent the specified coverlay openings |
| Vias | Regular 0.30 mm drill / 0.55 mm copper diameter, located outside the bend region |
| Stiffener | Top-side 0.20 mm PI only at the mating tail; `User.1` is a filled stiffener polygon |
| Finished tail | Target **0.30 ± 0.05 mm at the exposed mating fingers**, including PI stiffener and adhesive; supplier must confirm |
| Tail fingers | Bottom copper, 20 × 0.35 mm wide at 0.50 mm pitch; 3.50 mm exposed contact length |
| Tail outline | 10.50 mm width over the 5.00 mm stiffened mating region; supplier should meet Hirose's ±0.07 mm tail-width requirement |
| Copper clearance | At least 0.30 mm from ordinary outline; 0.20 mm at mating gold-finger tips only |
| Landing pads | 1.40 × 0.44 mm, 1.00 mm nominal row pitch; plated via joins upper solder-heating pad to lower contact pad |
| Bend region | Local Y24..51.5 mm; no vias/pads there; minimum 1.20 mm static bend radius; no sharp crease |
| Assembly | Hand-solder the two motherboard row landings; no exposed +12 V pads; baseline load power uses direct fused harness |

The base thickness cannot be combined arbitrarily with copper weight. A 0.11 mm base with two 1 oz copper layers and the specified coverlay is inconsistent with that nominal stackup; the routed prototype uses 0.12 mm / 0.5 oz instead. The uncovered gold-finger thickness differs from the covered flexible base, so adding 0.12 + 0.20 mm alone does not prove the connector's mating thickness. The manufacturer must confirm the finished, exposed finger stackup is inside 0.25..0.35 mm. The Hirose drawing also specifies a stiffener of at least 0.188 mm; the selected 0.20 mm PI satisfies that nominal minimum.

The row coverlay apertures have 0.29 mm pad expansion in CAD. Adjacent 1 mm-pitch windows overlap into common openings, preserving a full coverlay web across the deliberately omitted A17 and B2 positions. The mating-finger apertures have 0.10 mm expansion and overlap into a common opening. KiCad's explicit `allow_soldermask_bridges_in_footprints` setting represents these intentional openings. There is no global disabled soldermask check and no ignored copper/short/unconnected checks. JLC's usual 0.10 mm expansion would leave only 0.36 mm coverlay between 0.44 mm row pads, below its 0.50 mm minimum; the design deliberately does not depend on such narrow webs surviving processing.

Supplier preparation must put the top PI stiffener on its own Gerber, named for example `pit_0.20.gbr`, with material, side and thickness specified. The current native `User.1` layer carries the filled stiffener outline; `Dwgs.User` holds explanatory text outside the manufactured outline and is not a physical layer. Carrier sockets must stay **bottom contact**. Do not substitute a top-contact FH12A socket, reverse a contact face, omit the stiffener, or substitute a standard straight FFC whose motherboard termination cannot match the custom solder pads.

Before a fabrication release, review the coverlay openings and stiffener in the supplier's production files; confirm regular via acceptance, gold-finger finished thickness and requested tail-width tolerance. Fabrication DRC cannot verify CN503's physical net mapping, the flush motherboard solder joint, installed routing past the AV connector, or high-speed G1 bus behavior. No further pre-reassembly motherboard measurements are requested by this draft; these are prototype qualification items.

Primary specifications checked 2026-10-10:

- [Hirose FH12-20S-0.5SH(55)](https://www.hirose.com/en/product/p/CL0586-0524-9-55): connector dimensions, bottom contact, current rating and 0.30 mm mating thickness. The linked series catalog page 10 provides mating FPC dimensions.
- [JLCPCB flexible PCB capabilities](https://jlcpcb.com/capabilities/flex-pcb-capabilities): stackups, regular vias, coverlay, edge clearance, stiffeners and bending.
