#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the native save test to an existing prepared card and install its loader.

Only the audited module's exact4096-byte profile, optional1120-byte cache seed,
optional32-byte runtime mode and runtime envelope checksums change. VMU pair/profile files are read only. No
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
CACHE_FIELDS={'cache_seed_module_offset','cache_seed_bytes','cache_seed_sha256',
        'cache_record_bytes','cache_detector_epoch','cache_catalog_path','cache_catalog_sha256'}
MODE_FIELDS={'runtime_mode_module_offset','runtime_mode_bytes','runtime_mode_sha256'}
MODE_CHOICES_FIELD='runtime_mode_choices'
MODE_CHOICES_LEASE_ONLY={'normal':0,'control':1,'profile':2,'lease-only':3}
MODE_CHOICES_HOOKS_ONLY={**MODE_CHOICES_LEASE_ONLY,'hooks-only':4}
CATALOG_SCHEMA='kui.native.resolution-cache.catalog.v1'
CATALOG_PATH='native-cache/catalog.json'
GAME_SLUGS={'ps2','skies','sa2','grandia2','evolution1'}

def cache_codec():
    # Legacy ZIPs intentionally need only the two original shipped codecs.
    try:import vmu_native_cache_seed
    except ImportError as error:raise ValueError('The package native cache codec is missing; extract the complete ZIP') from error
    return vmu_native_cache_seed

def mode_codec():
    # Older packages intentionally carry no runtime-mode codec or selector.
    try:import vmu_native_mode
    except ImportError as error:raise ValueError('The package native runtime mode codec is missing; extract the complete ZIP') from error
    return vmu_native_mode

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
    allowed=(FIELDS,FIELDS|CACHE_FIELDS,FIELDS|MODE_FIELDS,FIELDS|CACHE_FIELDS|MODE_FIELDS,
             FIELDS|MODE_FIELDS|{MODE_CHOICES_FIELD},FIELDS|CACHE_FIELDS|MODE_FIELDS|{MODE_CHOICES_FIELD})
    require(isinstance(manifest,dict) and set(manifest) in allowed,
            'Unexpected personalization manifest fields')
    if MODE_CHOICES_FIELD in manifest:
        choices=manifest[MODE_CHOICES_FIELD]
        require(isinstance(choices,dict) and choices in (MODE_CHOICES_LEASE_ONLY,MODE_CHOICES_HOOKS_ONLY) and
                all(type(value) is int for value in choices.values()),
                'Native runtime mode capabilities differ from the audited choices')
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
    if CACHE_FIELDS<=set(manifest):
        codec=cache_codec();seed_offset=manifest['cache_seed_module_offset'];seed=codec.template_seed()
        for field in ('cache_seed_module_offset','cache_seed_bytes','cache_record_bytes','cache_detector_epoch'):
            require(type(manifest[field]) is int,'Native cache manifest counts must be integers')
        require(manifest['cache_seed_bytes']==codec.SEED_BYTES and
                manifest['cache_record_bytes']==codec.RECORD_BYTES and
                manifest['cache_detector_epoch']==codec.DETECTOR_EPOCH and
                manifest['cache_seed_sha256']==sha(seed) and
                seed_offset>=64 and not seed_offset&3 and seed_offset+len(seed)<=len(module),
                'Native cache manifest does not bound its exact canonical seed')
        require(seed_offset+len(seed)<=offset or offset+WIRE_BYTES<=seed_offset,
                'Native cache seed overlaps the owner profile')
        require(module[seed_offset:seed_offset+len(seed)]==seed and codec.find_seed(module)==seed_offset,
                'Native cache seed is absent, misplaced or duplicated')
        cache_catalog(package,manifest)
    if MODE_FIELDS<=set(manifest):
        codec=mode_codec();mode_offset=manifest['runtime_mode_module_offset'];mode=codec.template_mode()
        require(type(mode_offset) is int and type(manifest['runtime_mode_bytes']) is int and
                manifest['runtime_mode_bytes']==codec.MODE_BYTES and
                manifest['runtime_mode_sha256']==sha(mode) and mode_offset>=64 and
                not mode_offset&3 and mode_offset+len(mode)<=len(module),
                'Native runtime mode manifest does not bound its canonical template')
        require(mode_offset+len(mode)<=offset or offset+WIRE_BYTES<=mode_offset,
                'Native runtime mode overlaps the owner profile')
        if CACHE_FIELDS<=set(manifest):
            seed_offset=manifest['cache_seed_module_offset'];seed_bytes=manifest['cache_seed_bytes']
            require(mode_offset+len(mode)<=seed_offset or seed_offset+seed_bytes<=mode_offset,
                    'Native runtime mode overlaps the cache seed')
        require(module[mode_offset:mode_offset+len(mode)]==mode and codec.find_mode(module)==mode_offset,
                'Native runtime mode is absent, misplaced or duplicated')
    return module,manifest,info

