# Shared loader and VMU regression gates

The optimizations belong in the common filesystem reader, fresh card-admission
reader and history session. Their correctness and work limits must be checked
there. Game-specific save eligibility and SDK compatibility remain separate
from whether a shared path batches its reads.

## Temporary storage ownership

A virtual-card loan is a scheduling condition, not a disc error. Audit every
guest API that can arrive during a loan, including status, initialization,
callback registration and cancellation; request submission alone is not
enough. A driver's handling of CHECK busy does not establish that it tolerates
the same result from DRIVE or a dropped callback registration.

For admitted ordinary-thread callers, wait using that owner's real scheduler
and original stack, then retry the actual operation after ownership returns.
Retain arguments, mode, completion and output-buffer semantics. Interrupt or
unknown callers require their independently verified nonblocking contract;
never fabricate a successful operation or call a guessed scheduler address.

The VC2 regression must execute the old loan-busy path through the original
DRIVE error branch, then the corrected linked wait/resume path. Keep this
regression when altering shared ownership or adding another SDK profile.
See [the control-call race](vc2-gd-loan-control-race.md).

## Filesystem image reads

`python3 tests/test_game_image_batch.py` executes the production
`kui_game_image_read()` with independent source-byte and callback-count oracles.
It covers ten physical-layout/output combinations, seven request sizes and five
container labels on canonical opened maps. The container labels demonstrate
that batching has no format-name whitelist; the existing `test-game-image`
suite separately validates the actual GDI, ISO, CUE, CDI, RAW and ZSO parsers.

| Axis | Required cases |
| --- | --- |
| Raw data | Mode 1 and Mode 2 Form 1 at 2352 and 2448 bytes |
| Other layouts | Cooked 2048, Mode 2/2336, raw audio, main-channel extraction from 2448 |
| Size | 1, 2, 15, 16, 17, 33 and 257 sectors |
| Boundaries | Nonzero file offsets, subrequests, separate files and shared-file track boundaries |
| Faults | Allocation failure, poisoned I/O, cancellation and a bad header in a later window |
| Lifetime | Nested multi-sector reads use independent workspaces; every heap allocation is released |

The bounded path permits at most 16 physical sectors per callback and reads
exactly the requested span within one track. A 257-sector raw-to-user request
requires 17 callbacks instead of 257. A one-sector call and allocation-failure
fallback keep the previous path. Cooked data and directly usable raw data keep
their existing contiguous reads.

These are filesystem callback counts, not physical SD-command counts or console
timings. The reader checks raw sync/mode and duplicated XA/Form 1 subheaders
for every sector. Whole executable SHA/CRC checks remain the caller's work;
batching does not establish EDC validation or durable save behavior.

## Representative shared matrix

| Layer | Representative gate | Contract retained |
| --- | --- | --- |
| Container parsing | `test-game-image`, `test-game-image-workspace` | Bounded maps, format errors, cancellation, allocation failure and nested opens |
| Physical image mapping | `test-retail-image` | Contiguous/fragmented extents, file offsets, cooked/raw/Mode 2 layouts, cache invalidation and exact stream bounds |
| Resident services | `test-retail-cursor`, `test-retail-gd`, `test-retail-gd-ce` | Whole sectors and CE page-sized pieces, terminal errors and caller-visible completion |
| Compressed images | `test-retail-zso`, `test-retail-lzo` | Codec errors, source changes, short reads and poisoned-cache refusal |
| Fresh filesystem admission | FAT32/exFAT cache and adversarial admission fixtures | Current ownership scan, independent FAT copies, crosslink refusal, verified prefixes and fresh epochs |
| Save durability | Persistent-session, durable-worker and loan fixtures | Journal ordering, readback, SYNC acknowledgement and ownership return before publication |
| Successive cold stages | Successive-card and successive-admission fixtures | Same card and unchanged prepared profile; fresh current data hashes and no live backend reuse |

The normal native image, native physical-disc and supported WinCE routes need
their own existing linked ownership/stack checks when rebuilt. A common read
optimization is not evidence that every game has been run or that an unknown
SDK family is supported. The matrix protects shared behavior without creating
a new per-title performance policy.

## Installed cohort gate

`tools/check_shared_loader_performance.py` requires the exact Standard SCI,
compact Background A2/A1 and physical-disc A2/A1 builds, plus the compiled UI
runtime. SCIF and the matching normal Smash outer/child pair are optional
additional cohorts. The final package runs with clean source and matching
compile receipts; each module, embedded stage, helper ELF and selected
virtual-card slot is hash-bound to the report. The UI must carry the generated
admission manifest for that exact Standard build.

