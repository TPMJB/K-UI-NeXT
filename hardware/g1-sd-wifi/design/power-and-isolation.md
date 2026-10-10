# Rev A power and G1 isolation circuit

Status: implemented pin/net circuit block for native EDA generation, not a released fabrication design. `blocks/power-bus.json` defines each fitted/DNP component and physical pin. The controller and FPGA blocks consume these nets. Part numbers are design selections, not JLCPCB stock promises. The remaining qualification items below stay open in the manufacturing release gate.

## Supplies and direct input

The bridge's primary supply is **a dedicated regulated console 5-V/GND wire pair at J20**. The 40-contact signal flex does not carry the board's combined MCU/FPGA/card/Wi-Fi supply. Its console 3.3-V conductors supply only the host-side translator/output-buffer rail. No 12-V rail is accepted. The local F20 1.5-A fuse and Q20 reverse-input MOSFET do not protect a mistaken 12-V connection.

J20 is a pair of low-profile solder pads, not a tall connector. Use a short twisted pair; 24–26 AWG stranded insulated wire is the initial harness assumption, with the exact motherboard/PSU tap and fastening documented before installation. A short wire, its source rail, fuse time-current behavior and measured peak load must be qualified together. The extra 5-V path replaces the inadequate current margin of two small flex contacts; it is not an invitation to select an unverified power point.

| Component | Circuit role | Pin contract |
|---|---|---|
| F20, 046701.5NR | Local 1.5-A fast fuse | 1 input `CONSOLE_5V`, 2 `FUSED_5V` |
| Q20, PMV48XP,215 | Reverse-input protection | G1=GND, S2=`PRIMARY_5V`, D3=`FUSED_5V` |
| U20, TPS2116DRLR | Retained-domain input mux | VIN1/3=`PRIMARY_5V`, VIN2/6=`RESERVE_5V`, VOUT/2,7=`+5V_HOLD`, MODE/5=PR1/4=GND |
| U21/U22, TPS62162DSGR | Independent 3.3-V logic/storage converters | VIN2=EN3=`+5V_HOLD`, SW7→L20/L21→output, VOS6 senses output, FB5=AGND4=PGND1=EP9=GND |
| U71, TPS22919DCKR | Default-off C5 input switch | IN1=`PRIMARY_5V`, OUT6=QOD5=`+5V_C5`, ON3=`C5_SWITCH_EN` |

U20 in automatic mode selects the higher input and blocks reverse current into an unselected source. There is no connection from the held rail back to the console 3.3-V supply. Both converters use TI's fixed 3.3-V part, two 22-µF nominal output capacitors, local input capacitors and a 2.2-µH XFL3012-222MEC inductor. FB is grounded, not left floating. VOS must be routed to the output capacitor through a quiet sense trace. Converter input/output hot loops and the exposed pad require a real PCB layout and thermal review.

Both converters are 1-A rated silicon; the circuit is **not** evidence that two continuous 1-A loads are possible inside this console. An initial power budget reserves up to 0.35 A on `+3V3_LOGIC`, 0.5 A on `+3V3_STORAGE`, and 0.6 A on the C5 primary supply during radio activity. At 90% converter efficiency this is roughly 0.63 A of 5-V input for logic/storage plus 0.6 A C5, before inrush and losses. These are engineering allowances rather than measured loads. A 0.7-A flex power path would not cover that combined case. The fuse, mux, wire and console source need load measurements; the firmware should turn C5 off when unused and reject bridge operation if the supply fails.

The C5 supply is not retained. U34 forms `C5_SWITCH_EN = PWR_FAILn AND C5_PWR_EN`, and a 100-kΩ ON pull-down keeps the switch off with controller logic absent. Quick output discharge empties the C5 supply after power-off. `+3V3_C5_IO` is **the C5 module's local 3.3-V output** for the controller block's translator supply. Do not connect it directly to `+3V3_LOGIC` or power it from the carrier. USB removal is the baseline. U71 does not claim reverse-current blocking; the controller block must isolate every SPI/UART/BOOT/EN route before restoring a separate source or programming cable.

## Loss detection, reset and reserve provision

All TPS3808 supervisors are powered from held `+3V3_LOGIC`. U23 monitors logic and drives `LOGIC_RESETn`; U24 monitors storage; U25 monitors the host's unheld `CONSOLE_3V3`. The G33 nominal threshold is 3.07 V. U26 uses TPS3808G01 and the 976-kΩ upper / 100-kΩ + 4.7-kΩ lower divider (R209, R210, R270) to monitor **`PRIMARY_5V` before the reserve mux**, nominal threshold:

`Vloss = 0.405 V × (1 + 976/104.7) = 4.180 V`.

The original 976/100 divider gave 4.358 V. `PRIMARY_5V` sits after the harness, F20 and Q20, whose drops at about 1.2 A total roughly 0.15–0.2 V; with a console 5 V rail at its low tolerance and GD-ROM spin-up sag, that left too little margin against false warnings during disc activity. The two bucks still regulate with about 3.6 V input, so 4.18 V costs no useful warning time. Measure the chosen 5 V tap during GD-ROM spin-up and C5 transmit with the bridge loaded, and adjust R270 if the bench shows otherwise.

