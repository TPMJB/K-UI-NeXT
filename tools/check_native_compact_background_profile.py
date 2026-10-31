#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact 72 KiB worker / 284 KiB combined Background profile.

The geometry and mutable inventory are authored from the source contracts.
Expected instruction maps are authored offline after source, linked SH and
typed-pointer review. Unknown roles fail; auditors never derive expectations
from the artifact under test.
"""

ONE = (1, 0, 0, 0xfff1)
WORKER_BYTES = 0x12000
LEASE_BYTES = 0x47000
MARK = '__kui_native_background_compact_'
GENERAL_MARKERS = {
    'stage': '__kui_native_general_lean_stage_layout',
    'worker': '__kui_native_general_lean_worker_layout',
    'resident': '__kui_native_general_lean_resident_layout',
    'auxiliary': '__kui_native_general_lean_auxiliary',
}

# Fixed expected maps, authored offline from the first compact GCC15.2 build
# carrying the d6d517fa133a comparison label. Worker prepared signatures retain
# all 48 historical kernels: 39 unchanged normalized contracts, nine reviewed
# compact geometry/one-staging contracts. Raw closures retain the same typed
# callees, the 43 cold functions/four immutable tables and nine heap predicates.
# Auditors must never learn the expected hashes from their input artifact.
# Independently reviewed against the complete same-source 112 KiB reference:
# eighteen stage opcode streams are unchanged; installer allocation and typed
# pointers match the compact geometry and banner. Historical pins stay intact.
# Re-pinned for the round-toward-zero (zr) and early Shinobi SDK families and zr roots:
# only the six edited discovery functions changed instruction streams; every
# other changed stage body differs solely by relocated symbol/string addresses
# (checked word by word against both linked stages). Worker, residents and
# auxiliary images are byte-identical.
# Re-pinned for the early (pd 1.46) Maple driver family: in the worker only
# layout binding, arm, the runtime check, SDK coherence preparation and the
# transfer context check changed instruction streams (family checks, the
# early lock pointer, and the unmasked early server); in the stage only driver
# discovery, the early layout witnesses, the cache schemas and the coherence
# admit/install skip. Every other changed worker, stage and resident body
# differs solely by relocated addresses, the build label, or a renumbered
# compiler local (_chain's switch table), checked word by word and by typed
# relocation against both linked builds.
# Re-pinned for the early exit pair: in the worker only arm, the hook witness
# (prepare_hook_entry: one loop for 12- and 8-byte windows) and the exit shape
# check changed instruction streams; in the stage only exit discovery, the
# cache schema, the new head encoder and the veneer writer (which leaves the
# pair's first entry for the second, then writes its head). The installer's
# exit loop is unchanged. Every other changed body differs solely by relocated
# addresses, the build label or _chain's renumbered switch table; residents
# and auxiliary are unchanged.
# Re-pinned for the 3 KiB worker stack: stack_valid and the transfer-context
# check change only their stack-size literal (0x1000 to 0xc00) and the relocated
# stack bottom; arm's raw bytes change only by that bottom's address; in the
# stage only native_passive_run's two placement literals (span less the stack).
STAGE_PINS = {
    'cache_gateway': {'_native_resolution_replay': (312,
        'da5cd71a0e5059e2d3326938a70f5f9226ff3d1288704d226cb57093a9f492d2')},
    'dual': {
        '_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
        '_native_dual_reader_bind': (68, '162080646eef37ae49b21a1ce663379acddbe447883fdb071bdb58e843a92ca2'),
        '_select_resident': (272, '8a3491a7fbcb74315c07e7554082b5edd8dcf2b3f5418f43bf80c86203775cd2'),
    },
    'installer': frozenset({(2084, 'a981b8446d8be954f301e5428794b02ae16f078513608b45a47172700c4a9b92')}),
    'read_ahead': {
        '_kui_vmu_native_background_map_address': (92, '14f07de519d8e507e8d2a6d69cdce033ce605aae4773114e5cbfb3fdad56163c'),
        '_native_readahead_bind': (1244, '47adaa8b02b0f3d770e56cbceb009899dad3b2f0524b471d9b3a77c7e78dbdd2'),
    },
    'sdk': {
        '_fingerprint': (88, 'e2b45f877ebd02c1c2f707b1e4cee6ff06c75e8d912e8dac4c801901c1bbc9b6'),
        '_kui_native_sdk_sci_route_admit': (796, 'b65bdd2880ff14a1f572fe1064713d5fb83c0486e9902cf35c1ce218a9ae15a6'),
        '_kui_native_sdk_sci_route_discover': (2848, '8b3ce1ab700db1fa376cd877e117bb91b6c08b1cf4b9df0973b2affe44ab5deb'),
        '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
        '_live_words': (80, 'adfebd80bdc6af79b5eda13805d5433dbf95f04fcc831056912b3cba0948e847'),
        '_native_sdk_sci_route_bind': (952, '498a2175c40395256bb0d8b0f9c68192554a701940f5e22fca1bf7b964ed5604'),
        '_normalized': (192, 'b33337d8c77ef5a9ffd4d01133ea862d64b0e14d66cc6cd0e70422b7cb4244b9'),
        '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    },
    'timeline_binder': ('a042bafffa63f760a2a475fe5e3330bb77157f92d8b627b729b8af6c10bd426e', 180),
    'toggle': {
        '_native_passive_run': (2084, 'a981b8446d8be954f301e5428794b02ae16f078513608b45a47172700c4a9b92'),
        '_native_persist_storage_init': (720, '702520d0ab790c53c5adfc9244b27927856b9415a767a2cf1f0284e2451518ab'),
        '_native_runtime_diagnostics': (44, 'e2e0b8687699dc8f9608b79443ea8fc983a20db965cbf96a2fc6ce3e885b137b'),
        '_native_runtime_mode': (88, 'f61a9ac8bb9d78e46322290b1f5dcfde787b4c3a3fa751f68b4f2c26f59db810'),
    },
}
WORKER_PINS = {'dual': {'_reader_capture_region': (100, '5e44f03ee931c23cf0e1f8ddaecc54889997c2a03b3ba4d869c3638361cd4886'),
          '_reader_cooperative_service': (248, '01435fd3ff8959b30d745c44afd1b7b05ae469a8f821806eea49f22bb8830802'),
          '_reader_streaming': (120, 'a9dbfda834446d06f56d603fdb8c7e7637da6384f20610115ce66f386fe9f5ca')},
 'live_boundary': (216, '7643b2b42d8d3921ace028480ff7abb8b4a2e9987707c7132fe9e4745080a2e4'),
 'prepared': {'.rodata': ('4d4183a2bd743782c17d31f8d1b94dbbddc85643d39ac8ecab226ad8ac6abc73', 1176),
              '_chain': ('32ee04713adbef2163e718e068dfaa0f0d469785cd447f6f94cf58e9a4d71419', 908),
              '_controller_context_valid': ('43d5b6f1aee195f92cd33672814eff6b8fff1bca07c86b921846b90c29204c2b',
                                            1336),
              '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
              '_hook_word': ('69410002bf135878c32df63d2459603428947eadc760aa6309775f82b8801f9c', 132),
              '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
              '_kui_native_loader_timeline_visit': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83',
                                                    4),
              '_kui_vmu_native_lifetime_prepare': ('996ea1f8d21d486d54cab7d747a8fc00c5472b6e2e011512c4830f3cfa0ca30f',
                                                   744),
              '_kui_vmu_native_passive_arm': ('351ddf94e91fdb1749fca5829e6385a6f762519ec8e818cbf87e9f81c2459b84',
                                              2620),
              '_kui_vmu_native_passive_complete': ('19d1f32ed5f93ba620b875d2dd4b41023a9d091eb419d6f666c653e633bd7955',
                                                   3828),
              '_kui_vmu_native_passive_split_submit': ('55fe13bdcab382490f2c96bd76a7ba7eefee1ea118c859eca2db677597615626',
                                                       860),
              '_kui_vmu_native_passive_submit': ('08332141fa19ba049c32ae75f83b7b6936d303ee25357d4ffd6d623dd90b17d9',
                                                 1272),
              '_kui_vmu_native_readonly_release_check': ('0b7e53dfcc8c3b384e8b33b8d874ced89398c26ca02282fc23fb70411ad6c708',
                                                         640),
              '_kui_vmu_native_runtime_fault': ('7c5b43286c4644754e0918f0cf35523e82de8a3e8a6221ad2fcf30e05d356309',
                                                308),
              '_kui_vmu_native_sdk_coherence_prepare': ('2153a40846250ca5cd86e8a1df857b9439bc410f2f7bcd129828a2a1caeea44f',
                                                        456),
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
              '_kui_vmu_readonly_overlay_complete': ('49ac01ef15ed1e6bbd9fae4b40576a0771336428f5fb5401c69a4fa2e8b6e9cf',
                                                     1016),
              '_kui_vmu_readonly_overlay_complete_absent': ('7c35f00e4e76474403bd0d4fa4aca6f5960aa8e11160d00e687c67c16bc293d5',
                                                            284),
              '_kui_vmu_readonly_overlay_prepare': ('3743c1ef7a3223abe05c5afc2f653ee2eb050d0ed32cdc66c9a35d6751f2afa6',
                                                    1176),
              '_normalize_heap_dispatch': ('25fc53a6a5f09e94ff22de610fae56711ac461ca5daae3012b871caeadd29ba4',
                                           276),
              '_normalize_heap_dispatch_if_routing': ('54551c3ce68e045933e39c229955468e2bee6ead6fe9766f42935d1593309802',
                                                      40),
              '_persistent_context_guard': ('c64d3f0b1e8a23c587834effc7697d01d17a20b0a2032a0fe4713d6b69bbdf77',
                                            128),
              '_persistent_lease': ('2cc5f048419d235babba86e995e68fa108301c09aa8822d858095d934da52bac', 108),
              '_persistent_storage_guard': ('02e12598eb83ac2289688d9db13d328df9367b683f164c26a34efa1284f7f2f8',
                                            396),
              '_poll_lease_valid': ('ac832b82ea8d821dede21c5e9e825e9f7a4479be1dae662c525cdd4f92fb64a8', 168),
              '_prepare_hook_entry': ('23541a88a818d61d8601b1acdb8e6635d71480736032e494fe8b9cfa3738a86a', 248),
              '_read_heap': ('c62b9300537310eef59a5369cd8283b59a64a55631de283ea33af52128069783', 44),
              '_read_heap_raw': ('b102f5d502f2a2e45998b1555e7bd6e56d2b7048059ebb9bb37ed6ee415a58cd', 104),
              '_reader_capture_region': ('053bea37d427ea128cb1db463929f2f864db96e1cf76769cf285fa593e6a9f72', 100),
              '_reader_cooperative_service': ('5f291ce85d2e5f3f14470d1ad18889f861de268b13340e1928edd37452ec32c7',
                                              248),
              '_reader_streaming': ('6e039ba01afc9b34ddd3d82bdddac98662af88639e1c5e1a1989a1fcfd563759', 120),
              '_readonly_overlay_increment': ('e3a6368a10593e4ae7510faf736d644a9d9e383e262070923241741b3f6893b1',
                                              14),
              '_readonly_overlay_le32': ('1daa47eecc292df8356826c0e5a683e3a2c27cec212f901db7b1f01cf3b897df', 30),
              '_readonly_overlay_parent_response': ('d396e68edd3158708ba5a8c9ff5235821bb62b71cc9dc40650cadb9098eaeb12',
                                                    116),
              '_readonly_overlay_pointer_overlap': ('1ee60abf2148621140e7d53a347536afad5e8f7759627bce7da0d045d7ea0348',
                                                    32),
              '_readonly_overlay_put32': ('04dba701f2b2b5a13739b4fde066b45ecceceb28684e620d217ff60cb1308163', 24),
              '_readonly_overlay_ram_span': ('5d29104681aa3d3a32c948c924049eb53df8a632f91ee767e9b237c9206c73ce',
                                             56),
              '_readonly_overlay_target_response_bytes': ('4b38d95dfa7ff193118d30455a04e211fa54b7343e9beb12bdd72645835ad036',
                                                          216),
              '_report': ('b792f785d42437bf256924b076c33195d2e47ab832d14ffc03cbb418fd7b5ffe', 340),
              '_stack_valid': ('dd348cfbf4d1379d905dcd08aed813db8e2df61b1ce3cf76edfee604bc5d5d26', 116),
              '_transfer_context_valid': ('157869c251dd3280e882ab4c410b1ef7ddfe9a011863bbaa291141fe7cbb2892',
                                          1208)},
 'read_ahead': {'_reader_capture_region': (100,
                                           '5e44f03ee931c23cf0e1f8ddaecc54889997c2a03b3ba4d869c3638361cd4886'),
                '_reader_cooperative_service': (248,
                                                '01435fd3ff8959b30d745c44afd1b7b05ae469a8f821806eea49f22bb8830802'),
                '_reader_streaming': (120, 'a9dbfda834446d06f56d603fdb8c7e7637da6384f20610115ce66f386fe9f5ca')},
 'toggle': {'_kui_vmu_native_passive_arm': (2620,
                                            'bfde74557a0bc698363707e3d21fa98e58e70b8b70843e7679e49ee8da6c3772'),
            '_kui_vmu_native_passive_complete': (3828,
                                                 '7f14eee76224a588f8a5357314597ed4092c22f4bc43fc5992b1b79a2c5dd665'),
            '_kui_vmu_native_passive_submit': (1272,
                                               'd1479682b2b5b74ea22500de613ff7d029fb1a7d9bbbf3f95293692e7621b960'),
            '_kui_vmu_native_persistent_sd_transaction': (100,
                                                          '31c02bb2e026c9cb1c56747d3b8f40e57b822903c666ec15103a53110561e2a1'),
            '_persistent_admit': (28, '3a0f2eec925207698c4a1d90b8c1e1be53149d53fe3dc5662a938bbc0653d62d'),
            '_persistent_handler': (28, 'f5be4bad2bd940719b70c2f848eca4a48a4604414339b7f0bc64442a0e0ed2b4'),
            '_persistent_lease': (108, 'c7468d9fb7bf14fb97f103749436bafe8b5aaa834b975fe116b0cc2d46bbafbf')},
 'witness': {'_controller_context_valid': (1336,
                                           '1b9f1a54dd12741ac1e3b224abaaca5db7cc8c46f0697dbd70036f0218b48dd4'),
             '_failure_witness.isra.0': (16, '58c27c9105db483091949ba7b5974c96f68d9c7750a4eb7f795dbc8bd087b840'),
             '_installed_hooks_valid': (128, 'be27723c917e5daf7c9406e9083e15cedfc5a9c4b56ac85f0e29802e258b5265'),
             '_kui_vmu_native_exit_shape_valid.constprop.0': (156,
                                                              '5eb69fc36500ea2d7e0e4644bf6b4360094e443e860cb7fac45b740cf5e24624'),
             '_kui_vmu_native_passive_complete': (3828,
                                                  '7f14eee76224a588f8a5357314597ed4092c22f4bc43fc5992b1b79a2c5dd665'),
             '_kui_vmu_native_passive_submit': (1272,
                                                'd1479682b2b5b74ea22500de613ff7d029fb1a7d9bbbf3f95293692e7621b960'),
             '_kui_vmu_native_patch_encode': (168,
                                              '14d3523640e1b669dd84bcb4e3987dfed013d0d8e47aca22ec8312e1c2938afc'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128,
                                                                    '544fb0f7bb7fd79aed73d0fb85d6eacc10834c5d4e4b4fe5137628ff8f485b5e'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (108,
                                                                      '2b7de2d570cf11cdf3cb904d583064df18c89705ca3e2768bf8a0794dc68aba2'),
             '_kui_vmu_native_timer_callback_owned': (92,
                                                      '7479884130e2f44a69f4497a4e3d7a090db95dcff1b03bc1ad8932909e75cfe1'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_normalize_heap_dispatch': (276, '2baa207a7323ac876c8b9933fd0bc044d4781c08c44177fdf9c9f7140f48cff1'),
             '_normalize_heap_dispatch_if_routing': (40,
                                                     '0a979b16cce6a56f53501fbde6f45763a694949dcef0a8c2d836d45d86785874'),
             '_poll_lease_valid': (168, '77c610637ae16639d86c483200e4ae9cfa8634dc3923a831c00384de6f9aaea2'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_reader_capture_region': (100, '5e44f03ee931c23cf0e1f8ddaecc54889997c2a03b3ba4d869c3638361cd4886'),
             '_reader_cooperative_service': (248,
                                             '01435fd3ff8959b30d745c44afd1b7b05ae469a8f821806eea49f22bb8830802'),
             '_reader_streaming': (120, 'a9dbfda834446d06f56d603fdb8c7e7637da6384f20610115ce66f386fe9f5ca'),
             '_readonly_rx.constprop.0': (76, '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, '10d3b8565a76bc6e48b50cec3cce59d2dae06d425f1348fd70a93efc3d8e135b'),
             '_timer_runtime_in': (26, 'a0ff5d4d27ccbf7b073b8bc6f0e6b26659e6b729763f7b02d49aa124ea023824'),
             '_timer_runtime_ram': (104, '4fb7919a6f74ec1c9aa3f434e446b352f7961eb7c565580f0c4e2e52a3eff415')}}
# Offline comparison to the complete same-source 112 KiB reference proved
# identical normalized instructions and typed targets. Only three map/terminal
# geometry literals and one transaction-span literal changed. The failure
# transfer is byte/typed-target identical to its published General pin.
LOW_PINS = {
    'loan_quiesce': (748,'2eee70d3deaabc2a92613e92de0d363ec0bbc86e847f43c9129095ccd8a5b216'),
    'map_refusal': {
        '_native_background_map': ('50260110f81563aca9bec06857997efa74397dd34ee1f058f75d49c011f0fe39',188),
        '_native_resident_terminal': ('d5754f702f81b48f4742211746626a13327bddf47702ff26fac2b76ab6be1196',96),
        '_kui_vmu_observe_low_failure': ('4733a395aa815e4a3227afe93d42acaf69d9a076a84a4fe138e68874d6c6f22d',60),
    },
}
# The 6144-byte auxiliary matches the published General code byte-for-byte
# except six reviewed 32-bit geometry literals: lease, payload ceiling,
# context/entry/state offsets, and cache offset. All instructions are intact.
AUX_CODE_PIN = (6144,'5e22bc3ced6dbd9eb0fd43f5a80aeafc2cb47d53c134db41fa66838561fe0f9f')
COLD_FUNCTIONS = {'_alias@vmu_native_stage_placement.c': (32, '5d5b341944aa4da08467cfca39b78f276224218bd3ed9fd094c99456668b974d'),
 '_allocation_safe@vmu_native_heap_probe_stage.c': (88,
                                                    '7644947f5ca5dc3a6c03083937459ff074dfb9216d50c3dee4ec505aa99c2c97'),
 '_at@vmu_native_heap_budget.c': (92, 'e25b91e557630914f31039a157f937c46ce0d1b911673aa527237d1567ef492e'),
 '_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_code_sha@vmu_native_heap_probe_stage.c': (88,
                                             'f9b57c489a7d5cfdfa7edbfba35b47e8a16adc80bafabe434003b67bc98dd32d'),
 '_control@vmu_native_heap_probe_stage.c': (28,
                                            '0f855a783d8dbf24c0b484b0066b7bd1f2854a5edc1d8d33bb17748e118b9ee6'),
 '_excludes_stage@vmu_native_stage_placement.c': (64,
                                                  'bfeee624c026dafa79a8e4bee2686c1507acb493b82af5bf87b2f72e3f9910c2'),
 '_fresh@vmu_native_heap.c': (116, '1620c90c4f08b858ba6cee7e17c4c976e8f969a5f977990a1f71fa8f707dd008'),
 '_heap_bottom_code_span@vmu_native_heap.c': (88,
                                              'a1c9d8b31237481198448e334870bbebc10c9e7314134d797aea24aafdf464f7'),
 '_heap_bottom_halfword@vmu_native_heap.c': (58,
                                             'd66affe12d8e87985e0bbe094cc393649bad89f9e4494f55004afda5f193ab39'),
 '_identity@vmu_native_heap.c': (412, '42679059ab5ff1e0d5777288c8cefa98d771950f8d14af975966d79d8e3ae975'),
 '_index@vmu_native_heap.c': (100, '41006ceec659908744271fc09e025bae57a3011ecff01bf49fb0a5febcbb5bfe'),
 '_kui_vmu_native_heap_bottom_allocated': (172,
                                           '767222ef27c1b61996e54dd369acb5ad5e5a4bf4b71289506cf1c3679f5b6ffa'),
 '_kui_vmu_native_heap_bottom_code_valid': (360,
                                            'ab891a340a48e48752807f1c38e8b24e08680c95888dc2983e07d07ffdc07821'),
 '_kui_vmu_native_heap_bottom_fresh': (148, 'e54ed95ff5e5c32416b70af7bd25f5070aa744c98323869286d6ea9fe41c48ac'),
 '_kui_vmu_native_heap_bottom_placeholder': (196,
                                             '8208902f250eb81ca8e3d536385d2efa61a44a2125f784bc1206d5b510e2e684'),
 '_kui_vmu_native_heap_bottom_ready': (144, '99edc80c905755d4c0955a8cd602a3732456c41cf60c03bd02de4c2ab92fe397'),
 '_kui_vmu_native_heap_budget_prepare': (2370, 'aab1584b75a91ea6c789dfdbaf52a68f13e45dbd4514e9b33fe44615d323e223'),
 '_kui_vmu_native_heap_fresh': (268, 'a59da3e567ebdb186baa2a80497a6309f6e0c7acd7299e355e52ac191a6e307d'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               '423fe820e93ba8d5c58dd64e8387e1889f1c1359758af26263c9fb742442e89e'),
 '_kui_vmu_native_heap_lease_valid': (52, '8ca30ab0bcf66f2ad733fbc1f88316bc287fd099274d2d15aec67c5c5d10a0c3'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   '0951255068758297e8e1af60570e579e676f573dae7b23b72e8b1469aff7e389'),
 '_kui_vmu_native_heap_preallocate': (36, 'c8f0609f20bef19ab8a640fb7fc25e2536ae7bba469022bac7516e1b130047d8'),
 '_kui_vmu_native_stage_bottom_allocation_safe': (308,
                                                  'a6bddeddf57558082c9c8f92d2021fdfccb628aa49e6dd02aea7a776e186603d'),
 '_kui_vmu_observe_stage_install': (444, '89d69b974a69f26eaf3c5eaf11239ddc046d69ff1a22a43f5c047a0d1e3f15ab'),
 '_le32@vmu_native_heap_budget.c': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
 '_long_literal@vmu_native_heap_budget.c': (128,
                                            'fef4b78442d393f8f94b03d0e76fd0ee4fa384c6050bdf00345aec958800a688'),
 '_memcmp': (34, '3d7da64606fe412df2341ca8e1c2c56d14d3f4798465f1ec02f6d2994c6aefd9'),
 '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
 '_memset': (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a'),
 '_metadata@vmu_native_heap.c': (416, 'a46a6caa83cc32addc3d96382dca331e76b5be0c1fd9a169b4ba322ee9df8596'),
 '_native_allocate_bottom@vmu_native_heap_probe_stage.c': (304,
                                                           '3708f7b26f55e87d901d349f9afbaf78d057bdbfea80265164e941f48604d500'),
 '_native_allocator_unchanged@vmu_native_heap_probe_stage.c': (68,
                                                               '9f21b32f09058a9103028b417bf33f155542e7bdc36f3497f6fe9d4991bb7383'),
 '_native_budget_prepare@vmu_native_heap_probe_stage.c': (92,
                                                          'd1d076322f6b0df770e76b0e7174e80cf3f8810507bb6a4de3d0077bb57594cf'),
 '_native_budget_publish@vmu_native_heap_probe_stage.c': (132,
                                                          'f3ff7fb06792f11b4476a368fa4b55a423fe6b487cc07d484a461c6cbdb97f6b'),
 '_native_low_bindings@vmu_native_heap_probe_stage.c': (64,
                                                        '247e01141df0d5c92f51ed749fcca40c43be5a9c5165c34a055bd168300af0ea'),
 '_native_read@vmu_native_heap_probe_stage.c': (84,
                                                'b6bc33c6d4c36f9e6625ccf6afdc55b98e2e8f684d95194687fa946071a4804d'),
 '_overlaps.constprop.0@vmu_native_heap_budget.c': (124,
                                                    '8978153f7d78db404944ca81823c9d751d9c4c8a478f63de79dd7cd2037dff39'),
 '_ram@vmu_native_heap_budget.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_read_is@vmu_native_heap.c': (80, 'df7a8595e4b4c2f7d45fb8d2d8f8be8f920c58b92147181418ee435a08004a60'),
 '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_span@vmu_native_stage_placement.c': (80, 'ae035645c09ef51c54f3b1d7a23ca0c672620f3a1da0249e2fbcc8113b028bc2'),
 '_word@vmu_native_heap_budget.c': (52, 'aa48f92d2d40d911e172fd18c1e14038c26b3c44b8e8c734cd6741f1bb9dac50')}
COLD_OBJECTS = {'_child_code@vmu_native_heap_budget.c': (100, '8b9249ed63eb68bec146752d114c643074f861aced0b0f03b07cb20cd90faa01'),
 '_heap_bottom_allocate_words@vmu_native_heap.c': (206,
                                                   '528ea52471bc42b01f1f4c4a40e7510f5346db70a539a35a40b8c97fbe8ba174'),
 '_heap_bottom_release_words@vmu_native_heap.c': (154,
                                                  '441e8180758616454557f02badc0b5b6a2b23b752ab88f5f6fb8e442ce84cb9e'),
 '_parent_code@vmu_native_heap_budget.c': (152,
                                           '07ce6930bce857d18e95a47261e637ebf8fbfc2f8ffb9624fd6baa36270eb1e1')}
WORKER_FUNCTIONS = {'_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_identity@vmu_native_heap.c': (412, 'c3692d923384e5784ee390f6d6524ab8e9d63437ed7b74384659f44d01c04c31'),
 '_index@vmu_native_heap.c': (100, '2c4b3152af3c223d58c5472b6407afb7df77699a4bf92a53a5db6cb8fe27eb3d'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               '3aa70e70b57e532223605a2d87cf51c28e1da54b11bc3fe8b4d2b7f2596a799f'),
 '_kui_vmu_native_heap_lease_valid': (52, 'a93edfb8764431a826e5a854154889c3804d20c2c36636341dac18d30763918c'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   'f9dd525a2348893974ba7105bd071e2519bae683780f0db44a147256c0ecefc6'),
 '_metadata@vmu_native_heap.c': (416, '51a40db96aa89a5ad88e9c7c369c775ca48b5788acbe604d2b43f9af4af13324'),
 '_read_is@vmu_native_heap.c': (80, '11553b9960c7f716fcd49ddff4e3618024c3d97466a7ef6d137c3357857a2908'),
 '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305')}

# Complete SH32 inventory. Exactly three buffers shrink: two command-list
# copies become 4096 bytes each; one 524-byte staged response replaces 31.
# Overlay metadata before staged[] remains 928 bytes, hence 928+524=1452.
MUTABLE_OBJECTS = {
    '_config@vmu_native_passive.c': (8, 2304),
    '_reader_stream@vmu_native_passive.c': (8, 44),
    '_issued_chain@vmu_native_passive.c': (8, 4096),
    '_protected_count@vmu_native_passive.c': (8, 4),
    '_protected_ranges@vmu_native_passive.c': (8, 512),
    '_hook_witness_count@vmu_native_passive.c': (8, 4),
    '_hook_witness@vmu_native_passive.c': (8, 384),
    '_state@vmu_native_passive.c': (8, 100),
    '_durable_card@vmu_native_passive.c': (8, 4160),
    '_overlay@vmu_native_passive.c': (8, 1452),
    '_completion_frame@vmu_native_passive.c': (8, 76),
    '_live_plan@vmu_native_passive.c': (8, 456),
    '_captured@vmu_native_passive.c': (8, 4096),
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


def marked(image, role):
    require(role in GENERAL_MARKERS, 'Unknown compact Background image role: '+role)
    marker=MARK+role
    value=image[4].get(marker)
    require(value is None or value==ONE, 'Malformed compact Background marker: '+marker)
    return value is not None


def _profile(image, role):
    symbols=image[4]
    general=GENERAL_MARKERS[role]
    if general not in symbols:
        require(not marked(image,role) and
                symbols.get('__kui_native_isolation_lease_bytes')!=(LEASE_BYTES,0,0,0xfff1) and
                MARK+'worker_layout' not in symbols,
                'Compact Background image lacks its General prerequisite')
        return False
    require(symbols[general]==ONE, 'Malformed compact Background General prerequisite: '+general)
    if not marked(image,role):
        require(symbols.get('__kui_native_isolation_lease_bytes')!=(LEASE_BYTES,0,0,0xfff1) and
                MARK+'worker_layout' not in symbols,
                'Compact Background geometry lost its explicit image marker')
        return False
    require(not any(name.startswith('__kui_native_vmu_only_') or
                    name.startswith('__kui_native_vmu_lite_') for name in symbols),
            'Compact Background profile cannot also select the standalone VMU service')
    lease=symbols.get('__kui_native_isolation_lease_bytes')
    require(lease==(LEASE_BYTES,0,0,0xfff1),
            'Compact Background lease differs from the exact 284 KiB contract')
    if role=='worker':
        require(symbols.get(MARK+'worker_layout')==ONE and
                symbols.get('__kui_native_worker_span')==(WORKER_BYTES,0,0,0xfff1),
                'Compact Background worker lacks its exact 72 KiB linker geometry')
    return True


def stage_profile(stage):
    return _profile(stage,'stage')


def worker_profile(worker):
    return _profile(worker,'worker')


def low_profile(image):
    return _profile(image,'resident')


def auxiliary_profile(image):
    return _profile(image,'auxiliary')


def stage_pins(stage, role):
    require(stage_profile(stage), 'Compact stage pins require the exact compiled profile')
    require(role in STAGE_PINS and STAGE_PINS[role], 'Unreviewed compact Background stage role: '+role)
    return STAGE_PINS[role]


def worker_pins(worker, role):
    require(worker_profile(worker), 'Compact worker pins require the exact compiled profile')
    require(role in WORKER_PINS and WORKER_PINS[role], 'Unreviewed compact Background worker role: '+role)
    return WORKER_PINS[role]


def low_pins(image, role):
    symbols=image[4]
    worker=MARK+'worker' in symbols or MARK+'worker_layout' in symbols
    resident=MARK+'resident' in symbols
    require(worker!=resident,
            'Compact low pins require one exact resident or paired worker role')
    require(worker_profile(image) if worker else low_profile(image),
            'Compact low pins require an exact resident or paired worker profile')
    require(role in LOW_PINS and LOW_PINS[role], 'Unreviewed compact Background low role: '+role)
    return LOW_PINS[role]
