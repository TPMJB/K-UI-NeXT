#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package one private Toy GUI and its matching adapters, inheriting all else."""
import argparse
import json
from pathlib import Path
import tempfile

from check_native_cdda_compat import expected_descriptor
from runtime_package import verify as verify_runtime
from update_package import build_package, verify, INHERIT_ROOT

TOY_PATHS=('apps/games/cdda-native-toy-raw.kui','apps/games/cdda-native-toy-cooked.kui')

def package(runtime,raw,cooked,build,output):
    output=Path(output)
    if output.exists():raise ValueError('Refusing to overwrite an existing test package')
    inputs=[('runtime.kui',Path(runtime)),(TOY_PATHS[0],Path(raw)),(TOY_PATHS[1],Path(cooked))]
    loaded=[]
    for name,path in inputs:
        data=path.read_bytes();info=verify_runtime(data)
        if info['build']!=build:raise ValueError('Test bank build ID mismatch: '+name)
        if name=='runtime.kui':
            if b'TOY TXT TEST\0' not in data[64:]:raise ValueError('GUI is not the private Toy test launcher')
        else:
            profile='toy-cooked' if name==TOY_PATHS[1] else 'toy-raw'
            expected=expected_descriptor(profile)
            if data[64:].count(b'KUINCD01')!=1 or data[64:].count(expected)!=1:
                raise ValueError('Toy adapter descriptor/format mismatch: '+name)
        loaded.append((name,data))
    with tempfile.TemporaryDirectory(prefix='kui-toy-bank-') as temporary:
        stage=Path(temporary)
        for name,data in loaded:
            target=stage/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        build_package(stage,output,build,'testing',inherit_missing=True)
    result=verify(output)
    replaced={row['path'] for row in result['members'] if row['policy']!=INHERIT_ROOT}
    expected={'runtime.kui',*TOY_PATHS}
    if replaced!=expected:raise ValueError('Unexpected test-bank replacement set')
    if any(row['policy']!=INHERIT_ROOT for row in result['members'] if row['path'] not in expected):
        raise ValueError('Native/VMU/CE module was not inherited')
    result['private_toy_test']=True
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',required=True,type=Path)
    parser.add_argument('--toy-raw',required=True,type=Path)
    parser.add_argument('--toy-cooked',required=True,type=Path)
    parser.add_argument('--build-id',required=True)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    try:print(json.dumps(package(args.runtime,args.toy_raw,args.toy_cooked,args.build_id,args.output),indent=2))
    except (OSError,ValueError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()