def cache_catalog(package,manifest):
    require(CACHE_FIELDS<=set(manifest),'This package has no audited native cache catalog')
    require(manifest['cache_catalog_path']==CATALOG_PATH and
            isinstance(manifest['cache_catalog_sha256'],str) and
            re.fullmatch('[0-9a-f]{64}',manifest['cache_catalog_sha256']),
            'Native cache catalog manifest fields differ')
    raw=read_regular(Path(package)/CATALOG_PATH,package,maximum=16384)
    require(sha(raw)==manifest['cache_catalog_sha256'],'Native cache catalog differs from its pinned digest')
    catalog=json.loads(raw)
    require(isinstance(catalog,dict) and set(catalog)=={'schema','source_commit','detector_epoch','games'} and
            catalog['schema']==CATALOG_SCHEMA and catalog['source_commit']==manifest['source_commit'] and
            type(catalog['detector_epoch']) is int and catalog['detector_epoch']==manifest['cache_detector_epoch'] and
            isinstance(catalog['games'],list) and 1<=len(catalog['games'])<=len(GAME_SLUGS),
            'Native cache catalog schema/source/epoch differs')
    result={};codec=cache_codec()
    for game in catalog['games']:
        require(isinstance(game,dict) and set(game)=={'slug','title','record_path','record_sha256',
                'record_digest','image_sha256','image_bytes','load_address'},'Unexpected native cache game fields')
        slug=game['slug']
        require(isinstance(slug,str) and slug in GAME_SLUGS and slug not in result,
                'Unknown or duplicated native cache game')
        require(isinstance(game['title'],str) and 1<=len(game['title'])<=80 and
                all(32<=ord(c)<127 for c in game['title']),'Invalid native cache game title')
        require(game['record_path']=='native-cache/'+slug+'.nvr' and type(game['image_bytes']) is int,
                'Native cache record path/size differs')
        wire=read_regular(Path(package)/game['record_path'],package,expected=codec.RECORD_BYTES)
        metadata=codec.record_metadata(wire,game['record_digest'])
        require(all(game[field]==metadata[field] for field in
                ('record_sha256','record_digest','image_sha256','image_bytes','load_address')),
                'Native cache game metadata differs from its pinned record')
        result[slug]={**game,'wire':wire}
    return result

def list_games(package):
    _,manifest,_=generic_module(package)
    return cache_catalog(Path(package).resolve(strict=True),manifest)

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

def personalize_bytes(module,manifest,info,wire,*,cache_record=None,cache_digest=None,runtime_mode=0):
    require(len(wire)==WIRE_BYTES and wire!=template_profile(),'Requires a real canonical owner profile')
    decode_profile(wire)
    offset=manifest['profile_module_offset']
    payload=bytearray(module[64:]);payload[offset-64:offset-64+WIRE_BYTES]=wire
    allowed=[(32,36),(60,64),(offset,offset+WIRE_BYTES)]
    require(type(runtime_mode) is int and runtime_mode in (0,1,2,3,4),'Native runtime mode is unsupported')
    require(runtime_mode!=3 or manifest.get(MODE_CHOICES_FIELD,{}).get('lease-only')==3,
            'This package has no audited lease-only runtime mode capability')
    require(runtime_mode!=4 or manifest.get(MODE_CHOICES_FIELD,{}).get('hooks-only')==4,
            'This package has no audited hooks-only runtime mode capability')
    if MODE_FIELDS<=set(manifest):
        codec=mode_codec();mode_offset=manifest['runtime_mode_module_offset']
        mode=codec.fill_mode(module[mode_offset:mode_offset+codec.MODE_BYTES],runtime_mode)
        payload[mode_offset-64:mode_offset-64+len(mode)]=mode
        allowed.append((mode_offset,mode_offset+len(mode)))
    else:require(runtime_mode==0,'This package has no audited native runtime mode selector')
    if cache_record is not None or cache_digest is not None:
        require(CACHE_FIELDS<=set(manifest) and cache_record is not None and cache_digest is not None,
                'Requires the complete audited cache seed and independently pinned record')
        codec=cache_codec();seed_offset=manifest['cache_seed_module_offset']
        seed=codec.fill_seed(module[seed_offset:seed_offset+codec.SEED_BYTES],cache_record,cache_digest)
        payload[seed_offset-64:seed_offset-64+len(seed)]=seed
        allowed.append((seed_offset,seed_offset+len(seed)))
    result=envelope(bytes(payload),info['memory_bytes'],info['build'])
    after=verify(result)
    require(len(result)==len(module) and after['build']==info['build'],'Personalization changed module layout/build')
    require(all(old==new or any(begin<=index<end for begin,end in allowed)
                for index,(old,new) in enumerate(zip(module,result))),
            'Changed bytes escape the exact profile/cache seed/runtime mode and runtime checksum spans')
    return result

