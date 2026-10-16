# G1 hardware preview

These are native KiCad 10.0.7 raytraced renders of the current G1 microSD / Wi-Fi carrier design. They are suitable for the 2.0.0 press kit as a preview of future hardware work. The board is in development and is not included in the K-UI 2.0.0 software release. A working board is the tentative milestone for 3.0; no hardware release date is promised.

## Images

| File | Use | Resolution |
| --- | --- | --- |
| [g1-board-hardware-preview.png](g1-board-hardware-preview.png) | Ready-to-post hardware image with a neutral background and floor shadows | 2384 × 2384 |
| [g1-board-hero-transparent.png](g1-board-hero-transparent.png) | Same isometric view with transparency for a press layout | 2384 × 2384 |
| [g1-board-top-transparent.png](g1-board-top-transparent.png) | Orthographic top view | 1488 × 2776 |
| [g1-board-bottom-transparent.png](g1-board-bottom-transparent.png) | Orthographic underside view | 1488 × 2776 |

Suggested short caption:

> In development: the K-UI G1 microSD / Wi-Fi carrier, shown in a native CAD render. The design aims to retain the original GD-ROM; hardware and firmware bring-up are still ahead.

Suggested longer caption:

> A preview of the K-UI G1 microSD / Wi-Fi carrier, designed to retain the original GD-ROM. This is an actual CAD render of the current 59 × 115 mm, six-layer design, with its BIOS cutout and CN503 clearance notch. Optional BIOS circuitry is provisioned in the design. The board is in development; a working prototype is the tentative milestone for K-UI 3.0.

Alt text:

> Green, irregularly shaped Dreamcast G1 carrier PCB in an isometric CAD view, with fine-pitch ICs, small passive components, two flex connectors, a rectangular BIOS opening and a long connector clearance notch.

## Source and rendering scope

- Repository: [TPMJB/K-UI-NeXT](https://github.com/TPMJB/K-UI-NeXT).
- Hardware branch: `design/g1-sd-wifi-20261009`.
- Source revision: [`fedc5703e2501e035caf8928d80c055729202617`](https://github.com/TPMJB/K-UI-NeXT/tree/fedc5703e2501e035caf8928d80c055729202617/hardware/g1-sd-wifi).
- Native PCB: `hardware/g1-sd-wifi/controller/KUI-G1-Bridge-RevA.kicad_pcb`.
- PCB SHA-256: `8c891b8b5a62dc336a8c3707fa846e28394412a2acb061c2c4e02a207ac74ed8`.
- Native renderer: KiCad 10.0.7, high quality, source stackup colors. Green solder mask and white silkscreen match the draft ordering settings.
- Board geometry, copper, vias, pads, cutouts and component placements come directly from the native PCB. No AI-generated geometry or component placement was used. These renders do not change the hardware source or its routing.
- The project has 40 DNP footprints. The default native population/display was used; optional BIOS/reserve parts remain unpopulated, while their PCB pads and markings remain visible.
- These are partial assembly visualizations. The C5 module, compression contacts and retention fixture are not modeled. The microSD socket (J201), 50 MHz oscillator (X10), custom regulators/inductors and PSRAM also lack resolved component bodies in these images; their actual pads and footprint markings remain visible. The renders do not represent a complete assembled product or verify fit.
- Standard component models: [KiCad packages3D 10.0.6](https://gitlab.com/kicad/libraries/kicad-packages3D/-/tree/10.0.6), revision `e62ed1fc7862da83f789bd562671b5e4b82afcdf`.
- The RP2350B body uses Raspberry Pi's exact `rpi_rp2350b.step` embedded in its [official RP2350B Minimal Viable Board design](https://pip.raspberrypi.com/documents/RP-010329-CA), R4-S1 public release. Its zero-offset, unit-scale native transform matches this PCB's RP2350B model transform. Extracted STEP SHA-256: `f034c7c03de1a8fa0eaccae24b2b0a115df5c4ae63c99487a257506f97fa2b1b`.

The 8 MiB PSRAM provision is bridge/controller working memory, not an expansion of the Dreamcast SH-4's main RAM. These images make no claim about tested G1 performance or game compatibility.
