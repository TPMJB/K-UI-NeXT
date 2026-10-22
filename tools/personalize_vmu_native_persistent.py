#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the native save test to an existing prepared card and install its loader.

Only the audited module's exact4096-byte placeholder and runtime envelope
checksums change. VMU image/journal/identity/profile files are read only. No
card is created, cloned, restored or reformatted by this command.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

# The console ZIP places this script at top level and these proven codecs in
# tools/. Running the source file directly already adds its own tools/ path.
sys.path.insert(0,str(Path(__file__).resolve().parent/'tools'))
from runtime_package import MAX_BYTES,envelope,verify
from vmu_persist_profile import FILE_BYTES,WIRE_BYTES,decode_profile,same_ownership,template_profile

SCHEMA='kui.native.persistent.personalization.v1'
BASE_MODULE=Path('base/KUI/apps/games/retail-boot.kui')
MANIFEST=Path('base/native-persistent-personalization.json')
INSTALL=Path('KUI/apps/games/retail-boot.kui')
PROFILE_NAME=re.compile(r'retail-persistent-profile-[0-9]{4}\.bin')
PAIR_NAME=re.compile(r'p(?!0000)[0-9]{4}')
FIELDS={'schema','source_commit','build','module_sha256','module_bytes',
        'profile_module_offset','profile_bytes','profile_sha256','storage_writes_enabled'}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(condition,message):
    if not condition:raise ValueError(message)

def ordinary_path(path,root,*,directory=False):
    """Reject symlinks/special files at every component below a selected root."""
    path=Path(path).absolute();root=Path(root).absolute()
    try:relative=path.relative_to(root)
    except ValueError as error:raise ValueError('Path leaves the selected card/package') from error
    require('..' not in relative.parts,'Path leaves the selected card/package')
    current=root
    require(stat.S_ISDIR(current.lstat().st_mode),'Root must be an ordinary directory')
    for index,part in enumerate(relative.parts):
        current=current/part;mode=current.lstat().st_mode
        last=index==len(relative.parts)-1
        require(stat.S_ISDIR(mode) if not last or directory else stat.S_ISREG(mode),
                'Requires ordinary directories/files: '+str(current))
    return path

def read_regular(path,root,expected=None,maximum=None):
    path=ordinary_path(path,root)
    size=path.stat().st_size
    require(expected is None or size==expected,'Unexpected file length: '+str(path))
    require(maximum is None or size<=maximum,'Oversized file: '+str(path))
    data=path.read_bytes()
    require(len(data)==size,'File changed while reading: '+str(path))
    return data

def generic_module(package):
    package=Path(package).resolve(strict=True)
    manifest=json.loads(read_regular(package/MANIFEST,package,maximum=16384))
    require(isinstance(manifest,dict) and set(manifest)==FIELDS,'Unexpected personalization manifest fields')
    require(manifest['schema']==SCHEMA and manifest['storage_writes_enabled'] is True,
            'Requires an audited native persistent save-test manifest')
    for field,length in (('source_commit',40),('module_sha256',64),('profile_sha256',64)):
        require(isinstance(manifest[field],str) and re.fullmatch('[0-9a-f]{'+str(length)+'}',manifest[field]),
                'Invalid manifest '+field)
    require(manifest['build']==manifest['source_commit'][:12],'Build/source identifier mismatch')
    for field in ('module_bytes','profile_module_offset','profile_bytes'):
        require(type(manifest[field]) is int,'Manifest counts must be integers')
    require(64+4<=manifest['module_bytes']<=MAX_BYTES+64,'Invalid generic module size')
    template=template_profile();offset=manifest['profile_module_offset']
    require(manifest['profile_bytes']==WIRE_BYTES and manifest['profile_sha256']==sha(template) and
            offset>=64 and not offset&3 and offset+WIRE_BYTES<=manifest['module_bytes'],
            'Manifest does not bound the exact canonical profile template')
    module=read_regular(package/BASE_MODULE,package,expected=manifest['module_bytes'])
    info=verify(module)
    require(sha(module)==manifest['module_sha256'] and info['build']==manifest['build'],
            'Generic module differs from its audited build')
    require(module[offset:offset+WIRE_BYTES]==template and module.find(template)==offset and
            module.find(template,offset+1)==-1,'Profile template is absent, misplaced or duplicated')
    return module,manifest,info

def inspect_profile(path,root):
    path=Path(path)
    if not path.is_absolute():path=root/path
    path=ordinary_path(path,root)
    require(PROFILE_NAME.fullmatch(path.name),'Select a canonical retail-persistent-profile-NNNN.bin file')
    wire=read_regular(path,root,expected=WIRE_BYTES)
    profile=decode_profile(wire)
    require(wire!=template_profile(),'The public build template is not an owner card preparation')
    pair=root/profile['pair_path'].lstrip('/')
    require(path.parent==pair,'Profile path differs from its prepared pair')
    ordinary_path(pair,root,directory=True)
    require(PAIR_NAME.fullmatch(pair.name),'Invalid prepared pair directory')
    for name,size in zip(('card.vmd','card.journal','identity.bin'),FILE_BYTES):
        file=ordinary_path(pair/name,root)
        require(file.stat().st_size==size,'Prepared pair file length changed: '+str(file))
    identity=read_regular(pair/'identity.bin',root,expected=16)
    require(identity==bytes.fromhex(profile['image_id']),'Prepared profile identity differs from identity.bin')
    # Preparation hashes are historical provenance. Real saves legitimately
    # change card.vmd/journal; raw ownership/recovery runs on the console.
    return {'path':path,'wire':wire,'summary':profile,'identity':identity}

