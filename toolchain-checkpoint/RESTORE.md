# Pinned local Dreamcast compiler/runtime checkpoint

This is a private intermediate build cache, not a console install package.
It contains no game images, SD profiles, card identifiers, VMU saves, or project
source files. It preserves the installed C-only SH toolchain and already-built
KOS runtime libraries/required host utilities; GCC build trees are excluded.

Versions: GCC 15.2.0, binutils 2.45.1, newlib 4.6.0.20260123;
KOS fcfa7d869471591ca1c777543261a7bfea7cb726.

To restore into a project checkout:

1. Verify the archive SHA256 against its separately retained checkpoint proof.
2. Ensure `.deps/kos` is a clean git checkout at the exact KOS commit above.
   Fetch/check out that pinned upstream commit if the checkout is missing.
3. Extract the archive into the project root. It overlays `.deps/sh-elf`, KOS
   `lib/dreamcast`, `addons/lib/dreamcast`, and bin2c/genromfs/scramble binaries.
   Do not overlay an unrelated or locally modified dependency installation.
4. Recreate `.deps/kos/environ.sh` from `doc/environ.sh.sample`, replacing
   `/opt/toolchains/dc/kos` with the absolute `.deps/kos` path,
   `/opt/toolchains/dc/sh-elf` with the absolute `.deps/sh-elf` path, and
   `/opt/toolchains/dc/bin` with the absolute `.deps/bin` path.
5. Recreate `.deps/sh-elf/sh-elf/include/kos` as a symlink to the restored
   absolute `.deps/kos/include/kos` path. The archived symlink references the
   original workspace, so this step is necessary after relocation.
6. Source `.deps/kos/environ.sh`; confirm `sh-elf-gcc -dumpfullversion` prints
   `15.2.0` and compile/link a simple `#include <kos.h>` main with `kos-cc`.

Do not run the full compiler setup script merely to validate this cache: doing
so may rebuild otherwise complete KOS libraries from newly checked-out source
mtime values. A complete compiler marker is included in `.deps/sh-elf`.
The complete file/hash manifest records each archived regular file and symlink.
