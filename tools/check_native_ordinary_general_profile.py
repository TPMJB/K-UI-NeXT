#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Modern ordinary General ABI and witness pins from pristine 3a6.

The ordinary reader has no session capability, retains every reply staging
slot, and has a compiler-outlined controller/raw-reader closure. Historical
General and session profiles remain separate. All expected constants below
come from independently built ordinary-reference A1/A2 workers.
"""
import hashlib
import struct
from check_vmu_native_worker_gateway import symbol_bytes
from check_native_session_witness_profile import binary, signatures, SHAPE_TABLE
ONE=(1,0,0,0xfff1)
BASELINE_COMMIT='3a6ae064bb3f456da8d71587a8db0ce974f139cf'
REFERENCE_ELF_SHA256={1: 'a700e5970ce4acdc997b1d58b0aaa0f6a1307d37a37791dcfe2d8418ec552a1a',
 2: 'f7d4ecef458c94218100dbc46eb961500462ffa9f04966ffb8711ac03919f868'}
PINS={1: {'_controller_context_valid': ('90ea99a592562009bd84ae6c6b365e5241513c56a5bfe132aab91d16b4f3e935', 72),
     '_controller_layout_valid': ('c35affd52b29c1602e2fc9640bc0279d5ee8d2c70849f05d1203f503b446aa1c', 748),
     '_controller_pair_valid': ('b7007b20806bea9a50bc5e694862b7ac72ee41043fda4a56d779705e075177b0', 196),
     '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
     '_held_report_gesture': ('c37a4feec01d1d60de23ee9130e045c06faaf4b1cf8583041c9054416851d026', 36),
     '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
     '_kui_vmu_native_exit_shape_valid.constprop.0': ('73f960510420f9114e16729f6b0f63303104db03ae2507cfecc87ada91e381ad',
                                                      132),
     '_kui_vmu_native_passive_complete': ('98bd7b2ce67f8adfc892dbad266f681eb7050f3ef786594a293a72b95bb8c297', 3440),
     '_kui_vmu_native_passive_submit': ('41cb6535514de56e9b047c89e4ac61e8a2d2d5c9ea6438a2140de112f718b6e7', 1408),
     '_kui_vmu_native_patch_encode': ('2ee6e8b4c1272c5916db0236959a09af2e07a5f4622d7c655982b4bd7eddc4dd', 168),
     '_kui_vmu_native_terminal_dispatch_span.constprop.0': ('843a0460a5268fd47a94404e6a0df8e6269c0a7049aa71a6d32196e25c47d2ea',
                                                            128),
     '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': ('d045495217f025c5e362cc651783a2d791db46aab61bc9004d882d9635085d7b',
                                                              108),
     '_kui_vmu_native_timer_callback_owned': ('035d109aa67cbb6b585f263dd0d0b64d7679386bdf457ebbbbe84894abaabce6', 92),
     '_memcpy': ('7fa8d1eee8135c7721a67c5ada882db55d17c24a41b305927c114c697ac5066d', 72),
     '_normalize_heap_dispatch': ('3419cac763b385db2a6f931e4df44172bb9dff4fbaba7ed25f3c5534524762ec', 276),
     '_normalize_heap_dispatch_if_routing': ('54551c3ce68e045933e39c229955468e2bee6ead6fe9766f42935d1593309802', 40),
     '_patch_ram': ('ee99409b66f8e0fff23f34fae0ef53e503429aeea4ce8117e1d584ea56ecb7c5', 80),
     '_poll_lease_valid': ('ac832b82ea8d821dede21c5e9e825e9f7a4479be1dae662c525cdd4f92fb64a8', 168),
     '_read_heap': ('98154596efda0b82f86e858c35201c19f6d8773459cfc703474205cf1c2a998f', 48),
     '_read_value': ('114eb5e180ab1a3e10d2991991db4cd64069ca22b1c85a1f612a9cc5d9029f58', 36),
     '_read_value.part.0': ('1af38c39e6bb370c70947263231e5c742c989f186ac8af979a2654bafde206e2', 104),
     '_reader_capture_region': ('ba0fe03f2355bdee2e98166022692dab9fa03e444a7c52f890b3407c656a7165', 100),
     '_reader_cooperative_service': ('17975bf4c258a07eeaaf7638e5cab6b690aff3ef6c7cba40b2b47296152748bf', 100),
     '_reader_readahead_exports': ('e180abcbd6e9a2144e4ae5e0cc25c009c49d65018c5815367873b5fb16d36ccd', 200),
     '_reader_streaming': ('6e039ba01afc9b34ddd3d82bdddac98662af88639e1c5e1a1989a1fcfd563759', 120),
     '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
     '_timer_runtime_in': ('7d3d7f5268d3a727c863c90fde2de8693613748f81e564271569b4ef3f4ecf8e', 26),
     '_timer_runtime_ram': ('2cd3fc97ffa9bd27467b7c6e12ae1bcebcdb41e591742863a7cac78ad34661dd', 104)},
 2: {'_controller_context_valid': ('90ea99a592562009bd84ae6c6b365e5241513c56a5bfe132aab91d16b4f3e935', 72),
     '_controller_layout_valid': ('c35affd52b29c1602e2fc9640bc0279d5ee8d2c70849f05d1203f503b446aa1c', 748),
     '_controller_pair_valid': ('b7007b20806bea9a50bc5e694862b7ac72ee41043fda4a56d779705e075177b0', 196),
     '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
     '_held_report_gesture': ('c37a4feec01d1d60de23ee9130e045c06faaf4b1cf8583041c9054416851d026', 36),
     '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
     '_kui_vmu_native_exit_shape_valid.constprop.0': ('73f960510420f9114e16729f6b0f63303104db03ae2507cfecc87ada91e381ad',
                                                      132),
     '_kui_vmu_native_passive_complete': ('2dbc8aa571f56dade367394370b9fc89d9fd3583f1c75e53b4c209002749a390', 3436),
     '_kui_vmu_native_passive_submit': ('7ce6162578e7ae39e63c374665497355df58278f4e758753d242c70584f66051', 1408),
     '_kui_vmu_native_patch_encode': ('2ee6e8b4c1272c5916db0236959a09af2e07a5f4622d7c655982b4bd7eddc4dd', 168),
     '_kui_vmu_native_terminal_dispatch_span.constprop.0': ('843a0460a5268fd47a94404e6a0df8e6269c0a7049aa71a6d32196e25c47d2ea',
                                                            128),
     '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': ('d045495217f025c5e362cc651783a2d791db46aab61bc9004d882d9635085d7b',
                                                              108),
     '_kui_vmu_native_timer_callback_owned': ('035d109aa67cbb6b585f263dd0d0b64d7679386bdf457ebbbbe84894abaabce6', 92),
     '_memcpy': ('7fa8d1eee8135c7721a67c5ada882db55d17c24a41b305927c114c697ac5066d', 72),
     '_normalize_heap_dispatch': ('3419cac763b385db2a6f931e4df44172bb9dff4fbaba7ed25f3c5534524762ec', 276),
     '_normalize_heap_dispatch_if_routing': ('54551c3ce68e045933e39c229955468e2bee6ead6fe9766f42935d1593309802', 40),
     '_patch_ram': ('ee99409b66f8e0fff23f34fae0ef53e503429aeea4ce8117e1d584ea56ecb7c5', 80),
     '_poll_lease_valid': ('ac832b82ea8d821dede21c5e9e825e9f7a4479be1dae662c525cdd4f92fb64a8', 168),
     '_read_heap': ('98154596efda0b82f86e858c35201c19f6d8773459cfc703474205cf1c2a998f', 48),
     '_read_value': ('114eb5e180ab1a3e10d2991991db4cd64069ca22b1c85a1f612a9cc5d9029f58', 36),
     '_read_value.part.0': ('1af38c39e6bb370c70947263231e5c742c989f186ac8af979a2654bafde206e2', 104),
     '_reader_capture_region': ('ba0fe03f2355bdee2e98166022692dab9fa03e444a7c52f890b3407c656a7165', 100),
     '_reader_cooperative_service': ('17975bf4c258a07eeaaf7638e5cab6b690aff3ef6c7cba40b2b47296152748bf', 100),
     '_reader_readahead_exports': ('e180abcbd6e9a2144e4ae5e0cc25c009c49d65018c5815367873b5fb16d36ccd', 200),
     '_reader_streaming': ('6e039ba01afc9b34ddd3d82bdddac98662af88639e1c5e1a1989a1fcfd563759', 120),
     '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
     '_timer_runtime_in': ('7d3d7f5268d3a727c863c90fde2de8693613748f81e564271569b4ef3f4ecf8e', 26),
     '_timer_runtime_ram': ('2cd3fc97ffa9bd27467b7c6e12ae1bcebcdb41e591742863a7cac78ad34661dd', 104)}}
MUTABLE_OBJECTS={'_captured@vmu_native_passive.c': (8, 12288),
 '_chain_frames@vmu_native_passive.c': (8, 640),
 '_completion_frame@vmu_native_passive.c': (8, 76),
 '_config@vmu_native_passive.c': (8, 2304),
 '_controller_pair@vmu_native_passive.c': (8, 188),
 '_durable_card@vmu_native_passive.c': (8, 4160),
 '_held_report@vmu_native_passive.c': (8, 4),
 '_hook_witness@vmu_native_passive.c': (8, 384),
 '_hook_witness_count@vmu_native_passive.c': (8, 4),
 '_hook_witness_ready@vmu_native_passive.c': (8, 4),
 '_issued_chain@vmu_native_passive.c': (8, 12288),
 '_issued_plan@vmu_native_passive.c': (8, 44),
 '_kui_vmu_native_passive_consumer': (1, 4),
 '_kui_vmu_native_passive_resume': (1, 4),
 '_kui_vmu_native_readonly_release_resume': (1, 4),
 '_kui_vmu_native_sdk_coherence_state': (8, 48),
 '_lifetime_plan@vmu_native_passive.c': (8, 616),
 '_live_plan@vmu_native_passive.c': (8, 456),
 '_overlay@vmu_native_passive.c': (8, 17172),
 '_overlay_frames@vmu_native_passive.c': (8, 896),
 '_protected_count@vmu_native_passive.c': (8, 4),
 '_protected_ranges@vmu_native_passive.c': (8, 512),
 '_reader_stream@vmu_native_passive.c': (8, 44),
 '_receivers@vmu_native_passive.c': (8, 192),
 '_row@retail_display.c': (8, 4),
 '_state@vmu_native_passive.c': (8, 100)}

def require(value,message):
    if not value:raise ValueError(message)


def aliases(worker):
    """Expose only actual local helpers with their unique ELF FILE owners."""
    from check_vmu_native_persistent import prepared_symbol_records
    records=prepared_symbol_records(worker);symbols=dict(worker[4])
    for alias,name in {'_patch_ram':'_ram@vmu_native_patch.c',
                       '_timer_runtime_ram':'_ram@vmu_native_runtime.c',
                       '_timer_runtime_in':'_in@vmu_native_runtime.c'}.items():
        found=[(address,count,kind,owner) for key,address,count,kind,owner in records if key==name]
        require(len(found)==1 and found[0][2]==2,'Ordinary General lacks source-owned helper: '+name)
        require(alias not in symbols or symbols[alias]==found[0],
                'Ordinary General helper alias differs from its source-owned function: '+alias)
        symbols[alias]=found[0]
    return (*worker[:4],symbols)


def owned_objects(worker):
    from check_vmu_native_persistent import prepared_symbol_records
    actual={}
    for name,address,count,kind,owner in prepared_symbol_records(worker):
        if kind!=1 or not count:continue
        require(owner<len(worker[3]),'Ordinary General object lacks its ELF section')
        section=worker[3][owner]
        if section[2]&7!=3:continue
        require(name not in actual and section[1] in (1,8) and
                section[3]<=address<address+count<=section[3]+section[5] and
                0xac100000<=address<address+count<=0xac11b000,
                'Ordinary General mutable object escapes private data/BSS: '+name)
        require(worker[4].get(name.split('@',1)[0])==(address,count,kind,owner),
                'Ordinary General named mutable symbol differs from its actual FILE-owned record: '+name)
        actual[name]=(section[1],count)
    require(actual==MUTABLE_OBJECTS,'Ordinary General complete mutable inventory changed')
    return actual


def profile(worker):
    # These two new outlined roots distinguish modern ordinary General from
    # the retained historical inlined controller ABI. A partial pair refuses.
    roots=('_controller_layout_valid','_controller_pair_valid')
    if '__kui_native_session_controls_worker' in worker[4]:return None
    from native_runtime_layout import GENERAL,layout_for
    if layout_for(worker[4]) is not GENERAL:return None
    if not any(name in worker[4] for name in roots):return None
    require(all(name in worker[4] for name in roots),'Ordinary General outlined controller pair is incomplete')
    from check_native_general_lean_profile import worker_profile
    require(worker_profile(worker),'Ordinary witness requires the full112KiB/324KiB General profile')
    require(not any(name in worker[4] for name in ('__kui_native_session_virtual_slot',
        '__kui_native_physical_disc_worker','__kui_native_background_compact_worker',
        '__kui_native_vmu_lite_worker','__kui_native_vmu_only_worker')),
        'Ordinary General contains a conflicting session/physical/compact capability')
    require(all(worker[4].get(name)==ONE for name in (
        '__kui_native_owned_timer_callbacks_worker','__kui_native_controller_failure_witness_worker',
        '__kui_native_terminal_dispatch_worker','__kui_native_dual_readers_worker',
        '__kui_native_dispatch_read_gate_worker','__kui_native_readahead_tick_budget_worker')),
        'Ordinary General lacks its exact timer/witness/dual/dispatch/budget capabilities')
    owned_objects(worker)
    # Baseline workers predate the later explicit slot metadata. Select only
    # by the complete independently pinned normalized completion body, then
    # require any candidate metadata to agree with that executed slot.
    digest,count,_=signatures(aliases(worker),('_kui_vmu_native_passive_complete',))['_kui_vmu_native_passive_complete']
    slots=[slot for slot in PINS if PINS[slot]['_kui_vmu_native_passive_complete']==(digest,count)]
    require(len(slots)==1,'Ordinary General completion does not match either reviewed virtual slot')
    slot=slots[0];marker=worker[4].get('__kui_native_virtual_slot_worker')
    require(marker is None or marker==(slot,0,0,0xfff1),'Ordinary General virtual slot metadata disagrees with linked code')
    return slot


def virtual_slot(worker):
    """Independent fixture selector that leaves completion mutants executable.

    The full admission validates the complete completion pin. Its functional
    mutations must reach actual SH execution, so the fixture's slot comes
    from the separately pinned submission body instead.
    """
    digest,count,_=signatures(aliases(worker),('_kui_vmu_native_passive_submit',))['_kui_vmu_native_passive_submit']
    slots=[slot for slot in PINS if PINS[slot]['_kui_vmu_native_passive_submit']==(digest,count)]
    require(len(slots)==1,'Ordinary fixture submission does not identify a reviewed virtual slot')
    marker=worker[4].get('__kui_native_virtual_slot_worker')
    require(marker is None or marker==(slots[0],0,0,0xfff1),'Ordinary fixture slot metadata differs from submission')
    return slots[0]


def pinned(worker):
    worker=aliases(worker);slot=profile(worker)
    require(slot in PINS,'Missing independently reviewed ordinary General witness map')
    actual=signatures(worker,PINS[slot]);result={}
    for name,expected in PINS[slot].items():
        digest,count,references=actual[name];address,body,section=symbol_bytes(worker,name,2)
        require(section[1]==1 and section[2]&7==6 and (digest,count)==expected,
                'Ordinary General instructions/typed targets changed: '+name)
        result[name]=dict(address=hex(address),bytes=count,sha256=hashlib.sha256(body).hexdigest(),
            normalized_sha256=digest,typed_references=[list(row) for row in references],reference_source=BASELINE_COMMIT)
    from check_native_bottom_heap import records
    from check_native_exit_shape import WORDS
    available=records(worker);require(SHAPE_TABLE in available,'Ordinary General lacks its source-owned exit policy table')
    address,body,kind,section=available[SHAPE_TABLE]
    require(kind==1 and section[1]==1 and section[2]&7==2 and body==WORDS,
            'Ordinary General exit policy differs from its authored immutable ten rows')
    result[SHAPE_TABLE]=dict(address=hex(address),bytes=len(body),sha256=hashlib.sha256(body).hexdigest(),reference_source=BASELINE_COMMIT)
    return result