def install(root,*,package,profile=None,game=None,control=False,profile_runtime=False,lease_only=False,hooks_only=False):
    root=Path(root).resolve(strict=True)
    module,manifest,info=generic_module(package)
    require(all(type(value) is bool for value in (control,profile_runtime,lease_only,hooks_only)) and
            sum((control,profile_runtime,lease_only,hooks_only))<=1,
            'Select at most one of control, runtime profiling, lease-only and hooks-only')
    runtime_mode=1 if control else 2 if profile_runtime else 3 if lease_only else 4 if hooks_only else 0
    require(runtime_mode==0 or MODE_FIELDS<=set(manifest),
            'This package has no audited native runtime mode selector')
    require(runtime_mode!=3 or manifest.get(MODE_CHOICES_FIELD,{}).get('lease-only')==3,
            'This package has no audited lease-only runtime mode capability')
    require(runtime_mode!=4 or manifest.get(MODE_CHOICES_FIELD,{}).get('hooks-only')==4,
            'This package has no audited hooks-only runtime mode capability')
    chosen=None
    if game is not None:
        catalog=cache_catalog(Path(package).resolve(strict=True),manifest)
        require(isinstance(game,str) and game in catalog,'Unknown packaged native cache game; use --list-games')
        chosen=catalog[game]
    selected=select_profile(root,profile)
    personalized=personalize_bytes(module,manifest,info,selected['wire'],
        cache_record=chosen['wire'] if chosen else None,cache_digest=chosen['record_digest'] if chosen else None,
        runtime_mode=runtime_mode)
    destination=root/INSTALL
    ordinary_path(destination.parent,root,directory=True)
    existed=destination.exists() or destination.is_symlink()
    previous=read_regular(destination,root,maximum=MAX_BYTES+64) if existed else None
    result={'build':info['build'],'pair_path':selected['summary']['pair_path'],
        'profile':str(selected['path'].relative_to(root)),'module_sha256':sha(personalized),
        'installed_file':str(INSTALL),'already_installed':previous==personalized}
    if MODE_FIELDS<=set(manifest):result['runtime_mode']=mode_codec().MODE_NAMES[runtime_mode]
    if chosen:result.update(game=chosen['slug'],game_title=chosen['title'],image_sha256=chosen['image_sha256'])
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
    parser.add_argument('sd_root',type=Path,nargs='?')
    parser.add_argument('--profile',type=Path,help='Explicit canonical profile path on this card, relative to its root or absolute')
    parser.add_argument('--package',type=Path,default=Path(__file__).resolve().parent,
                        help=argparse.SUPPRESS)
    selection=parser.add_mutually_exclusive_group()
    selection.add_argument('--game',help='Bind one packaged immutable executable proof; see --list-games')
    selection.add_argument('--list-games',action='store_true',help='List packaged cache records without opening an SD card')
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--control',action='store_true',help='Compare the same loader with virtual VMU disabled')
    mode.add_argument('--profile-runtime',action='store_true',help='Enable VMU runtime timing diagnostics')
    mode.add_argument('--lease-only',action='store_true',
        help='Compare the same reserved RAM without virtual VMU hooks or game save writes')
    mode.add_argument('--hooks-only',action='store_true',
        help='Compare installed VMU hooks with physical Maple traffic and no virtual replies or game save writes')
    args=parser.parse_args()
    try:
        if args.list_games:
            require(not (args.control or args.profile_runtime or args.lease_only or args.hooks_only),
                    'Runtime mode selection requires an SD card installation')
            for slug,item in sorted(list_games(args.package).items()):print(slug+'\t'+item['title'])
            return
        if args.sd_root is None:parser.error('sd_root is required unless --list-games is selected')
        result=install(args.sd_root,package=args.package,profile=args.profile,game=args.game,
            control=args.control,profile_runtime=args.profile_runtime,lease_only=args.lease_only,hooks_only=args.hooks_only)
        action='Already installed' if result['already_installed'] else 'Installed'
        if result.get('runtime_mode')=='control':
            print(action+' native loader control '+result['build']+' using '+result['pair_path'])
            print('Control mode: virtual VMU is disabled; existing prepared saves remain on the card.')
        elif result.get('runtime_mode')=='lease-only':
            print(action+' native RAM reservation comparison '+result['build']+' using '+result['pair_path'])
            print('Lease-only mode: reserved RAM remains allocated; virtual VMU hooks and game save writes are disabled.')
        elif result.get('runtime_mode')=='hooks-only':
            print(action+' native hooks comparison '+result['build']+' using '+result['pair_path'])
            print('Hooks-only mode: VMU hooks remain installed; physical Maple traffic passes without virtual replies or game save writes.')
        else:
            print(action+' native persistent VMU test '+result['build']+' using '+result['pair_path'])
            if 'game' in result:print('Executable cache: '+result['game_title']+' ('+result['game']+'); live console guards remain enabled.')
            if result.get('runtime_mode')=='profile':print('VMU runtime profiling enabled; follow README-FIRST.md for the diagnostic report.')
        print('Image, journal, identity and preparation profiles were unchanged.')
        print('Safely eject the card, then follow README-FIRST.md.')
    except (OSError,ValueError,KeyError,TypeError,UnicodeError) as error:
        parser.exit(2,'Native VMU personalization refused: '+str(error)+'\n')

if __name__=='__main__':main()