Divider tolerance, the supervisor's specified threshold error, supply drop and hysteresis must be included in the bench acceptance band. CT is open on each supervisor, giving the documented nominal 20-ms release delay. Reset assertions follow the supervisor's actual response/pulse requirements rather than an assumed instantaneous detector. U23's output also connects to the RP2350 RUN net in the controller block. U26's low output is an interrupt to MCU/FPGA and a hardware disarm/C5 cut-off; it does **not** reset the held MCU, so the MCU can attempt a bounded stop/flush.

The reserve option is explicitly **DNP**: R202's 22-Ω charging limit, D20's Schottky diode and four 100-µF GRM32ER61A107ME20L 1210 capacitors. The 1-MΩ bleeder anchors VIN2 when that option is absent. Bulk capacitance already fitted to the normal rails is ordinary decoupling, not a guaranteed recovery source.

A useful upper-bound calculation illustrates the limit. If the four optional parts actually delivered 400 µF at 4.5 V, the converter could use energy only down to 3.6 V, and conversion were 90% efficient:

`Eload = 0.5 × 400 µF × (4.5² − 3.6²) × 0.90 ≈ 1.31 mJ`.

At a 1.32-W held-domain load, that is only about **0.99 ms**. At an illustrative 50% DC-bias/tolerance capacitance reduction it becomes about 0.50 ms. This is not a card-flush guarantee. The actual minimum capacitor energy, maximum controller/card load, loss-detection delay, mux transition, rail dropout and card busy time must be measured. Firmware must acknowledge durable writes only after the card's completion contract is met, and the prototype must pass repeated interruption tests. A larger reserve energy store or a different write policy remains necessary if those tests require it. No tall electrolytic or supercapacitor is placed under the 6-mm shield in this baseline.

## Hardware bus permit

The hardware gate is independent of FPGA firmware:

`PWR_GOOD = LOGIC_RESETn & STORAGE_PGOOD & HOST_OKn & PWR_FAILn`

`BUS_SAFE = PWR_GOOD & BRIDGE_ARM & FPGA_READY & FPGA_RESETn & BUS_RESETn & BIOS_RECOVERYn`

U27–U30 implement these AND terms with ordinary Ioff-capable LVC gates. `BRIDGE_ARM`, `FPGA_READY` and `FPGA_RESETn` have default-low pull-downs. `FPGA_READY` must be an FPGA user-mode output asserted only after internal reset and configuration validation; a configuration DONE pin by itself is not substituted for it. The MCU keeps `BRIDGE_ARM` low until it validates the FPGA/link. These choices avoid a circular dependency in which the FPGA waits for BUS_SAFE before asserting READY.

`BIOS_RECOVERYn` is the physical stock-recovery interlock from the BIOS block. The stock/no-BIOS baseline ties it high through the BIOS block's fitted bypass. The recovery position pulls it low and disables all bridge outputs even if MCU/FPGA are running. The original console ROM is handled by the BIOS block's independent CE bypass; this power block does not claim G1 can replace the stock ROM.

Any host reset, MCU-requested FPGA reset, physical recovery, upstream loss, failed rail or absent arm/READY disables the data/output buffers. Removing logic power also invokes the data translator's VCCA-zero isolation and Ioff outputs; removing console power invokes VCCB-zero isolation and host-powered output Ioff. Host output OEs have pull-ups to **CONSOLE_3V3**, not to a retained rail. Thus the host can be powered while the bridge is absent without a floating enable. Waveform qualification of slow ramps, rail ordering and brownout still remains mandatory.

## Data and nine host controls

U35 is SN74LVC16T245DGGR, with **A=bridge** (`BUS_DD0..15`, `+3V3_LOGIC`) and **B=host** (`ATA_DD0..15`, `CONSOLE_3V3`). VCCA pins 31/42 supply the control pins. VCCB pins 7/18 are the host supply. All four supply pins receive a nearby bypass capacitor.

| Signal | Meaning |
|---|---|
| `DATA_DIR=1` | A→B, device read drives host |
| `DATA_DIR=0` | B→A, host write enters bridge |
| `REQ_DATA_OEn=0` | FPGA requests active data transceiver |
| `DATA_OEn` | `REQ_DATA_OEn OR NOT BUS_SAFE`; both transceiver OEs share it |

REQ_DATA_OEn and the final DATA_OEn each have a pull-up to VCCA. DATA_DIR defaults low. Weak 47-kΩ data biases avoid floating receiver inputs during tri-state idle. Firmware/HDL must disable both transceiver OEs before changing direction and enforce the actual PIO/MWDMA setup/hold/turnaround times, including the final DMA word. The hardware safe gate cannot infer an incorrectly programmed ATA device owner.

