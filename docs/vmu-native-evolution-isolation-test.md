# Evolution: isolate reservation and VMU hooks

On `046252128e06`, the owner again saw a black screen before the opening
movie, with the microSD reader light off. No allocation-failure diagnostic
appeared. That observation does not prove that no read was attempted, or
that every other allocation succeeded.

This package adds a report for a previously silent disc-buffer refusal and
a reservation-only comparison. It is an investigation build; Evolution
compatibility is not established yet.

## 1. Normal virtual VMU, with mapping-refusal report

Extract into a fresh folder and run from that folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1
```

Use the same GDI, SCI/Standard, Auto encoding and controller setup. Keep CDDA,
background loading and RAM patches off. Do not use `--control` or
`--profile-runtime`.

Let Evolution reach the opening movie. Photograph any diagnostic. The new
mapping-refusal route enters the existing terminal page, normally showing
`DETAIL 00000100`. That page does not contain the refused buffer address.
Other existing terminal paths can also produce that detail, so retain the
whole screen and the point at which it appeared.

The loader already reported refused READ destinations. A refused CHECK
status buffer could instead return failure without a page. The new route
stops at the failed memory-map guard before SD I/O or a guest-buffer write.
It retains the same mapping predicate and existing validated terminal path.

## 2. Same reservation, without a virtual VMU

Install the comparison mode:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1 --lease-only
```

Confirm the reservation-only banner during startup. Test only whether the
opening movie plays and the game reaches its title/menu. There is no virtual
card in this mode; it cannot perform in-game virtual-VMU saves. The normal
card preparation/recovery still runs before the game. The personalizer
updates the module without changing the prepared card files.

The mode keeps the same owner allocation, `00030000`-byte reservation and
initialized worker layout. It restores the exact original heap-initializer
pointer and installs no Maple, free, reinitializer or exit hooks. Resident
worker entry and SD-transaction pointers are zeroed before the game starts.
The disc-reader reservation guard stays active. A later heap reset cannot
leave a callable worker behind; the remaining reservation guard is deliberately
conservative. This mode is for diagnosis, not ordinary play with a virtual card.

Record whether both modes show the movie, stay black, or produce a page.
If the reservation-only movie works, the active hooks/runtime become the next
focus. If it fails too, reservation placement and the protected-buffer check
remain candidates. Neither result alone identifies a complete fix.

Re-run the first command to return to normal virtual-VMU mode.

## Scope and validation

The earlier exact-revision allocation-failure diagnostic remains in normal
mode. The reservation-only mode leaves that game code unchanged.

The new failure route reuses the existing terminal guard and private-stack
gateway within the existing resident memory budget. There is no new low-memory
callback or expanded writable mapping. Host tests exercise the actual mapping
body for accepted and refused ranges and retain legacy behavior. The release
packager repeats compiled-artifact, relocation, stack and mutation audits.

This batch adds no new SDK family, virtual slot, transport, CDDA coexistence
or physical-disc support.
