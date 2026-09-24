# Original K-UI startup

startup.png is the unmodified 640x480 preview of TPMJB's approved K-UI artwork
from [K-UI_DS resources/boot-preview.png](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/resources/boot-preview.png).
Git blob: 33b49462795c7db399c1a5e9af7b9b96b1378140.

The user requested reuse of this original design and startup sound. The host
encoder converts it to RGB565, preserving its layout, without importing the
DreamShell renderer. It synthesizes the same three-note startup composition
and timbre documented in that revision's utils/build_boot_assets.py. Its
original stereo channels were identical; the new output preserves one channel
at 44.1 kHz. The quiet decay is shortened to end by 2.62 seconds and the original
long silent tail is removed; total PCM duration is 2.65 seconds. No sampled
third-party sound or DreamShell audio player is copied.

The splash is part of the SD runtime, so updating it needs no new boot disc.
It is shown only at startup, can be skipped with B, and adds no drawing work
during capture. The chime has a 2.7-second budget including audio setup; its
player drains audio before preference/card/drive work. The shell separately
limits splash display to three seconds so slow preference loading cannot hold
the splash on screen.
