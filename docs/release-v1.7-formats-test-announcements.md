# Experimental Games disc formats test

K-UI has a development build with direct Games support for supported GDI, ISO,
BIN/CUE, CDI and standalone BIN/IMG layouts. A computer importer handles
CSO/ZSO and CHD; compressed decoding is not part of the resident game reader.
The Original/2048 picker remains available, and Windows CE defaults to the
background SCI reader.

This is a hardware test, not a final release or a measured compatibility/speed
claim. CDDA playback, subchannel emulation and several other layouts remain
unsupported. Back up KUI, preserve original images, and install this build's
matching runtime and Games payloads together. Keep the compatible bootstrap.

See [installation and test instructions](release-v1.7-formats-test.md) and
[layout/import limits](games-formats-test.md). Report build ID, title/region,
image/version, boot encoding, storage connection, reader and FMV/audio results.