def select_profile(root,selected=None):
    root=Path(root).resolve(strict=True)
    if selected is not None:return inspect_profile(selected,root)
    parent=ordinary_path(root/'KUI/vmu-test',root,directory=True)
    candidates=[];failures=[]
    for pair in sorted(parent.iterdir()):
        if not PAIR_NAME.fullmatch(pair.name):continue
        try:ordinary_path(pair,root,directory=True)
        except (OSError,ValueError) as error:failures.append(str(error));continue
        for path in sorted(pair.iterdir()):
            if not PROFILE_NAME.fullmatch(path.name):continue
            try:candidates.append(inspect_profile(path,root))
            except (OSError,ValueError,KeyError,TypeError) as error:failures.append(str(error))
    require(candidates,'No usable existing persistent profile found'+(': '+failures[0] if failures else ''))
    # Repeated exports of the same physical ownership may have different
    # snapshot hashes after legitimate saves. They select the same owned pair.
    groups=[]
    for item in candidates:
        group=next((g for g in groups if same_ownership(g[0]['summary'],item['summary'])),None)
        if group is None:groups.append([item])
        else:group.append(item)
    if len(groups)!=1:
        choices='\n'.join('  --profile '+str(g[-1]['path'].relative_to(root)) for g in groups)
        raise ValueError('Several prepared cards/ownership maps are available; select one explicitly:\n'+choices)
    return groups[0][-1]

def personalize_bytes(module,manifest,info,wire):
    require(len(wire)==WIRE_BYTES and wire!=template_profile(),'Requires a real canonical owner profile')
    decode_profile(wire)
    offset=manifest['profile_module_offset']
    payload=bytearray(module[64:]);payload[offset-64:offset-64+WIRE_BYTES]=wire
    result=envelope(bytes(payload),info['memory_bytes'],info['build'])
    after=verify(result)
    require(len(result)==len(module) and after['build']==info['build'],'Personalization changed module layout/build')
    allowed=((32,36),(60,64),(offset,offset+WIRE_BYTES))
    require(all(old==new or any(begin<=index<end for begin,end in allowed)
                for index,(old,new) in enumerate(zip(module,result))),
            'Changed bytes escape the exact profile and runtime checksum spans')
    return result

def install(root,*,package,profile=None):
    root=Path(root).resolve(strict=True)
    module,manifest,info=generic_module(package)
    selected=select_profile(root,profile)
    personalized=personalize_bytes(module,manifest,info,selected['wire'])
    destination=root/INSTALL
    ordinary_path(destination.parent,root,directory=True)
    existed=destination.exists() or destination.is_symlink()
    previous=read_regular(destination,root,maximum=MAX_BYTES+64) if existed else None
    result={'build':info['build'],'pair_path':selected['summary']['pair_path'],
        'profile':str(selected['path'].relative_to(root)),'module_sha256':sha(personalized),
        'installed_file':str(INSTALL),'already_installed':previous==personalized}
    if previous==personalized:return result
    # Recheck association immediately before the sole destination mutation.
    require(read_regular(selected['path'],root,expected=WIRE_BYTES)==selected['wire'] and
            read_regular(selected['path'].parent/'identity.bin',root,expected=16)==selected['identity'],
            'Selected profile/identity changed before installation')
    temporary=None
    try:
        fd,name=tempfile.mkstemp(prefix='.native-vmu-',dir=destination.parent)
        temporary=Path(name)
        with os.fdopen(fd,'wb') as output:
            output.write(personalized);output.flush();os.fsync(output.fileno())
        require(temporary.read_bytes()==personalized,'Installed module temporary readback failed')
        ordinary_path(destination.parent,root,directory=True)
        if destination.exists() or destination.is_symlink():ordinary_path(destination,root)
        os.replace(temporary,destination);temporary=None
        directory_fd=os.open(destination.parent,os.O_RDONLY|os.O_DIRECTORY)
        try:os.fsync(directory_fd)
        finally:os.close(directory_fd)
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root',type=Path)
    parser.add_argument('--profile',type=Path,help='Explicit canonical profile path on this card, relative to its root or absolute')
    parser.add_argument('--package',type=Path,default=Path(__file__).resolve().parent,
                        help=argparse.SUPPRESS)
    args=parser.parse_args()
    try:
        result=install(args.sd_root,package=args.package,profile=args.profile)
        action='Already installed' if result['already_installed'] else 'Installed'
        print(action+' native persistent VMU test '+result['build']+' using '+result['pair_path'])
        print('Image, journal, identity and preparation profiles were unchanged.')
        print('Safely eject the card, then follow README-FIRST.md.')
    except (OSError,ValueError,KeyError,TypeError,UnicodeError) as error:
        parser.exit(2,'Native VMU personalization refused: '+str(error)+'\n')

if __name__=='__main__':main()
