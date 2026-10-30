#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact separately reviewed General VMU-toggle linked profiles.

The preceding lean General maps remain unchanged. These static maps cover
its new mode guards and physical passthrough; unchanged reader bodies retain
all historical instruction models. No pin is learned from an inspected ELF.
"""
import hashlib
import struct
from check_vmu_native_worker_gateway import symbol_bytes
ONE = (1, 0, 0, 0xfff1)
STAGE_MARKER = '__kui_native_vmu_toggle_stage'
WORKER_MARKER = '__kui_native_vmu_toggle_worker'
CAPABILITY = struct.pack('<8I', 0x5649554b, 0x3130544d, 1, 32, 7, 0x51000, 0x30000, 1)

STAGE_PINS = {'cache_gateway': {'_native_resolution_replay': (284,
                                                 'a26bf9f2e49c550ac85498e16ab04f98438ba2d35b0ea06f0905309b5ccf424b')},
 'dual': {'_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
          '_native_dual_reader_bind': (68,
                                       'e45de5c5282542db4bc03005e75786519aa12c0e93a5ba10618d776636227ded'),
          '_select_resident': (272, '977dafef238a1fbad6387e19016bad5effcf99e95401ed5ca9323b8623db2678')},
 'installer': frozenset({(2064, '11d31bd1bb41b17ac241b2215272a0b4f39129834714f43c858d7f5f8bbee33f')}),
 'read_ahead': {'_kui_vmu_native_background_map_address': (92,
                                                           '431865c1e385488b2c6be9c59219f826be4803aa55d5a8b3e06748a6c9fb3536'),
                '_native_readahead_bind': (1244,
                                           '181782f71e9711ec802e3d23609902f74c24807a0ca1c430b29c6f2c74d1e278')},
 'sdk': {'_fingerprint': (88, 'bf7c2239d88c281e4751e5f5fe6aad574e264b1fa999cbf5bf1332f039056da8'),
         '_kui_native_sdk_sci_route_admit': (796,
                                             '6e40aba612d634a6da3b5594534173bb4ff44c39e0e1c8f82ae160a80589d701'),
         '_kui_native_sdk_sci_route_discover': (1856,
                                                '5ff192756fa2ac42d1ae7347daa95d491d8bdb86f3934c94468a1ec84a7f96aa'),
         '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
         '_live_words': (80, 'dc1b0e5e6615fadc8aaea7ff95eb3ba7843cee979b23f7a23e41722932bcfcd1'),
         '_native_sdk_sci_route_bind': (952,
                                        'c2d28fb261e461215aa0920b13f4f0143f7ed9c6c6b3916b3ae46de690f38d6e'),
         '_normalized': (192, '96a4bd17950ccae6e508554b2813f662228851924f33eb19cc4069f07c6ea048'),
         '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334')},
 'timeline_binder': ('48dfc82b7248f3359b47e5a6b17b680a390e83f43b788f7a031a4a8a8dd5f988', 180),
 'toggle': {'_native_passive_run': (2064, '11d31bd1bb41b17ac241b2215272a0b4f39129834714f43c858d7f5f8bbee33f'),
            '_native_persist_storage_init': (720,
                                             '7064200a973283e13f01c9ad76ca27e4c979f04ab3c3d9ef0f76811eb9cee447'),
            '_native_runtime_diagnostics': (44,
                                            '61970c51dd31c8fd39e9b450014b77f87cd3f5c39c7113725351fb3099947eb4'),
            '_native_runtime_mode': (88, '45437f22192458754a2f38c9a867273e639392dd9ce005f1d5d0f29c18c9a9dc')}}

WORKER_PINS = {'dual': {'_reader_capture_region': (100, 'c72e3817d131eaa98589d3007d4084bed10291ecba293ac1a7ddc3e742ba9efe'),
          '_reader_cooperative_service': (248,
                                          '2c14b32e6cc688f0b373cfd663975b7e4b7f1cf6d3974cfc524ae9f0aa206c73'),
          '_reader_streaming': (120, '22fc64df90bd8d045c8e42d38abbffd911cd7d4e367f916782d999cfaad54545')},
 'live_boundary': (216, 'd177fc5fa1abf7b029790e120f36c29b8e171acaea06a64bb4cb2a6af9084baa'),
 'prepared': {'.rodata': ('4d4183a2bd743782c17d31f8d1b94dbbddc85643d39ac8ecab226ad8ac6abc73', 1176),
              '_chain': ('38d4676748b3ed12f0161960668dc50d1effde4cee29091b5d361d7508d1db50', 908),
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
              '_kui_vmu_native_passive_arm': ('c5ee57237ee0d9d0d9f211d6c57ab0fdc28fb3abcbed1886c6ae8cceffe895b1',
                                              2576),
              '_kui_vmu_native_passive_complete': ('6c408a49a8ab2d58cf2e68465f4d439cd730ebb3bd1b0a4ee88d151eb7cf4fb7',
                                                   3828),
              '_kui_vmu_native_passive_split_submit': ('55fe13bdcab382490f2c96bd76a7ba7eefee1ea118c859eca2db677597615626',
                                                       860),
              '_kui_vmu_native_passive_submit': ('5f842abc44187530f99c70f03a2b3515bbb726394f6fd60333807cf2db0508fb',
                                                 1272),
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
              '_persistent_lease': ('2cc5f048419d235babba86e995e68fa108301c09aa8822d858095d934da52bac', 108),
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
                                           'c72e3817d131eaa98589d3007d4084bed10291ecba293ac1a7ddc3e742ba9efe'),
                '_reader_cooperative_service': (248,
                                                '2c14b32e6cc688f0b373cfd663975b7e4b7f1cf6d3974cfc524ae9f0aa206c73'),
                '_reader_streaming': (120,
                                      '22fc64df90bd8d045c8e42d38abbffd911cd7d4e367f916782d999cfaad54545')},
 'toggle': {'_kui_vmu_native_passive_arm': (2576,
                                            '0ce30fca1ce803f4e0f981bfa56375dbbe16a53603c0ec2830dbf30a19eba3dd'),
            '_kui_vmu_native_passive_complete': (3828,
                                                 '56572922876ccbf04b8536890b98650a00723f4ac79544308f23750ec1e7f2ed'),
            '_kui_vmu_native_passive_submit': (1272,
                                               '5d2f7ed886ea31a6b68d0f26dbe6f3c48706e6feba0d9c3b5c675e83841a8e15'),
            '_kui_vmu_native_persistent_sd_transaction': (100,
                                                          'e2e38d87976e58c4526393a29409a80e19566ad4695c94fc3b28941946d36f6c'),
            '_persistent_admit': (28, '8c1635fe78c3696ad54ca4705fe0c84bd8c9f99f52f9140fb35cb8775250f53f'),
            '_persistent_handler': (28, 'd947f854d6b820ac8c427fe7427e8ec03b84d9023df1e7e4a0657978e68b97b2'),
            '_persistent_lease': (108, '2ea5542a0e1febe8dbdd9121152f7ea1cb3b88450160310afd880a3d69db1eea')},
 'witness': {'_controller_context_valid': (1336,
                                           '41fac7e5f9a5e2df4b011ceeeb8e12a50460a14bd35d0142749e64f7c1fdd36e'),
             '_failure_witness.isra.0': (16,
                                         'a5a412944eb2527e002cc98a018d5e23663709634978f77ee28e73d3bde2eaca'),
             '_installed_hooks_valid': (128,
                                        '3246f03b634c5c1e6d522f0a3428b774e547f921f07142e142b0e5ffb739b77a'),
             '_kui_vmu_native_exit_shape_valid.constprop.0': (128,
                                                              '8fb1e4404f000ac3a74c497dd54cc84613fbd63f5d7e21e60421065b15bb6100'),
             '_kui_vmu_native_passive_complete': (3828,
                                                  '56572922876ccbf04b8536890b98650a00723f4ac79544308f23750ec1e7f2ed'),
             '_kui_vmu_native_passive_submit': (1272,
                                                '5d2f7ed886ea31a6b68d0f26dbe6f3c48706e6feba0d9c3b5c675e83841a8e15'),
             '_kui_vmu_native_patch_encode': (168,
                                              '40bfb37200858d90d246fce71d000460c912eec5679991f3e5e4cef854ac73fe'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128,
                                                                    'a244b8b7b249fe0dd630103bea25a546f8d7dd6e9c46aa82b1e792f22993314c'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (108,
                                                                      '29948b6b73618390ba0d533004efde09cd3390f77526d9dd31889bd1975e27e7'),
             '_kui_vmu_native_timer_callback_owned': (92,
                                                      'd0ea59080d40e2d4d86fed614ac09a78ef4f7ab9659a1a6670621e59802debfb'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_normalize_heap_dispatch': (276,
                                          'fd9caa746c386e62c9b3ffba925aaf3d1c8778f441b3df52009808588a9b8741'),
             '_normalize_heap_dispatch_if_routing': (40,
                                                     '71eefe33646722484f47e89882fe235945cc08fe7a9bd56373232fb467a13773'),
             '_poll_lease_valid': (168, 'e7e243573a44dd80a819274cc443172347468dc51268e9b28dc9bc3bcdcf5081'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_reader_capture_region': (100,
                                        'c72e3817d131eaa98589d3007d4084bed10291ecba293ac1a7ddc3e742ba9efe'),
             '_reader_cooperative_service': (248,
                                             '2c14b32e6cc688f0b373cfd663975b7e4b7f1cf6d3974cfc524ae9f0aa206c73'),
             '_reader_streaming': (120, '22fc64df90bd8d045c8e42d38abbffd911cd7d4e367f916782d999cfaad54545'),
             '_readonly_rx.constprop.0': (76,
                                          '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, '0b638e0ac0e14bb8f00e5ba53db69b05ca4b9ddfa921dd88a3a32c50a0e0d2ec'),
             '_timer_runtime_in': (26, 'a0ff5d4d27ccbf7b073b8bc6f0e6b26659e6b729763f7b02d49aa124ea023824'),
             '_timer_runtime_ram': (104, '4fb7919a6f74ec1c9aa3f434e446b352f7961eb7c565580f0c4e2e52a3eff415')}}

def require(ok, message):
    if not ok:
        raise ValueError(message)

def marked(image, marker):
    value = image[4].get(marker)
    require(value is None or value == ONE, 'Malformed VMU toggle marker: ' + marker)
    return value is not None

def stage_profile(stage):
    if not marked(stage, STAGE_MARKER):
        require('_kui_vmu_native_toggle_capability' not in stage[4],
                'VMU toggle capability lost its explicit stage marker')
        return False
    from check_native_general_lean_profile import stage_profile as lean
    require(lean(stage), 'VMU toggle requires the complete exact lean General stage')
    _, body, section = symbol_bytes(stage, '_kui_vmu_native_toggle_capability', 1)
    require(body == CAPABILITY and section[2] & 7 == 2,
            'VMU toggle capability differs from physical A2 passthrough/324KiB/192KiB contract')
    return True

def worker_profile(worker):
    if not marked(worker, WORKER_MARKER):
        return False
    from check_native_general_lean_profile import worker_profile as lean
    require(lean(worker), 'VMU toggle requires the complete exact lean General worker')
    return True

def stage_pins(stage, role):
    require(stage_profile(stage), 'VMU toggle stage pins require its explicit profile')
    require(role in STAGE_PINS and STAGE_PINS[role], 'Missing VMU toggle stage role: ' + role)
    return STAGE_PINS[role]

def worker_pins(worker, role):
    require(worker_profile(worker), 'VMU toggle worker pins require its explicit profile')
    require(role in WORKER_PINS and WORKER_PINS[role], 'Missing VMU toggle worker role: ' + role)
    return WORKER_PINS[role]

def exact_toggle_kernels(stage, worker):
    require(stage_profile(stage) and worker_profile(worker),
            'VMU toggle stage and worker profiles must be paired')
    result = {}
    for label, image, selected in (('stage', stage, STAGE_PINS['toggle']),
                                   ('worker', worker, WORKER_PINS['toggle'])):
        rows = {}
        for name, expected in selected.items():
            _, body, section = symbol_bytes(image, name, 2)
            actual = (len(body), hashlib.sha256(body).hexdigest())
            require(actual == expected and section[2] & 7 == 6,
                    'VMU toggle complete reviewed kernel changed: ' + name)
            rows[name] = dict(bytes=actual[0], sha256=actual[1])
        result[label] = rows
    return result
