#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install the VMU wizard GUI and plain physical-VMU Standard reader.

Existing standalone/Background loaders, cards, journals, settings and earlier
checkpoints are preserved. Restore recovers the exact two prior target states.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import tempfile
import zlib

SCHEMA='kui.vmu.wizard.delivery.v3'
BACKUP_SCHEMA='kui.vmu.wizard.backup.v3'
STATE_SCHEMA='kui.vmu.wizard.state.v3'
RUNTIME=Path('KUI/runtime.kui')
PHYSICAL=Path('KUI/apps/games/retail-physical.kui')
DUAL=Path('KUI/apps/games/retail-native-dual.kui')
NATIVE_MODULES=(DUAL,Path('KUI/apps/games/retail-boot.kui'),Path('KUI/apps/games/retail-native-standalone.kui'))
TARGETS={'runtime':RUNTIME,'physical':PHYSICAL}
PARENT=Path('KUI/vmu-wizard')
MAX_BYTES=4*1024*1024+64
PHYSICAL_DESCRIPTOR=struct.pack('<8I',0x5049554b,0x31594850,1,32,0,0,0,0)


def require(value,message):
    if not value:raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def digest(value):
    return isinstance(value,str) and re.fullmatch('[0-9a-f]{64}',value) is not None


def wire(value):
    return (json.dumps(value,indent=2,sort_keys=True)+'\n').encode()


def verify(data):
    """The same version-1 envelope checks used by runtime_package.py."""
    require(68<=len(data)<=MAX_BYTES and data[:8]==b'KUIRUN1\0','Invalid runtime envelope')
    version,header,size,address,entry,memory,crc,flags=struct.unpack_from('<8I',data,8)
    require(version==1 and header==64 and address==entry==0x8c010000 and flags==0 and
        data[52:60]==bytes(8) and re.fullmatch(rb'[0-9a-f]{12}',data[40:52]) and
        zlib.crc32(data[:60])==struct.unpack_from('<I',data,60)[0],'Runtime header is damaged or unsupported')
    require(size>=4 and size%4==0 and len(data)==64+size and size<=memory<=8*1024*1024 and memory%4==0 and
        zlib.crc32(data[64:])==crc,'Runtime length or payload checksum differs')
    return data[40:52].decode('ascii')


def ordinary(path,root,directory=False):
    path,root=Path(path).absolute(),Path(root).absolute()
    try:parts=path.relative_to(root).parts
    except ValueError as error:raise ValueError('Path leaves the selected card/package') from error
    require('..' not in parts and stat.S_ISDIR(root.lstat().st_mode),'Root must be an ordinary directory')
    current=root
    for index,part in enumerate(parts):
        current/=part;mode=current.lstat().st_mode
        require(stat.S_ISDIR(mode) if directory or index+1<len(parts) else stat.S_ISREG(mode),
                'Symlink or special file refused: '+str(current))
    return path


def read(path,root,maximum=MAX_BYTES):
    ordinary(path,root)
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
    with os.fdopen(fd,'rb') as source:
        before=os.fstat(source.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_size<=maximum,'Oversized or non-regular file: '+str(path))
        data=source.read(maximum+1);after=os.fstat(source.fileno())
    require(len(data)==before.st_size==after.st_size and before.st_mtime_ns==after.st_mtime_ns and
        before.st_ino==after.st_ino,'File changed while reading: '+str(path))
    return data


def maybe_read(path,root,maximum=MAX_BYTES):
    relative=Path(path).relative_to(root);current=root
    for part in relative.parts[:-1]:
        current/=part
        if not current.exists() and not current.is_symlink():return None
        ordinary(current,root,directory=True)
    return read(path,root,maximum) if path.exists() or path.is_symlink() else None


def optional(path,root,maximum=16384):
    return maybe_read(path,root,maximum)


