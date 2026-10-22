# Armada CE Maple discovery

This is the first console discovery test for a Windows CE virtual VMU adapter.
It identifies ROM-backed Maple module records and samples five fixed register
values using the established, guarded GD call boundary. It does not advertise
virtual A2, intercept Maple, read DMA lists or save payloads, or write guest
memory or hardware registers. Its only new writes are its own bounded report
and workspace. The ordinary game reader still operates as before.

## Install and test

1. With the console off, back up the SD's `KUI/apps/games/ce-probe.kui` to your PC.
2. Merge the ZIP's `KUI` folder onto the SD. Only `ce-probe.kui` is replaced.
3. Cold boot and select **Original Armada**, using the original raw GDI supplied
   for analysis. In launch options use **Background loading On, CD music Off**.
   Music Off selects this diagnostic; the separate music module is unchanged.
4. Stay at the main menu for about 30 seconds, then play for about one minute.
   A controller in port A is sufficient. A physical VMU can remain in A1;
   leave physical A2 empty. No save or virtual-card import test is needed.
5. Press **A+B+X+Y+Start** and send one clear photograph of the whole report,
   including BUILD and the bottom FAULT row. The screen stays until power off.
   Mention whether controls, effects and game loading behaved normally.

Restore the backed-up `ce-probe.kui` and cold boot after the test to resume the
previous ordinary CE loader. The K-UI runtime, Skies module, CD-music modules,
and existing virtual cards are not installed or changed by this ZIP.

## Report

All values are hexadecimal. Counts refer to discovery samples, not Maple
requests, reads, writes or save acknowledgements.

| Row | Five values, left to right |
| --- | --- |
| SAMPLE OK REFUSED READS STATUS | Attempted samples; stable samples including a missing module; refused samples; reads in last sample; last status |
| TTB PTEH CCR PROCESS HEAD | CPU translation root; PTEH; cache control; current PROCESS address; kernel module-list head |
| MAPLE NODE BASE SPAN SECTIONS ATTACH | Maple module node; explicit-slot virtual base; image span; section count; attachment mask |
| MAPLEDEV NODE BASE SPAN SECTIONS ATTACH | Same metadata for Mapledev |
| MDSTAR MDTSEL MDEN MDST ISTNRM | Five separate register observations, not an atomic DMA transaction |
| FAULT WHY AT VALUE EXPECT DETAIL | Status/reason; address; observed value; expected value; field or node index |

Status `1` means both exact ROM module records were observed consistently.
Status `2` is a consistent list with at least one missing ROM module; external
DLLs are not identified from their shape or a guessed name pointer. Other
statuses retain the refusal witness. Zero registers can mean the sample stopped
at an earlier CPU-context check. A register value of zero is otherwise valid.
No result grants ownership, a callable address, or permission to install a hook.

Sampling occurs only after a positively accepted GD PLAY/PLAY2 request, with a
32-attempt boot-lifetime cap. Each attempt walks at most 64 nodes and performs
at most 1,024 word reads. Repeating every field makes 50 nodes the largest
complete stable sample within this budget; longer lists report a bounded
budget or list refusal. Context, bounds, roots, and all traversed node fields
are repeated. Reporting after return only reads our authored snapshot.
Repeated values do not establish a lifetime lease or exclude an ABA change.

## Findings from the owner's startup bundle

The full Armada kernel matches SHA256
`a7628c78090fce2c93012d5e4db6142096e138dceb7ebbf555142e902ca03c1f`.
The relocated external Maple and Mapledev text matches the complete ROM text,
including padding. This establishes common RVAs and code identity; it does not
establish which copy executes or its live physical backing.

| Proven source fact | Consequence |
| --- | --- |
| The kernel configures MDTSEL=1 and MDEN=1. Main-loop writes target mapped +0x88, not MDST +0x18. | The existing native software-start hook cannot be transplanted. An idle MDST observation cannot exclude a later VBlank start. |
| Maple uses SYSINTR18; CE masks and rearms ASIC bits 12/13. | Completion must retain CE's interrupt lifecycle. |
| ROMHDR assigns `0x8cfd0000..0x8d000000` to OEM driver globals. | The native VMU worker's RAM placement cannot be reused. |
| The packet builder uses the same ordinary Maple descriptor format. | The protocol and durable storage backend remain reusable after a CE bridge is established. |
| Completion RVA `0x5b0c` starts with six register pushes. | This is a static passive-hook candidate; live mapping, entry context and installation are still unproved. |

`tools/armada_ce_maple_audit.py` reproduces the private input identities,
complete text relocation comparison, ROM module geometry, interrupt-copy
provenance, and named contract hashes. It cross-checks the probe's production
ROM identities against independently parsed owner metadata. It emits metadata
and hashes, never executable bytes. The shipped evidence contains no private
kernel, game executable, driver, save or audio payload.

## Isolation and validation

The controller occupies the already guarded low binding window
`0x8c004000..0x8c007c00`, under the existing firmware-vector preflight and
bootstrap survival checks. Its static node workspace stays off the private GD
stack. It introduces no large CE allocation and makes no change to ROMHDR.
The stage retains exact whole-kernel SHA, IP, raw-layout and CE-IRQ admission.
The tested SCI DMA resume correction remains enabled.

Host ASan/UBSan checks cover malformed pointers and contexts, every read failure
and metadata-change position, ROM/external distinction, cycles, list/read
budgets, sample caps, and report-only entry with inaccessible read fixtures.
Stack-checker tests require the new read callback's frame, reject unknown
callbacks, and retain legacy callback handling. Private-input mutation tests
reject changes to each owner file and every production ROM identity constant.
The native build checks placement, embedded byte identity, integer instructions,
actual dispatch/report callers and the complete retained stack graph.

The default CE stage, ordinary CE background resident and native resident have
identical preprocessed C before and after this isolated change. Existing CE
reader, SCI stream, load-planner, audio-observer and admission tests pass.
Console behavior of the new probe remains to be tested.
