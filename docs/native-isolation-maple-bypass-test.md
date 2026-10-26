<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution Maple callback control @BUILD@

The previous snapshot recovered eight valid original Evolution wait contexts:
all eight had the expected getter, mask and register arguments. The game was
still waiting for scanline zero, with video interrupts and scan status advancing.
That result did not establish an interrupt-timing fault.

This diagnostic keeps New demand reads, SDK SCI delivery, the 192 KiB reserved
cache arena and the 384 KiB bottom reservation. Prefetch and the virtual VMU
remain off. It omits only the two calls into Maple submit/completion C code.
The assembly register saves, interrupt masking, private-stack switch, restore,
original submit replay and original SDK reply consumer remain in place.

Maple C callbacks are **OFF**; heap, reclaim and SDK coherence guards remain.
The bypass also skips the per-poll C lease, DMA, context and packet checks.
This is a diagnostic comparison, not a normal configuration or an established fix.

## Test once

Extract this archive into a fresh folder and run there:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

The installer accepts the verified 5fa97, 3f51 and 995 isolation sessions and
keeps their original pre-isolation loader backups. No card preparation,
formatting or backup deletion is needed.

Safely eject and cold boot. Use the same Evolution image and settings:
**SCI / Background reader New (X) / Auto encoding / CD music Off / RAM patches Off**.
Keep the controller/physical VMU setup unchanged, with physical A2 empty.
Confirm the startup banner identifies **MAPLE C CALLBACKS OFF**, New demand
reading, prefetch off and virtual VMU off.

A missing-memory-card warning is expected. Check whether it passes the same
black-screen transition and plays the opening FMV; report the last working point.
No button gesture, new diagnostic photos or repeated Standard run is requested.
The archive retains the Standard/A2 control that already passed.

## Restore

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

This restores the exact original two loader files using the retained initial
backup, including an originally absent file. The GUI, prepared profile, card,
journal, identity, launch settings and game images stay in place. Select New
(X) again for ordinary combined-reader testing.

A pass would implicate the omitted Maple C work. A failure would leave New's
demand/SCI integration and the retained assembly overhead among the open leads.
Neither result alone identifies a particular defective instruction.
