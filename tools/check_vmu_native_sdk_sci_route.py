#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the opt-in SDK-owned SCI callback route using linked SH instructions.

The SDK's complete interrupted/FPU frame remains its responsibility. This
closed model executes the resident callback, ownership predicate, hook/unhook
and dispatcher; only the deep reader C payload is an ABI-compliant adversary.
It proves routing and state preservation, not console progress or performance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from test_vmu_native_sci_sdk_restore import InterruptCPU,owner_image,SDK_DISPATCH,SDK_EVENT
from test_vmu_native_heap_execution import SH,MASK,signed
from sh4_delay_slots import validate_delay_slot
from check_vmu_native_worker_gateway import read_elf,symbol_bytes

MARKER='__kui_native_sdk_sci_route_'
COLD_PROGRESS_MARKER='__kui_native_cold_progress_stage'
SDK_READY=b'SDK SCI ROUTE READY / LEVEL 1'
SDK_PROGRESS=(b'COLD 40 SDK ROUTE BIND BEGIN',SDK_READY,b'COLD 41 SDK ROUTE BOUND')
MULTI_OWNER_MARKER='__kui_native_sdk_sci_multi_owner_stage'
STRUCTURAL_MARKER='__kui_native_sdk_sci_structural_'
OWNER_CONSTANTS={
    'Evolution1':dict(bytes=2143652,digest='ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35',
        default=0x8c1d9f50,common=0x8c1da274),
    'Evolution2':dict(bytes=4024288,digest='52e00d2b3e9db00016b1f6d782cf8a2141617f3d00bd628200de757fd2e11605',
        default=0x8c1add0c,common=0x8c1ae030),
}
IPRB=0xffd00008
REGION=0x8c420400
SDK_VBR=0x8c00f400
ERI_SLOT=SDK_VBR+0x25c
RXI_SLOT=SDK_VBR+0x260
DEFAULT=0x8c1add0c
COOKIE=0x53434931
COOKIE_AT=REGION+0x530
DEFAULT_AT=REGION+0x534
OWNER_AT=REGION+0x538
FRAME=REGION+0x240
HOOKED=REGION+0x2d0
REARMS=REGION+0x2e4
LOAN=REGION+0x2f4
GUARD=0x8c007ba0
PRIVATE_TOP=0x8c007cf0
OWNER_SP=0x8c00ed00
RETURN=0x8c008a00
# Reviewed immutable resident function bodies. Relocation/literal bindings are
# checked as part of each complete pin and the typed descriptor below.
PINS={
    '_sdk_sci_ready':(80,'f5f233456192a049dbc3b50e23054c866793ed3e6fb140ca4edc3a47c2b4fca5'),
    '_hook':(104,'2bf6e6a20e9bdb206adeb76838331d548586dfeac634362bb303351b2b57c521'),
    '_unhook':(68,'d73a29319264ea8cbcba29b9825319f21d8befbd7dd8519d0679896fecd2dbe9'),
    '_kui_retail_sdk_sci_dispatch':(124,'282e815b796ea367e6ca53c938e04926ad4a83f9c68fa2b82094a415ef452206'),
    '_kui_retail_sdk_sci_entry':(152,'eb38f74342dafb9934a5f62b8b3cbc5bc55feb14d66fbc7af571b6a19f408116'),
}
STAGE_PINS={
    '_native_sdk_sci_route_bind':(692,'3a324dfb4b14f6d3b996a4a14408951d827836b431b73cc1181f1653e239e9cd'),
    '_kui_native_sdk_sci_route_admit':(800,'6924792714a86593648c5b608797af4e5d367c55b1e7cc55312b97a9df551b82'),
}
RA_PINS={
    '_sdk_sci_ready':(80,'cc136e30a9192db0ee681a5fd1823950d828b6e542eb2024d88afa29cc0dffc6'),
    '_hook':(104,'404d6102f077b57e7ca7345169940eb3498b4b7d87d34af29e40b03a25dbe779'),
    '_unhook':(68,'c2dbf8b636eb9c82b63902a0879c108588a0d591e2155df6c0bb7fd607e1cfab'),
    '_kui_retail_sdk_sci_dispatch':(124,'c3889fffbfc64eacd9699148765783812c3d5841334fed483024bdb2ae7b57e3'),
    '_kui_retail_sdk_sci_entry':(152,'62b195c002f1a9ad8a06a165a372ae4cafc47e35809af25064de32c6df25f023'),
}
RA_STAGE_PINS={
    '_native_sdk_sci_route_bind':(692,'ff2f3192e0ef834f748618f8974e31bda0d702e15e4466cf3e9361f560f3dd45'),
    '_kui_native_sdk_sci_route_admit':(800,'86a952cde09954838cfae3dca5d94522356c98cce8d6623e2b9468f66bb44925'),
}
# The diagnostic auxiliary leaves the low resident and SDK route unchanged.
# Its separately admitted larger blob moves only these cold-stage bindings;
# keep the preceding read-ahead artifact's exact literal pins intact.
RA_DIAGNOSTIC_STAGE_PINS={
    '_native_sdk_sci_route_bind':(692,'ef87deb32dd2d9ccef7954459f244c269c60339c4531bca0e3de94f3b16f3f90'),
    '_kui_native_sdk_sci_route_admit':(800,'bbdb6f34b5fb0362a142ec4feed63718fbd7159bca475eccb3a2faaad5c3a5c2'),
}
# The one-block PIO report and auxiliary blob shift only cold-stage literal
# bindings. Earlier SDK/read-ahead/diagnostic artifacts retain their pins.
RA_PIO_STAGE_PINS={
    '_native_sdk_sci_route_bind':(692,'0b8809cb4f2f5351de3d4d683ac6b17fab4d86c533b55d006b7c593a01891475'),
    '_kui_native_sdk_sci_route_admit':(800,'7f220ee4bfbfca0150bbd6a2b6e84be26d325ba6a9307d0911bae18a1c328ae8'),
}
RA_PIO_PAIR_STAGE_PINS={
    '_native_sdk_sci_route_bind':(692,'06e1614361b740747ec2137ff2251e20d1dbf094fce4ce9ee85374aca860c5b3'),
    '_kui_native_sdk_sci_route_admit':(800,'4e6ef241b9def59fa9deafd03a6554fdc1e460780d25fbfb086d7deab30ac0fe'),
}
# Separate cold-only owner expansion; every preceding profile keeps its pins.
MULTI_OWNER_STAGE_PINS={
    '_native_sdk_sci_route_bind':(748,'9cbcdc2e75b7e6ecab19784e5b9f880f9e22bde9399144328d876b7af7488724'),
    '_kui_native_sdk_sci_route_admit':(908,'ec64806bf0d9f966def872b037d0f9bdb1531f8275423c7f9d0cad1dd0786a09'),
    '_u32':(30,'6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
}
# The structural family has a bounded dynamic VBR and separate exact code
# pins. Earlier exact-owner artifacts retain every map above unchanged.
STRUCTURAL_PINS={
    '_sdk_sci_ready':(88,'878946332b6fe1651fd161314e795f1f7267d3fe054984affff9a987bf91d3b9'),
    '_hook':(104,'60c91970448795e525b79b4c00c4314418b95686a820971e727f6ca97b414e80'),
    '_unhook':(68,'c2dbf8b636eb9c82b63902a0879c108588a0d591e2155df6c0bb7fd607e1cfab'),
    '_kui_retail_sdk_sci_dispatch':(124,'ed20fd9d1f99d45e58b52379401ff11109c2dad341a168a4ef909f5c1ab297b5'),
    '_kui_retail_sdk_sci_entry':(152,'a859d76f04b9b0b7ade808afb8feda6071e145c11bacb6cadf52959d066a9b72'),
}
STRUCTURAL_STAGE_PINS={
    '_native_sdk_sci_route_bind':(916,'414de55141080d1112f413afe44a0e9e07423b1f3db783d991ee617d895cb3ed'),
    '_kui_native_sdk_sci_route_admit':(796,'bca05e993e391c6eb85a99a6c72529c98a2db02ed94ae606c0b72243174a160c'),
    '_kui_native_sdk_sci_route_discover':(1856,'ed8a15b5b37c15f439674f13fc042a893391ea3884d780ac19d5716600e2d2ba'),
    '_u32':(30,'6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    '_fingerprint':(88,'e20af76b12f03eacf8db050294266ab776acccdb206b65fef7caf8aacbf50a7a'),
    '_live_words':(80,'19abfcf3270af6ef4dbbd6bd2cdfaf0cd0b871ab32dddda689dc9a7828511474'),
    '_literal_at':(62,'1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
    '_normalized':(192,'81b53b61f1e4881bf120571908420787023e52cde950546614ff528d8ca6a940'),
}
WITNESS_STRUCTURAL_STAGE_PINS={
    '_native_sdk_sci_route_bind':(940,'36e179cecf7006dad0efe246025ba507c6d4d8a64f931ceebd85fe8ad7ca38bc'),
    '_kui_native_sdk_sci_route_admit':(796,'867487e84e88e5356bcb74b46edabbc0d0547b97f0ec60bce882a547b33e2a50'),
    '_kui_native_sdk_sci_route_discover':(1856,'1eddf50fcea34cd0d29abe58844be548c5e9ad36ed2d4091775b78e82df2cfb1'),
    '_u32':(30,'6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    '_fingerprint':(88,'50712a7089e4227859732393c4a2043bf88cc719a59359280f5cb523dd186b0b'),
    '_live_words':(80,'0ac896ed0e8ab0c8cf57936bd854f7853b10b928a2e0b0a99c1c5fdc730025e3'),
    '_literal_at':(62,'1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
    '_normalized':(192,'975fceb4f0112ddd63c12191f4a02efc71c1e31215d1608c84a7215f11153236'),
}
# The dedicated preflight failure page only relocates these binders' literal
# targets. Every changed word was matched to the same symbol/interior offset
# or unchanged progress string. Retain exact complete hashes for both layouts.
PREFLIGHT_WITNESS_STRUCTURAL_STAGE_PINS={
    '_native_sdk_sci_route_bind':(940,'11c8770f7a94476b7f34eed68692341053dad807c1f00206dfcd9bf2fd9ff8c3'),
    '_kui_native_sdk_sci_route_admit':(796,'7a236298eeba9526bcaed78e5a51577234d876cfa2cafe3a4a764409a7f59125'),
    '_kui_native_sdk_sci_route_discover':(1856,'d985601faed1cc11722a0e312b4e3b55d2daa8eca2150c760190f6f60910f708'),
    '_fingerprint':(88,'b6c1b8909b253ecdbedd73b8d9fd37cfbc87931183716a20f255cf3cd0fe0935'),
    '_live_words':(80,'58f2d2d6c72424c4c3046166a6291b1ee91516905c8ebd29c986efb22b95f0c9'),
    '_normalized':(192,'86855064c7be2d98fc94ce4b9fabb54051724bfcc2eddd46a2ca4fd5c1900941'),
}
MEMORY_CACHE={}
MEMORY_REFERENCES={}


# Exact whole bodies reviewed for the paired owned-timer/owner-stack profile.
GAME_STARTUP_STAGE_PINS={
    '_native_sdk_sci_route_bind':(940, '322a94b8aac17debfe7030e569085c348102ca4d61d2b7542532c4311a85f584'),
    '_kui_native_sdk_sci_route_admit':(796, 'e9811c7da4ac147c224f67f3855af8c87c3f4d8e9f692454d8f586dce6a9c05c'),
    '_kui_native_sdk_sci_route_discover':(1856, '233ed32ecf6c2282fd518b75a43e66f22d4e2944b3b08e9bb0cd26cb06a8bb8f'),
    '_u32':(30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    '_fingerprint':(88, 'b6c1b8909b253ecdbedd73b8d9fd37cfbc87931183716a20f255cf3cd0fe0935'),
    '_live_words':(80, '58f2d2d6c72424c4c3046166a6291b1ee91516905c8ebd29c986efb22b95f0c9'),
    '_literal_at':(62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
    '_normalized':(192, '4342bf401110e7042bd4096b624fe0688c8cb9fb181741be7806b4eee2aaaa9a'),
}

def require(test,message):
    if not test:raise ValueError(message)


def pio_companion(worker,proof):
    """Select new pins only for the independently executed exact PIO pair."""
    marker=worker[4].get('__kui_native_readahead_pio_worker')
    require(bool(marker)==bool(proof and proof.get('pio_candidate')),
            'read-ahead PIO worker/auxiliary proof differs')
    if marker:
        producer=proof.get('pio_producer',{})
        pair=pio_pair_companion(worker,proof)
        require(marker==(1,0,0,0xfff1) and proof.get('enabled') and
                proof.get('paired_markers') and proof.get('diagnostic_capture') and
                proof.get('schema')=='kui.native.read-ahead.audit.v1' and
                proof.get('artifact_sha256',{}).get('worker')==hashlib.sha256(worker[0]).hexdigest() and
                all(producer.get(key) is True for key in ('enabled','actual_linked_producer_executed',
                    'tick_only','no_optional_dma_or_new_commands','fetch_polled_true')) and
                (pair or producer.get('one_fresh_block_per_tick') is True and
                    producer.get('never_resume_or_seek_or_command') is True and
                    producer.get('maximum_blocks_per_visit')==1) and
                producer.get('fetch_token_limit')==256,
                'read-ahead PIO pins require the complete exact linked producer proof')
    else:pio_pair_companion(worker,proof)
    return bool(marker)


def pio_pair_companion(worker,proof):
    """The pair policy is separately marked and has exact measured gates."""
    marker=worker[4].get('__kui_native_readahead_pio_pair_worker')
    require(bool(marker)==bool(proof and proof.get('pio_pair_candidate')),
            'read-ahead PIO pair worker/auxiliary proof differs')
    if marker:
        producer=proof.get('pio_producer',{})
        require(marker==(1,0,0,0xfff1) and
                worker[4].get('__kui_native_readahead_pio_worker')==(1,0,0,0xfff1) and
                proof.get('enabled') and proof.get('paired_markers') and proof.get('pio_candidate') and
                proof.get('artifact_sha256',{}).get('worker')==hashlib.sha256(worker[0]).hexdigest() and
                all(producer.get(key) is True for key in ('enabled','pair_profile','pair_candidate',
                    'actual_linked_producer_executed','second_block_timing_admitted',
                    'second_limit_inclusive','positive_valid_intervals_required',
                    'per_block_positive_elapsed_required','all_ordinary_guards_rechecked_before_second',
                    'all_admission_guards_rechecked_between_blocks',
                    'actual_cache_copy_before_each_validity_publication',
                    'first_and_second_errors_do_not_issue_stop')) and
                producer.get('maximum_blocks_per_visit')==2 and
                producer.get('one_fresh_block_per_tick') is False and
                producer.get('second_elapsed_limit_ticks')==producer.get('second_limit_ticks')==1144 and
                producer.get('reserve_ticks')==1200 and producer.get('soft_window_ticks')==2344,
                'read-ahead PIO pair pins require the exact linked timing and ownership proof')
    return bool(marker)


def multi_owner_profile(stage):
    marker=stage[4].get(MULTI_OWNER_MARKER) if stage is not None else None
    require(marker is None or marker==(1,0,0,0xfff1),
            'SDK multi-owner stage marker is not absolute one')
    return marker is not None


def initialized_at(image,address,count):
    for section in image[3]:
        if section[1]==1 and section[2]&2 and section[3]<=address<address+count<=section[3]+section[5]:
            at=section[4]+address-section[3]
            return image[0][at:at+count]
    raise ValueError('unowned initialized address '+hex(address))


def pin_bodies(resident):
    found={}
    structural=resident[4].get(STRUCTURAL_MARKER+'resident')
    require(structural is None or structural==(1,0,0,0xfff1),'structural SDK resident marker differs')
    selected=STRUCTURAL_PINS if structural else RA_PINS if '__kui_native_readahead_resident' in resident[4] else PINS
    require(selected,'structural SDK resident pins have not been reviewed')
    for name,(count,digest) in selected.items():
        address,body,section=symbol_bytes(resident,name,2)
        require(section[2]&4 and 0x8c004000<=address<address+len(body)<=0x8c007ba0,
                'SDK SCI code escapes immutable low resident: '+name)
        require(len(body)==count and hashlib.sha256(body).hexdigest()==digest,
                'SDK SCI immutable instruction/literal pin changed: '+name)
        found[name]=(address,len(body),digest)
    require(bool(found),'SDK SCI reviewed function pins are missing')
    callback=resident[4]['_kui_retail_sdk_sci_entry'][0]
    descriptor,wire,section=symbol_bytes(resident,'_kui_native_sdk_sci_route_exports',1)
    pointer,cell_bytes,cell_kind,cell_owner=resident[4]['_kui_retail_async_region_storage']
    cell_section=resident[3][cell_owner]
    require(cell_bytes==4 and cell_kind==1 and cell_section[1]==8 and cell_section[2]&7==3 and
            0x8c004000<=pointer<pointer+4<=GUARD,
            'SDK SCI region pointer is not an owned four-byte low BSS cell')
    require(not section[2]&1 and wire==struct.pack('<5I',0x4b535231,1,20,callback,pointer),
            'SDK SCI descriptor type/constant/target/region binding differs')
    entry,word,entry_section=symbol_bytes(resident,'_kui_native_sdk_sci_route_entry',1)
    require(entry==resident[1][4]+36 and not entry_section[2]&1 and
            word==struct.pack('<I',descriptor),'SDK SCI entry+36 descriptor pointer changed')
    for name in ('_kui_retail_vector_interrupt','_kui_retail_vector_forward',
                 '_kui_retail_irq_entry','_kui_retail_release_100','_kui_retail_release_400',
                 '_kui_retail_release_600','_kui_retail_rehook','_vbr_set'):
        require(name not in resident[4],'SDK route still contains raw replacement-vector entry '+name)
    scalar_control_flow(resident,found)
    return found


def stage_pins(stage,*,diagnostic=False,pio=False,pio_pair=False,multi_owner=False,structural=False,
               worker=None,read_ahead_proof=None,resident=None):
    result={}
    read_ahead='__kui_native_readahead_stage' in stage[4]
    require(not diagnostic or read_ahead,'SDK diagnostic stage pins require the read-ahead profile')
    require(not pio or diagnostic,'SDK PIO stage pins require the diagnostic profile')
    require(not pio_pair or pio,'SDK PIO pair stage pins require the PIO profile')
    require(multi_owner==multi_owner_profile(stage),'SDK multi-owner stage profile differs')
    require(not multi_owner or pio_pair,'SDK multi-owner profile requires the exact PIO pair companion')
    marker=stage[4].get(STRUCTURAL_MARKER+'stage')
    require(marker is None or marker==(1,0,0,0xfff1),'structural SDK stage marker differs')
    require(structural==bool(marker),'structural SDK stage pin selection differs')
    require(not structural or multi_owner and pio_pair,'structural SDK pins require exact-owner and PIO companions')
    witness=False
    if worker is not None:
        from check_native_controller_failure_witness import witness_companion
        witness=witness_companion(worker,read_ahead_proof)
    progress=stage[4].get(COLD_PROGRESS_MARKER)
    require(bool(progress)==witness and (not witness or progress==(1,0,0,0xfff1) and structural and
            stage[4].get('__kui_native_dual_readers_stage')==(1,0,0,0xfff1)),
            'SDK cold witness pins require the complete exact worker proof and stage progress marker')
    from check_native_game_startup_profile import game_startup_profile
    owned_timers=game_startup_profile(worker,resident) if worker is not None else False
    require(not owned_timers or resident is not None and witness, 'Owned-timer SDK pins require paired low owner-stack proof')
    selected=GAME_STARTUP_STAGE_PINS if owned_timers else WITNESS_STRUCTURAL_STAGE_PINS if witness else STRUCTURAL_STAGE_PINS if structural else MULTI_OWNER_STAGE_PINS if multi_owner else RA_PIO_PAIR_STAGE_PINS if pio_pair else RA_PIO_STAGE_PINS if pio else \
        RA_DIAGNOSTIC_STAGE_PINS if diagnostic else \
        RA_STAGE_PINS if read_ahead else STAGE_PINS
    require(selected,'SDK multi-owner immutable stage pins are missing')
    for name,(count,digest) in selected.items():
        address,body,section=symbol_bytes(stage,name,2)
        require(section[2]&4 and 0x8ce00000<=address<address+len(body)<=0x8cfe0000,
                'SDK SCI cold binder/admission code escapes temporary stage')
        actual=hashlib.sha256(body).hexdigest()
        accepted=((count,digest),)
        if witness and not owned_timers and name in PREFLIGHT_WITNESS_STRUCTURAL_STAGE_PINS:
            accepted+=(PREFLIGHT_WITNESS_STRUCTURAL_STAGE_PINS[name],)
        require((len(body),actual) in accepted,
                'SDK SCI cold binder/admission instruction/literal pin changed: '+name)
        result[name]=(address,len(body),actual)
    return result


def multi_owner_table(stage):
    """Bind every exact owner fingerprint to the immutable typed C table."""
    from test_native_sdk_sci_owner_contract import OWNERS
    records={record['game']:record for record in OWNERS}
    expected=bytearray()
    for name in ('evolution2','evolution1'):
        record=records[name];constant=OWNER_CONSTANTS[name.replace('evolution','Evolution')]
        require(record['program_bytes']==constant['bytes'] and record['program_sha256']==constant['digest'],
                'SDK owner fixture identity differs from its typed cold tuple')
        expected.extend(struct.pack('<5I',record['program_bytes'],record['vector'],SDK_VBR,ERI_SLOT,RXI_SLOT))
        expected.extend(bytes.fromhex(record['program_sha256']))
        expected.extend(bytes.fromhex(record['windows'][0][2]))
        for address,count,digest in record['windows'][1:]:
            expected.extend(struct.pack('<2I',address,count)+bytes.fromhex(digest))
    address,body,section=symbol_bytes(stage,'_owners',1)
    require(len(body)==488 and not address&3 and section[2]&7==2 and
            0x8ce00000<=address<address+len(body)<=0x8cfe0000 and body==expected,
            'SDK immutable exact-owner fingerprint table differs')
    return dict(address=hex(address),bytes=len(body),sha256=hashlib.sha256(body).hexdigest(),
        typed_two_owner_records_checked=True,original_contract_fingerprints_matched=True,
        owner_digests=[records[name]['program_sha256'] for name in ('evolution1','evolution2')])


def scalar_control_flow(resident,pins):
    """Follow actual local code edges, excluding literal-pool data words."""
    for name,(base,count,_) in pins.items():
        pending=[base];seen=set()
        while pending:
            pc=pending.pop()
            if pc in seen:continue
            require(base<=pc<base+count and not pc&1,'SDK SCI code edge escapes pinned function')
            seen.add(pc);op=struct.unpack('<H',initialized_at(resident,pc,2))[0]
            require(op>>12!=15 and op&0xf0ff not in
                    (0x405a,0x406a,0x4056,0x4066,0x402e,0x4027) and op!=0x002b,
                    'SDK SCI code writes VBR/FPU state or uses raw RTE')
            delayed=op>>12 in (0xa,0xb) or op&0xf0ff in (0x400b,0x402b) or op==0xb or op>>8 in (0x8d,0x8f)
            if delayed:
                slot=struct.unpack('<H',initialized_at(resident,pc+2,2))[0]
                validate_delay_slot(slot,pc+2)
                require(slot>>12!=15 and slot&0xf0ff not in (0x405a,0x406a,0x4056,0x4066,0x402e,0x4027),
                        'SDK SCI delay slot changes protected control/FPU state')
                seen.add(pc+2)
            if op>>12==0xa:pending.append(pc+4+2*signed(op&4095,12))
            elif op>>12==0xb or op&0xf0ff==0x400b:pending.append(pc+4)
            elif op&0xf0ff==0x402b or op==0xb:pass
            elif op>>8 in (0x89,0x8b,0x8d,0x8f):
                pending.extend((pc+4+2*signed(op&255,8),pc+(4 if delayed else 2)))
            else:pending.append(pc+2)


def assert_c_abi(c,before):
    r,other,pr,sr,_,_,gbr,fpscr,fpul,fr,controls=before
    assert c.r[8:]==r[8:] and c.other==other and c.pr==pr and c.sr&~1==sr&~1
    assert (c.gbr,c.fpscr,c.fpul,c.fr,c.control)==(gbr,fpscr,fpul,fr,controls)


class SDKCPU(InterruptCPU):
    """Actual linked scalar SH with physical aliases and both integer banks."""
    def __init__(self,resident,seed=0,mask=15,bank=0,blocked=False,owner=b'',*,previous=DEFAULT,vbr=SDK_VBR):
        key=(id(resident[0]),id(owner))
        if key not in MEMORY_CACHE:
            backing=SH(owner)
            for kind,offset,address,_,count,_,_,_ in resident[2]:
                if kind==1 and count:backing.store_block(address,resident[0][offset:offset+count])
            MEMORY_CACHE[key]=bytes(backing.memory)
            MEMORY_REFERENCES[key]=(resident[0],owner)
        self.memory=MEMORY_CACHE[key];self.overlay={};self.writes=[]
        self.pc=self.executing=self.instructions=0
        rng=random.Random(seed)
        self.r=[rng.getrandbits(32) for _ in range(16)];self.r[15]=OWNER_SP
        self.other=[rng.getrandbits(32) for _ in range(8)]
        self.sr=0x40000100|(bank<<29)|(mask<<4)|(int(blocked)<<28)|(seed&1)
        self.pr=RETURN;self.mach=rng.getrandbits(32);self.macl=rng.getrandbits(32)
        self.gbr=rng.getrandbits(32);self.fpscr=rng.getrandbits(32);self.fpul=rng.getrandbits(32)
        self.fr=[rng.getrandbits(32) for _ in range(32)]
        self.sdk_vbr=vbr;self.eri_slot=vbr+0x25c;self.rxi_slot=vbr+0x260
        self.watched_slots={self.eri_slot,self.rxi_slot};self.table_reads=[]
        self.control=dict(vbr=vbr,ssr=rng.getrandbits(32),spc=rng.getrandbits(32)&~1,
                          sgr=rng.getrandbits(32)&~3,dbr=rng.getrandbits(32))
        self.symbols=resident[4];self.iprb=0x5a1f;self.event=0x4e0
        self.calls=0;self.accepted=[];self.callee_return=0;self.targets=set()
        self.pointer=self.symbols['_kui_retail_async_region_storage'][0]
        self.active=self.symbols['_kui_retail_hook_active'][0]
        self.fault=self.symbols['_kui_retail_hook_fault'][0]
        self.callback=self.symbols['_kui_retail_sdk_sci_entry'][0]
        self.write(self.pointer,REGION);self.write(self.active,0);self.write(self.fault,0)
        self.write(GUARD,0x4b554947);self.write(COOKIE_AT,COOKIE)
        self.previous=previous
        self.write(DEFAULT_AT,previous);self.write(OWNER_AT,vbr)
        self.write(self.eri_slot,self.callback);self.write(self.rxi_slot,self.callback)
        self.write(LOAN,0);self.write(FRAME+4,0x70);self.write(FRAME+8,0x10)
        self.write(HOOKED,0);self.write(REARMS,0xfffffffe)
        self.r[4]=self.event
        self.iprb_writes=[];self.vbr_writes=[];self.callee_mode='abi';self.corrupt_guard=False
        self.initial=self.snapshot();self.writes=[]

    def snapshot(self):
        return (self.r.copy(),self.other.copy(),self.pr,self.sr,self.mach,self.macl,self.gbr,
                self.fpscr,self.fpul,self.fr.copy(),self.control.copy())

    def plain(self,op,pc):
        if op&0xf0ff==0x0029:self.r[op>>8&15]=self.sr&1;return
        # LDC.L @Rn+,SR must change physical bank selection too.
        if op&0xf0ff==0x4007:
            n=op>>8&15;value=self.read(self.r[n]);self.r[n]=(self.r[n]+4)&MASK
            self.set_sr(value);return
        if op&0xf0ff==0x402e:self.vbr_writes.append(self.r[op>>8&15])
        super().plain(op,pc)

    def read(self,address,count=4):
        if address==IPRB:
            require(count==2,'SCI priority must be a halfword read');return self.iprb
        if address in self.watched_slots:self.table_reads.append((address,count))
        at=self.offset(address,count)
        return int.from_bytes(bytes(self.overlay.get(at+i,self.memory[at+i]) for i in range(count)),'little')

    def write(self,address,value,count=4):
        if address==IPRB:
            require(count==2,'SCI priority must be a halfword write')
            self.iprb=value&0xffff
            if hasattr(self,'iprb_writes'):self.iprb_writes.append(self.iprb)
            return
        at=self.offset(address,count);value&=(1<<(8*count))-1
        self.writes.append((self.executing,address,value,count))
        for i,b in enumerate(value.to_bytes(count,'little')):self.overlay[at+i]=b

    def callee(self):
        self.calls+=1;continuation=self.pr
        if self.callee_mode=='abi':
            assert self.sr&0x100000f0==0x100000f0 and self.r[15]==PRIVATE_TOP
        if self.corrupt_guard:self.write(GUARD,0xbad0bad0)
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd+self.calls)&MASK
        self.r[0]=self.callee_return;self.sr^=1
        self.mach^=0xaaaaaaaa;self.macl^=0x55555555;self.pr^=0x13572468
        self.pc=continuation

    def run_symbol(self,name,stop=RETURN):
        self.run_to(self.symbols[name][0],stop)
        return self


def callback_cases(resident,*,previous=DEFAULT):
    cases=0
    dispatcher=resident[4]['_kui_retail_sdk_sci_dispatch'][0]
    for seed in range(3):
        for mask in range(16):
            for bank in (0,1):
                for blocked in (False,True):
                    for mode in ('handled','forward','busy','badguard','postguard'):
                        c=SDKCPU(resident,seed,mask,bank,blocked,previous=previous)
                        c.targets={dispatcher};c.callee_return=previous if mode=='forward' else 0
                        if mode=='busy':c.write(c.active,1)
                        if mode=='badguard':c.write(GUARD,0)
                        if mode=='postguard':c.corrupt_guard=True
                        before=c.snapshot();c.writes=[]
                        forward=mode in ('forward','busy','badguard')
                        c.run_symbol('_kui_retail_sdk_sci_entry',previous if forward else RETURN)
                        r,other,pr,sr,mach,macl,gbr,fpscr,fpul,fr,controls=before
                        assert c.r[8:]==r[8:] and c.other==other and c.r[4]==r[4]
                        assert c.pr==pr and c.sr==sr
                        assert (c.gbr,c.fpscr,c.fpul,c.fr,c.control)==(gbr,fpscr,fpul,fr,controls)
                        assert c.calls==int(mode not in ('busy','badguard'))
                        assert c.read(c.fault)==int(mode in ('badguard','postguard'))
                        assert not c.iprb_writes and not c.vbr_writes
                        assert all((OWNER_SP-16<=address<OWNER_SP or address in (c.fault,GUARD))
                                   for _,address,_,_ in c.writes)
                        if mode in ('busy','badguard'):
                            assert not any(0x8c007ba0<address<PRIVATE_TOP for _,address,_,_ in c.writes)
                        cases+=1
    return cases


def ownership(c,variant):
    if variant=='cold':c.write(COOKIE_AT,0)
    elif variant=='wrongcookie':c.write(COOKIE_AT,COOKIE^1)
    elif variant=='wrongowner':c.write(OWNER_AT,SDK_VBR+0x400)
    elif variant=='vbrchanged':c.control['vbr']=SDK_VBR+0x400
    elif variant=='eri_changed':c.write(ERI_SLOT,c.previous)
    elif variant=='rxi_changed':c.write(RXI_SLOT,c.previous)
    else:assert variant=='ready'


def ownership_cases(resident,*,previous=DEFAULT):
    hooks=unhooks=dispatches=0
    variants=('ready','cold','wrongcookie','wrongowner','vbrchanged','eri_changed','rxi_changed')
    for mask in range(16):
        for variant in variants:
            for hooked in (0,1):
                for field in range(0,256,16):
                    c=SDKCPU(resident,field,mask,previous=previous);ownership(c,variant)
                    c.write(HOOKED,hooked);c.iprb=(0xa50f&~0xf0)|field
                    old=c.iprb;old_saved=c.read(FRAME+4);old_rearms=c.read(REARMS);last=0x10
                    old_vbr=c.control['vbr'];level=0x10 if variant=='ready' else 0
                    c.writes=[];before=c.snapshot();c.run_symbol('_hook');assert_c_abi(c,before)
                    assert c.control['vbr']==old_vbr and not c.vbr_writes
                    assert c.iprb==(old&~0xf0)|level
                    assert c.iprb_writes==([] if field==level else [(old&~0xf0)|level])
                    assert c.read(HOOKED)==1
                    assert c.read(FRAME+4)==(old_saved if hooked and field==last else field)
                    assert c.read(FRAME+8)==level
                    assert c.read(REARMS)==((old_rearms+int(hooked and field!=last))&MASK)
                    assert all(address in (HOOKED,FRAME+4,FRAME+8,REARMS,IPRB) or OWNER_SP-128<=address<OWNER_SP
                               for _,address,_,_ in c.writes)
                    hooks+=1
                    c=SDKCPU(resident,field,mask,previous=previous);ownership(c,variant)
                    c.write(HOOKED,hooked);c.iprb=(0xa50f&~0xf0)|field
                    old=c.iprb;old_vbr=c.control['vbr'];c.writes=[];before=c.snapshot();c.run_symbol('_unhook');assert_c_abi(c,before)
                    desired=(old&~0xf0)|0x70 if hooked and field==last else old
                    assert c.iprb==desired and c.iprb_writes==([desired] if hooked and field==last else [])
                    assert c.read(HOOKED)==0 and c.read(FRAME+8)==(0 if hooked else 0x10)
                    assert c.control['vbr']==old_vbr and not c.vbr_writes
                    assert all(address in (HOOKED,FRAME+8,IPRB) or OWNER_SP-128<=address<OWNER_SP
                               for _,address,_,_ in c.writes)
                    unhooks+=1
    irq=resident[4]['_kui_retail_async_irq'][0]
    for mask in range(16):
        for variant in variants:
            for event in (0x4e0,0x500,0x300,0x4e1,0):
                for active in (0,1):
                    for loan in (0,1,2,3):
                        for result in (0,1):
                            c=SDKCPU(resident,event,mask,previous=previous);ownership(c,variant)
                            c.r[4]=event;c.write(c.active,active);c.write(LOAN,loan)
                            c.targets={irq};c.callee_mode='deep-reader';c.callee_return=result
                            old_vbr=c.control['vbr'];c.writes=[]
                            before=c.snapshot();c.run_symbol('_kui_retail_sdk_sci_dispatch');assert_c_abi(c,before)
                            owns=variant=='ready' and event in (0x4e0,0x500) and not active and not loan
                            assert c.calls==int(owns)
                            assert c.r[0]==(0 if owns and not result else previous)
                            assert not c.iprb_writes and not c.vbr_writes and c.control['vbr']==old_vbr
                            assert all(OWNER_SP-128<=address<OWNER_SP for _,address,_,_ in c.writes)
                            dispatches+=1
    # Exercise a live ownership transition, including owner-field changes.
    # Cookie/VBR/table readiness may change while the engine remains hooked;
    # our previous level must never be learned as a new game-owned level.
    transitions=0
    for mask in range(16):
        for field in range(0,256,16):
            for variant in variants[1:]:
                c=SDKCPU(resident,field,mask,previous=previous);c.iprb=0xa50f|field
                c.run_symbol('_hook');assert c.read(FRAME+4)==field
                assert c.read(FRAME+8)==0x10 and c.iprb&0xf0==0x10
                ownership(c,variant)
                c.run_symbol('_hook');assert c.read(FRAME+4)==field
                assert c.read(FRAME+8)==0 and c.iprb&0xf0==0
                c.run_symbol('_unhook');assert c.iprb&0xf0==field
                assert c.read(HOOKED)==c.read(FRAME+8)==0
                # Ready again, then lose a slot while a real reader is live.
                c=SDKCPU(resident,field,mask,previous=previous);c.iprb=0xa50f|field;c.run_symbol('_hook')
                ownership(c,variant);c.targets={irq};c.callee_mode='deep-reader'
                c.r[4]=0x4e0;c.run_symbol('_kui_retail_sdk_sci_dispatch')
                assert c.calls==0 and c.r[0]==previous
                assert c.iprb&0xf0==0 and c.read(FRAME+4)==field and c.read(FRAME+8)==0
                assert not c.vbr_writes;transitions+=1
                # Foreign/serialized ownership must take precedence over a
                # lost-route repair, including when our SCI field is live.
                for reason in ('active','loan','event'):
                    c=SDKCPU(resident,field,mask,previous=previous);c.iprb=0xa50f|field;c.run_symbol('_hook')
                    ownership(c,variant);c.r[4]=0x300 if reason=='event' else 0x4e0
                    if reason=='active':c.write(c.active,1)
                    if reason=='loan':c.write(LOAN,1)
                    c.targets={irq};c.callee_mode='deep-reader';c.iprb_writes=[];c.writes=[]
                    c.run_symbol('_kui_retail_sdk_sci_dispatch')
                    assert c.calls==0 and c.r[0]==previous and not c.iprb_writes
                    assert c.read(FRAME+8)==0x10 and c.read(FRAME+4)==field
                    assert all(OWNER_SP-128<=address<OWNER_SP for _,address,_,_ in c.writes)
                    transitions+=1
    return hooks,unhooks,dispatches,transitions


class BinderStop(Exception):
    def __init__(self,phase,detail):
        self.phase,self.detail=phase,detail
        super().__init__(f'phase{phase:02X}/detail{detail:02X}')


class ColdBinderCPU(SDKCPU):
    """Execute the actual cold binder with distinct P1 cache and P2 RAM.

    memcpy/memcmp are faithful bounded ABI substitutes; the independently
    host-tested pure admission is a typed-plan substitute. The binder itself,
    selected-reader bounds, every branch/store/readback and OCBP execute.
    """
    def __init__(self,stage,resident,*,cache=True,payload=0x8c400000,stream=0,owner_name='Evolution2'):
        self.owner_constants=OWNER_CONSTANTS[owner_name]
        self.owner_name=owner_name
        mark=stage[4].get(COLD_PROGRESS_MARKER)
        require(mark is None or mark==(1,0,0,0xfff1),'cold progress stage marker differs')
        self.progress=mark is not None
        self.expected_display_lines=SDK_PROGRESS if self.progress else (SDK_READY,)
        self.display_lines=[]
        super().__init__(resident,seed=7,mask=15,bank=1,previous=self.owner_constants['default'])
        backing=SH(b'')
        for image in (resident,stage):
            for kind,offset,address,_,count,_,_,_ in image[2]:
                if kind==1 and count:backing.store_block(address,image[0][offset:offset+count])
        self.memory=bytes(backing.memory);self.ram={};self.cache={};self.cache_on=cache
        self.stage=stage;self.resident=resident;self.symbols=stage[4]
        self.api={stage[4][name][0]:name for name in
                  ('_memcpy','_memcmp','_native_report','_retail_display_line','_kui_native_sdk_sci_route_admit')}
        self.targets=set(self.api);self.apis=[];self.flushes=[];self.readbacks=[]
        self.admit_result=1;self.ready=False;self.writes=[]
        self.r[15]=0x8cfeffe0;self.pr=RETURN;self.r[4]=payload
        self.payload=payload;self.region=((payload&0x1fffffff)|0x80000000)+0x20400
        self.control['vbr']=SDK_VBR
        self.descriptor=resident[4]['_kui_native_sdk_sci_route_exports'][0]
        self.cell=resident[4]['_kui_retail_async_region_storage'][0]
        self.callback=resident[4]['_kui_retail_sdk_sci_entry'][0]
        self.low_end=resident[4]['__retail_resident_binary_end'][0]
        self.blob=0x8cea0000
        self.put_bytes(self.blob,self.memory[self.offset(0x8c004000,self.low_end-0x8c004000):
                                             self.offset(self.low_end,0)])
        for name,value in (('_resident_blob',self.blob),('_resident_bytes',self.low_end-0x8c004000),
                           ('_resident_limit',GUARD),('_exec_bytes',self.owner_constants['bytes'])):
            self.put32(stage[4][name][0],value)
        self.put_bytes(stage[4]['_native_driver'][0]+72,bytes.fromhex(self.owner_constants['digest']))
        self.put32(self.cell,self.region)
        control=resident[4]['_kui_vmu_observe_boot_control'][0]
        self.put32(control+8,4);self.put32(control+12,payload&0x1fffffff)
        self.put32(self.region+0x378,1);self.put32(self.region+0x470,stream)
        self.put32(ERI_SLOT,self.previous);self.put32(RXI_SLOT,self.previous)
        self.put32(self.region+0x53c,0xa5c39e17)
        self.initial=self.snapshot();self.writes=[]

    def put_bytes(self,address,data):
        at=self.offset(address,len(data))
        self.ram.update((at+i,b) for i,b in enumerate(data))

    def put32(self,address,value):self.put_bytes(address,struct.pack('<I',value&MASK))

    def ram_bytes(self,at,count):return bytes(self.ram.get(at+i,self.memory[at+i]) for i in range(count))

    def cached_line(self,at):
        line=at&~31
        if line not in self.cache:self.cache[line]=[bytearray(self.ram_bytes(line,32)),False]
        return line,self.cache[line]

    def read(self,address,count=4):
        if not hasattr(self,'ram'):return super().read(address,count)
        at=self.offset(address,count)
        if address&0xe0000000==0x80000000 and self.cache_on:
            value=bytes(self.cached_line(at+i)[1][0][(at+i)&31] for i in range(count))
        else:
            value=self.ram_bytes(at,count)
            if address&0xe0000000==0xa0000000 and self.region<=((address&0x1fffffff)|0x80000000)<self.region+0x760:
                self.readbacks.append((self.executing,address,int.from_bytes(value,'little')))
        return int.from_bytes(value,'little')

    def write(self,address,value,count=4):
        if not hasattr(self,'ram'):return super().write(address,value,count)
        at=self.offset(address,count);data=(value&((1<<(8*count))-1)).to_bytes(count,'little')
        self.writes.append((self.executing,address,value,count))
        if address&0xe0000000==0x80000000 and self.cache_on:
            for i,b in enumerate(data):
                _,entry=self.cached_line(at+i);entry[0][(at+i)&31]=b;entry[1]=True
        else:self.put_bytes(address,data)

    def plain(self,op,pc):
        self.executing=pc;n,m=op>>8&15,op>>4&15
        if op>>8==0xc8:self.flag(not(self.r[0]&(op&255)));return
        if op&0xf0ff==0x00a3: # OCBP writes a complete dirty32B line, then invalidates.
            at=self.offset(self.r[n],1)&~31;entry=self.cache.pop(at,None)
            if entry and entry[1]:self.ram.update((at+i,b) for i,b in enumerate(entry[0]))
            self.flushes.append(at);return
        if op>>8==0x84:self.r[0]=signed(self.read(self.r[op>>4&15]+(op&15),1),8)&MASK;return
        if op&0xf00f==0x600c:self.r[n]=self.r[m]&255;return
        if op&0xf0ff==0x4019:self.r[n]>>=8;return
        if op&0xf0ff==0x4029:self.r[n]>>=16;return
        if op&0xf00f==0x600a:
            value=self.r[m]+(self.sr&1);self.r[n]=(-value)&MASK;self.flag(value!=0);return
        super().plain(op,pc)

    def callee(self):
        name=self.api[self.pc];self.apis.append(name);continuation=self.pr;result=0
        if name=='_native_report':raise BinderStop(self.r[4],self.r[5])
        if name=='_memcpy':
            dest,source,count=self.r[4:7];assert count in (4,20)
            value=bytes(self.read(source+i,1) for i in range(count))
            for i,b in enumerate(value):self.write(dest+i,b,1)
            result=dest
        elif name=='_memcmp':
            left,right,count=self.r[4:7];assert count==20
            result=int(any(self.read(left+i,1)!=self.read(right+i,1) for i in range(count)))
        elif name=='_kui_native_sdk_sci_route_admit':
            assert self.r[4:7]==[0x8c010000,self.owner_constants['bytes'],0x8c010000]
            assert bytes(self.read(self.r[7]+i,1) for i in range(32)).hex()==self.owner_constants['digest']
            assert self.r[7]==self.stage[4]['_native_driver'][0]+72
            args=[self.read(self.r[15]+4*i) for i in range(6)]
            assert args[:5]==[SDK_VBR,self.sr,self.callback,self.stage[4]['_native_read'][0],0]
            out=args[5];assert self.r[15]+24<=out<=self.r[15]+80-32
            result=self.admit_result
            if result==1:
                plan=(1,32,SDK_VBR,self.owner_constants['common'],ERI_SLOT,RXI_SLOT,self.callback,self.previous)
                if hasattr(self,'plan_mutation'):
                    word,value=self.plan_mutation;plan=list(plan);plan[word]=value
                for i,value in enumerate(plan):
                    self.write(out+4*i,value)
        else:
            assert name=='_retail_display_line'
            require(len(self.display_lines)<len(self.expected_display_lines),'unexpected extra cold SDK display call')
            line=self.expected_display_lines[len(self.display_lines)];expected=line+b'\0'
            text=bytes(self.read(self.r[4]+i,1) for i in range(len(expected)))
            assert text==expected;self.display_lines.append(line)
            if line==SDK_READY:self.ready=True
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd+len(self.apis))&MASK
        self.r[0]=result&MASK;self.sr^=1;self.pr^=0x13572468;self.pc=continuation

    def patch_descriptor(self,word,value,*,live=True,immutable=True):
        if live:self.put32(self.descriptor+4*word,value)
        if immutable:self.put32(self.blob+self.descriptor-0x8c004000+4*word,value)
        if word==4 and live and immutable:
            self.cell=value
            if not value&3 and 0x8c004000<=value<GUARD:self.put32(value,self.region)

    def run_bind(self):
        self.run_symbol('_native_sdk_sci_route_bind')
        assert self.ready and tuple(self.display_lines)==self.expected_display_lines
        assert_c_abi(self,self.initial)
        assert self.read(ERI_SLOT|0x20000000)==self.callback
        assert self.read(RXI_SLOT|0x20000000)==self.callback
        assert self.read((self.region+0x534)|0x20000000)==self.previous
        assert self.read((self.region+0x538)|0x20000000)==SDK_VBR
        assert self.read((self.region+0x530)|0x20000000)==COOKIE
        assert self.read((self.region+0x53c)|0x20000000)==0xa5c39e17
        # Evicting all remaining dirty lines may not resurrect stale readiness.
        for at,(line,dirty) in tuple(self.cache.items()):
            if dirty:self.ram.update((at+i,b) for i,b in enumerate(line))
        self.cache.clear()
        assert self.read((self.region+0x530)|0x20000000)==COOKIE
        published=[address for _,address,_,_ in self.writes if address in
                   (ERI_SLOT,RXI_SLOT,self.region+0x534,self.region+0x538,self.region+0x530)]
        assert published==[self.region+0x534,self.region+0x538,ERI_SLOT,RXI_SLOT,self.region+0x530]
        return self


def check_cold_binder(stage,resident,*,self_test=False,refused_artifact=False,owner_name='Evolution2'):
    owner=OWNER_CONSTANTS[owner_name]
    if refused_artifact:
        _,body,_=symbol_bytes(stage,'_native_sdk_sci_route_bind',2)
        require(hashlib.sha256(body).hexdigest()=='e6f04a443bb4411f32628234d739042edb8793e3bd0d8e2bd61f00f4354988fc',
                'old refusal regression requires the shipped b656 binder')
        c=ColdBinderCPU(stage,resident,owner_name=owner_name)
        try:c.run_bind()
        except BinderStop as stop:
            require((stop.phase,stop.detail)==(10,3),'old binder refused at an unexpected checkpoint')
            assert '_kui_native_sdk_sci_route_admit' not in c.apis and not c.flushes and not c.ready
            return dict(shipped_b656_actual_binder_refusal_reproduced=True,phase=10,detail=3,
                        region_cell=hex(c.cell),standard_limit_literal='0x8c0077fc',publication_writes=0)
        raise ValueError('shipped binder unexpectedly accepted its out-of-standard-bound typed cell')
    positives=negatives=mutations=plan_refusals=0
    for cache in (False,True):
        for payload in (0x0c400000,0x8c400000,0xac400000):
            for stream in (0,1):
                ColdBinderCPU(stage,resident,cache=cache,payload=payload,stream=stream,owner_name=owner_name).run_bind();positives+=1
        for edge in ('code_end','limit_minus4'):
            c=ColdBinderCPU(stage,resident,cache=cache,owner_name=owner_name)
            c.patch_descriptor(4,c.low_end if edge=='code_end' else GUARD-4)
            c.run_bind();positives+=1
        cases=[('before_code_end',3),('at_limit',3),('misaligned',3),('p2_cell',3),
               ('standard_limit',3),('limit_minus4_bound',3),('bad_magic',3),('bad_callback',3),
               ('live_mismatch',3),('cell_mismatch',4),('pending',5),('preexisting_cookie',5),
               ('foreign_owner',6),('admission_refusal',7)]
        for case,detail in cases:
            c=ColdBinderCPU(stage,resident,cache=cache,owner_name=owner_name)
            if case=='before_code_end':c.patch_descriptor(4,c.low_end-4)
            elif case=='at_limit':c.patch_descriptor(4,GUARD)
            elif case=='misaligned':c.patch_descriptor(4,c.cell+1)
            elif case=='p2_cell':c.patch_descriptor(4,c.cell|0x20000000)
            elif case=='standard_limit':c.put32(stage[4]['_resident_limit'][0],0x8c007800)
            elif case=='limit_minus4_bound':c.put32(stage[4]['_resident_limit'][0],0x8c0077fc)
            elif case=='bad_magic':c.patch_descriptor(0,0)
            elif case=='bad_callback':c.patch_descriptor(3,c.callback|1)
            elif case=='live_mismatch':c.patch_descriptor(1,2,immutable=False)
            elif case=='cell_mismatch':c.put32(c.cell,c.region+32)
            elif case=='pending':c.put32(c.region+0x370,1)
            elif case=='preexisting_cookie':c.put32(c.region+0x530,COOKIE)
            elif case=='foreign_owner':c.admit_result=0
            elif case=='admission_refusal':c.admit_result=-1
            try:c.run_bind()
            except BinderStop as stop:
                assert (stop.phase,stop.detail)==(10,detail),(case,stop)
                assert not c.flushes and not c.ready
                assert not any(address in (ERI_SLOT,RXI_SLOT,c.region+0x530,c.region+0x534,c.region+0x538)
                               for _,address,_,_ in c.writes)
                negatives+=1
            else:raise ValueError('cold binder accepted malformed descriptor/context '+case)
    if multi_owner_profile(stage):
        for cache in (False,True):
            for word,value in ((0,0),(0,2),(1,0),(1,28),(2,SDK_VBR+0x400),
                               (4,ERI_SLOT+4),(5,RXI_SLOT+4),(6,0),(6,0xac007458)):
                c=ColdBinderCPU(stage,resident,cache=cache,owner_name=owner_name)
                c.plan_mutation=(word,value)
                try:c.run_bind()
                except BinderStop as stop:
                    assert (stop.phase,stop.detail)==(10,7)
                    assert not c.flushes and not c.ready
                    assert not any(address in (ERI_SLOT,RXI_SLOT,c.region+0x530,c.region+0x534,c.region+0x538)
                                   for _,address,_,_ in c.writes)
                    plan_refusals+=1
                else:raise ValueError('cold binder accepted a changed shared SDK plan field')
    if self_test:
        address,body,section=symbol_bytes(stage,'_native_sdk_sci_route_bind',2)
        ocbp=[i for i in range(0,len(body),2) if struct.unpack_from('<H',body,i)[0]&0xf0ff==0x00a3]
        require(len(ocbp)==4,'cold binder publication instruction count changed')
        for number,at in enumerate(ocbp):
            changed=bytearray(stage[0]);start=section[4]+address-section[3]
            struct.pack_into('<H',changed,start+at,0x0009)
            c=ColdBinderCPU((bytes(changed),*stage[1:]),resident,owner_name=owner_name)
            try:c.run_bind()
            except BinderStop as stop:
                assert (stop.phase,stop.detail)==(10,9 if number==3 else 8);mutations+=1
            else:raise ValueError('cold binder accepted an omitted cache publication')
    return dict(actual_linked_cold_binder_executed=True,game=owner_name,owner_digest=owner['digest'],positive_cases=positives,
        malformed_descriptor_or_context_cases=negatives,omitted_ocbp_mutation_rejections=mutations,
        malformed_shared_plan_refusals=plan_refusals,plan_shared_runtime_tuple_checked=bool(plan_refusals),
        selected_async_bound_checked=True,code_end_and_limit_minus4_admitted=True,
        code_end_minus4_and_limit_refused=True,p1_writeback_cache_and_p2_ram_distinct=True,
        full_cacheline_writeback_and_sentinel_preserved=True,cookie_published_last=True,
        pure_admission_call='typed ABI substitute; actual pure helper separately host-tested',
        memcpy_memcmp='faithful bounded ABI substitutes',console_performance_verified=False)


def check_sdk_dispatch(resident,startup):
    """Execute an independently hashed original SDK lookup and linked callback."""
    if resident[4].get(STRUCTURAL_MARKER+'resident') is not None:
        require(resident[4].get(STRUCTURAL_MARKER+'resident')==(1,0,0,0xfff1),
                'structural original SDK replay marker differs')
        from prove_native_sdk_sci_structural_runtime import original_dispatch
        return original_dispatch(resident,startup)
    from test_native_sdk_sci_owner_contract import owner_contract,contract_cases
    require(__debug__,'SDK original dispatcher model requires Python assertions')
    contract=owner_contract(startup)
    original_execution=contract_cases(contract)
    owner=contract['image'];cases=0
    previous=contract['default'];continuation=contract['continuation']
    require((contract['vbr'],contract['eri_slot'],contract['rxi_slot'])==
            (SDK_VBR,ERI_SLOT,RXI_SLOT),'original SDK owner runtime tuple differs')
    target=resident[4]['_kui_retail_sdk_sci_dispatch'][0]
    for mask in range(16):
        for bank in (0,1):
            for event in (0x4e0,0x500):
                for mode in ('handled','forward','busy','badguard','postguard'):
                    c=SDKCPU(resident,event,mask,bank,False,owner,previous=previous)
                    c.write(contract['event'],event);c.r[0]=c.sr
                    c.targets={target};c.callee_return=previous if mode=='forward' else 0
                    if mode=='busy':c.write(c.active,1)
                    if mode=='badguard':c.write(GUARD,0)
                    if mode=='postguard':c.corrupt_guard=True
                    # The original event lookup and JSR execute directly from
                    # the independently verified image. Full FPU save/restore
                    # remains outside this bounded integer/callback slice.
                    c.run_to(contract['dispatch'],c.callback)
                    assert c.r[4]==event and c.pr==continuation
                    assert c.control['vbr']==SDK_VBR
                    before=c.snapshot();c.writes=[]
                    forward=mode in ('forward','busy','badguard')
                    c.run_symbol('_kui_retail_sdk_sci_entry',previous if forward else continuation)
                    r,other,pr,sr,_,_,gbr,fpscr,fpul,fr,control=before
                    assert c.r[8:]==r[8:] and c.other==other and c.r[4]==event
                    assert c.pr==pr and c.sr==sr and c.calls==int(mode not in ('busy','badguard'))
                    assert c.read(c.fault)==int(mode in ('badguard','postguard'))
                    assert (c.gbr,c.fpscr,c.fpul,c.fr,c.control)==(gbr,fpscr,fpul,fr,control)
                    assert not c.vbr_writes and not c.iprb_writes
                    assert all(OWNER_SP-16<=address<OWNER_SP or address in (c.fault,GUARD)
                               for _,address,_,_ in c.writes)
                    cases+=1
    return dict(actual_sdk_integer_dispatch_to_linked_callback_cases=cases,
        owner_digest=contract['digest'],game=contract['game'],owner_program_bytes=len(owner),
        original_sdk_owner_contract={key:value for key,value in contract.items() if key!='image'},
        original_sdk_owner_execution=original_execution,
        exact_external_owner_source_and_program_sha_checked=True,
        actual_sdk_event_table_lookup_and_jsr_executed=True,
        return_to_sdk_continuation_and_default_forward_checked=True,
        busy_and_both_guard_fault_paths_checked=True,
        full_sdk_fpu_prologue_and_epilogue_executed=False,
        native_dispatch_c_payload='ABI-adversarial substitute',
        console_performance_verified=False)


def check(resident,*,worker=None,stage=None,self_test=False,read_ahead_proof=None):
    require(__debug__,'SDK SCI instruction model requires Python assertions')
    marker=resident[4].get(MARKER+'resident')
    callback=resident[4].get(MARKER+'callback')
    paired=worker[4].get(MARKER+'worker') if worker is not None else None
    staged=stage[4].get(MARKER+'stage') if stage is not None else None
    require(bool(marker)==bool(callback),'SDK SCI resident/callback marker mismatch')
    if not marker:
        require(not paired and not staged,'SDK SCI stage/worker marker without resident')
        return {'enabled':False}
    require(worker is not None,'SDK SCI route requires paired worker artifact')
    require(stage is not None,'SDK SCI route requires paired cold stage artifact')
    ra=resident[4].get('__kui_native_readahead_resident')
    ra_worker=worker[4].get('__kui_native_readahead_worker')
    ra_stage=stage[4].get('__kui_native_readahead_stage')
    require(bool(ra)==bool(ra_worker)==bool(ra_stage),'SDK read-ahead companion markers differ')
    if ra:
        require(ra==ra_worker==ra_stage==(1,0,0,0xfff1),'SDK read-ahead companion marker value differs')
        require(read_ahead_proof is not None and read_ahead_proof.get('enabled') and
                read_ahead_proof.get('paired_markers') and
                read_ahead_proof.get('schema')=='kui.native.read-ahead.audit.v1' and
                all(read_ahead_proof.get('artifact_sha256',{}).get(name)==hashlib.sha256(image[0]).hexdigest()
                    for name,image in (('resident',resident),('worker',worker),('stage',stage))),
                'SDK read-ahead profile requires the complete exact companion proof')
    diagnostic=worker[4].get('__kui_native_readahead_diagnostic_worker')
    require(bool(diagnostic)==bool(read_ahead_proof and read_ahead_proof.get('diagnostic_capture')),
            'SDK read-ahead diagnostic worker/auxiliary proof differs')
    if diagnostic:
        require(ra and diagnostic==(1,0,0,0xfff1) and
                read_ahead_proof.get('diagnostics',{}).get('enabled') and
                read_ahead_proof.get('diagnostics',{}).get('schema')==
                    'kui.native.read-ahead.diagnostics.audit.v1',
                'SDK diagnostic profile requires the exact linked auxiliary snapshot proof')
    pio=pio_companion(worker,read_ahead_proof)
    pio_pair=pio_pair_companion(worker,read_ahead_proof)
    require(not pio or diagnostic,'SDK PIO profile lacks its diagnostic worker')
    require(marker==callback==paired==(1,0,0,0xfff1),'SDK SCI paired marker mismatch')
    if stage is not None:require(staged==(1,0,0,0xfff1),'SDK SCI paired stage marker mismatch')
    require('__kui_native_sci_priority7_resident' not in resident[4],
            'SDK SCI route must retain level1 and exclude priority7')
    structural_resident=resident[4].get(STRUCTURAL_MARKER+'resident')
    structural_stage=stage[4].get(STRUCTURAL_MARKER+'stage')
    require(bool(structural_resident)==bool(structural_stage),'structural SDK stage/resident markers differ')
    if structural_resident:
        require(structural_resident==structural_stage==(1,0,0,0xfff1) and pio_pair,
                'structural SDK requires paired absolute markers and the exact PIO pair')
        from prove_native_sdk_sci_structural_runtime import check as structural_check
        return structural_check(resident,worker,stage,self_test=self_test,read_ahead_proof=read_ahead_proof)
    pins=pin_bodies(resident)
    multi_owner=multi_owner_profile(stage)
    owner_table=multi_owner_table(stage) if multi_owner else {'enabled':False}
    cold=stage_pins(stage,diagnostic=bool(diagnostic),pio=pio,pio_pair=pio_pair,multi_owner=multi_owner)
    owner_names=tuple(OWNER_CONSTANTS) if multi_owner else ('Evolution2',)
    binders={name:check_cold_binder(stage,resident,self_test=self_test,owner_name=name) for name in owner_names}
    binder=binders['Evolution2']
    callback_by_owner={name:callback_cases(resident,previous=OWNER_CONSTANTS[name]['default']) for name in owner_names}
    ownership_by_owner={name:ownership_cases(resident,previous=OWNER_CONSTANTS[name]['default']) for name in owner_names}
    callbacks=sum(callback_by_owner.values())
    hooks,unhooks,dispatches,transitions=tuple(sum(v[i] for v in ownership_by_owner.values()) for i in range(4))
    rejected=0
    marker_refusals=0
    if self_test:
        if multi_owner:
            for replacement in (None,(0,0,0,0xfff1),(2,0,0,0xfff1),(1,4,1,1),(1,0,0,1)):
                symbols=dict(stage[4])
                if replacement is None:symbols.pop(MULTI_OWNER_MARKER)
                else:symbols[MULTI_OWNER_MARKER]=replacement
                image=(*stage[:4],symbols)
                try:stage_pins(image,diagnostic=bool(diagnostic),pio=pio,pio_pair=pio_pair,multi_owner=True)
                except ValueError:marker_refusals+=1
                else:raise ValueError('SDK multi-owner pin selector accepted a changed profile marker')
            address,body,section=symbol_bytes(stage,'_owners',1)
            for at in range(len(body)):
                altered=bytearray(stage[0]);altered[section[4]+address-section[3]+at]^=1
                try:multi_owner_table((bytes(altered),*stage[1:]))
                except ValueError:rejected+=1
                else:raise ValueError('SDK multi-owner table accepted a changed fingerprint or tuple')
        for name,(address,count,_) in pins.items():
            section=resident[3][resident[4][name][3]];start=section[4]+address-section[3]
            for at in range(count):
                altered=bytearray(resident[0]);altered[start+at]^=1
                try:pin_bodies((bytes(altered),*resident[1:]))
                except ValueError:rejected+=1
                else:raise ValueError('SDK SCI audit accepted changed callback/ownership instruction')
        for image,pinned,probe in ((resident,
            {name:(record[0],record[1]) for name,record in {
                name:resident[4][name] for name in ('_kui_native_sdk_sci_route_entry','_kui_native_sdk_sci_route_exports')}.items()},
            pin_bodies),(stage,{name:(record[0],record[1]) for name,record in cold.items()},
                lambda image:stage_pins(image,diagnostic=bool(diagnostic),pio=pio,pio_pair=pio_pair,multi_owner=multi_owner))):
            if image is None:continue
            for name,(address,count) in pinned.items():
                section=image[3][image[4][name][3]];start=section[4]+address-section[3]
                for at in range(count):
                    altered=bytearray(image[0]);altered[start+at]^=1
                    try:probe((bytes(altered),*image[1:]))
                    except ValueError:rejected+=1
                    else:raise ValueError('SDK SCI audit accepted a changed typed binding/cold publisher')
    return dict(enabled=True,multi_owner=multi_owner,paired_sdk_sci_markers=True,immutable_descriptor_bound=True,
        multi_owner_admission_table=owner_table,multi_owner_marker_mutation_rejections=marker_refusals,
        cold_binder_execution_by_owner=binders,callback_cases_by_owner=callback_by_owner,
        ownership_cases_by_owner=ownership_by_owner,
        callback_private_stack_prefix_bytes=0,sdk_owner_stack_save_bytes=16,
        cold_binder_and_admission_pins=cold,cold_binder_instruction_modelled=True,cold_binder_execution=binder,
        cold_binder_source_reviewed=True,read_ahead_companion_required=bool(ra),
        read_ahead_diagnostic_companion_required=bool(diagnostic),
        read_ahead_pio_companion_required=pio,
        read_ahead_pio_pair_companion_required=pio_pair,
        cold_binder_source_sha256=hashlib.sha256((ROOT/'src/loader/vmu_native_sdk_sci_route_stage.inc').read_bytes()).hexdigest(),
        immutable_function_pins=pins,callback_instruction_cases=callbacks,
        hook_ownership_instruction_cases=hooks,unhook_ownership_instruction_cases=unhooks,
        dispatcher_instruction_cases=dispatches,live_route_loss_transition_cases=transitions,mutation_rejections=rejected,
        all_16_sdk_caller_imasks_checked=True,both_gpr_banks_checked=True,
        sdk_original_sr_sp_pr_r4_and_callee_saved_preserved=True,
        busy_low_stack_never_entered=True,bad_guard_forwarded_without_c=True,
        default_callback_tail_forwarded=True,bootstrap_priority_zero=True,retained_priority_one=True,
        vbr_and_both_sdk_slots_rechecked=True,no_vbr_write_or_raw_rte_entry=True,
        no_fpu_state_changes=True,native_deep_reader_c_payload_model='ABI-adversarial substitute',
        sdk_full_fpu_context_model='owned by SDK, outside this bounded model',
        console_cause_confirmed=False,console_performance_verified=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    p.add_argument('--self-test',action='store_true')
    p.add_argument('--refused-build',type=Path,help='optional preserved b656 build for actual DETAIL03 regression')
    p.add_argument('--startup',type=Path,action='append',help='repeat for each exact external owner startup')
    a=p.parse_args()
    try:
        r,w,s=(read_elf(a.directory/name) for name in ('resident-sci.elf','native-worker.elf','stage.elf'))
        read_ahead=None
        if '__kui_native_readahead_resident' in r[4]:
            from check_vmu_native_readahead import check as check_read_ahead
            read_ahead=check_read_ahead(a.directory,self_test=a.self_test)
        result=check(r,worker=w,stage=s,self_test=a.self_test,read_ahead_proof=read_ahead)
        if a.startup:
            owners=[check_sdk_dispatch(r,path) for path in a.startup]
            result['external_sdk_dispatch_by_owner']={proof['owner_digest']:proof for proof in owners}
            require(len(result['external_sdk_dispatch_by_owner'])==len(owners),'duplicate original SDK owner proof')
            result['external_sdk_dispatch']=next((proof for proof in owners if proof['game']=='evolution2'),owners[0])
            if result.get('structural_profile'):
                require({owner['digest'] for owner in OWNER_CONSTANTS.values()}<=set(result['external_sdk_dispatch_by_owner']) and
                        {proof.get('structural_family') for proof in owners}>={'full-fpu','compact-fpu'},
                        'structural SDK replay requires retained exact owners and both reviewed family ABIs')
            elif result.get('multi_owner'):
                require(set(result['external_sdk_dispatch_by_owner'])=={owner['digest'] for owner in OWNER_CONSTANTS.values()},
                        'multi-owner SDK replay requires both exact original owners')
        if a.refused_build:
            result['shipped_refusal_regression']=check_cold_binder(read_elf(a.refused_build/'stage.elf'),read_elf(a.refused_build/'resident-sci.elf'),refused_artifact=True)
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as e:
        p.exit(2,'SDK SCI-route audit refused: '+str(e)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
