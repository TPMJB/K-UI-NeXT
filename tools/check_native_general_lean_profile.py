#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Select the separately reviewed, complete lean General linked profiles.

Pins are authored from the frozen, independently reviewed candidate, never
learned from the artifact under inspection. Historical maps stay unchanged.
"""
import struct

from check_vmu_native_worker_gateway import symbol_bytes

ONE = (1, 0, 0, 0xfff1)
STAGE_MARKER = '__kui_native_general_lean_stage_layout'
WORKER_MARKER = '__kui_native_general_lean_worker_layout'
LOW_MARKER = '__kui_native_general_lean_resident_layout'
WORKER_BYTES = 0x1c000
LEASE_BYTES = 0x51000

# Complete static maps are populated only after source/instruction review.
# A missing role must refuse rather than fall through to another profile.
STAGE_PINS = {'cache_gateway': {'_native_resolution_replay':
    (284, 'c030caaa0cd0864f714eeb28b7689214801f0522ba57f3433002956af6a579a7')},
 'dual': {'_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
          '_native_dual_reader_bind': (68,
                                       '19980350b670f72d42982f3a27df79d6fe10bee4556fbd1d8aacf407979f1b8c'),
          '_select_resident': (272, '56fab4e337839d7c596e03733467c1cb6fcb38c349d26a4934e1850844becac5')},
 'installer': frozenset({(2016, 'ad06f7f577fcf38639194faa8ae3f2e6421097715da5d2ebb7426b8615d131e8')}),
 'read_ahead': {'_kui_vmu_native_background_map_address': (92,
                                                           '431865c1e385488b2c6be9c59219f826be4803aa55d5a8b3e06748a6c9fb3536'),
                '_native_readahead_bind': (1244,
                                           '9f9f60f3af4928a7354fb0f7edc032f50e3ca408bc8b5a5dd006b648ffc55d24')},
 'sdk': {'_fingerprint': (88, 'ba5036ee9f290880818f07e6ffc00e012a3c8a6af8905824f2be2b11d2ea0467'),
         '_kui_native_sdk_sci_route_admit': (796,
                                             '7ec3e0660667041fb6aa67b2f261d47b083cd6538e6365badeda0329434d88eb'),
         '_kui_native_sdk_sci_route_discover': (1856,
                                                '2fc5a7605767fe54c75307b74f15d32f861bf3b5bd492526efbbffd8661e5d0d'),
         '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
         '_live_words': (80, 'd847dc4a3ba5a1802558e1fd693868e0cc54a50131c8021c43810fa0f4ec6492'),
         '_native_sdk_sci_route_bind': (952,
                                        '40ce227d7887246dd09214184bfdb8db2eb6b957f5b242413f8c0cc338f1a49c'),
         '_normalized': (192, 'f41b3bc8f3df47c2981c7349d1d7b75eb1286e6cbbdc891b807426d5b24e9011'),
         '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334')},
 'timeline_binder': ('f434705478c6fdae738cf36ce93784406f6dd996835b9b1c1175e8bb79e8f00e', 180)}
WORKER_PINS = {'dual': {'_reader_capture_region': (100, '42d071690b483af9f73517c26cac56bad92a068f8222d9f2b6e1edb6985e1c0a'),
          '_reader_cooperative_service': (248,
                                          '8e7d888e7a5e9c12f072f4def210b0878513863f6420ecf5d8176bd4b531873d'),
          '_reader_streaming': (120, '91825843f2471ee612bcb890b5c3382edfd073a7eac8c183fefe601177faa354')},
 'live_boundary': (216, '2d9fc98b4e4c2a9785a09db674d2f0a1c26a47422f031bae6aab6167a8da6b10'),
 'prepared': {'.rodata': ('4d4183a2bd743782c17d31f8d1b94dbbddc85643d39ac8ecab226ad8ac6abc73', 1176),
              '_chain': ('9d21773697372fdda022cd183d3ebea0d0f0950ccad7194f86d55c69f94979df', 908),
              '_controller_context_valid': ('43d5b6f1aee195f92cd33672814eff6b8fff1bca07c86b921846b90c29204c2b',
                                            1336),
              '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7',
                                          16),
              '_hook_word': ('69410002bf135878c32df63d2459603428947eadc760aa6309775f82b8801f9c', 132),
              '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56',
                                         128),
              '_kui_native_loader_timeline_visit': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83',
                                                    4),
              '_kui_vmu_native_lifetime_prepare': ('4691642d40ae532e2f81cfe46c3f81a019412c0da0e43fb4f7913d4a44b87312',
                                                   640),
              '_kui_vmu_native_passive_arm': ('c83fc0e85c2214476d28799b3d756dd352c9c7907009e155ca0e53d5f203d402',
                                              2464),
              '_kui_vmu_native_passive_complete': ('e53a2a229eb85b0cb26e6cd139d3f4ea060d2a893fb4af80a95285cf4be9e162',
                                                   3440),
              '_kui_vmu_native_passive_split_submit': ('55fe13bdcab382490f2c96bd76a7ba7eefee1ea118c859eca2db677597615626',
                                                       860),
              '_kui_vmu_native_passive_submit': ('77900211d9a78f9dad86515e7ef0b23c665c48aaec4be78f4b9a36c9b3ff90a7',
                                                 1236),
              '_kui_vmu_native_readonly_release_check': ('a2c80231dcf06627d3a0dc18585bd5eeb13bdbbeb818bda1231cb5f41569dcfa',
                                                         632),
              '_kui_vmu_native_runtime_fault': ('7c5b43286c4644754e0918f0cf35523e82de8a3e8a6221ad2fcf30e05d356309',
                                                308),
              '_kui_vmu_native_sdk_coherence_prepare': ('106a54ebdf9eca5c6d7e42dd4caf856fa715beb7e927c50f308cbeb10fb03928',
                                                        436),
              '_kui_vmu_native_terminal_dispatch_span.constprop.0': ('843a0460a5268fd47a94404e6a0df8e6269c0a7049aa71a6d32196e25c47d2ea',
                                                                     128),
              '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': ('d045495217f025c5e362cc651783a2d791db46aab61bc9004d882d9635085d7b',
                                                                       108),
              '_kui_vmu_native_trace_frame_entry': ('24be2fc972ae425d21e9fd5a0b8ad551f1f64bc20c8d7cb9118c00818bba1111',
                                                    164),
              '_kui_vmu_native_trace_game': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83',
                                             4),
              '_kui_vmu_native_trace_irq': ('31b72b692190f12dabe3bee1767a9d43913918730d08b7c7c802bea816c8177e',
                                            14),
              '_kui_vmu_native_trace_wait_after': ('36746417db83b1f0060aacf65c6ad9fb76ce55aea985779d0d1244b70c6f30e6',
                                                   180),
              '_kui_vmu_native_trace_wait_before': ('e9b3f06c9f25ba26990d028291ee7c37f06e4fae6df7069ac0086d316321b5d9',
                                                    152),
              '_kui_vmu_native_trace_wait_loop': ('14823a20fee8b883e5f8e7ebd6e7e10af9715be40c12e4674d8fa89f2839adcd',
                                                  176),
              '_kui_vmu_readonly_overlay_complete': ('169409986ccf516934704346241585b3fb3bbabbd918c617b1cf6ef60bd14504',
                                                     1028),
              '_kui_vmu_readonly_overlay_complete_absent': ('7c35f00e4e76474403bd0d4fa4aca6f5960aa8e11160d00e687c67c16bc293d5',
                                                            284),
              '_kui_vmu_readonly_overlay_prepare': ('bc2cedb815a5fd0af3f5e3485225b47225510c52c12f98b0ec2926733ca81fbc',
                                                    1196),
              '_normalize_heap_dispatch': ('25fc53a6a5f09e94ff22de610fae56711ac461ca5daae3012b871caeadd29ba4',
                                           276),
              '_normalize_heap_dispatch_if_routing': ('54551c3ce68e045933e39c229955468e2bee6ead6fe9766f42935d1593309802',
                                                      40),
              '_persistent_context_guard': ('c64d3f0b1e8a23c587834effc7697d01d17a20b0a2032a0fe4713d6b69bbdf77',
                                            128),
              '_persistent_lease': ('20f26ce415ec66dc37d8bf3ea79a097e3b1537547fc96058877ec5a258a5d480', 104),
              '_persistent_storage_guard': ('c51bd14af15bb22af8ad0a384c1c700a5c23a90fcd25e012ad977b7e7b77fa04',
                                            396),
              '_poll_lease_valid': ('ac832b82ea8d821dede21c5e9e825e9f7a4479be1dae662c525cdd4f92fb64a8', 168),
              '_prepare_hook_entry': ('e46707ed62da6217391c4aee61d074980abd67826655c1b508159548cabefd74',
                                      228),
              '_read_heap': ('c62b9300537310eef59a5369cd8283b59a64a55631de283ea33af52128069783', 44),
              '_read_heap_raw': ('b102f5d502f2a2e45998b1555e7bd6e56d2b7048059ebb9bb37ed6ee415a58cd', 104),
              '_reader_capture_region': ('ba0fe03f2355bdee2e98166022692dab9fa03e444a7c52f890b3407c656a7165',
                                         100),
              '_reader_cooperative_service': ('26a497c1c90813b193743eb08f26bdad6cf702f94019d0c728267b1bfdc41fba',
                                              248),
              '_reader_streaming': ('6e039ba01afc9b34ddd3d82bdddac98662af88639e1c5e1a1989a1fcfd563759', 120),
              '_readonly_overlay_increment': ('e3a6368a10593e4ae7510faf736d644a9d9e383e262070923241741b3f6893b1',
                                              14),
              '_readonly_overlay_le32': ('1daa47eecc292df8356826c0e5a683e3a2c27cec212f901db7b1f01cf3b897df',
                                         30),
              '_readonly_overlay_parent_response': ('d396e68edd3158708ba5a8c9ff5235821bb62b71cc9dc40650cadb9098eaeb12',
                                                    116),
              '_readonly_overlay_pointer_overlap': ('1ee60abf2148621140e7d53a347536afad5e8f7759627bce7da0d045d7ea0348',
                                                    32),
              '_readonly_overlay_put32': ('04dba701f2b2b5a13739b4fde066b45ecceceb28684e620d217ff60cb1308163',
                                          24),
              '_readonly_overlay_ram_span': ('5d29104681aa3d3a32c948c924049eb53df8a632f91ee767e9b237c9206c73ce',
                                             56),
              '_readonly_overlay_target_response_bytes': ('4b38d95dfa7ff193118d30455a04e211fa54b7343e9beb12bdd72645835ad036',
                                                          216),
              '_report': ('b792f785d42437bf256924b076c33195d2e47ab832d14ffc03cbb418fd7b5ffe', 340),
              '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
              '_transfer_context_valid': ('2e38d21c53efbfdb3cfb9c8c8913ebda457be30e92e2f0612e7cedd8a8fd708d',
                                          1180)},
 'read_ahead': {'_reader_capture_region': (100,
                                           '42d071690b483af9f73517c26cac56bad92a068f8222d9f2b6e1edb6985e1c0a'),
                '_reader_cooperative_service': (248,
                                                '8e7d888e7a5e9c12f072f4def210b0878513863f6420ecf5d8176bd4b531873d'),
                '_reader_streaming': (120,
                                      '91825843f2471ee612bcb890b5c3382edfd073a7eac8c183fefe601177faa354')},
 'witness': {'_controller_context_valid': (1336,
                                           'cea3f4df4c115576e1fcf06ce4ca8e6bc27ce150c2c10b8d7d53f7aed4f120f8'),
             '_kui_vmu_native_exit_shape_valid.constprop.0':
                 (128, 'dc29e1893944084af7db70f7fa4d1780be2781c92ee2a6e9ddbf055eba8395ad'),
             '_failure_witness.isra.0': (16,
                                         'c5a0f12dba8b0a930b566181318d388a561f2417c7ccce4aa173aa7ee90c2475'),
             '_installed_hooks_valid': (128,
                                        '7073b8839f556dcc567803d319240eb594032293be4b0689c84fbdbfc50db6f0'),
             '_kui_vmu_native_passive_complete': (3440,
                                                  '5ed0552908e0405508688076674786e350194dad7ae0faf0830f8255a7c63b08'),
             '_kui_vmu_native_passive_submit': (1236,
                                                '6847960e31a2ba8523dbf47838902ae118bca44c096c39f0bf9c8ca1bcb250b9'),
             '_kui_vmu_native_patch_encode': (168,
                                              '3bdc25e5c362985e9a8b0cfdb3020c03a1d16e5ebfe654fe8dbe38d5d5495279'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128,
                                                                    '2be85dc461b46bf3f24517b7f9050dc63bcad0b0988849848ee265a99508e85b'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (108,
                                                                      '37f60e98b0fbe4945ee4865b15ea1e87e747fa3876ad9503efb35c194dde01ed'),
             '_kui_vmu_native_timer_callback_owned': (92,
                                                      '92d3af36c0206ad4fd3f6e87aace7bc09f2999674e5f340a96cbfde2f642ee4f'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_normalize_heap_dispatch': (276,
                                          'cbbe4c36f7289a17e5bfdebbcf732ccfd0937c518c002d9127d939bcabd5b9bd'),
             '_normalize_heap_dispatch_if_routing': (40,
                                                     'c1f2bcb7fb72133e10aec2fc5680026f3e2f1bc1c322e1c993f14c9d8b2500de'),
             '_poll_lease_valid': (168, '3b61541d4f49a6069300204ca5479cacbf151ecc98949c07ffe45ec91fe178ef'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_reader_capture_region': (100,
                                        '42d071690b483af9f73517c26cac56bad92a068f8222d9f2b6e1edb6985e1c0a'),
             '_reader_cooperative_service': (248,
                                             '8e7d888e7a5e9c12f072f4def210b0878513863f6420ecf5d8176bd4b531873d'),
             '_reader_streaming': (120, '91825843f2471ee612bcb890b5c3382edfd073a7eac8c183fefe601177faa354'),
             '_readonly_rx.constprop.0': (76,
                                          '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, '235c412fc8b3c14cb5fe25d2f1b43acb5474ce8c41de7858304effea49ae22d2'),
             '_timer_runtime_in': (26, 'a0ff5d4d27ccbf7b073b8bc6f0e6b26659e6b729763f7b02d49aa124ea023824'),
             '_timer_runtime_ram': (104, '4fb7919a6f74ec1c9aa3f434e446b352f7961eb7c565580f0c4e2e52a3eff415')}}
LOW_PINS = {'loan_quiesce':
    (748, '2df1d1e5691fe4efe7aa8752e27d3c2181643ad0fd20ccc5e055a9178026a4b2'),
 'map_refusal': {
    '_native_background_map': ('c69763991931d8b069f1d5a63455a541327acef72848edfa189ac63bd6328cff', 188),
    '_native_resident_terminal': ('5b44ac05b98ebc7021de6361a7a66d377db91f906ebb1580a4038cf5d1f392de', 96),
    '_kui_vmu_observe_low_failure': ('4733a395aa815e4a3227afe93d42acaf69d9a076a84a4fe138e68874d6c6f22d', 60),
}}
MUTABLE_OBJECTS = {
    '_config@vmu_native_passive.c': (8, 2304),
    '_reader_stream@vmu_native_passive.c': (8, 44),
    '_issued_chain@vmu_native_passive.c': (8, 12288),
    '_protected_count@vmu_native_passive.c': (8, 4),
    '_protected_ranges@vmu_native_passive.c': (8, 512),
    '_hook_witness_count@vmu_native_passive.c': (8, 4),
    '_hook_witness@vmu_native_passive.c': (8, 384),
    '_state@vmu_native_passive.c': (8, 100),
    '_durable_card@vmu_native_passive.c': (8, 4160),
    '_overlay@vmu_native_passive.c': (8, 17172),
    '_completion_frame@vmu_native_passive.c': (8, 76),
    '_live_plan@vmu_native_passive.c': (8, 456),
    '_captured@vmu_native_passive.c': (8, 12288),
    '_receivers@vmu_native_passive.c': (8, 192),
    '_overlay_frames@vmu_native_passive.c': (8, 896),
    '_chain_frames@vmu_native_passive.c': (8, 640),
    '_hook_witness_ready@vmu_native_passive.c': (8, 4),
    '_lifetime_plan@vmu_native_passive.c': (8, 616),
    '_issued_plan@vmu_native_passive.c': (8, 44),
    '_controller_pair@vmu_native_passive.c': (8, 188),
    '_row@retail_display.c': (8, 4),
    '_kui_vmu_native_sdk_coherence_state': (8, 48),
    '_kui_vmu_native_passive_consumer': (1, 4),
    '_kui_vmu_native_passive_resume': (1, 4),
    '_kui_vmu_native_readonly_release_resume': (1, 4),
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def marked(image, marker):
    value = image[4].get(marker)
    require(value is None or value == ONE, 'Malformed lean General marker: ' + marker)
    return value is not None


def stage_profile(stage):
    from check_native_compact_background_profile import stage_profile as compact_profile
    compact=compact_profile(stage)
    if not marked(stage, STAGE_MARKER):
        require(stage[4].get('__kui_native_isolation_lease_bytes') != (LEASE_BYTES, 0, 0, 0xfff1),
                'Compact General stage lost its explicit layout marker')
        return False
    required = ('__kui_native_split_controller_general_stage',
                '__kui_native_split_controller_isolation_stage',
                '__kui_native_bottom_heap_stage', '__kui_native_dual_low_callbacks_stage',
                '__kui_native_private_pool_budget_stage', '__kui_native_readahead_stage')
    require(all(stage[4].get(name) == ONE for name in required),
            'Lean stage requires its exact General/bottom/selected-callback profile')
    lease_bytes=0x47000 if compact else LEASE_BYTES
    require(stage[4].get('__kui_native_isolation_lease_bytes') == (lease_bytes, 0, 0, 0xfff1),
            'Lean stage does not own its exact reviewed lease')
    return True


def worker_profile(worker):
    from check_native_compact_background_profile import worker_profile as compact_profile
    compact=compact_profile(worker)
    if not marked(worker, WORKER_MARKER):
        require(not any(name in worker[4] for name in
                    ('__kui_native_general_timeline_elided_worker',
                     '__kui_native_general_fault_capture_elided_worker')),
                'Elided General worker lost its explicit lean layout marker')
        return False
    required = ('__kui_native_split_controller_general_worker',
                '__kui_native_split_controller_isolation_worker',
                '__kui_native_bottom_heap_worker', '__kui_native_owned_timer_callbacks_worker',
                '__kui_native_general_timeline_elided_worker',
                '__kui_native_general_trace_elided_worker',
                '__kui_native_general_fault_capture_elided_worker',
                '__kui_native_general_unbounded_pairs_worker', '__kui_native_readahead_worker')
    require(all(worker[4].get(name) == ONE for name in required),
            'Lean worker requires its complete General/owned-timer/elision profile')
    require('__kui_native_raw_owner_terminal_worker' not in worker[4],
            'Lean worker must not advertise the removed raw owner observer')
    lease_bytes=0x47000 if compact else LEASE_BYTES
    worker_bytes=0x12000 if compact else WORKER_BYTES
    require(worker[4].get('__kui_native_isolation_lease_bytes') == (lease_bytes, 0, 0, 0xfff1),
            'Lean worker does not own its exact reviewed lease')
    require(worker[4].get('__kui_native_worker_span') == (worker_bytes, 0, 0, 0xfff1),
            'Lean worker linker did not select its exact reviewed span')
    from check_native_general_diagnostics import inspect
    require(inspect(worker) is not None, 'Lean worker lacks the explicit diagnostic elision proof')
    require(all(name not in worker[4] for name in
                ('_trace', '_hex_font', '_evolution_digest', '_reader_capture', '_reader_cooperative')),
            'Lean worker retained an optional trace/reader observation object')
    _, body, section = symbol_bytes(worker, '_kui_vmu_native_trace_game', 2)
    require(body == bytes.fromhex('0b000900') and section[2] & 7 == 6,
            'Lean worker trace ABI callback is not the proved inert leaf')
    _, header, _ = symbol_bytes(worker, '_kui_vmu_native_worker_exports', 1)
    fields = struct.unpack('<20I', header)
    require(fields[:3] == (0x4b4e5031, 4, 80) and
            fields[9:12] == (0xac100000 + worker_bytes - 4096,
                              0xac100000 + worker_bytes,
                              0xac100000 + worker_bytes),
            'Lean worker does not retain its exact private stack/header geometry')
    return True


def low_profile(image):
    from check_native_compact_background_profile import low_profile as compact_profile
    compact_profile(image)  # Reject malformed compact markers before old-profile selection.
    if not marked(image, LOW_MARKER):
        return False
    require(image[4].get('__kui_vmu_native_background_resident') == ONE,
            'Lean low guards require their Background resident')
    if '__kui_native_readahead_resident' in image[4]:
        require(all(image[4].get(name) == ONE for name in
                    ('__kui_native_readahead_resident', '__kui_native_sdk_sci_route_resident',
                     '__kui_native_sdk_sci_structural_resident')),
                'Lean New low guards lack their exact SDK SCI profile')
    else:
        require('__kui_native_sdk_sci_route_resident' not in image[4] and
                all(name in image[4] for name in
                    ('_kui_retail_release_100', '_kui_retail_release_400', '_kui_retail_release_600')),
                'Lean Legacy low guards lack their separate raw-vector engine')
    return True


def stage_pins(stage, role, historical):
    if not stage_profile(stage):
        return historical
    from check_native_compact_background_profile import stage_profile as compact_profile, stage_pins as compact_pins
    if compact_profile(stage):
        return compact_pins(stage,role)
    from check_native_vmu_toggle_profile import stage_profile as toggle_profile, stage_pins as toggle_pins
    if toggle_profile(stage):
        return toggle_pins(stage, role)
    require(role in STAGE_PINS and STAGE_PINS[role], 'Missing lean General stage pins: ' + role)
    return STAGE_PINS[role]


def worker_pins(worker, role, historical):
    if not worker_profile(worker):
        return historical
    from check_native_compact_background_profile import worker_profile as compact_profile, worker_pins as compact_pins
    if compact_profile(worker):
        return compact_pins(worker,role)
    from check_native_vmu_toggle_profile import worker_profile as toggle_profile, worker_pins as toggle_pins
    if toggle_profile(worker):
        return toggle_pins(worker, role)
    require(role in WORKER_PINS and WORKER_PINS[role], 'Missing lean General worker pins: ' + role)
    return WORKER_PINS[role]


def low_pins(image, role, historical):
    # The dual companion validates its already-executed Legacy guard report
    # against the paired worker's exact profile rather than reopening ELF.
    from check_native_compact_background_profile import worker_profile as compact_worker, low_profile as compact_low, low_pins as compact_pins
    symbols=image[4]
    worker=WORKER_MARKER in symbols or any(name in symbols for name in
        ('__kui_native_background_compact_worker','__kui_native_background_compact_worker_layout'))
    resident=LOW_MARKER in symbols or '__kui_native_background_compact_resident' in symbols
    require(not(worker and resident),'Low pin companion has ambiguous worker/resident roles')
    if worker:
        selected=worker_profile(image);compact=compact_worker(image)
    elif resident:
        selected=low_profile(image);compact=compact_low(image)
    else:
        compact_low(image)  # Refuse orphan compact geometry before historical fallback.
        return historical
    if not selected:
        return historical
    if compact:
        return compact_pins(image,role)
    require(role in LOW_PINS and LOW_PINS[role], 'Missing lean General low guard pins: ' + role)
    return LOW_PINS[role]


def prepared_objects(worker):
    """Require the complete source-qualified mutable object inventory."""
    require(worker_profile(worker), 'Lean state inventory requires its explicit worker')
    from check_native_compact_background_profile import worker_profile as compact_profile, MUTABLE_OBJECTS as compact_objects
    compact=compact_profile(worker)
    expected=compact_objects if compact else MUTABLE_OBJECTS
    worker_bytes=0x12000 if compact else WORKER_BYTES
    require(expected, 'Lean mutable-object inventory has not been reviewed')
    from check_vmu_native_persistent import prepared_symbol_records
    records = prepared_symbol_records(worker)
    actual = {}
    for name, address, count, kind, owner in records:
        if kind != 1 or not count:
            continue
        section = worker[3][owner]
        if section[2] & 7 != 3:
            continue
        require(name not in actual and section[1] in (1, 8) and
                section[3] <= address < address + count <= section[3] + section[5] and
                0xac100000 <= address < address + count <= 0xac100000 + worker_bytes - 4096,
                'Lean mutable object lacks unique private state ownership: ' + name)
        actual[name] = (section[1], count)
    require(actual == expected, 'Lean complete mutable-object inventory changed')
    return dict(objects=len(actual), bytes=sum(row[1] for row in actual.values()),
                complete_source_qualified_inventory=True)


def profile_mutation_checks(stage, worker, resident, legacy):
    """Keep the new profile selectors and complete state inventory fail closed.

    These are metadata and actual ELF-symbol/header mutations. Instruction
    bodies and typed pointer targets have separate whole-kernel mutations in
    their executing auditors; this covers the new profile-selection boundary.
    """
    from check_loader_layout import SYM, region
    from check_native_compact_background_profile import worker_profile as compact_profile, MUTABLE_OBJECTS as compact_objects
    compact=compact_profile(worker)
    expected_objects=compact_objects if compact else MUTABLE_OBJECTS
    worker_bytes=0x12000 if compact else WORKER_BYTES
    counts = dict(marker_mutations=0, header_mutations=0,
                  mutable_inventory_mutations=0, unknown_role_mutations=0)

    def refused(callback, mutant, message):
        try:
            require(callback(mutant), message)
        except (ValueError, KeyError, struct.error):
            return
        raise ValueError(message)

    images = ((stage, stage_profile, STAGE_MARKER),
              (worker, worker_profile, WORKER_MARKER),
              (resident, low_profile, LOW_MARKER),
              (legacy, low_profile, LOW_MARKER))
    for image, selector, marker in images:
        require(selector(image), 'Lean selector mutation test requires its exact original profile')
        # Every affirmative prerequisite belongs to this explicit profile.
        # Delete each marker and also forge its absolute value independently.
        suffix = '_stage' if selector is stage_profile else '_worker' if selector is worker_profile else '_resident'
        names = [name for name in image[4] if name.startswith('__kui_native_') and
                 (name == marker or name.endswith(suffix)) and
                 image[4][name] == ONE and
                 (name == marker or name in (
                     '__kui_native_split_controller_general_stage',
                     '__kui_native_split_controller_isolation_stage',
                     '__kui_native_bottom_heap_stage', '__kui_native_dual_low_callbacks_stage',
                     '__kui_native_private_pool_budget_stage', '__kui_native_readahead_stage',
                     '__kui_native_split_controller_general_worker',
                     '__kui_native_split_controller_isolation_worker',
                     '__kui_native_bottom_heap_worker', '__kui_native_owned_timer_callbacks_worker',
                     '__kui_native_general_timeline_elided_worker',
                     '__kui_native_general_trace_elided_worker',
                     '__kui_native_general_fault_capture_elided_worker',
                     '__kui_native_general_unbounded_pairs_worker', '__kui_native_readahead_worker',
                     '__kui_native_readahead_resident', '__kui_native_sdk_sci_route_resident',
                     '__kui_native_sdk_sci_structural_resident'))]
        if selector is low_profile and image[4].get('__kui_vmu_native_background_resident') == ONE:
            names.append('__kui_vmu_native_background_resident')
        if compact:
            names.append('__kui_native_background_compact'+suffix)
            if selector is worker_profile:
                names.append('__kui_native_background_compact_worker_layout')
        for name in names:
            for value in (None, (2, 0, 0, 0xfff1)):
                symbols = dict(image[4])
                if value is None:
                    del symbols[name]
                else:
                    symbols[name] = value
                refused(selector, (*image[:4], symbols),
                        'Lean selector accepts a missing/forged prerequisite: ' + name)
                counts['marker_mutations'] += 1
    for image, selector, name in (
            (stage, stage_profile, '__kui_native_isolation_lease_bytes'),
            (worker, worker_profile, '__kui_native_isolation_lease_bytes'),
            (worker, worker_profile, '__kui_native_worker_span'),
            *((image,low_profile,'__kui_native_isolation_lease_bytes')
              for image in (resident,legacy) if compact)):
        symbols = dict(image[4]); value, size, kind, owner = symbols[name]
        symbols[name] = (value + 32, size, kind, owner)
        refused(selector, (*image[:4], symbols), 'Lean selector accepts forged layout: ' + name)
        counts['marker_mutations'] += 1
    symbols = dict(worker[4]); symbols['__kui_native_raw_owner_terminal_worker'] = ONE
    refused(worker_profile, (*worker[:4], symbols),
            'Lean selector accepts a removed observer capability')
    counts['marker_mutations'] += 1
    address, body, section = symbol_bytes(worker, '_kui_vmu_native_worker_exports', 1)
    for field in (0, 1, 2, 9, 10, 11):
        data = bytearray(worker[0]); offset = section[4] + address - section[3] + 4 * field
        struct.pack_into('<I', data, offset, struct.unpack_from('<I', data, offset)[0] ^ 4)
        refused(worker_profile, (bytes(data), *worker[1:]),
                'Lean selector accepts altered private header geometry')
        counts['header_mutations'] += 1
    prepared_objects(worker)
    for section in worker[3]:
        if section[1] != 2:
            continue
        strings = worker[3][section[6]]
        names = region(worker[0], strings[4], strings[5], 'Lean mutant symbol strings')
        source = ''
        for offset in range(section[4], section[4] + section[5], SYM.size):
            name, value, size, info, other, owner = SYM.unpack_from(worker[0], offset)
            name = names[name:].split(b'\0', 1)[0].decode('ascii')
            if info & 15 == 4:
                source = name
                continue
            qualified = name + ('@' + source if not info >> 4 else '')
            if qualified not in expected_objects:
                continue
            # Removing an object, changing its size, or moving it across the
            # private-stack boundary must change the complete inventory.
            for at, changed in ((8, 0), (8, size + 4),
                                (4, 0xac100000 + worker_bytes - 4096)):
                data = bytearray(worker[0]); struct.pack_into('<I', data, offset + at, changed)
                refused(prepared_objects, (bytes(data), *worker[1:]),
                        'Lean mutable inventory accepts a changed object: ' + qualified)
                counts['mutable_inventory_mutations'] += 1
    require(counts['mutable_inventory_mutations'] == 3 * len(expected_objects),
            'Lean state mutation corpus does not cover every reviewed object')
    for image, selector in ((stage, stage_pins), (worker, worker_pins),
                            (resident, low_pins), (legacy, low_pins)):
        refused(lambda mutant: (selector(mutant, '__unknown_role__', {}), True)[1], image,
                'Lean selector silently falls back for an unknown pin role')
        counts['unknown_role_mutations'] += 1
    return dict(counts, mutation_rejections=sum(counts.values()))
