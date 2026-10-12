# Toy Commander DATA scheduling: static admission and test package

B, Y and Z are experimental exact-title readers for the original raw Toy
Commander image. B retains two-sector service with the new observer; Y limits
startup service to one sector per visit; Z uses the bounded SCI DATA engine
before the first accepted PLAY mailbox. See the
[hardware instructions](../toy-data-schedule-test.md) for B → Y → Z captures.
No hardware playback improvement or cause of the repeatable video crash has
been established for this batch.

## Exact delivered artifacts

| Profile | Build ID | Runtime SHA256 |
|---|---|---|
| B | `143d51bf7603` | `bb2d25eb99b3ee6b75db87b643239bce7ef755b3f6bd017ca5525a4ebf6e6a97` |
| Y | `143d51bf7601` | `0f0a362345c43ff35cfd8960728dce19f72b24b04f1e253b958458245eacf9a3` |
| Z | `143d51bf7602` | `f58f8dad7f51233f01088f68eb09fa78a7228aea3489ce8b62c12d691a77a1ef` |

Runtime build source was local commit
`143d51bf76dd23ab86a128ff30a492b4f5ad8bac`; the delivered source archive also
includes the admission/package tools at local commit
`64eafa35fa3879820eb20332dc684b73b3bca395`. Publication adds these evidence
files without changing the delivered runtimes.

`K-UI-Toy-DATA-Scheduling-Tests.zip` is 9,294,319 bytes, SHA256
`f34c202c560de4ed56ca451b2380694285d32ea9563bbc0bcc2fe6e41b8f8cde`.
It includes complete ELF/bin/map/stack evidence, report legends, source,
host logs and the exact R restoration. Every ZIP member checksum and ZIP CRC
passed. R SHA256 remains
`99e1c0ad84a916fa9e08cf68b494c2d0efe0748fe956e579841c3d7614e49037`.

## Linked admission

The separate scheduling gate admits all three immutable runtime hashes.
It checks retained reader/audio/installer instructions and literal targets,
owned objects, original cache/heap protections, exports, private stack bridges,
complete compiler stack evidence and actual emitted frames. Existing admission
gates were retained. `SHARED_SCI=0`, `ASYNC_CDDA=0`, `PRIVATE_P2=1`,
`NATIVE_CACHE=0`, `GD_FIXED_STEP=2` and `DATA_PAYLOAD_MODE=0` remain fixed;
the original CDDA algorithm is retained.

| Conservative bound, bytes | B/Y | Z | Available |
|---|---:|---:|---:|
| Low stack | 1,156 | 1,156 | 1,232 |
| GD stack | 1,940 | 2,792 | 4,000 |
| Audio stack | 4,092 | 4,944 | 8,096 |

These bounds sum compiled authored C frames and assembly allowances. They
exclude external game/SDK callee frames and are not measured runtime stack use.

Actual target frames were checked for 136 functions in B/Y and 185 in Z.
Z's dedicated 2 KiB IRQ stack, vector forwarding, guards and startup retirement
checks pass. Cleanup precedes accepted PLAY publication; rejected mailboxes
do not retire startup handling. Original audio transport stays on its retained
path. SCHED captures its engine state before terminal cleanup.

Machine-readable [B](toy-data-schedule-2026-10-11/B-linked-admission.json),
[Y](toy-data-schedule-2026-10-11/Y-linked-admission.json) and
[Z](toy-data-schedule-2026-10-11/Z-linked-admission.json) admissions and the
[package manifest](toy-data-schedule-2026-10-11/build.json) bind this evidence
to the supplied artifacts.

## Host verification and limits

The retained 38 Toy pilot suites, production TRACE/DATA differential suites,
one-sector scheduling fixtures, all three production SCHED report modes,
startup SCI/GD/wire fixtures and 23 linked audit tests passed.
[Logs](toy-data-schedule-2026-10-11/) include behavioral and admission mutations,
accepted/rejected PLAY, cancellation, failure, reset, read-only CRC mapping,
terminal CHECK consumption, rolling records and invalid/wrapped clock samples.
Startup fixtures ran with ASan/UBSan; LeakSanitizer was disabled because this
environment restricts the required process inspection.

The observer reads the first credited sector only and records its checksum cost.
It does not validate the remainder of the request. A timing/CRC comparison needs
matching requests and the same hardware playback point. Successful transport,
passing host fixtures and static admission do not prove Dreamcast playback
quality or uninterrupted CDDA on the new candidates.