The two combined compact modules serve both New/X and Legacy/Y at the existing
`retail-native-dual[-a1].kui` paths. All five linked component owners must agree
on 72 KiB worker / 284 KiB reservation / 192 KiB cache and the exact virtual
slot. The standalone Standard 72 KiB worker remains a different profile. Larger
General/session-control modules and mixed marker/lease combinations are
refused by the normal release gate. The package installs exactly eight
functional files, with no larger optional Background payload. Both reader
engines and the physical/virtual VMU toggle remain covered by the compact
family's complete audit.

Every common stage must select its tested seven-argument replay and extent
wrappers in the actual C callgraph and linked caller literals. Their instruction
models check cache restoration, complete output publication and mutations. The
Smash child selects its established zero-argument replay instead; its separate
3,840-case/21-mutation proof is required alongside the inherited common extent
proof. Linking an unused generic replay wrapper cannot satisfy this check.

The gate also executes linked byte-table CRC and fresh FAT-window models. It
runs the shared image-reader, launcher-checksum and history tests; existing full
checksum/history reports may be reused only after every recorded dependency
hash is checked. These include the real FAT32 and exFAT regressions. Existing
full per-profile ownership, stack, protocol and original-SDK audits remain
mandatory. The performance gate does not claim measured console speed.

## Focused next-test qualification

The correction test records its reduced scope explicitly. It authenticates the
complete compact gameplay worker against an independently rebuilt pre-shared
source reference, and binds unchanged Standard, disc and Smash components to
the previously completed 945 delivery reports. Whole initialized loads,
geometry, symbols, BSS ownership and the sole permitted display-label change
are compared; the larger Background workers from that delivery are excluded.
The current entry/stage embedding chains, relocation tables, canonical profiles,
paired slots and clean source receipts are still checked.

Changed cold work receives current linked cache/FAT/CRC models plus the focused
cold-load ABI, relay, loader behavior and stack driver. Its report must match
all current outer/child artifacts and every source dependency. Historical
persistent/original-corpus/complete GIN suites are labelled as not reexecuted;
their old reports remain under `baseline-945`, not current success claims.
The focused archive contains one eight-file `KUI` install folder and no held
screen or larger optional Background payload.

## Continuing shared integration requirements

The eight-file cohort above describes its historical test scope, not complete
global installed coverage. Use `global_loader_rollout.py` for its explicit
ordinary/CE/CDDA role inventory and qualify collection children separately.
Source flags or a rebuilt GUI do not update cold stages embedded in omitted
modules. Bind current Main parity, compile receipts, actual GUI admission and
embedded/physical equality to the final delivered files.

Collection checks cover cold and after-return entry, current card data and
pending completion, not only first boot. Typed prepared-helper transport
variants retain the exact immutable helper prefix but have their own complete
hashes. Check static admission and fresh RAM sealing independently, reject
stale/mixed carriers, and preserve capsule reuse when only its declared runtime
window differs. VMU Tools entry remains usable without unrelated SD I/O;
settings load before an unknown value can be written. These shared gates
protect the next supported title without claiming universal qualification.
## Return callback provenance

A private return callback's argument is not necessarily a firmware command.
Swirl passes0 to its MAIGO callback; forwarding it as BIOS function0 rebooted
the console. Qualify the actual original frontend wrapper, callback selection,
arguments and caller address before routing a return. A test that enters the
new controller directly cannot establish that frontend contract. Keep genuine
unrelated firmware commands on their original route.

## After-return startup evidence

Separate a first launch after power-on from a launch after another child has
returned. Record the exact sequence before attributing a startup failure to
card rebinding or a particular device. A white display after a company logo
does not identify the renderer, card or reader as the cause.

Check both the transported card authority and every new owner's initialization
of its private mutable state. Linked receipt transitions and zeroed helper/
reader state are necessary bounded checks; they do not establish real cache,
interrupt or device behavior. Compare a suspected omitted cleanup with the
original return **and next startup**. The next startup may already perform it.
An independently valid cleanup defect must not be advertised as a diagnosed
game fix without evidence connecting it to the observed failure.

## Bootstrap code carried across a guest lifetime

Admission of a bootstrap reader does not prove that its discarded slot stays
immutable during a game. Keep any future return dependency in the return
controller's own declared lease. Authenticate the complete captured bytes
against launch-sealed identity, validate that copy while the guest runs, then
restore and authenticate the execution address only after old producers and
dirty aliases have retired. Never refresh the expected checksum from changed
guest memory, ignore padding, or execute an unchecked repair. Apply the same
lifetime rule to shared loader integrations; it is not a title-specific CRC
exception. Preservation and console qualification remain separate checks.
