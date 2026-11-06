#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the source-bound Smash Pack shared-card compatibility build."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

from check_vc2_ce_vmu_package import audit
from check_native_vmu_only import audit as audit_native_child
from package_vc2_ce_vmu_late_test import ROOT, MAIN, git, installed_module, sha, wire
from vmu_persist_profile import template_profile
from package_large_disc_test import executable_limit, vmu_build as disc_vmu_build
from check_vc2_stage_cached_relay import audit as relay_cache_audit

HELPER_BYTES=49152
CAPSULE_BYTES=50688
DEDICATED_PRODUCT='KUIVC2TEST'


def explicit_reader_control(module):
    """Bind comparison03 to its unique typed role, independently of captions."""
    payload=module[64:]
    words=(0x33434953,0x4c525443,1,24,3,0)
    tag=struct.pack('<6I',*words)
    first,second=(struct.pack('<I',word) for word in words[:2])
    at=payload.find(first)
    if (at<0 or at%4 or payload.count(first)!=1 or payload.count(second)!=1 or
        payload.find(second)!=at+4 or payload[at:at+24]!=tag):
        raise ValueError('Comparison03 must carry one aligned canonical explicit-route tag')
    return dict(variant=3,tag_payload_offset=at,tag_bytes=24,
        auto_selected_for_exact_smash_pack=True,global_vmu_preference_required=None)


