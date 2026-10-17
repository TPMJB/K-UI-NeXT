# Supplier confirmations before prototype orders

Use the current BOM, CPL, native artwork and order-settings files together.
They remain unapproved until the recorded design conditions close. Catalog IDs
identify parts; they do not reserve stock or authorize substitution.

| Order | Confirmation to obtain |
|---|---|
| Rigid carrier | Six copper layers, 0.8 mm nominal (±0.1 mm), 1 oz outer / 0.5 oz inner, ENIG, 0.127 mm track and space; accept actual drill sizes in the plotted files. Use the supplier standard stack of this thickness. |
| Rigid outline | 59 × 115 mm, BIOS opening and CN503 notch; preview milling radius and confirm it clears the actual console parts. |
| Via treatment | Confirm QFN thermal/via-in-pad fill/cap/tenting and stencil process with the assembler; an optional advertised free process is not automatically selected or approved. |
| Assembly | BASE BOM/CPL: 269 SMT placements, 55 types; confirm RC0402FR-07220RL (C112291, R2044–R2048) in the JLC parts library. Preview exact rotations and pin 1, including FPC sockets, RP2350, FPGA, TVS, fuses and springs. C5 is a separate manual installation. |
| Factory fixtures | Harwin S7221-45R C22445132 and Abracon ASE-50.000MHZ-LC-T C596955 have catalog fixture notices. Obtain the factory process/fixture agreement and cost. This does not provide the installed C5 clamp. |
| Protection parts | D21 must be High Diode SMF5.0CA C22458622; F21 must be Littelfuse 0467.500NR C206990. Manufacturer-recommended custom lands are supplied. Confirm solder/stencil suitability; review any substitution. |
| Flex A and B | Separate passive two-layer flex orders, 0.12 mm nominal base, 0.5 oz copper, ENIG, top PI tail stiffener, bottom exposed contacts. Confirm finished tail thickness 0.30 ±0.05 mm, width 10.5 ±0.07 mm, coverlay windows and stiffener drawing. |

CN503 A/B continuity and actual carrier/support fit are owner checks, not
supplier confirmations. Do not order either flex arm before the row proof.
The installed C5 support must maintain its 0.90 mm gap and resist at least
6.24 N of spring reaction without board bow. Current fault screening and
protection coordination remain engineering conditions, separate from these
manufacturing-process confirmations.
