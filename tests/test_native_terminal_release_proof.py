#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual normal-mode terminal-proof producer on final Background ELFs."""
import argparse
import ast
import hashlib
import inspect
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import check_vmu_native_persistent as persistent


def audit(builds):
    tree=ast.parse(inspect.getsource(persistent.inspect_base))
    calls=[n for n in ast.walk(tree) if isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and
           n.func.id=='audit_map_refusal']
    assert len(calls)==1,'Missing unique actual terminal-proof producer'
    expression=compile(ast.Expression(calls[0]),'<actual-release-terminal-proof>','eval')
    results=[]
    for directory in map(Path,builds):
        low=persistent.read_elf(directory/'resident-sci.elf')
        background='_kui_vmu_native_background_manifest_bind' in low[4]
        assert background,'This regression requires a real Background low reader'
        proof=eval(expression,vars(persistent),dict(directory=directory,self_test=False,
            background=background,objdump=str(ROOT/'.deps/sh-elf/bin/sh-elf-objdump')))
        assert proof['schema']=='kui.native.gd-map-refusal.v1' and proof['cached_worker_terminal']
        assert proof['mutation_rejections']>0,'Release mode lost mandatory terminal escape evidence'
        expected=sum(row['bytes'] for row in proof['functions'].values())
        assert proof['mutation_rejections']==expected,'Incomplete real two-mask/halfword mutation sweep'
        results.append(dict(build=str(directory),self_test=False,background=background,
            real_mutation_rejections=proof['mutation_rejections'],guard_functions=proof['functions'],
            resident_sha256=hashlib.sha256(low[0]).hexdigest()))
    return dict(schema='kui.native.terminal-release-proof.v1',status='PASS',cases=results,
        producer_expression=ast.unparse(calls[0]),source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in ('tools/check_vmu_native_persistent.py','tests/test_native_terminal_release_proof.py')},
        limits='Actual release-mode terminal proof producer; does not claim the full native audit ran here.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('builds',nargs='+',type=Path)
    print(json.dumps(audit(parser.parse_args().builds),indent=2))
