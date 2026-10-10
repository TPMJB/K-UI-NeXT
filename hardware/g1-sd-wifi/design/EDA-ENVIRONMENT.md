# Native CAD and logic toolchain

The development checks on 2026-10-10 use KiCad 10.0.6 (including its Python
`pcbnew` module and matching symbol/footprint packages), Icarus Verilog 12.0,
and Yosys 0.33 (`2584903a060`). KiCad 10 is required to read the existing
controller starter schematic, whose file format version is `20260306`.

On Ubuntu 24.04, use the official release instructions at
<https://www.kicad.org/download/details/ubuntu/> and the signed
`ppa:kicad/kicad-10.0-releases` repository. Its signing-key fingerprint,
checked against the Launchpad archive API, is
`FDA854F61C4D0D9572BB95E5245D5502FAD7A805`. Do not disable repository signature
verification. The library packages are `kicad-footprints` and `kicad-symbols`;
3D model packages are optional for electrical/board checks.

Run board generators using **system Python**, `/usr/bin/python3`, because
KiCad installs `pcbnew` at `/usr/lib/python3/dist-packages/pcbnew.py`.
The separate primary runtime's default `python3` does not automatically
include that path. Libraries are at `/usr/share/kicad/footprints` and
`/usr/share/kicad/symbols`. Generators that use `kiutils` require version
1.4.8 or a separately verified compatible version.

```sh
kicad-cli version
/usr/bin/python3 -c 'import pcbnew; print(pcbnew.Version())'
iverilog -V
yosys -V
```

For the managed development container, package installation required
`apt-get -o APT::Sandbox::User=root ...` to avoid the unavailable user-switch
operation. This does not disable signature checks. An inaccessible pre-existing
APT partial-download directory was repaired before installation. These
container-specific workarounds are unnecessary on an ordinary Ubuntu host.

Successful smoke checks: native controller starter schematic exported to a
KiCad XML netlist; legacy CF-board native PCB exported to SVG; `pcbnew` loaded
the installed 100-pad TQFP footprint. These check tool availability and native
file parsing only. They do not establish electrical correctness, routing,
mechanical fit, or readiness to fabricate the G1 bridge.

## Routing runtime

The official [Freerouting v2.5.0 release](https://github.com/freerouting/freerouting/releases/tag/v2.5.0)
JAR requires Java 25. The current development runtime uses the official
[Eclipse Temurin 25 JRE](https://adoptium.net/temurin/releases/), version
`25.0.4.1+1-LTS`. Downloaded archives were checked against SHA-256 values
published by the respective release APIs:

| Archive | SHA-256 |
| --- | --- |
| `freerouting-2.5.0.jar` | `f6f51bb02245e8e717f9359bd260cc9c5c0b1bc0acc8b7cb2cd5b8ffeb5de3c7` |
| Temurin Linux x64 HotSpot JRE `25.0.4.1_1` | `1731a34baadec5479258ea0202e4d5d865d2efeee60cb0c7d7eb056fe96ca219` |

Set `KUI_ROUTING_JAVA` to the Java 25 executable and `FREEROUTING` to the
verified JAR outside the repository. The system's default Java 17 remains
insufficient for this release. The headless JAR runs without an X server:

```sh
"$KUI_ROUTING_JAVA" -Djava.awt.headless=true -jar "$FREEROUTING" \
  -de input.dsn -do output.ses \
  --router.autorouter.max_passes=100 -da -dct 0 \
  --gui.enabled=false --api_server.enabled=false
```

`-da` disables anonymous analytics; the API-server flag disables the local
REST server. The deprecated `-mp` flag maps to the modern pass setting above.
Keep routing local. KiCad's `pcbnew.ExportSpecctraDSN` successfully exported
the legacy CF-board, and the JAR imported it and wrote a 55,948-byte SES file
in the runtime smoke test. This checks the export/router/session path only;
the smoke input was already routed and is not the new G1 carrier.

The official v2.5.0 standalone Linux CLI was also hash-verified and tried,
but failed on that DSN with a GraalVM serialization error for
`ComponentOutline`. Use the Java JAR for this development workflow.

The full carrier is considerably harder than the tool smoke test. An initial
bounded modern run expired before producing an SES. Its early pass scores and
incomplete counts regressed; those intermediate values do not establish a
scoring defect because the documented board-history restoration had not run.
The actual import and native DRC results, rather than the router's score, are
the acceptance evidence.

### Headless legacy core runner

The official [Freerouting v1.9.0 release](https://github.com/freerouting/freerouting/releases/tag/v1.9.0)
also exposes a headless board reader and public maze-router APIs. The Java 17
source adapter [Headless19Ripup.java](../tools/Headless19Ripup.java) calls those
unmodified APIs with bounded attempts, canonical trace-tail cleanup, and an
SES checkpoint after each pass. It keeps the checkpoint with the fewest
incomplete connections. It does not patch the routing algorithm or disable
native DRC. The old GUI entry point requires X11; the development sandbox
cannot open X11 sockets, so the adapter avoids that entry point.

The downloaded v1.9.0 JAR's locally recorded SHA-256 is
`9084a4888937a7f31f857ecc12aa7a37407f51160e4d2892dff9c9bb47ae3102`.
That older GitHub asset has no published digest; this is a reproducibility
checksum, not an independent upstream checksum verification. Keep the JAR
and compiled classes outside the repository:

```sh
javac -cp "$FREEROUTING19" -d "$KUI_ROUTING_CLASSES" \
  hardware/g1-sd-wifi/tools/Headless19Ripup.java
java -Djava.awt.headless=true \
  -cp "$FREEROUTING19:$KUI_ROUTING_CLASSES" Headless19Ripup \
  input.dsn output.ses 12 1200 1500 false
```

The final four arguments are maximum passes, total seconds, milliseconds per
connection attempt, and whether to fan out first. The dedicated `In1.Cu`
reference plane remains inactive for signal traces. Test routing on a fresh
CF-board DSN with no wiring reduced 44 incompletes to 13 over two bounded
passes and wrote importable checkpoints. This verifies the runner, not the
carrier design.

Export a new DSN whenever connector pin mapping or the circuit changes. The
board generator's `export-dsn` command adds explicit all-layer obstacles for
the 0.30 mm copper-to-edge constraint, because the default native DSN export
does not carry that KiCad rule. Its optional `--signal-starter` output allows
0.15 mm power-pin escapes while preserving the native 0.40 mm preferred POWER
netclass. Such a session still requires local rail widening, power/feedback
review, refill, native DRC, and schematic parity before fabrication. Import
with `generate_board.py import-session`; do not rerun `place` over routed
copper. A stale pad-net mapping is deliberately rejected.

## Generated hierarchical schematics

`kiutils` 1.4.8 can generate a hierarchy accepted by KiCad 10 using schema
version `20230121` and modern per-symbol/project instances. A generated
two-child-sheet smoke test exported references `R1`/`R2` and correctly
joined the two corresponding shared global nets.

For each child instance, the parent `HierarchicalSheet` contains an
`instances/project/path` whose project name is the root schematic stem,
path is `/<root schematic UUID>`, and page is the child's page number.
Each symbol in the child contains a matching project instance with path
`/<root schematic UUID>/<parent sheet UUID>`, its actual reference and unit.
The parent has the root `sheet_instances` path `/` on page 1; children omit
that root-only section. Global labels with identical names connect across
the child sheets. Local labels alone do not cross sheets.
