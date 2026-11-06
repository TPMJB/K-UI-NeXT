# CE return validation detail

The d04 console result was `RET CE000004 / 00000000` in VC2. Its initial
validation preceded retirement and intentionally collapsed every predicate
to detail0. Build3337 retained every check and passed the failure mask as
detail. A later report of the same zero detail while testing3337 remains
unexplained. It is not evidence by itself that an older controller ran.

Trace revision1 changes only the reason family to `CE01xxxx`, and exports
the absolute ELF marker `__kui_smash_ce_return_trace_revision=1`. Initial
validation now displays `CE010004`; final validation displays `CE010007`.
The second line is the unchanged failure mask. This identifies the executing
controller trace independently of the GUI build label. Every integrity guard
and the existing failure/terminal behavior remain in place.

| Controller contract | Initial failure | Final failure | Detail |
| --- | --- | --- | --- |
| d04 boolean validation | `CE000004` | `CE000007` | Zero |
| 3337 failure masks, no revision marker | `CE000004` | `CE000007` | Nonzero failure mask |
| Trace revision1 | `CE010004` | `CE010007` | Same nonzero failure mask |

The complete linked failure-to-pixels replay checks the decision, stopped()
arguments, renderer arguments, all16 hexadecimal digits and both interlaced
fields. The supplied3337 code renders nonzero masks; the supplied d04 code
reproduces zero detail. These results bind the tested bytes, not the live
console's selected controller or physical display state. The zero observation
is unresolved; no check is bypassed or rewritten to explain it away.

| Detail bit | Failed check |
| --- | --- |
| `00000001` | Exact CE profile |
| `00000002` | Immutable helper length is41152 |
| `00000004` | Expected helper CRC is nonzero |
| `00000008` | Expected controller/runtime CRC is nonzero |
| `00000010` | Expected raw-reader CRC is nonzero |
| `00000020` | Sealed ROOT return seed is valid |
| `00000040` | ROOT context flag is3 |
| `00000080` | Original VID1 is valid |
| `00000100` | Live receipt CRC and exact owner |
| `00000200` | Receipt sequence can advance |
| `00000400` | Controller and complete runtime CRC |
| `00000800` | Complete512-byte authenticated raw-reader image CRC |
| `00001000` | Complete41152-byte immutable helper CRC |

Multiple failures OR their bits together. Invalid length fields never expand
the fixed41152-byte helper read. The linked test isolates every bit, repairs
it to zero and traps reads beyond that immutable span.

The eb6 console report CE010004/00000800 identifies the live8100 reader as
the sole failed domain. Its bootstrap lifetime was incorrectly extended
through gameplay. The02ec protected-copy experiment used B400..B600; the next
candidate removes that startup write and unproved saved-copy lifetime. Its
initialized controller carries the exact fresh reader's260-byte code prefix.
The build gate independently requires the fresh resident, raw file and source
asset to match the reviewed full512-byte SHA; the remaining252 bytes are zero.
Predicate11 validates all512 logical bytes against the launch-sealed CRC.
The prefix also belongs to the controller code CRC, so direct prefix damage
normally reports both checks, mask00000C00. Predicate-isolation tests reseal
only their fixture's code CRC to exercise bit800 separately.

After complete retirement,8100 is restored from that prefix plus zeros and
its entire actual512-byte CRC is checked before the first raw read. CE01000D
reports a failed destination check with the actual CRC as detail. The expected
CRC always remains the original launch-sealed value. Padding is never excluded,
changed live8100 bytes never supply trust, and no reader executes from the
controller's relocated data prefix.

The bounded return image shares one CRC implementation across its two public
entrypoints. Earlier images used a128-entry folded table. The startup-locked
candidate uses the complete bitwise IEEE polynomial instead, reclaiming that
512-byte table so the reader repair fits inside the exact accepted initialized
extent. Every byte, all13 predicates and incremental CRC semantics remain
checked against the same expected values. Golden-vector and changed-polynomial
execution tests qualify both implementations separately. This changes only
exit-time verification; ordinary loaders, readers, native workers and immutable
CE helpers retain their existing CRC implementations. No startup speedup or
measured return time is claimed from that space tradeoff.

Swirl's private argument0 at8300 is qualified against its original wrapper
caller419a6, callback cell and MAIGO flag. The original command3 fallback also
checks the copied thunk and its boot-vector side effect. Original-source
tests cover both routes, source-guard refusals and d04's37-instruction BIOS
misdispatch. These proofs do not establish physical return success.
