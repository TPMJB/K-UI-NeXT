#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reviewed full-session witness closure, independent of pointer placement.

Expected signatures are authored from the clean 3a6ae064 session-controls
reference workers, not the candidate under inspection. Their complete physical
load bytes match the pre-existing full-feature worker. Only ELF-proven pointer
words are normalized; source-qualified target identity and interior offsets
remain part of each signature. Historical non-session pins are unchanged.
"""
import hashlib

from check_vmu_native_worker_gateway import symbol_bytes
from vmu_native_relocations import generate

ONE = (1, 0, 0, 0xfff1)
BASELINE_COMMIT = '3a6ae064bb3f456da8d71587a8db0ce974f139cf'
SHAPE_TABLE = '_kui_vmu_native_exit_shape_words.7@vmu_native_passive.c'
PINS = {1: {'_controller_context_valid': ('90ea99a592562009bd84ae6c6b365e5241513c56a5bfe132aab91d16b4f3e935', 72),
     '_controller_layout_valid': ('c35affd52b29c1602e2fc9640bc0279d5ee8d2c70849f05d1203f503b446aa1c', 748),
     '_controller_pair_valid': ('b7007b20806bea9a50bc5e694862b7ac72ee41043fda4a56d779705e075177b0', 196),
     '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
     '_held_report_gesture': ('c37a4feec01d1d60de23ee9130e045c06faaf4b1cf8583041c9054416851d026', 36),
     '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
     '_kui_vmu_native_background_map_address': ('bc9ef92fb3016060dfe6d70cd27818efba9520662384aaed756d64bafe58dc30',
                                                92),
     '_kui_vmu_native_exit_shape_valid.constprop.0': ('73f960510420f9114e16729f6b0f63303104db03ae2507cfecc87ada91e381ad',
                                                      132),
     '_kui_vmu_native_passive_complete': ('384ac9384a89f547c04861e590eba432f2bcb9201e13712f71c48fc021c588be',
                                          3488),
     '_kui_vmu_native_passive_submit': ('41cb6535514de56e9b047c89e4ac61e8a2d2d5c9ea6438a2140de112f718b6e7', 1408),
     '_kui_vmu_native_patch_encode': ('d70b2ebfbb8eb1eaeb84fa1ae54fec07dafd2b8871bc4874119139b16e43cdc6', 168),
     '_kui_vmu_native_terminal_dispatch_span.constprop.0': ('843a0460a5268fd47a94404e6a0df8e6269c0a7049aa71a6d32196e25c47d2ea',
                                                            128),
     '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': ('d045495217f025c5e362cc651783a2d791db46aab61bc9004d882d9635085d7b',
                                                              108),
     '_kui_vmu_native_timer_callback_owned': ('035d109aa67cbb6b585f263dd0d0b64d7679386bdf457ebbbbe84894abaabce6',
                                              92),
     '_memcpy': ('7fa8d1eee8135c7721a67c5ada882db55d17c24a41b305927c114c697ac5066d', 72),
     '_normalize_heap_dispatch': ('3419cac763b385db2a6f931e4df44172bb9dff4fbaba7ed25f3c5534524762ec', 276),
     '_normalize_heap_dispatch_if_routing': ('54551c3ce68e045933e39c229955468e2bee6ead6fe9766f42935d1593309802',
                                             40),
     '_patch_ram': ('ee99409b66f8e0fff23f34fae0ef53e503429aeea4ce8117e1d584ea56ecb7c5', 80),
     '_poll_lease_valid': ('ac832b82ea8d821dede21c5e9e825e9f7a4479be1dae662c525cdd4f92fb64a8', 168),
     '_read_value': ('114eb5e180ab1a3e10d2991991db4cd64069ca22b1c85a1f612a9cc5d9029f58', 36),
     '_read_value.part.0': ('1af38c39e6bb370c70947263231e5c742c989f186ac8af979a2654bafde206e2', 104),
     '_reader_capture_region': ('f66d7382dc5f4a79e15dad47ab8721896cf040bb6f24375c8019000da3652317', 64),
     '_reader_cooperative_service': ('17975bf4c258a07eeaaf7638e5cab6b690aff3ef6c7cba40b2b47296152748bf', 100),
     '_reader_readahead_exports': ('e180abcbd6e9a2144e4ae5e0cc25c009c49d65018c5815367873b5fb16d36ccd', 200),
     '_reader_streaming': ('07da025c14b5e267b3c4e0b1658d86732f2a4036967cc8b9e75af10a34e9ea1b', 120),
     '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
     '_timer_runtime_in': ('7d3d7f5268d3a727c863c90fde2de8693613748f81e564271569b4ef3f4ecf8e', 26),
     '_timer_runtime_ram': ('2cd3fc97ffa9bd27467b7c6e12ae1bcebcdb41e591742863a7cac78ad34661dd', 104)},
 2: {'_controller_context_valid': ('90ea99a592562009bd84ae6c6b365e5241513c56a5bfe132aab91d16b4f3e935', 72),
     '_controller_layout_valid': ('c35affd52b29c1602e2fc9640bc0279d5ee8d2c70849f05d1203f503b446aa1c', 748),
     '_controller_pair_valid': ('b7007b20806bea9a50bc5e694862b7ac72ee41043fda4a56d779705e075177b0', 196),
     '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
     '_held_report_gesture': ('c37a4feec01d1d60de23ee9130e045c06faaf4b1cf8583041c9054416851d026', 36),
     '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
     '_kui_vmu_native_background_map_address': ('bc9ef92fb3016060dfe6d70cd27818efba9520662384aaed756d64bafe58dc30',
                                                92),
     '_kui_vmu_native_exit_shape_valid.constprop.0': ('73f960510420f9114e16729f6b0f63303104db03ae2507cfecc87ada91e381ad',
                                                      132),
     '_kui_vmu_native_passive_complete': ('c331a4a828d200a779692a8505bf0f189590383eebbab449bca5ecd365684336',
                                          3488),
     '_kui_vmu_native_passive_submit': ('7ce6162578e7ae39e63c374665497355df58278f4e758753d242c70584f66051', 1408),
     '_kui_vmu_native_patch_encode': ('d70b2ebfbb8eb1eaeb84fa1ae54fec07dafd2b8871bc4874119139b16e43cdc6', 168),
     '_kui_vmu_native_terminal_dispatch_span.constprop.0': ('843a0460a5268fd47a94404e6a0df8e6269c0a7049aa71a6d32196e25c47d2ea',
                                                            128),
     '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': ('d045495217f025c5e362cc651783a2d791db46aab61bc9004d882d9635085d7b',
                                                              108),
     '_kui_vmu_native_timer_callback_owned': ('035d109aa67cbb6b585f263dd0d0b64d7679386bdf457ebbbbe84894abaabce6',
                                              92),
     '_memcpy': ('7fa8d1eee8135c7721a67c5ada882db55d17c24a41b305927c114c697ac5066d', 72),
     '_normalize_heap_dispatch': ('3419cac763b385db2a6f931e4df44172bb9dff4fbaba7ed25f3c5534524762ec', 276),
     '_normalize_heap_dispatch_if_routing': ('54551c3ce68e045933e39c229955468e2bee6ead6fe9766f42935d1593309802',
                                             40),
     '_patch_ram': ('ee99409b66f8e0fff23f34fae0ef53e503429aeea4ce8117e1d584ea56ecb7c5', 80),
     '_poll_lease_valid': ('ac832b82ea8d821dede21c5e9e825e9f7a4479be1dae662c525cdd4f92fb64a8', 168),
     '_read_value': ('114eb5e180ab1a3e10d2991991db4cd64069ca22b1c85a1f612a9cc5d9029f58', 36),
     '_read_value.part.0': ('1af38c39e6bb370c70947263231e5c742c989f186ac8af979a2654bafde206e2', 104),
     '_reader_capture_region': ('f66d7382dc5f4a79e15dad47ab8721896cf040bb6f24375c8019000da3652317', 64),
     '_reader_cooperative_service': ('17975bf4c258a07eeaaf7638e5cab6b690aff3ef6c7cba40b2b47296152748bf', 100),
     '_reader_readahead_exports': ('e180abcbd6e9a2144e4ae5e0cc25c009c49d65018c5815367873b5fb16d36ccd', 200),
     '_reader_streaming': ('07da025c14b5e267b3c4e0b1658d86732f2a4036967cc8b9e75af10a34e9ea1b', 120),
     '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
     '_timer_runtime_in': ('7d3d7f5268d3a727c863c90fde2de8693613748f81e564271569b4ef3f4ecf8e', 26),
     '_timer_runtime_ram': ('2cd3fc97ffa9bd27467b7c6e12ae1bcebcdb41e591742863a7cac78ad34661dd', 104)}}


def require(value, message):
    if not value:
        raise ValueError(message)


def profile(worker):
    marker = worker[4].get('__kui_native_session_controls_worker')
    if marker is None:
        return None
    require(marker == ONE, 'Session witness marker must be exact absolute one')
    require(all(worker[4].get(name) == ONE for name in (
        '__kui_native_owned_timer_callbacks_worker',
        '__kui_native_controller_failure_witness_worker',
        '__kui_native_terminal_dispatch_worker', '__kui_native_dual_readers_worker')),
        'Session witness requires its exact paired timer, controller and dual-reader capabilities')
    from check_native_session_controls import worker_geometry
    geometry = worker_geometry(worker)
    require(worker[4]['_config'][1] == 2316 and worker[4]['_overlay'][1] == 1452,
            'Session witness configuration/one-reply staging layout changed')
    from check_vmu_native_persistent import prepared_symbol_records
    symbols = prepared_symbol_records(worker)
    for local, size in (('_state', 100), ('_controller_pair', 188), ('_config', 2316),
                        ('_live_plan', 456), ('_lifetime_plan', 616),
                        ('_overlay', 1452), ('_durable_card', 4160)):
        matches = [row for row in symbols if row[0] == local + '@vmu_native_passive.c']
        require(len(matches) == 1, 'Session witness owned object is ambiguous: ' + local)
        _, address, count, kind, owner = matches[0]
        section = worker[3][owner]
        require((address, count, kind, owner) == worker[4][local] and count == size and
                kind == 1 and section[1] == 8 and section[2] & 7 == 3,
                'Session witness raw owned-object type/span differs: ' + local)
    return geometry['virtual_slot']


def binary(worker):
    segments = [p for p in worker[2] if p[0] == 1]
    first = 0x0c100000
    end = max((p[2] & 0x1fffffff) + p[4] for p in segments)
    require(first < end <= first + 0x1c000, 'Session witness initialized span changed')
    out = bytearray(end - first)
    for p in segments:
        offset = (p[2] & 0x1fffffff) - first
        require(0 <= offset <= len(out) - p[4], 'Session witness load outside physical span')
        out[offset:offset + p[4]] = worker[0][p[1]:p[1] + p[4]]
    return bytes(out)


def signatures(worker, names):
    from check_vmu_native_persistent import prepared_signature
    flat = binary(worker)
    wire = generate(worker[0], flat, 0x8c100000, 0x1c000)
    return {name: prepared_signature(worker, flat, wire, name) for name in names}


def pinned(worker):
    slot = profile(worker)
    require(slot in PINS, 'Missing independently reviewed session witness map')
    actual = signatures(worker, PINS[slot])
    result = {}
    for name, expected in PINS[slot].items():
        digest, count, references = actual[name]
        address, body, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6 and (digest, count) == expected,
                'Session witness instructions or typed targets changed: ' + name)
        result[name] = dict(address=hex(address), bytes=count,
            sha256=hashlib.sha256(body).hexdigest(), normalized_sha256=digest,
            typed_references=[list(row) for row in references], reference_source=BASELINE_COMMIT)
    # The outlined predicate's literal binds a separate immutable table. Its
    # ten policy rows are authored in the common exit-shape audit, not inferred
    # from the candidate, and must be bound in addition to the code reference.
    from check_native_bottom_heap import records
    from check_native_exit_shape import WORDS
    available = records(worker)
    require(SHAPE_TABLE in available, 'Missing source-owned session exit policy table')
    address, body, kind, section = available[SHAPE_TABLE]
    require(kind == 1 and section[1] == 1 and section[2] & 7 == 2 and body == WORDS,
            'Session exit policy table differs from its authored ten immutable rows')
    result[SHAPE_TABLE] = dict(address=hex(address), bytes=len(body),
        sha256=hashlib.sha256(body).hexdigest(), reference_source=BASELINE_COMMIT)
    return result
