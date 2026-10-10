#!/bin/sh
set -eu
cd "$(dirname "$0")"
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
iverilog -g2012 -s tb_bridge -o "$build_dir/bridge.vvp" bridge.v tb_bridge.v
vvp "$build_dir/bridge.vvp"
iverilog -g2012 -s tb_fifo -o "$build_dir/fifo.vvp" bridge.v tb_fifo.v
vvp "$build_dir/fifo.vvp"
iverilog -g2012 -s tb_review -o "$build_dir/review.vvp" bridge.v tb_review.v
vvp "$build_dir/review.vvp"
iverilog -g2012 -s tb_activation -o "$build_dir/activation.vvp" bridge.v tb_activation.v
vvp "$build_dir/activation.vvp"
# Deliberately broken RTL must be rejected by the independent wire-level suite.
# Compilation errors never count as a detected behavioral regression.
python3 - "$build_dir" <<'PY3'
from pathlib import Path
import subprocess,sys
build=Path(sys.argv[1])
source=Path('bridge.v').read_text()
mutations={
    'activation_bypass':('wire ata_operational = drive_safe & bridge_active & !soft_reset & !diagnostic_pending;',
                         'wire ata_operational = drive_safe & !soft_reset & !diagnostic_pending;'),
    'drop_dev0_diagnostic':("7:if(ata_write_latch[7:0]==8'h90 ||", "7:if((ata_write_latch[7:0]==8'h90 && selected) ||"),
    'synchronous_safety_relock':('always @(posedge FPGA_CLK50 or negedge activation_resetn) begin',
                                 'always @(posedge FPGA_CLK50) begin'),
    'drop_sticky_safety_cleanup':('if (!drive_safe || soft_reset || safety_event_pending) begin',
                                 'if (!drive_safe || soft_reset) begin'),
}
for name,(old,new) in mutations.items():
    assert source.count(old)==1,(name,'mutation anchor must be unique')
    rtl=build/(name+'.v')
    output=build/(name+'.vvp')
    rtl.write_text(source.replace(old,new))
    subprocess.run(['iverilog','-g2012','-s','tb_activation','-o',str(output),str(rtl),'tb_activation.v'],check=True)
    result=subprocess.run(['vvp',str(output)],text=True,capture_output=True,timeout=20)
    failures=sum(line.startswith('FAIL ') for line in result.stdout.splitlines())
    assert result.returncode!=0 and failures>0,(name,'broken behavior escaped regression',result.stdout,result.stderr)
    print('Negative control detected:',name,'(%d failed assertions)'%failures)
PY3
yosys -Q -p 'read_verilog bridge.v; hierarchy -check -top bridge; proc; memory_dff; memory_collect; opt_clean; check -assert; stat' > "$build_dir/synthesis.log"
# Generic synthesis is structural only. It is NOT a Lattice place-and-route result.
grep -E 'Warning:|\$mem_v2|Number of cells:|End of script' "$build_dir/synthesis.log"
yosys -Q -p "read_verilog bridge.v; synth_lattice -family xo2 -top bridge -iopad -json $build_dir/xo2.json; check -assert" > "$build_dir/xo2-synthesis.log"
python3 - "$build_dir/xo2.json" <<'PY3'
import json,collections,sys
cells=json.load(open(sys.argv[1]))['modules']['bridge']['cells']
counts=collections.Counter(c['type'] for c in cells.values())
assert counts['DP8KC'] <= 8, counts
assert counts['LUT4'] + 2 * counts['CCU2D'] <= 2112, counts
assert counts['TRELLIS_FF'] <= 2112, counts
print('MachXO2 technology mapping:',dict(sorted(counts.items())))
print('Resource budget check passed; physical placement and timing not validated.')
PY3
