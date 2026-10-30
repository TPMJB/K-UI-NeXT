#!/usr/bin/env python3
"""Limited candidate gate; does not replace the full persistent ISA audit."""
import argparse,hashlib,json,subprocess
from pathlib import Path
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_native_vmu_toggle_profile import exact_toggle_kernels
from check_native_general_lean_profile import prepared_objects

def check(directory,baseline):
    stage=read_elf(directory/'stage.elf'); worker=read_elf(directory/'native-worker.elf')
    result={'experimental':True,'full_persistent_instruction_model_complete':False,
            'kernels':exact_toggle_kernels(stage,worker),'state':prepared_objects(worker)}
    rejected=0
    for image,other,is_stage in ((stage,worker,True),(worker,stage,False)):
        role='stage' if is_stage else 'worker'
        for name in result['kernels'][role]:
            address,body,section=symbol_bytes(image,name,2)
            for at in (0,len(body)//2,len(body)-1):
                data=bytearray(image[0]);data[section[4]+address-section[3]+at]^=1
                mutant=(bytes(data),*image[1:])
                try:exact_toggle_kernels(mutant,other) if is_stage else exact_toggle_kernels(other,mutant)
                except ValueError:rejected+=1
                else:raise ValueError('Changed kernel accepted: '+name)
    result['kernel_mutations_rejected']=rejected
    result['readers']={}
    for name in ('resident-sci.bin','resident-legacy.bin'):
        new=(directory/name).read_bytes();old=(baseline/name).read_bytes()
        # Replace the one build string, leaving every other byte compared.
        import re
        builds=re.findall(rb'(?<![0-9a-f])[0-9a-f]{12}(?![0-9a-f])',new)
        matches=[b for b in builds if new.replace(b,b'34dc7b5e15ee')==old]
        if new!=old and len(matches)!=1:raise ValueError('Reader differs beyond its build label: '+name)
        result['readers'][name]={'bytes':len(new),'unchanged_except_build_label':True,'sha256':hashlib.sha256(new).hexdigest()}
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('baseline',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();r=json.dumps(check(a.directory,a.baseline),indent=2)+'\n'
    if a.output:a.output.write_text(r)
    print(r)
