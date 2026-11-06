#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Strict full-session prepared-worker closure.

The fixed maps come from independently rebuilt clean 3a6ae064 A1/A2 workers.
Their complete physical load images match the pre-existing session workers.
All historical prepared maps remain unchanged. Pointer normalization retains
the source-qualified target, type and interior offset; only the exact verified
build label is variable. Outlined address, context and lease guards are part
of this complete map rather than omitted from their smaller caller bodies.
"""

COMMON = {'_controller_context_valid': ('90ea99a592562009bd84ae6c6b365e5241513c56a5bfe132aab91d16b4f3e935', 72),
 '_controller_layout_valid': ('c35affd52b29c1602e2fc9640bc0279d5ee8d2c70849f05d1203f503b446aa1c', 748),
 '_controller_pair_valid': ('b7007b20806bea9a50bc5e694862b7ac72ee41043fda4a56d779705e075177b0', 196),
 '_controller_shutdown_literal': ('e204c1a4497ceabc9217bd374c025f56724c38f54ab47e17c1b13dfc221bd41a',
                                  132),
 '_controller_shutdown_mmio_state': ('e8bdc653a468992c0b56c065c1d45b78f87417da09be8117f5f84bbdee74f8df',
                                     88),
 '_controller_shutdown_source': ('b2235bcd3a6cc5010d978917fea149b52e7f39fbee4744ca64c1ec9e7f76e67c',
                                 520),
 '_controller_shutdown_source_read': ('6d9992e5f1101643fc738cafd453a5810b4380ff46eafc711b989331c6c14a86',
                                      32),
 '_controller_shutdown_span': ('40df857c87050b8955fb305817677e78b440765b9ec7adc9a02219fc6169ee87', 100),
 '_controller_suspended': ('10b770231b6b24405eeb7023f6ae4da6e9c69ec2bcd9bbca51d6b212f8ac783d', 380),
 '_fail': ('34efa939b712d6b1ddd0d9c59af160b6bbcac2bcbd411f4cfbf84bb26f2c7527', 28),
 '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
 '_held_report_gesture': ('c37a4feec01d1d60de23ee9130e045c06faaf4b1cf8583041c9054416851d026', 36),
 '_hook_word': ('6c579e60911823ed1fbf6a57622bab2d7de2ebf46228c2c2c21d27f8fc144249', 112),
 '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
 '_issued_physical': ('1cbca02d39df730690fdf4c8e3169902d1d45df68696fd366cdea547625452ef', 104),
 '_kui_native_loader_timeline_visit': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83',
                                       4),
 '_kui_vmu_native_background_map_address': ('bc9ef92fb3016060dfe6d70cd27818efba9520662384aaed756d64bafe58dc30',
                                            92),
 '_kui_vmu_native_exit_shape_valid.constprop.0': ('73f960510420f9114e16729f6b0f63303104db03ae2507cfecc87ada91e381ad',
                                                  132),
 '_kui_vmu_native_lifetime_prepare': ('51a7cf0ccf76c9ce4ab5d7395ee3fa12dad3e8a1435bf63cfed5e3d7896c2532',
                                      752),
 '_kui_vmu_native_passive_split_submit': ('e534ea1834eff1b69ba6208ee0d1dfedf4801d2a906c45791eb5ef1281cb22ca',
                                          884),
 '_kui_vmu_native_patch_encode': ('d70b2ebfbb8eb1eaeb84fa1ae54fec07dafd2b8871bc4874119139b16e43cdc6',
                                  168),
 '_kui_vmu_native_readonly_release_check': ('0b7e53dfcc8c3b384e8b33b8d874ced89398c26ca02282fc23fb70411ad6c708',
                                            640),
 '_kui_vmu_native_runtime_fault': ('bea7a869314bcdec5c8206e14b65d287636f526454fe6e470ec9ab45d75816ec',
                                   308),
 '_kui_vmu_native_sdk_coherence_prepare': ('676f33b17b15bbfdbd583eaddbefd989ae8e09909adde204239633b838658d5b',
                                           420),
 '_kui_vmu_native_terminal_dispatch_span.constprop.0': ('843a0460a5268fd47a94404e6a0df8e6269c0a7049aa71a6d32196e25c47d2ea',
                                                        128),
 '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': ('d045495217f025c5e362cc651783a2d791db46aab61bc9004d882d9635085d7b',
                                                          108),
 '_kui_vmu_native_timer_callback_owned': ('035d109aa67cbb6b585f263dd0d0b64d7679386bdf457ebbbbe84894abaabce6',
                                          92),
 '_kui_vmu_native_trace_frame_entry': ('24be2fc972ae425d21e9fd5a0b8ad551f1f64bc20c8d7cb9118c00818bba1111',
                                       164),
 '_kui_vmu_native_trace_game': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83', 4),
 '_kui_vmu_native_trace_irq': ('31b72b692190f12dabe3bee1767a9d43913918730d08b7c7c802bea816c8177e', 14),
 '_kui_vmu_native_trace_wait_after': ('36746417db83b1f0060aacf65c6ad9fb76ce55aea985779d0d1244b70c6f30e6',
                                      180),
 '_kui_vmu_native_trace_wait_before': ('e9b3f06c9f25ba26990d028291ee7c37f06e4fae6df7069ac0086d316321b5d9',
                                       152),
 '_kui_vmu_native_trace_wait_loop': ('14823a20fee8b883e5f8e7ebd6e7e10af9715be40c12e4674d8fa89f2839adcd',
                                     176),
 '_kui_vmu_readonly_overlay_complete': ('49ac01ef15ed1e6bbd9fae4b40576a0771336428f5fb5401c69a4fa2e8b6e9cf',
                                        1016),
 '_kui_vmu_readonly_overlay_complete_absent': ('7c35f00e4e76474403bd0d4fa4aca6f5960aa8e11160d00e687c67c16bc293d5',
                                               284),
 '_kui_vmu_readonly_overlay_prepare': ('3743c1ef7a3223abe05c5afc2f653ee2eb050d0ed32cdc66c9a35d6751f2afa6',
                                       1176),
 '_lease_valid': ('01cf615d1d2a16f67d88aab1345eaf5854fa1147c67fa4a51fa9a3cb2d714025', 24),
 '_live_boundary': ('73b14f62db52df70862faf3938b134343b14d54fb89558ba38d5867138106a1e', 216),
 '_memcpy': ('7fa8d1eee8135c7721a67c5ada882db55d17c24a41b305927c114c697ac5066d', 72),
 '_native_session_control_visit': ('2bcede8bfd63c0bda6920c0eb8fa94990ad506811c6d603797e8f8a0ef3d3e9c',
                                   596),
 '_native_session_controller_complete.part.0': ('0add293b71942c6ef26ae7889590e4023ff86670c5456a36f29c69a39540b8af',
                                                316),
 '_native_session_return_guard': ('87eb412d05bc1cb080f27eb52dc576c65c90f827bf90246588ba7f25d487d5eb',
                                  104),
 '_normalize_heap_dispatch': ('3419cac763b385db2a6f931e4df44172bb9dff4fbaba7ed25f3c5534524762ec', 276),
 '_normalize_heap_dispatch_if_routing': ('54551c3ce68e045933e39c229955468e2bee6ead6fe9766f42935d1593309802',
                                         40),
 '_patch_ram': ('ee99409b66f8e0fff23f34fae0ef53e503429aeea4ce8117e1d584ea56ecb7c5', 80),
 '_persistent_context_guard': ('c64d3f0b1e8a23c587834effc7697d01d17a20b0a2032a0fe4713d6b69bbdf77', 128),
 '_persistent_lease': ('2cc5f048419d235babba86e995e68fa108301c09aa8822d858095d934da52bac', 108),
 '_persistent_storage_guard': ('5c520537d378c32b93f927645714db94244d6e22db32534aafbd617b5c477405', 384),
 '_poll_lease_valid': ('ac832b82ea8d821dede21c5e9e825e9f7a4479be1dae662c525cdd4f92fb64a8', 168),
 '_prepare_hook_entry': ('5d19b4a6dc4e4995fde5d613379bbc0cb8e28fa11854280a2686c357bdc5b582', 208),
 '_protect_range': ('fdab8c94cba19d50df3c78ce99b0e4b4da5def207c86189e6df7b6bb9aa28065', 84),
 '_read_heap': ('98154596efda0b82f86e858c35201c19f6d8773459cfc703474205cf1c2a998f', 48),
 '_read_value': ('114eb5e180ab1a3e10d2991991db4cd64069ca22b1c85a1f612a9cc5d9029f58', 36),
 '_read_value.part.0': ('1af38c39e6bb370c70947263231e5c742c989f186ac8af979a2654bafde206e2', 104),
 '_reader_capture_region': ('f66d7382dc5f4a79e15dad47ab8721896cf040bb6f24375c8019000da3652317', 64),
 '_reader_cooperative_service': ('17975bf4c258a07eeaaf7638e5cab6b690aff3ef6c7cba40b2b47296152748bf',
                                 100),
 '_reader_readahead_exports': ('e180abcbd6e9a2144e4ae5e0cc25c009c49d65018c5815367873b5fb16d36ccd', 200),
 '_reader_streaming': ('07da025c14b5e267b3c4e0b1658d86732f2a4036967cc8b9e75af10a34e9ea1b', 120),
 '_readonly_overlay_increment': ('e3a6368a10593e4ae7510faf736d644a9d9e383e262070923241741b3f6893b1', 14),
 '_readonly_overlay_le32': ('1daa47eecc292df8356826c0e5a683e3a2c27cec212f901db7b1f01cf3b897df', 30),
 '_readonly_overlay_parent_response': ('d396e68edd3158708ba5a8c9ff5235821bb62b71cc9dc40650cadb9098eaeb12',
                                       116),
 '_readonly_overlay_pointer_overlap': ('1ee60abf2148621140e7d53a347536afad5e8f7759627bce7da0d045d7ea0348',
                                       32),
 '_readonly_overlay_put32': ('04dba701f2b2b5a13739b4fde066b45ecceceb28684e620d217ff60cb1308163', 24),
 '_readonly_overlay_ram_span': ('5d29104681aa3d3a32c948c924049eb53df8a632f91ee767e9b237c9206c73ce', 56),
 '_readonly_overlay_target_response_bytes': ('4b38d95dfa7ff193118d30455a04e211fa54b7343e9beb12bdd72645835ad036',
                                             216),
 '_report': ('2b6cbef1598274a38b554b15570946253d886a763524b80b9eb247bfb9db923d', 700),
 '_session_low_span': ('e4f191cc5906e0fd52ffc8ccf68dfe1744d30c94987d99dc6522116abf7486e5', 40),
 '_session_next_valid': ('b285bec0836c5afd7bc3f7a3046eab7638f309ce4b75133bebcb81980b9faaa5', 216),
 '_session_prepared_ram': ('0084512336c207d3ffe6225688c8058f1f87937c8d5ace66be5592e59b936320', 88),
 '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
 '_timer_runtime_in': ('7d3d7f5268d3a727c863c90fde2de8693613748f81e564271569b4ef3f4ecf8e', 26),
 '_timer_runtime_ram': ('2cd3fc97ffa9bd27467b7c6e12ae1bcebcdb41e591742863a7cac78ad34661dd', 104),
 '_transfer_context_valid': ('92225d2c3037a43fc088a0d1177e975f8c87f7dbb2a6eca82fcdb5b69f9302c3', 200),
 '_transfer_frame_valid': ('381582aca89800ccf5207d80f9e97c8273f22c971f6d27e98395317f2087dde8', 504),
 '_transfer_sdk_context_valid': ('19e0a7c56618a635822d521d338f1c9f4b4539a295ba539046bd5d341bff7c5c',
                                 388)}
SLOT = {1: {'.rodata': ('28fe049be4654c6ec8b723659d750c11771c3bd99e1c1931d307e7e915dd9109', 1636),
     '_chain': ('af5cd85d6f99a29160829a5a57b803cf3aa641779ff3f49252f46a0bb8768da6', 808),
     '_kui_vmu_native_passive_arm': ('faa5a5bd24ffc1173557f52033fce0847b45107003393c5c467da30226cb7eb3',
                                     2816),
     '_kui_vmu_native_passive_complete': ('384ac9384a89f547c04861e590eba432f2bcb9201e13712f71c48fc021c588be',
                                          3488),
     '_kui_vmu_native_passive_submit': ('41cb6535514de56e9b047c89e4ac61e8a2d2d5c9ea6438a2140de112f718b6e7',
                                        1408)},
 2: {'.rodata': ('18bf7a9f50822e59351da97d1f78a317c9baa3cf77c27f92b92bb7bdea432ec7', 1636),
     '_chain': ('de77777218a51f11c50b262937731da9b1ccc4f96693cd0c2a879f22902fb3ff', 808),
     '_kui_vmu_native_passive_arm': ('6d73ddba4b60713833c0724d80822cc8a1ffb3e35eee93c57b05291b6862b7ed',
                                     2816),
     '_kui_vmu_native_passive_complete': ('c331a4a828d200a779692a8505bf0f189590383eebbab449bca5ecd365684336',
                                          3488),
     '_kui_vmu_native_passive_submit': ('7ce6162578e7ae39e63c374665497355df58278f4e758753d242c70584f66051',
                                        1408)}}


MUTABLE_OBJECTS = {'_captured@vmu_native_passive.c': (8, 12288),
 '_chain_frames@vmu_native_passive.c': (8, 640),
 '_completion_frame@vmu_native_passive.c': (8, 76),
 '_config@vmu_native_passive.c': (8, 2316),
 '_controller_fault@native_return_hardware.c': (8, 1),
 '_controller_pair@vmu_native_passive.c': (8, 188),
 '_controller_rx@native_return_hardware.c': (8, 1024),
 '_controller_tx@native_return_hardware.c': (8, 32),
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
 '_overlay@vmu_native_passive.c': (8, 1452),
 '_overlay_frames@vmu_native_passive.c': (8, 896),
 '_protected_count@vmu_native_passive.c': (8, 4),
 '_protected_ranges@vmu_native_passive.c': (8, 512),
 '_reader_stream@vmu_native_passive.c': (8, 44),
 '_receivers@vmu_native_passive.c': (8, 192),
 '_row@retail_display.c': (8, 4),
 '_session@vmu_native_passive.c': (8, 36),
 '_session_discs@vmu_native_passive.c': (8, 4112),
 '_session_firmware@vmu_native_passive.c': (8, 36),
 '_session_manifest_cell@vmu_native_passive.c': (8, 4),
 '_session_region@vmu_native_passive.c': (8, 4),
 '_session_return_plan@vmu_native_passive.c': (8, 488),
 '_session_switch@vmu_native_passive.c': (8, 60),
 '_state@vmu_native_passive.c': (8, 100)}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def image(worker):
    from check_native_session_witness_profile import profile
    slot = profile(worker)
    require(slot in SLOT, 'Missing independently reviewed session prepared profile')
    from check_native_general_lean_profile import worker_profile
    require(worker_profile(worker) and worker[4].get(
        '__kui_native_dispatch_read_gate_worker') == (1, 0, 0, 0xfff1),
        'Session prepared worker lacks its exact General/bottom/dispatch profile')
    from check_native_controller_failure_witness import timer_symbol_aliases
    from check_vmu_native_persistent import prepared_symbol_records
    owned_objects(worker)
    worker = timer_symbol_aliases(worker)
    names = dict(worker[4])
    # Duplicate static names are bound by the raw ELF FILE owner, not whichever
    # dictionary entry happened to survive a symbol-table read.
    owned = [row for row in prepared_symbol_records(worker)
             if row[0] == '_ram@vmu_native_passive.c' and row[3] == 2]
    require(len(owned) == 1, 'Session prepared RAM helper lacks unique FILE ownership')
    record = tuple(owned[0][1:])
    alias = '_session_prepared_ram'
    require(alias not in names or names[alias] == record,
            'Ambiguous source-owned session prepared helper: ' + alias)
    names[alias] = record
    return (*worker[:4], names)


def owned_objects(worker):
    from check_vmu_native_persistent import prepared_symbol_records
    actual = {}
    for name, address, count, kind, owner in prepared_symbol_records(worker):
        if kind != 1 or not count:
            continue
        require(owner < len(worker[3]), 'Session object lacks its ELF section')
        section = worker[3][owner]
        if section[2] & 7 != 3:
            continue
        require(name not in actual and section[1] in (1, 8) and
                section[3] <= address < address + count <= section[3] + section[5] and
                0xac100000 <= address < address + count <= 0xac11b000,
                'Session mutable object escapes unique private data/BSS: ' + name)
        actual[name] = (section[1], count)
    require(actual == MUTABLE_OBJECTS, 'Session complete mutable inventory changed')
    return actual


def pins(worker):
    worker = image(worker)
    from check_native_session_witness_profile import profile
    slot = profile(worker)
    require(slot in SLOT, 'Session prepared slot differs from its strict map')
    # This also binds the separate immutable exit policy table and its typed
    # timer/patch callees. The same functions are included in COMMON/SLOT so
    # prepared-signature mutation tests cover their complete bodies too.
    from check_native_controller_failure_witness import pinned
    pinned(worker)
    return {**COMMON, **SLOT[slot]}