def paired_helper(module,prepared):
    if len(prepared)!=HELPER_BYTES:raise ValueError('Persistent helper must contain all 48 KiB')
    payload=module[64:]
    capability=struct.pack('<8I',0x3146474b,0x574f4c46,1,32,0,48,0,3)
    if payload.count(capability)!=1:raise ValueError('Persistent capability must be unique flag 3/48')
    prefix=struct.pack('<4I',0x32554d56,0x44414f4c,2,44)
    if payload.count(prefix)!=1:raise ValueError('Persistent version 2 helper seed missing or ambiguous')
    at=payload.index(prefix)
    expected=struct.pack('<11I',0x32554d56,0x44414f4c,2,44,HELPER_BYTES,
                         zlib.crc32(prepared),0,0,0,0,0)
    if at%4 or payload[at:at+44]!=expected:raise ValueError('Persistent helper/seed pairing differs')
    template=template_profile()
    if payload.count(template)!=1:raise ValueError('Canonical launch-local UI profile is not unique')
    return dict(bytes=HELPER_BYTES,sha256=sha(prepared),crc32=f'{zlib.crc32(prepared):08x}',
        seed_payload_offset=at,seed_version=2,capability_flags=3,map_slots=48,
        capsule_bytes=CAPSULE_BYTES,capsule_blocks=99,config_bytes=1536,
        profile_template_bytes=4096,profile_template_sha256=sha(template),
        physical_identity_bound_at_launch=True,dedicated_product=DEDICATED_PRODUCT)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,default=ROOT/'build/vc2-ce-vmu-persist')
    parser.add_argument('--main',default=None)
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    parser.add_argument('--comparison-build',type=Path,action='append',default=[])
    parser.add_argument('--reader-control-build',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.suffix!='.zip':raise ValueError('Use a ZIP output filename')
    if git('status','--porcelain'):raise ValueError('Commit source before packaging')
    commit=git('rev-parse','HEAD').decode().strip();stamp=commit[:12]
    selected_main=args.main
    if selected_main is None:
        available=subprocess.run(['git','rev-parse','--verify','origin/main'],cwd=ROOT,
            stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
        selected_main=available.stdout.strip() if available.returncode==0 else MAIN
    main_commit=git('rev-parse',selected_main).decode().strip()
    subprocess.run(['git','merge-base','--is-ancestor',main_commit,commit],cwd=ROOT,check=True)
    proof=audit(args.build)
    proof['stage_cache_policy']=relay_cache_audit(args.build/'stage.elf')
    native_proof=audit_native_child(ROOT/'build/smash-native-child',transport=1)
    runtime=proof['helper'].get('persistent_runtime')
    if not (proof['status']=='PASS' and proof.get('late_loader') and runtime and
            runtime['durable_commit_ack_enabled'] and runtime['storage_services_linked'] and
            proof['persistent_writes'] and not proof['hardware_verified'] and
            proof['capability']['flags']==3 and proof['capability']['slots']==48):
        raise ValueError('Expected the distinct audited persistent VC2 build')
    if proof['helper']['comparison']['mode']!=0:
        raise ValueError('The top-level install must enable the normal VMU mode')
    files={
        'LICENSE.txt':(ROOT/'LICENSE').read_bytes(),
        'source/change-from-main.patch':git('diff','--binary',main_commit,commit),
        'source/package_vc2_ce_vmu_persist.py':Path(__file__).read_bytes(),
        'source/check_vc2_ce_vmu_package.py':(ROOT/'tools/check_vc2_ce_vmu_package.py').read_bytes(),
        'source/check_vc2_stage_cached_relay.py':(ROOT/'tools/check_vc2_stage_cached_relay.py').read_bytes(),
        'source/smash_reader_control.h':(ROOT/'include/kui/smash_reader_control.h').read_bytes(),
        'evidence/current-linked-package.json':wire(proof),
        'evidence/current-native-child-linked.json':wire(native_proof),
        'evidence/owner-source.json':(ROOT/'docs/evidence/vc2-ce-vmu-owner.json').read_bytes(),
        'evidence/gd-request-busy-source.json':(ROOT/'docs/evidence/vc2-gd-request-busy-source.json').read_bytes(),
        'evidence/gd-request-busy-execution.json':(ROOT/'docs/evidence/vc2-gd-request-busy-execution.json').read_bytes(),
        'evidence/smash-native-cold-child-source.json':(ROOT/'docs/evidence/smash-native-cold-child-source.json').read_bytes(),
        'evidence/smash-native-vmu-admission-source.json':(ROOT/'docs/evidence/smash-native-vmu-admission-source.json').read_bytes(),
        'evidence/runtime-geometry.json':(ROOT/'docs/evidence/vc2-ce-vmu-runtime-geometry.json').read_bytes(),
        'evidence/launch-profile-template.bin':template_profile(),
    }
    for item in args.evidence:
        target='evidence/'+item.name
        if target in files:raise ValueError('Duplicate evidence basename')
        files[target]=item.read_bytes()
    for target,module,elf,label in (
        ('KUI/runtime.kui',args.build/'runtime.kui',ROOT/'build/kui-runtime.elf','runtime'),
        ('KUI/apps/games/retail-physical.kui',args.build/'retail-native-foreground.kui',
         args.build/'entry.elf','persistent-reader'),
        ('KUI/apps/disc/native-disc.kui',ROOT/'build/disc-native/native-disc.kui',
         ROOT/'build/disc-native/entry.elf','disc'),
        ('KUI/apps/games/retail-smash-native.kui',
         ROOT/'build/smash-native-child/retail-smash-native.kui',
         ROOT/'build/smash-native-child/entry.elf','smash-native-child'),
    ):
        data,provenance=installed_module(module,elf,commit)
        files[target]=data;files['evidence/'+label+'.compile.json']=wire(provenance)
    # Main's GD Play changes affect both VMU slots as well as physical-only
    # boot. Ship their matching modules so updating the GUI cannot leave an
    # older slot-specific executable limit or preflight contract installed.
    for slot,directory,target in (
        ('A2',ROOT/'build/disc-vmu','KUI/apps/disc/native-disc-vmu.kui'),
        ('A1',ROOT/'build/disc-vmu-a1','KUI/apps/disc/native-disc-vmu-a1.kui'),
    ):
        data,artifacts,info=disc_vmu_build(directory,commit,executable_limit(commit),slot)
        files[target]=data;files.update(artifacts)
        files['evidence/disc-'+slot.lower()+'-capability.json']=wire(info)
    helper=(args.build/'helper/prepared.bin').read_bytes()
    pair=paired_helper(files['KUI/apps/games/retail-physical.kui'],helper)
    files['KUI/apps/games/vc2-vmu-helper.bin']=helper
    comparisons={}
    for directory in args.comparison_build:
        checked=audit(directory)
        checked['stage_cache_policy']=relay_cache_audit(directory/'stage.elf')
        comparison=checked['helper']['comparison'];mode=comparison['mode']
        if mode not in (1,3,4) or mode in comparisons or checked['persistent_writes']:
            raise ValueError('Expected distinct VMU-disabled texture comparison builds')
        if (comparison['virtual_card_enabled'] or comparison['runtime_storage_enabled'] or
            comparison['sampler_enabled']!=(mode==1) or comparison['dma_fence_enabled']!=(mode==1)):
            raise ValueError('Comparison mode does not match its admitted behavior')
        if comparison['maple_hook_installed']!=(mode!=4) or comparison['maple_text_extended']!=(mode!=4):
            raise ValueError('Comparison Maple installation policy differs')
        name={1:'01-VMU-OFF-FENCE',3:'02-VMU-OFF-BYPASS',4:'04-RESERVATION-ONLY-NO-MAPLE-HOOK'}[mode]
        prefix='Comparisons/'+name+'/'
        module,provenance=installed_module(directory/'retail-native-foreground.kui',directory/'entry.elf',commit)
        alternate=(directory/'helper/prepared.bin').read_bytes()
        pairing=paired_helper(module,alternate)
        # The complete high helper must retain the same code, RAM footprint,
        # providers and layouts. Only the typed immutable mode word differs.
        differences=[i for i,(a,b) in enumerate(zip(helper,alternate)) if a!=b]
        if len(alternate)!=len(helper) or not differences or len(differences)>4:
            raise ValueError('Comparison changed more than the immutable mode word')
        start=differences[0]&~3
        if any(i>=start+4 for i in differences) or struct.unpack_from('<I',helper,start)[0]!=0 or struct.unpack_from('<I',alternate,start)[0]!=mode:
            raise ValueError('Comparison difference is not one exact mode word')
        files[prefix+'KUI/apps/games/retail-physical.kui']=module
        files[prefix+'KUI/apps/games/vc2-vmu-helper.bin']=alternate
        if mode==4:files[prefix+'KUI/runtime.kui']=files['KUI/runtime.kui']
        files['evidence/comparison-'+str(mode)+'-linked.json']=wire(checked)
        files['evidence/comparison-'+str(mode)+'.compile.json']=wire(provenance)
        files[prefix+'README.txt']=(
            'Copy this folder\'s entire KUI tree over the installed files.\n'
            'Then launch Smash Pack and VC2 normally, with physical VMUs removed.\n'
            'No virtual VMU appears in this comparison. Continue without saving.\n'
            'Compare the same character/enemy textures as in the normal build.\n'
            'The GD reader, complete helper code and64KiB reservation are retained.\n'
            +('The Maple sampler and DMA fence remain active.\n' if mode==1 else
              'RAMEND is lowered as before; the original Maple text, padding\n'
              'and worker call are unchanged. No Maple hook is installed.\n' if mode==4 else
              'The Maple hook returns before any sampler, DMA fence, reply or VMU I/O.\n')+
            'Restore the entire normal KUI folder from the package afterwards.\n').encode()
        comparisons[mode]=dict(folder=prefix,behavior=comparison,helper=pairing,
            changed_helper_bytes=differences)
    if args.comparison_build and set(comparisons)!={1,3,4}:
        raise ValueError('Ship all three persistent-helper comparisons together')
    reader_control=None
    if args.reader_control_build:
        directory=args.reader_control_build.resolve()
        report=directory/'linked-package-final.json'
        subprocess.run([sys.executable,str(ROOT/'tools/check_native_standalone_foreground.py'),
            '--build',str(directory),'--report',str(report),'--require-clean','--require-ocram'],
            check=True,stdout=subprocess.DEVNULL)
        checked=json.loads(report.read_text())
        checked['stage_cache_policy']=relay_cache_audit(directory/'stage.elf',enabled=False)
        if (checked['result']!='PASS' or checked['capability']['heap_lease_bytes'] or
            checked['capability']['sdk_callbacks_published'] or checked['source_dirty']):
            raise ValueError('Reader control must be the physical-VMU foreground profile')
        module,provenance=installed_module(directory/'retail-native-foreground.kui',directory/'entry.elf',commit)
        checked['explicit_comparison_route']=explicit_reader_control(module)
        if struct.pack('<I',0x8c1d0658) in module:
            raise ValueError('Reader control contains the VC2 RAMEND patch target')
        prefix='Comparisons/03-NO-VMU-NO-RESERVATION/'
        files[prefix+'KUI/apps/games/retail-physical.kui']=module
        files[prefix+'KUI/runtime.kui']=files['KUI/runtime.kui']
        files['evidence/reader-control-linked.json']=wire(checked)
        files['evidence/reader-control.compile.json']=wire(provenance)
        files[prefix+'README.txt']=(
            'This is the previously reported clean-texture control.\n'
            'Copy this folder\'s entire KUI tree over the installation: both\n'
            'runtime.kui and apps/games/retail-physical.kui are required.\n'
            'The matching runtime recognizes this explicit control. Launch the\n'
            'unchanged Smash Pack image with Standard/Native boot, background\n'
            'reader, CD music and RAM patches OFF. Global Virtual VMU may be\n'
            'On or Off: this explicitly tagged control is selected either way.\n'
            'The startup banner must say SMASH PACK COMPARISON 03.\n'
            'Leave physical VMUs removed; continue without a memory card.\n'
            'This is the current generic foreground reader, without the VC2\n'
            'helper, Maple hook or64KiB CE reservation. Compare the same scene.\n'
            'Then restore the top-level normal KUI folder.\n').encode()
        reader_control=dict(folder=prefix,virtual_card_enabled=False,vc2_helper_enabled=False,
            vc2_ramend_patch=False,reserved_ce_helper_bytes=0,
            explicit_route=checked['explicit_comparison_route'],matching_runtime_included=True,
            console_execution_verified=False,module_sha256=sha(module))
    files['README.txt']=f'''K-UI Smash Pack texture split profile04 {stamp}

Copy this package's KUI folder over the existing SD installation. Install ALL
SEVEN files together: runtime.kui, apps/games/retail-physical.kui,
apps/disc/native-disc.kui, apps/games/vc2-vmu-helper.bin and
apps/games/retail-smash-native.kui, plus apps/disc/native-disc-vmu.kui and
apps/disc/native-disc-vmu-a1.kui.

This includes main {main_commit}. Use the same Sega Smash Pack SD image.

VC2 STARTUP AND NORMAL TEST
1. Remove physical VMUs; leave the controller connected.
2. Use Native/Default boot. Leave background reader, CD music and RAM patches
   OFF. The global Virtual VMU setting can be On or Off: this build selects
   the verified collection loader and its shared VC2 A2 card automatically.
   It does not change the global preference or other games' settings.
3. Launch Sega Smash Pack, pass its outer no-card prompt if shown, then VC2.
4. Check that VC2 sees a memory card. Save a recognizable setting or result.
5. Wait for saving and SD activity to finish. Power off, restart and enter
   VC2 again. Verify the same saved setting or result loads.
Report how long FILE OWNERSHIP takes. If it still takes a long time, photograph
the live VC2 VMU CHECKING FILE OWNERSHIP counters while it is running. They
refresh about once per second; all numbers are hexadecimal. ELAPSED MS and
READ MS distinguish overall work from SD transfer time. DMA BLOCKS and PIO
CALLS show which transfer path actually ran. FFFFFFFF timing means the stage
could not safely borrow an idle timer; the other counters are still useful.
No intentional pause has been added to startup.
The supplied video showed working DMA, zero PIO fallbacks, and an unavailable
timer. The changing numbers were the live ownership check. Its new dedicated
page clears previous text and retires cleanly before recovery/validation.
CPU CCR shows the live cache setting during the scan: it should normally read
00000100, with instruction-cache enable set and data-cache enable clear.
The setup writes00000908; its invalidation command bits then clear themselves.
The original post-bootstrap ownership scan ran with both caches disabled
(setup write00000808). This build uses the existing instruction-cache-only relay policy
for this VC2 stage and restores the game's original cache state afterward.
Console speedup is not yet measured. Compare the startup wait with build159.

The prior regular-launch photo came from the generic native VMU module:
its NATIVE VMU: LIVE OWNERSHIP line identifies that route. With the global
VMU setting On, the launcher previously bypassed the installed collection
loader. The corrected selector requires the original menu SHA/IP checks and
the installed collection module's capability before choosing this route.

GENESIS RPG TESTS
Use this exact route, with physical VMUs removed:
Games -> Sega Smash Pack details -> Y Options -> Advanced -> Developer tests
-> Phantasy Star II VMU or Shining Force VMU -> A.
These entries start the original
Genesis engine directly with the selected ROM. They present the SAME backing
card as A1, which that engine requires. VC2 continues to present it as A2.
The user has confirmed Phantasy Star II save/relaunch and Shining Force saving
through these direct entries. Their working native VMU implementation is retained.
The earlier invalid-launch-record fix is also retained. If a SMASH RPG VMU CHECK
screen unexpectedly appears, photograph its ONE page. The probe reports missing
callbacks, SDK presence/geometry, BUP state and actual virtual-card requests.
It adds no writes to the save card and disables itself when a usable card is seen.
Save inside the RPG first, then open the pack options menu with
A+B+X+Y+Start and choose Save Game. This stores cartridge SRAM, not an arbitrary
gameplay savestate. Finish SD activity before powering off. Restart the same
test entry, use the pack's Load Game, and verify the RPG's saved progress.
RPG saving still uses the direct entries above. Selecting an RPG inside the
normal Smash Pack menu is not the integrated VMU route yet. Restart K-UI to
change between VC2 and the direct RPG routes; seamless child switching is pending.

There is no intentional stop after the first save. If a VC2 fault screen appears,
photograph ALL THREE numbered pages. They cycle automatically. For any other
early refusal screen, photograph it once and report the last visible stage.

VC2 TEXTURE TEST - PROFILE04
The user confirmed profile02 still corrupts textures and profile03 renders
correctly. The cause is not yet isolated:03 also changes the reader and removes
the helper, installed Maple hook and64KiB reservation. No texture fix is claimed.
The next test is Comparisons/04-RESERVATION-ONLY-NO-MAPLE-HOOK/KUI.
1. Install the complete top-level KUI folder first, then copy the ENTIRE profile04
   KUI folder over it: runtime.kui, retail-physical.kui and vc2-vmu-helper.bin.
2. Keep Native/Default boot, background reader/CD music/RAM patches OFF and
   physical VMUs removed. Either global Virtual VMU setting is supported.
3. Launch the normal Smash Pack menu, then VC2. Continue WITHOUT a memory card;
   this comparison has no virtual VMU and cannot save. Compare the same scene.
4. Report whether the character/enemy textures are clean or corrupt. No new RPG
   save tests are needed for this comparison. Photograph any unexpected stop.
5. Restore the entire top-level KUI folder afterwards to restore virtual saves.

04 retains the persistent VC2 reader, helper/session layout, service ownership
and64KiB reservation. Only the kernel RAMEND word is changed; the original
Maple text bounds, bridge padding and worker instruction remain unchanged.
No Maple hook runs. Its helper differs from normal only in one immutable word.
A clean04 result implicates the installed Maple modifications; a corrupt04
result leaves the reservation/helper/reader differences under investigation.
Neither result alone identifies a particular instruction or memory collision.

The other controls remain available:01 disables the virtual card/runtime SD
work but retains Maple sampling and the DMA fence;02 also bypasses that work
inside the installed callback. Both retain the installed bridge and reservation.
03 uses the current generic foreground reader without the VC2 helper, Maple
hook or RAMEND change. Its matching runtime and tagged reader select it with
either global VMU setting; startup says SMASH PACK COMPARISON 03.
Existing card files are kept throughout these comparisons.

CARD AND STORAGE
One Sega Smash Pack VMU is reused for the collection, including the VC2 save
already tested. Its existing internal binding remains {DEDICATED_PRODUCT}; it
is not replaced or split into per-title cards. Each game can create its own
files on this card. Keep the card and its journal/identity files together.
The UI also generates a 50,688-byte session
capsule beside the helper; matching code is reused without rewriting the file.
Its current physical map is checked at launch and its config tail is resealed.
The distributed 4 KiB profile is a public template. Real card identity and
physical allocation are checked on the console before storage is admitted.

WHAT CHANGED
Profile04 adds an independently tested reserve-only installer branch. Full
original-source and callback validation still precede mutation. Tests compare
the entire kernel body and confirm only its RAMEND word differs; Maple text,
padding and worker remain original. Failed publication rolls RAMEND back.
All profiles retain identical helper code/layout and change only the mode word.
The normal save build retains its existing installed hook and storage behavior.
The VC2 persistent late relay now enables only its instruction cache during
cold ownership/recovery work. The data cache stays off and the existing P2
transition, invalidation and exact owner-state restoration are preserved.
This affects startup; it does not change the game-time reader or VMU code.
The ownership page uses aligned rows and a clean transition to phase results.
Comparison03 now has a typed explicit-route tag. The previous failure photo
showed build1734830d08df and NATIVE VMU: LIVE OWNERSHIP, identifying the generic
VMU module. With VMU On, an untagged generic control could fall back to that
older module. The new tag selects03 directly for the exact admitted Smash Pack
image, independently of the native VMU setting and without changing it.
Regular Smash Pack launch now inspects the admitted collection module even
when the global native VMU preference is On. Exact original menu/IP admission
and the collection capability are required. Ordinary installs, other games,
unknown revisions and the generic reader control keep their original routes.
VC2 startup uses 32-sector FAT windows, separate for each mirror, and a
temporary128KiB snapshot of the card after ownership admission and redo recovery.
The same sectors, mirrors and card contents are validated. Writes/syncs invalidate
the cache. This scratch memory belongs only to the stage and is discarded before
game execution. Directory/bitmap reads now use a 32-byte-aligned transfer buffer
as well; previously their unaligned destinations could force the slower PIO path.
Ownership target lookups reject impossible matches before the exact lookup.
No ownership checks are omitted. Cold-stage timing and existing SCI transfer
counters are displayed live. These changes do not alter the runtime VMU helper.
The Code Veronica fix is retained: mixed bare/explicit-disc names group after matching actual
release metadata and disc ordinals. Different releases and duplicate discs stay
separate. Native RPG diagnostics expose why the existing shared A1 card is refused.
Concurrent GD read requests now yield through WSEGACD's own Sleep import on
their original CE stack, then retry the same request after VMU storage release.
They receive a real request token instead of a refused texture read.
An eight-block (4 KiB) VMU read cache avoids repeated SD transactions for
already verified card blocks. Saved blocks invalidate only after durable
commit succeeds; ownership and block CRC checks still run on cache hits.
The native RPG tests verify the supplied original menu, IP and Genesis engine,
then use the original GIN child argument and startup contracts. Native SDK
discovery, heap reservation and guarded executable exits remain enforced.
Read-ahead remains OFF. This isolates save/load behavior for the console test.

RAM
The persistent helper reserves 64 KiB: 48 KiB for its image/fixed records and
16 KiB for lower working memory. This is reserved loader memory, not a measured
amount of free WinCE RAM. Prefetch remains disabled until separately measured.

VALIDATION SCOPE
The package audit binds linked ranges, helper CRCs, separate stack closures,
integer instructions, version 2 seed, capability 3 and module provenance.
The preceding VC2 build saved and loaded after a console power cycle, but its
startup was slow and textures remained wrong. Subsequent cold tests confirmed
Phantasy Star II save/relaunch and Shining Force saving. This package corrects
normal launch selection and retains the working save code and texture controls.
Fresh console testing of the corrected normal route remains required.
Profile02 showed bad textures on hardware. The valid tagged03 run now has
user-confirmed clean textures. Profile04 has passed source-bound host tests
and linked audits; it has not been tested on a console. The startup cache and
display changes remain present and do not establish a texture fix.
Sega Swirl uses a different WinCE executable and is not covered
by VC2's source-specific adapter yet. The other Genesis titles use the same
engine, but their persistent-save eligibility still needs their ROM headers;
the engine enables Save Game only for cartridges declaring SRAM.
No retail game files or private SD identities are included. The source patch
applies to the exact main commit above.
'''.encode()
    files['BUILD.json']=wire(dict(commit=commit,main_included=main_commit,
        purpose='VC2 profile04 texture split: retain reservation and reader without installing Maple hook',
        hardware_verified=False,persistent_writes=True,durable_commit_ack_enabled=True,
        virtual_card='shared Sega Smash Pack VMU (existing KUIVC2TEST)',read_ahead_bytes=0,
        virtual_card_per_title=False,
        card_slots=dict(vc2='A2',native_genesis='A1'),
        native_child_tests=['Phantasy Star II','Shining Force'],
        native_child_card_console_verified=False,native_missing_card_probe=True,
        native_child_reported_results=dict(phantasy_star_ii='save and relaunch load',shining_force='save'),
        normal_smash_auto_collection_route=True,
        collection_route_requires_exact_menu_and_persistent_module=True,
        global_native_vmu_preference_changed=False,
        vc2_stage_instruction_cache_enabled=True,vc2_stage_data_cache_enabled=False,
        vc2_original_game_cache_state_restored=True,vc2_profile02_textures_reported_corrupt=True,
        vc2_profile03_textures_reported_clean=True,
        vc2_profile04_console_verified=False,
        vc2_profile04_kernel_delta='RAMEND word only; Maple text/padding/worker original',
        vc2_texture_defect_fixed=False,texture_comparisons=comparisons,
        vc2_reader_control=reader_control,
        vc2_startup_fat_window_blocks=32,vc2_stage_only_card_cache_bytes=131072,
        vc2_stage_metadata_alignment_bytes=32,vc2_live_ownership_counters=True,
        recv_mixed_disc_name_grouping=True,
        seamless_hot_child_transition=False,
        map_slots=48,capability_flags=3,helper=pair,
        reserved_helper_bytes=65536,worker_stack_bytes=4096,
        concurrent_gd_requests='source-admitted native Sleep and exact request retry',
        refused_zero_token_substituted_during_vmu_loan=False,
        vmu_read_cache_bytes=4096,
        free_wince_ram_measured=False,expected_checkpoint='save, restart, and load retained data',
        intentional_checkpoint_stop=False,
        installed={name:dict(bytes=len(data),sha256=sha(data))
                   for name,data in files.items() if name.startswith('KUI/')}))
    files['SHA256SUMS.txt']=''.join(sha(data)+'  '+name+'\n'
        for name,data in sorted(files.items())).encode()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    temporary=args.output.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,data in sorted(files.items()):archive.writestr(name,data)
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() is not None:raise ValueError('ZIP CRC validation failed')
        with tempfile.TemporaryDirectory() as directory:
            archive.extractall(directory)
            for name,data in files.items():
                if (Path(directory)/name).read_bytes()!=data:raise ValueError('ZIP extraction differs: '+name)
    subprocess.run(['unzip','-t',str(temporary)],check=True,stdout=subprocess.DEVNULL)
    temporary.replace(args.output)
    print(json.dumps(dict(status='PASS',file=str(args.output),bytes=args.output.stat().st_size,
        sha256=sha(args.output.read_bytes()),commit=commit,main_included=main_commit,
        helper=pair,extract_verified=True,persistent_writes=True,durable_commit_ack_enabled=True)))


if __name__=='__main__':main()
