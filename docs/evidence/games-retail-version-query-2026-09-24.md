# DOA2 reaches the reader: missing version query, 2026-09-24

## Hardware result

The owner photograph of build `289e10a1ab20` shows `GD REQUEST REJECTED`,
GD function `00000000`, command `00000028`, and zero LBA/count/destination,
SD result and SD blocks. This stop is inside resident C dispatch: the prior
startup stack-guard collision no longer prevents entry to the reader.
Command `0x28` is decimal 40, GET_VERS, absent from our supported commands.
Zero SD reads are expected at this point; this is not an SD transfer error.

## Contract and exact caller

DreamShell ISO Loader `syscalls.c` (`get_params_count`, `get_ver_str`) accepts
one destination parameter and writes 28 bytes: the 27 ASCII bytes
`GDC Version 1.10 1999-03-31`, then state byte `0x02`, without a terminator.
Its transfer-byte counter remains zero. Primary independent cross-check:
[inolen/redream, bios/syscalls.c, GDC_GET_VER](https://github.com/inolen/redream/blob/master/src/guest/bios/syscalls.c).
That implementation also writes 28 bytes, overwrites the final byte with
`0x02`, and does not record a disc-transfer size for this command.

In the already supplied DOA2 `T3601N V1.100 U` executable, wrapper
`0x8c1227de` creates `[destination, 0]` and requests command 40. Only the first
word is part of the command contract. Destination is `0x8c2fb7a8`. On success,
startup consumes the ASCII version digits at offsets 12, 14 and 15. The next
separately used global begins 32 bytes later. The next visible operation is
one 2048-byte sector read at FAD 45166 (LBA 45016), the ISO volume descriptor,
through the existing PIO/DMA command paths. This trace does not establish
compatibility with every later game operation.

## Change

The independent retail service accepts GET_VERS, validates all 28 destination
bytes, and writes the compatibility response only during EXEC. The byte
buffer need not be word aligned. REQUEST and CHECK perform no storage I/O.
Completion reports success and zero disc-transfer bytes; the normal one-time
CHECK acknowledgement remains in effect. This is virtual driver metadata,
not a query to the installed physical drive. Game files are not modified.

The focused retail-GD test adds exact byte/canary checks, P0/P1/P2 aliases,
end-of-RAM boundaries, one-word parameter bounds, invalid destinations,
execution-time mapping failure, and absence of storage I/O. The existing
reader test passed with address/undefined-behavior sanitizers. Leak detection
was disabled after this container's ptrace restriction prevented LeakSanitizer
from completing; the address and undefined-behavior checks remained enabled.
No broad suite is requested. One native build will enforce the existing
resident limit and conservative stack budget before issuing the SD update.

Use the same boot CD and dump: Games -> DOA2 -> Y -> A. Photograph the final
screen or report the furthest point reached. The selected-image probe remains
accepted and does not need repeating.
