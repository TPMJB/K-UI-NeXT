# Explicit recovery binding from checked saved data

Ordinary capture and ordinary Resume keep their original optical identity.
This option belongs only to separate-copy repair of a normal raw capture or
checkpointed partial capture whose exact original optical samples have become
unreadable. It defaults off. A readable but different original digest is a
wrong-disc result, and never enables this fallback. In-place repair does not
use this alternate binding because its original source bytes change during
valid patches.

The new binding is deliberately reported as **saved-anchor recovery binding**,
not as a match of the original capture identity. A normal partial checkpoint
contains an opaque aggregate identity, progress and hashes; it does not contain
the original complete TOC. Decoding that checkpoint with its stored identity
validates its syntax, record CRC and proposed prefix ranges, not its optical
identity or historical track layout. The independent repair path must validate
any saved descriptor/layout available and create a new explicit recovery
binding before writing output.

Admission first reopens and hashes every admitted original prefix twice,
comparing each pass with its original recorded CRC32 and SHA-256 when present.
CRC-only jobs retain their recorded CRC evidence; no SHA-256 evidence is
invented. Source file aliases, truncation, hash disagreement, unstable reads,
I/O failures and cancellation stop admission before a new recovery binding.
Uncommitted tails are outside the trusted prefixes.

Any readable original start/middle/end sample inside those trusted prefixes is
compared with saved bytes. A valid disagreement immediately rejects the disc;
unreadable or invalid returned sectors do not authorize different data. The
worker then searches bounded candidates spanning the complete admitted ranges
for two distinct matching saved/optical anchors. GD anchors must be valid
high-density data. Single-session CDs prefer valid data and can use audio;
audio requires paired exact reads, nonconstant samples and different anchor
payload hashes. Silent/repeated audio cannot supply two identity anchors.
This usefulness rule applies only when selecting new anchors. Known original
fixed samples, including silence, are still compared with every readable
optical observation; a changed silent sample is a disagreement. Exact receipt
rechecks also compare the recorded digest before classifying audio usefulness.
Mode 2 Form 1 uses the CD-specific structural validator. No generic ECC
reconstruction or audio-offset inference is involved.

The immutable 124-byte `KUISID1` receipt records the profile, exact two FADs,
their raw SHA-256 values, a source binding and a CRC32. The source binding
includes the normalized folder, opaque original capture identity, exact newly
admitted plan, original names and expected prefix sizes/hashes. It can be
stored inside the reserved bytes 384–507 of the checked 512-byte repair header,
before any copied-track or patch writes. The header's CRC covers it as well.

Resume and safe drive reinitialization recheck **those same anchors**. An
unreadable, changed, corrupt or mismatched receipt is refused; the worker must
not silently search for replacement anchors. Existing ordinary repair headers
and headers carrying an alternate receipt are different transaction classes.
The encoded receipt participates in the new repair job's source fingerprint,
so work cannot move between those identities unnoticed.

The helper's FAT32/exFAT image tests cover damaged fixed endpoints, partial
prefixes, saved CRC/SHA corruption, unstable source reads, valid wrong-disc
samples, exact-anchor changes/unreadability, cancellation/fatal reads, CRC-only
sources, CD Mode 1/Mode 2/audio, and silent/repeated audio refusal. They assert
zero source writes. Integrated repair tests separately cover header
publication and Resume behavior.
