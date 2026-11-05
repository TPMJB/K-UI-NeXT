Frozen delivered engine2 updater: negative regression fixture only

These exact C files and headers came from GUI5300782c62f6. They preserve the
broken software-read routing as a regression witness. They must never be
used to build a package producer, validator, root GUI or selected test GUI.

The engine-revision test compiles this implementation separately from current
Main. It shows that physical saved settings become invisible after a FULL
bank handoff, then verifies current engine3 refuses that code and preserves
real settings read/write behavior. SH payload instructions are not executed.

Only updater code and its generic CRC/FatFs adapter are frozen here. Unchanged
settings callers and common dependencies are linked from Main after byte-hash
checks against provenance.json. The legacy GUI fast-CRC macro is explicitly
disabled for this host regression, so no old loader code is imported.