There are **nine**, not eight, incoming controls: DA0–DA2, CS0n, CS1n, DIORn, DIOWn, DMACKn and RESETn. U36's eight LVC244 channels handle the first eight; U37's Schmitt LVC1G17 handles RESETn separately. Controls remain observable while outputs are disabled. RESETn defaults low if disconnected. These are host inputs only; no bridge output can reach a host control through these buffers.

## IRQ, DMA request and IORDY ownership

U38/U39 are host-powered SN74LVC1G125DBVR buffers. The FPGA supplies **both a value and a separate protocol ownership OE** for each shared line:

`INTRQ_OEn = REQ_INTRQ_OEn OR NOT BUS_SAFE`

`DMARQ_OEn = REQ_DMARQ_OEn OR NOT BUS_SAFE`

REQ_INTRQ_OEn and REQ_DMARQ_OEn default high. Both final OEs have host-domain pull-ups. These two extra FPGA outputs are required: qualifying IRQ/DMARQ only with BUS_SAFE would drive low onto the shared lines while the original optical device owns them. An assert-high-only shortcut also fails the ATA requirement to drive DMARQ high **and low** during a selected DMA transfer. The front end must release INTRQ when unselected or nIEN=1, release DMARQ when unselected/not doing DMA, and drive their states while selected/qualified.

U33/U31 invert the qualified `REQ_IORDY_LOW` request into U70's host-powered SN74LVC1G07 open-drain input. An unsafe or absent bridge releases IORDY; an active request only pulls it low. There is no extra on-board IORDY pull-up in this block because host termination must be established first. The front end asserts it only in a cycle it owns. Verify the Dreamcast pull-up and rise time before claiming a PIO mode.

## Parts, land patterns and qualification

Ordinary fitted devices are at most 1.45 mm tall in this block; the optional 1210 reserve capacitors have a 2.7-mm worst-case body thickness. The complete carrier/standoff/module/case stack still needs its prototype fit check within the measured 6-mm zone. No component above the CPU/GPU contact regions is permitted.

Four patterns are implemented in `controller/KUI_Footprints.pretty` and referenced through `KUI_Footprints:`. The two manual power pads have no paste and are excluded from the component placement/BOM. Three component patterns are custom to avoid substituting a similarly named package: `TI_DRL0008A` for the 0.5-mm-pitch TPS2116 land pattern, `TI_DSG0008A` for TPS62162's exposed-pad WSON, and `Coilcraft_XFL3012`. The DRL example has pad centers x=±0.74 mm, y=±0.75/±0.25 mm and 0.67×0.30-mm exposed pads; pin numbering is 1–4 down the left and 5–8 up the right. DSG0008A has a 0.9×1.6-mm copper/mask EP9 and two 0.9×0.7-mm stencil windows centered at y=±0.45 mm, giving 87.5% nominal exposed-pad paste coverage. The [current Coilcraft physical drawing](https://www.coilcraft.com/getattachment/02b6f684-78f7-41d5-8676-8d7ea7ead2c6/xfl3012d-%281%29.gif) calls for 1.00×2.90-mm lands at 2.03-mm centers; the start-lead dot corresponds to the right-hand pad1 in the native pattern, which the circuit connects to SW for lowest EMI. The older 2014 land pattern differs and is not used. KiCad 10.0.6 parsed all four files with electrical pad counts 8/9/2/2 matching the component pins; the two non-electrical DSG paste apertures are deliberately blank-number pads. An isolated footprint proof board passed native DRC with zero violations; this does not replace the complete board DRC. The exact mechanical drawings and mask/paste guidance must accompany generation and be visually inspected. The 0.65-mm-pitch generic TSOT-23-8 is **not** an acceptable substitute for DRL0008A.

Required first-article tests: input reverse polarity/current-limited start; all supply ramp/order combinations; Wi-Fi turn-off with host alive; zero supply on either data domain; FPGA erased/unconfigured; arm/READY stuck low; physical BIOS recovery; scope data/IRQ/DMARQ/IORDY during selecting the original GD-ROM and during final DMA word; rail ripple/inrush/thermal load; randomized power interruption during writes. Passing a netlist or DRC does not satisfy these tests.

Primary source links embedded in each component include [SN74LVC16T245](https://www.ti.com/lit/ds/symlink/sn74lvc16t245.pdf), [TPS62162](https://www.ti.com/lit/ds/symlink/tps62162.pdf), [TPS2116](https://www.ti.com/lit/ds/symlink/tps2116.pdf), [TPS3808](https://www.ti.com/lit/ds/symlink/tps3808.pdf), [TPS22919](https://www.ti.com/lit/ds/symlink/tps22919.pdf), and the TI LVC gate/buffer datasheets. The [ATA-3 working draft, Table 4](https://www.scs.stanford.edu/23wi-cs212/pintos/specs/ata-3-std.pdf) and [ATA/ATAPI-6 working draft](https://flint.cs.yale.edu/cs422/readings/hardware/ATA-d1410r3a.pdf) describe shared signal ownership. Dreamcast G1 behavior and the retained optical drive must additionally be validated on hardware.
