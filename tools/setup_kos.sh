#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
KUI_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$KUI_ROOT"
python3 tools/fetch_deps.py
python3 - <<'PY'
import json
from pathlib import Path
root = Path.cwd()
lock = json.loads((root / 'dependencies.json').read_text())
kos = root / '.deps/kos'
chain = kos / 'utils/kos-chain'
config = (chain / 'Makefile.dreamcast.cfg').read_text()
for old, new in {
    'toolchain_path=/opt/toolchains/dc/sh-elf': f'toolchain_path={root}/.deps/sh-elf',
    'enable_cpp=1': 'enable_cpp=0',
    'enable_objc=1': 'enable_objc=0',
    'enable_objcpp=1': 'enable_objcpp=0',
}.items():
    assert old in config, old
    config = config.replace(old, new)
(chain / 'Makefile.cfg').write_text(config)
environment = (kos / 'doc/environ.sh.sample').read_text()
environment = environment.replace('/opt/toolchains/dc/kos', str(kos))
environment = environment.replace('/opt/toolchains/dc/sh-elf', str(root / '.deps/sh-elf'))
environment = environment.replace('/opt/toolchains/dc/bin', str(root / '.deps/bin'))
(kos / 'environ.sh').write_text(environment)
PY
if [[ ! -x .deps/sh-elf/bin/sh-elf-gcc || ! -f .deps/sh-elf/.kui-toolchain-complete ]]; then
    make -C .deps/kos/utils/kos-chain makejobs="${KUI_JOBS:-4}"
    touch .deps/sh-elf/.kui-toolchain-complete
fi
# KOS's upstream environment does not promise compatibility with nounset.
set +u
source .deps/kos/environ.sh
set -u
make -C "$KOS_BASE" -j"${KUI_JOBS:-4}"
