# G1 bridge Rev A: review disposition

Updated 9 October 2026. Responds to [Claude's original review](https://github.com/TPMJB/K-UI-NeXT/blob/b2aa7ab76eefba30c6a959c24e101ba9ad6f5121/docs/g1-bridge-review-2026-10-09.md) of proposal commit `89f6dc6`. That review remains on Claude's branch; this document records the resulting requirements on the design branch.

The review supports the architecture and logical pin allocation. It does not establish an electrically complete design, timing closure or fabrication readiness. No native circuit, firmware or PCB was changed in this documentation pass. Shield measurements, connector access and console validation remain pending.

## Accepted requirements

| Review items | Disposition |
| --- | --- |
| CN503 mapping; PIO windows; SPI1; consecutive SD data pins | Retain the allocation. Compatibility of pin placement does not prove an implementation. |
| B1, B2: isolation and physical access | Keep independent power/reset isolation and qualify both connector rows or alternative tap points by continuity. Add accessible test points in the next circuit/layout pass. |
| B3, B4: Holly timing, IORDY, retained-drive behavior | Require measured evidence before choosing supported transfer modes or concluding that device 1 cannot contend with the Samsung drive. |
| H1, H3, M3, M4: ATA ownership, IRQ, reset and registers | Snoop selection/control writes, obey shared-line rules and real reset, serialize optical/bridge transfers, and restore device 0 when releasing the bus. |
| H4, H5: supply, height and RF | Prefer a separately regulated 5 V supply path as the candidate to evaluate; retain alternatives until current, temperature and fit are known. Keep the socketed C5 target and external antenna; changing to soldered/off-board mounting needs a fit decision. |
| M6: write gates, flush ordering and recovery | Retain read-only playback and explicit write sessions, ordered completion, verified file commits, ext4-port validation and reserve-power investigation. |
| L1-L4; BIOS scope | Complete the connector reference with unused contacts, leave digital audio unused, keep bench headers off the production board, and make physical BIOS selection independent of the MCU. Two ROMs need mutually exclusive chip enables. |

## Corrections before implementation

**The CF rig does not exist yet.** The [earlier CF design](../../cf-board/README.md) explicitly says it is not made or tried. Claude's suggested measurement session therefore needs a qualified fixture first. An interposer or buffered bench tap may serve that purpose; manufacturing the old CF PCB is not automatically required for this new board.

**PIO output ownership (M2).** A GPIO selects one PIO output source through FUNCSEL. PIO0 register replies and PIO1 data streaming cannot both drive DD0-15 under a fixed mux. Use one output-owning block with helper logic, or prove an isolated mux handoff. Its 10-15-clock lookup estimate is not a worst-case bus response guarantee. Include synchronization, capture/decode, FIFO/DMA arbitration, memory, output and buffer delays under competing traffic. Prove write capture against the host's hold interval; keep asynchronous strobe synchronizers enabled by default. [RP2350 datasheet, §§9, 11.5.6.3, 12.6](https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008373-DS-2-rp2350-datasheet.pdf?disposition=inline).

**Separate register and data timing (B3).** ATA PIO mode 0 has a 290 ns register strobe minimum and a 165 ns 16-bit data strobe minimum. Both require 50 ns read setup and 5 ns hold. These are standard requirements, not measurements of Holly's timing codes. Capture both cycle types. [ATA/ATAPI-5, T13/1321D revision 3, Tables 48-49](https://www.seagate.com/support/disc/manuals/ata/d1153r17.pdf).

**Preserve the final DMA word (M1/H3).** A direct DIOR-only enable must satisfy hold and release timing. Do not gate DMA output with instantaneous DMARQ: when terminating a burst, the device withdraws DMARQ during the final strobe while that word still transfers. Use latched transfer ownership through that word and the required hold interval, with release bounded after DMACK negation. Reconcile this with any between-strobe predrive. [ATA/ATAPI-5, Table 50 and §10.2.3.3](https://www.seagate.com/support/disc/manuals/ata/d1153r17.pdf).

**IORDY test (B3).** Do not directly clamp a shared console line with a GPIO. First establish its voltage and retained-drive ownership; use an isolated, buffered fixture with controlled timing and release. Begin with passive capture. Digital traces alone do not establish freedom from analog contention.

**Power rationale (B1/H4).** RP2350 FT pads permit 3.3 V input with IOVDD absent; 5 V tolerance requires power. This narrows B1's damage rationale for verified VA1 levels. Retain buffers for independent bus control. Ioff at zero volts does not qualify brownout: review OE bias, power-good thresholds and sequencing. Check C5 SPI/IRQ/READY backfeed while its switched supply is off, as well as console/USB isolation. [RP2350 §14.9](https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008373-DS-2-rp2350-datasheet.pdf?disposition=inline); [TI SN74LVC16245A §§7.5-7.6](https://www.ti.com/lit/ds/symlink/sn74lvc16245a.pdf).

**Silent activation (H2).** Invisible-until-unlock is a candidate custom protocol, not ordinary ATA discovery. K-UI would need to unlock before standard IDENTIFY probing, and re-unlock after reset. Keep firmware activation/IRQ permission separate from the actual ATA nIEN register state. Specify this contract before implementation.

**Durability (M6).** Retain the hold-up provision for evaluation. Qualified energy can help a card finish; it cannot guarantee arbitrary-card internal recovery or save host RAM. ATA completion/FLUSH must propagate ordering through the complete SD path. Optional card cache remains disabled until its explicit flush behavior is supported and qualified; host queue empty alone is insufficient. [SD Association cache description](https://www.sdcard.org/wp-content/uploads/2020/11/Mobile_Device_Innovations_20170227.pdf). Random cuts are an initial campaign, supplemented by targeted data/metadata, flush, rename, journal/replay and brownout cuts. Check completed-file hashes and absence of writes during playback. Test results apply to the actual card and implementation, not all microSD cards.

## Revised bring-up order

1. Obtain the two shield-clearance measurements and inspect both CN503 rows. Map any underside alternative by continuity with power disconnected.
2. Finish isolation and power circuitry, output ownership, register behavior and cycle budgets. Qualify a fixture and its probe/load behavior. Use a host-side model to exercise reset, selection, reads, stalls and release before connecting a responder.
3. Capture baseline console/drive behavior passively. Qualify controlled IORDY and device-1 tests on the fixture. Prove identification and hashed PIO reads with the GD-ROM retained before enabling DMA.
4. Qualify DMA, writes, ordered flush/recovery and power cuts, then integrate C5 transport. The provisional highest mode and sustained speed follow evidence.

The owner's pending measurements concern the **installed upper shield**, not just the GD-ROM: minimum IC501-top-to-shield and nearby PCB-to-shield clearance across the proposed footprint. They remain the next physical input; no new requirement to own a CF rig or logic analyzer is implied by this review.

## Reference completion and validation boundary

`CN503-reference.csv` now lists all 50 contacts. The ten added entries are unused by Rev A, including A25/B25 +12 V. Audio/ground entries follow the [iceGDROM riser circuit](https://github.com/zeldin/iceGDROM/blob/master/pcb/riser/riser.sch) and its [connector symbol](https://github.com/zeldin/iceGDROM/blob/master/pcb/riser/Molex_52602_0579.lib). They are not new attachment points. Actual board orientation, voltages and continuity remain installation checks.

Validation in this pass covers document links, complete/unique connector contacts and preservation of the existing 28 logical ATA assignments. No ERC, DRC, console timing, RF, thermal or power-cut result was produced.