def sync_directory(path):
    fd=os.open(path,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    try:os.fsync(fd)
    finally:os.close(fd)


def mkdirs(root,relative):
    current=root
    for part in relative.parts:
        require(part not in ('','.','..'),'Invalid output directory')
        ordinary(current,root,directory=True);current/=part
        if not current.exists() and not current.is_symlink():current.mkdir();sync_directory(current.parent)
        ordinary(current,root,directory=True)
    return current


def publish(path,data,root):
    ordinary(path.parent,root,directory=True)
    if path.exists() or path.is_symlink():ordinary(path,root)
    if data is None:
        path.unlink();sync_directory(path.parent)
        require(maybe_read(path,root) is None,'Removed physical module reappeared')
        return
    fd,name=tempfile.mkstemp(prefix='.'+path.name+'.',dir=path.parent);temporary=Path(name)
    try:
        with os.fdopen(fd,'wb') as output:output.write(data);output.flush();os.fsync(output.fileno())
        require(read(temporary,root)==data,'Temporary file differs on readback')
        ordinary(path.parent,root,directory=True)
        if path.exists() or path.is_symlink():ordinary(path,root)
        os.replace(temporary,path);sync_directory(path.parent)
        require(read(path,root)==data,'Published file differs on readback')
    finally:temporary.unlink(missing_ok=True)


def protected_modules(root):
    result={}
    for relative in NATIVE_MODULES:
        data=maybe_read(root/relative,root)
        if data is not None:verify(data)
        result[str(relative)]=data
    return result


def hashes(items):
    return {name:sha(data) if data is not None else None for name,data in items.items()}


def targets_read(root):
    result={name:maybe_read(root/path,root) for name,path in TARGETS.items()}
    require(result['runtime'] is not None,'Current GUI is missing')
    for data in result.values():
        if data is not None:verify(data)
    return result


def package_data(package):
    package=Path(package).absolute();ordinary(package,package,directory=True)
    record=json.loads(read(package/'wizard.json',package,16384))
    require(isinstance(record,dict) and set(record)=={'schema','source_commit','build','runtime_sha256',
        'physical_sha256','replaces','preserved_native_modules','console_compatibility_verified'} and
        record['schema']==SCHEMA and isinstance(record['source_commit'],str) and
        re.fullmatch('[0-9a-f]{40}',record['source_commit']) and record['build']==record['source_commit'][:12] and
        digest(record['runtime_sha256']) and digest(record['physical_sha256']) and
        record['replaces']==[str(path) for path in TARGETS.values()] and
        record['preserved_native_modules']==[str(path) for path in NATIVE_MODULES] and
        record['console_compatibility_verified'] is False,'Invalid wizard/physical-Standard package record')
    payload={'runtime':read(package/'gui/runtime.kui',package),
             'physical':read(package/'physical/retail-physical.kui',package)}
    require(all(verify(data)==record['build'] and sha(data)==record[name+'_sha256'] for name,data in payload.items()),
            'Packaged GUI or physical reader differs from its source build')
    physical=payload['physical'];at=physical.find(PHYSICAL_DESCRIPTOR,64)
    require(at>=64 and not at%4 and physical.find(PHYSICAL_DESCRIPTOR,at+1)==-1,
            'Physical Standard requires its unique exact zero-VMU descriptor')
    return package,record,payload


def backup_read(root,checkpoint,metadata):
    record=json.loads(read(checkpoint/'backup.json',root,16384))
    require(isinstance(record,dict) and set(record)=={'schema','build','candidate_sha256','original_sha256',
        'original_bytes','protected_module_sha256'} and record['schema']==BACKUP_SCHEMA and
        record['build']==metadata['build'] and record['candidate_sha256']==
        {name:metadata[name+'_sha256'] for name in TARGETS} and
        isinstance(record['original_sha256'],dict) and set(record['original_sha256'])==set(TARGETS) and
        isinstance(record['original_bytes'],dict) and set(record['original_bytes'])==set(TARGETS) and
        isinstance(record['protected_module_sha256'],dict) and
        set(record['protected_module_sha256'])=={str(path) for path in NATIVE_MODULES} and
        all(value is None or digest(value) for value in record['protected_module_sha256'].values()),'Invalid wizard checkpoint')
    originals={}
    for name in TARGETS:
        size,digest_value=record['original_bytes'][name],record['original_sha256'][name]
        if name=='physical' and size is None and digest_value is None:
            require(maybe_read(checkpoint/'physical-before.kui',root) is None,'Unexpected originally-absent physical backup')
            originals[name]=None;continue
        require(type(size) is int and 68<=size<=MAX_BYTES and digest(digest_value),'Invalid exact predecessor record')
        data=read(checkpoint/(name+'-before.kui'),root)
        require(len(data)==size and sha(data)==digest_value,'Original checkpoint differs: '+name)
        verify(data);originals[name]=data
    return record,originals


def backup_create(root,checkpoint,metadata,current,modules):
    parent=mkdirs(root,checkpoint.parent.relative_to(root));stage=Path(tempfile.mkdtemp(prefix='.checkpoint-',dir=parent))
    try:
        record=dict(schema=BACKUP_SCHEMA,build=metadata['build'],
            candidate_sha256={name:metadata[name+'_sha256'] for name in TARGETS},original_sha256=hashes(current),
            original_bytes={name:len(data) if data is not None else None for name,data in current.items()},
            protected_module_sha256=hashes(modules))
        for name,data in current.items():
            if data is not None:publish(stage/(name+'-before.kui'),data,root)
        publish(stage/'backup.json',wire(record),root)
        require(not checkpoint.exists() and not checkpoint.is_symlink(),'Wizard checkpoint appeared concurrently')
        require(targets_read(root)==current and protected_modules(root)==modules,'Installed files changed before checkpoint publication')
        os.rename(stage,checkpoint);sync_directory(parent)
    finally:
        if stage.exists():shutil.rmtree(stage)
    return backup_read(root,checkpoint,metadata)


def state_read(root,checkpoint,metadata,backup):
    raw=optional(checkpoint/'state.json',root)
    if raw is None:return None,None
    value=json.loads(raw)
    require(isinstance(value,dict) and set(value)=={'schema','build','candidate_sha256','original_sha256',
        'pending','installed','allowed'} and value['schema']==STATE_SCHEMA and value['build']==metadata['build'] and
        value['candidate_sha256']==backup['candidate_sha256'] and value['original_sha256']==backup['original_sha256'] and
        type(value['pending']) is bool and type(value['installed']) is bool and
        isinstance(value['allowed'],dict) and set(value['allowed'])==set(TARGETS),'Invalid wizard transaction receipt')
    for name,allowed in value['allowed'].items():
        require(isinstance(allowed,list) and 1<=len(allowed)<=2 and
            all(item is None or digest(item) for item in allowed) and len(set(allowed))==len(allowed) and
            set(allowed)<={backup['original_sha256'][name],metadata[name+'_sha256']},'Invalid wizard admissible target: '+name)
        if not value['pending']:
            expected=metadata[name+'_sha256'] if value['installed'] else backup['original_sha256'][name]
            require(allowed==[expected],'Final wizard receipt is inconsistent')
    return raw,value


def final_state(metadata,backup,target,installed):
    return dict(schema=STATE_SCHEMA,build=metadata['build'],candidate_sha256=backup['candidate_sha256'],
        original_sha256=backup['original_sha256'],installed=installed,pending=False,
        allowed={name:[digest_value] for name,digest_value in hashes(target).items()})


def apply(root,checkpoint,metadata,backup,state_raw,state,current,desired,modules,installed):
    require(targets_read(root)==current and protected_modules(root)==modules and
        optional(checkpoint/'state.json',root)==state_raw,'Installed files or receipt changed before publication')
    final=final_state(metadata,backup,desired,installed)
    if current==desired and state_raw==wire(final):return []
    pending=dict(final,pending=True,allowed={name:list(dict.fromkeys((hashes(current)[name],hashes(desired)[name]))) for name in TARGETS})
    changed=[]
    try:
        publish(checkpoint/'state.json',wire(pending),root)
        order=('physical','runtime') if installed else ('runtime','physical')
        for name in order:
            require(targets_read(root)=={**current,**{prior:desired[prior] for prior in changed}} and
                protected_modules(root)==modules,'Installed files changed before runtime replacement')
            if current[name]!=desired[name]:
                changed.append(name);publish(root/TARGETS[name],desired[name],root)
        require(targets_read(root)==desired and protected_modules(root)==modules,
                'GUI, physical reader or protected native module changed during installation')
        publish(checkpoint/'state.json',wire(final),root)
    except BaseException:
        # Re-arm the durable two-state receipt before undoing any target,
        # including a failure reported after final-receipt rename.
        publish(checkpoint/'state.json',wire(pending),root)
        for name in reversed(changed):
            now=maybe_read(root/TARGETS[name],root)
            if now==desired[name] and now!=current[name]:publish(root/TARGETS[name],current[name],root)
        if targets_read(root)==current:
            previous=state_raw or wire(final_state(metadata,backup,current,False))
            publish(checkpoint/'state.json',previous,root)
        raise
    return [str(TARGETS[name]) for name in changed]


def run(root,package,restore=False):
    require(type(restore) is bool,'Restore must be a boolean')
    root=Path(root).absolute();require('..' not in root.parts,'Card root must not contain parent traversal')
    ordinary(root,root,directory=True);_,metadata,payload=package_data(package)
    current=targets_read(root);modules=protected_modules(root)
    checkpoint=root/PARENT/'checkpoints'/metadata['build']
    require(not restore or checkpoint.exists() or checkpoint.is_symlink(),'No checkpoint exists for this wizard build')
    parent=mkdirs(root,PARENT);lock=parent/'installer.lock'
    if lock.exists() or lock.is_symlink():ordinary(lock,root)
    fd=os.open(lock,os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
    try:
        require(stat.S_ISREG(os.fstat(fd).st_mode),'Installer lock is not an ordinary file')
        try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError as error:raise ValueError('Another VMU wizard installer is running') from error
        if checkpoint.exists() or checkpoint.is_symlink():
            ordinary(checkpoint,root,directory=True);backup,original=backup_read(root,checkpoint,metadata)
        else:backup,original=backup_create(root,checkpoint,metadata,current,modules)
        state_raw,state=state_read(root,checkpoint,metadata,backup)
        allowed=state['allowed'] if state else {name:[value] for name,value in hashes(original).items()}
        require(all(value in allowed[name] for name,value in hashes(current).items()),
                'GUI or physical reader changed outside this wizard; preserving it')
        desired=original if restore else payload
        mkdirs(root,PHYSICAL.parent)
        changed=apply(root,checkpoint,metadata,backup,state_raw,state,current,desired,modules,not restore)
        return dict(restored=restore,installed=not restore,changed=changed,checkpoint=str(checkpoint.relative_to(root)),
            build=metadata['build'],preserved_native_module_sha256=hashes(modules))
    finally:os.close(fd)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root',type=Path)
    parser.add_argument('--restore',action='store_true',help='Restore the exact GUI/physical-reader predecessors')
    parser.add_argument('--package',type=Path,default=Path(__file__).resolve().parent,help=argparse.SUPPRESS)
    args=parser.parse_args()
    try:result=run(args.sd_root,args.package,args.restore)
    except (OSError,ValueError,KeyError,TypeError,UnicodeError,struct.error) as error:
        parser.exit(2,'VMU wizard installation refused: '+str(error)+'\n')
    print(json.dumps(result,indent=2))
    print('Exact previous GUI and physical-reader state restored.' if args.restore else 'Installed. Home > VMU tools > Import saves.')
    print('Existing Background/standalone loaders and cards are unchanged. Safely eject before booting.')


if __name__=='__main__':main()
