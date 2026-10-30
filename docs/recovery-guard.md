# Saved-read consistency and recovery quarantine

The independent ripper now retains the useful integrity checks from our
DS-based adaptation without changing its drive-command ownership rules.

Normal acquisition checks the requested Mode 1 sector address as well as its
framing and EDC. Full P/Q parity remains the separate Strict setting. Mode 2
keeps its existing supported framing/EDC path. Capture computes a CRC before
and after each storage write; if RAM changes during the call, that entire
chunk remains uncommitted. Both normal and Quick Resume discard the affected
tail and read it again. A capture-specific slicing-by-four CRC implementation
offsets the extra computation; console throughput needs measurement on the
new candidate rather than inference from host CPU timings.

Advanced CRC independently reopens and compares every saved chunk, including
audio. For up to 64 structural suspects it additionally retains the initial
bytes and two independently reopened observations, their CRCs, and byte
difference counts. Binary records use the existing `KUIRDB1` evidence format.
Samples and the report remain `.part` until successful sync, close and
publication. The original track/checkpoint/descriptor/manifest files remain
read-only. This scan deliberately performs more saved reads than the earlier
single-pass scanner; it does not add optical rereads to healthy capture.

Any disagreement between saved-file observations creates a job-local
`.kui-readback-unstable` quarantine. Its checksummed 512-byte record binds the
canonical folder and the filesystem serial/immutable geometry. Mutable exFAT
dirty/usage fields do not change that identity. An interrupted `.part` marker
also blocks continuation. Normal Resume, Quick Resume, Verify and existing
salvage continuation refuse a quarantined job. Optical read failures and
ordinary disagreements between optical candidates do not create this marker.

For ordinary capture jobs admitted by Advanced CRC, run it on the same saved
job and allow the full consistent repeat-read to finish to clear quarantine.
Unfinished salvage jobs currently have no supported consistency scan to clear
their quarantine; the ordinary scanner does not validate salvage journal and
candidate evidence. Preserve those jobs and markers for diagnosis.
Cancellation, a partial scan or a few matching suspect samples cannot clear
quarantine. A valid marker for a different folder/volume or a corrupt published
marker is refused. The explicit repair
worker can also clear a quarantine after two complete, matching source
fingerprints under its validated layout/identity contract. For a partial normal
capture, only its trusted committed prefix is used; its uncommitted tail is
never evidence. Full read consistency does not itself mean that corrupt bytes
have been repaired or that any independent catalogue matches.

Reports live under `/KUI/recovery/scan-NNNN.txt`, with bounded raw samples in
`scan-NNNN.samples.bin`. The report records structural problems, saved hashes,
storage disagreement and catalogue results separately. A stable but damaged
dump can therefore proceed to explicit repair without being called correct.

Host regression coverage uses the actual FatFs implementation on FAT32 and
exFAT. It injects unstable data/audio reads, cancellation, report failures,
torn/foreign/corrupt guards, source-buffer mutation and torn capture writes.
Original file bytes and committed progress are checked independently.
