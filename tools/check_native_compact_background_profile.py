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
STAGE_PINS = {
    'cache_gateway': {'_native_resolution_replay': (284,
        '8703985e85a93d33ea7cee2969d5804a0e9ae177de529a604a5fb435ea120747')},
    'dual': {
        '_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
        '_native_dual_reader_bind': (68, 'c7ad986a1e98325c56a9163ba88a78f84b71f991af52706bc60a45c1f194c785'),
        '_select_resident': (272, 'ab0f1e9960a8440095307e2fe40521dbf968982310c4b70749ead8e90eeab910'),
    },
    'installer': frozenset({(2068, '3b55ef273b90f1b87c8ac9146de51eb91587b4dbf5530ccbe2ed0ca14f8034b7')}),
    'read_ahead': {
        '_kui_vmu_native_background_map_address': (92, '14f07de519d8e507e8d2a6d69cdce033ce605aae4773114e5cbfb3fdad56163c'),
        '_native_readahead_bind': (1244, '081ab00589b18bce21819de41c196ac3df13ad9bc0fde8dc906ebb83d98953ea'),
    },
    'sdk': {
        '_fingerprint': (88, '377565ba88bf7ac2a1c594f11fbff2bcb45a291171eb720e5984ef97301a865b'),
        '_kui_native_sdk_sci_route_admit': (796, '79328f85df7f5c10893094a3c9fadb892212f65e05977e1edb54ed593ba6eecd'),
        '_kui_native_sdk_sci_route_discover': (2848, '2fb2a0d7247f07eb05faf4e2006b9db8784ab0b44dc2dfbcfb4a0dc065ce66b7'),
        '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
        '_live_words': (80, '85c7deebc366fc1b11e66ed76bc44fc1c7a06db33b2fe1b79d3b6d4f7e35d23d'),
        '_native_sdk_sci_route_bind': (952, '1f25bc80036fd66023203ae91a6f80a42dc434a5d8be297c8cd348bc1e91e2bf'),
        '_normalized': (192, '2f978711b8648e941b19a2570ce66d655b450899fde95a46826d2620cf916d1e'),
        '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    },
    'timeline_binder': ('69d76e28a24ed21b8e8a356683086eab775eca277983d8eac8133d3c426a91cc', 180),
    'toggle': {
        '_native_passive_run': (2068, '3b55ef273b90f1b87c8ac9146de51eb91587b4dbf5530ccbe2ed0ca14f8034b7'),
        '_native_persist_storage_init': (720, 'dc52ba326b1bc72fac46f86f4d1a55fef35b2b95e8fa0e2f5abbd55845f0529e'),
        '_native_runtime_diagnostics': (44, '14aead6043dc0dc27e3914f075e99cbde8712c4c51f61294ab06739891abcdfc'),
        '_native_runtime_mode': (88, '6061fd7767bed09fa1e52eb0eaa64e3e15f91e83159319e99927b146300f0a26'),
    },
}
WORKER_PINS = {'dual': {'_reader_capture_region': (100, '57b529433f0d35317577f8eea3a54b1c5de68356fc1c1458fc314ffb2b5423b8'),
          '_reader_cooperative_service': (248, '9c09196f68759a3833cc7f3f67ed08c4a7d8f80d3fafe64a19bdc4c1eeb6e621'),
          '_reader_streaming': (120, 'd59433123f190c0178ec08a122a79b17a59da1945b23d92f6947da3598e9eb1d')},
 'live_boundary': (216, 'b87fb280436084492059b07539cb5bec06fc5589f80957533b77f90ddcec7c64'),
 'prepared': {'.rodata': ('4d4183a2bd743782c17d31f8d1b94dbbddc85643d39ac8ecab226ad8ac6abc73', 1176),
              '_chain': ('5a0130b8dc81e6e0656676767bc040956ecd28dbac1a0deca571d158c57eed7c', 908),
              '_controller_context_valid': ('43d5b6f1aee195f92cd33672814eff6b8fff1bca07c86b921846b90c29204c2b',
                                            1336),
              '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
              '_hook_word': ('69410002bf135878c32df63d2459603428947eadc760aa6309775f82b8801f9c', 132),
              '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
              '_kui_native_loader_timeline_visit': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83',
                                                    4),
              '_kui_vmu_native_lifetime_prepare': ('4691642d40ae532e2f81cfe46c3f81a019412c0da0e43fb4f7913d4a44b87312',
                                                   640),
              '_kui_vmu_native_passive_arm': ('cfc2275bf5ad111a102f58f63250e1ae37895d556b91ea035ab3ff4fb9bf3885',
                                              2576),
              '_kui_vmu_native_passive_complete': ('19d1f32ed5f93ba620b875d2dd4b41023a9d091eb419d6f666c653e633bd7955',
                                                   3828),
              '_kui_vmu_native_passive_split_submit': ('55fe13bdcab382490f2c96bd76a7ba7eefee1ea118c859eca2db677597615626',
                                                       860),
              '_kui_vmu_native_passive_submit': ('08332141fa19ba049c32ae75f83b7b6936d303ee25357d4ffd6d623dd90b17d9',
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
              '_prepare_hook_entry': ('e46707ed62da6217391c4aee61d074980abd67826655c1b508159548cabefd74', 228),
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
              '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
              '_transfer_context_valid': ('2e38d21c53efbfdb3cfb9c8c8913ebda457be30e92e2f0612e7cedd8a8fd708d',
                                          1180)},
 'read_ahead': {'_reader_capture_region': (100,
                                           '57b529433f0d35317577f8eea3a54b1c5de68356fc1c1458fc314ffb2b5423b8'),
                '_reader_cooperative_service': (248,
                                                '9c09196f68759a3833cc7f3f67ed08c4a7d8f80d3fafe64a19bdc4c1eeb6e621'),
                '_reader_streaming': (120, 'd59433123f190c0178ec08a122a79b17a59da1945b23d92f6947da3598e9eb1d')},
 'toggle': {'_kui_vmu_native_passive_arm': (2576,
                                            '12909333efe7787deecaafb609c7e1285ceefbe070470a4ad7fcab81314d73d9'),
            '_kui_vmu_native_passive_complete': (3828,
                                                 '182a163d13002d032eac285f05264d0d5b68a1aa64e4181424c537185968b21c'),
            '_kui_vmu_native_passive_submit': (1272,
                                               '85cf09287a086ed2c626c0e50559eb4675ff3edd822a736d594665ea7531a68c'),
            '_kui_vmu_native_persistent_sd_transaction': (100,
                                                          'a1e67312405058a0bef8d1f6719ef1f3b2e0d4450b61f0773aba35d2d470b0bb'),
            '_persistent_admit': (28, '872d4da692837bd2cdf15ce40f1396ab6fe4d1c9f33e300f544849b053e73a79'),
            '_persistent_handler': (28, '1968769240bc1a34fc046039138f65726e547a098fd8a951013a29b9b953d65c'),
            '_persistent_lease': (108, '06f5362545ad82771c13192c5eb25fe19485cfaf1b82f83c8a1a088a85897bdf')},
 'witness': {'_controller_context_valid': (1336,
                                           '16d1e24cb171889fbc04e32f28d21642abade04638b4f0f9255366e324582419'),
             '_failure_witness.isra.0': (16, '6f67db28b31acce565808e144b363d8f82d41193d20f9d7c90b96ca6622feee3'),
             '_installed_hooks_valid': (128, 'd854e8efbe51e1c7cf46a016d1350509a4c0689102a9a24d59eca648e50e1a5b'),
             '_kui_vmu_native_exit_shape_valid.constprop.0': (128,
                                                              'f79b69e932183d9fa5516d2e7faf7d3f2b08ad6d7b703aaa6ba75afe7b28bc5d'),
             '_kui_vmu_native_passive_complete': (3828,
                                                  '182a163d13002d032eac285f05264d0d5b68a1aa64e4181424c537185968b21c'),
             '_kui_vmu_native_passive_submit': (1272,
                                                '85cf09287a086ed2c626c0e50559eb4675ff3edd822a736d594665ea7531a68c'),
             '_kui_vmu_native_patch_encode': (168,
                                              '5f368af2a0dfcc6be1ccb9cb1ebd1c8260885f270bffcce94db6f6ba60fbd493'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128,
                                                                    '2ccc39bf129b078f18cd80349f4c28dbcfcbcc4cbcc54773704596b5b366d2d0'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (108,
                                                                      '74b859521a7b2ef7ed7695dea57a8f11a0a6d5beced1a0ed024ba5e679720d74'),
             '_kui_vmu_native_timer_callback_owned': (92,
                                                      'd0ea59080d40e2d4d86fed614ac09a78ef4f7ab9659a1a6670621e59802debfb'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_normalize_heap_dispatch': (276, 'bf528136b2ab69e9f091d51bf7e8af24b7c74c9e70fef8434b20df531641c394'),
             '_normalize_heap_dispatch_if_routing': (40,
                                                     'c3c6bc6b555cff0845af6b426af73c3d42c4a7aed89fe07262df8097f48e2b49'),
             '_poll_lease_valid': (168, '8c34643b0730668b5a9f39767f11f0e618d009c8a8616d4ea5834b1a70e432d9'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_reader_capture_region': (100, '57b529433f0d35317577f8eea3a54b1c5de68356fc1c1458fc314ffb2b5423b8'),
             '_reader_cooperative_service': (248,
                                             '9c09196f68759a3833cc7f3f67ed08c4a7d8f80d3fafe64a19bdc4c1eeb6e621'),
             '_reader_streaming': (120, 'd59433123f190c0178ec08a122a79b17a59da1945b23d92f6947da3598e9eb1d'),
             '_readonly_rx.constprop.0': (76, '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, '6e94ef50be39f3e2df483a4acd181c27a3b9ecbc158ef36a062b88ae4259bcfc'),
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
 '_allocation_safe@vmu_native_heap_probe_stage.c': (52,
                                                    '9cdd14b561ea99e1a88e2c267472b42b0e5fa97373e127066cf9d3d61a862fa7'),
 '_at@vmu_native_heap_budget.c': (92, 'e25b91e557630914f31039a157f937c46ce0d1b911673aa527237d1567ef492e'),
 '_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_code_sha@vmu_native_heap_probe_stage.c': (64,
                                             '90c9ec042115f9f42baf91dbf2ad8236f68098faeee89c82d93953f195145ea5'),
 '_control@vmu_native_heap_probe_stage.c': (28,
                                            '28b80e1d9c7a8b89650d5ce726b338e13f6160a8183af096c0f215337eb112a3'),
 '_excludes_stage@vmu_native_stage_placement.c': (64,
                                                  '9493fa61c24a83d39e87be566a08304330aa34f2a44d992eb89970a7bdea2de7'),
 '_fresh@vmu_native_heap.c': (116, '1e1a2dc88d516475a96e941e69574d45268bea8729cc6dde2f12d16471fb6fe3'),
 '_heap_bottom_code_span@vmu_native_heap.c': (88,
                                              'e4fd231a6d2f0504cf1a5fe6d3441d5a3efa5486f15e1f8d3541986fab6b1efa'),
 '_heap_bottom_halfword@vmu_native_heap.c': (58,
                                             'd66affe12d8e87985e0bbe094cc393649bad89f9e4494f55004afda5f193ab39'),
 '_identity@vmu_native_heap.c': (412, '7e10221c6d1655cba0b3c007f71bfc9029221f0ebd9e24f6e7169590d1976e98'),
 '_index@vmu_native_heap.c': (100, '53ead62528a8d1ed9f6e27affb8b4b958da3a47b4a9299ee33861a9f64c3d28a'),
 '_kui_vmu_native_heap_bottom_allocated': (172,
                                           '9bee42b62685ec3554be1193a9257708cf4936307e003ad47092553aa1cdaf84'),
 '_kui_vmu_native_heap_bottom_code_valid': (360,
                                            '956630ec8feefe1fa6b074afcafcd5205af5945c001206780210e21c41a27656'),
 '_kui_vmu_native_heap_bottom_fresh': (148, '70abf583e1d19bbe39d0c3e6354428b779542a9bd96931a4df2ffda17302e76f'),
 '_kui_vmu_native_heap_bottom_placeholder': (196,
                                             '5d3ec5ac92c9dba29b5a5f381006f239c1d564cf51746bc14e31096116fcdf56'),
 '_kui_vmu_native_heap_bottom_ready': (144, '067e22ac07c76ec0b7bd280ca6baab07b866ebe60a4d3bebe3a2daf0e8eb4d8d'),
 '_kui_vmu_native_heap_budget_prepare': (2370, '8a30b612315b7be5232e36ebdb4cbc8bea52f285d26633bea2a987e3b01aeb4b'),
 '_kui_vmu_native_heap_fresh': (268, 'ca6245d66e98b6a34eaa2a2361233c63b9e16b6ee5b09cb37de8c5753ae3f293'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               '9a951662fb693c58c84b57b7eb95f0277f6b2b23ce47309efcd1cee5fa50d730'),
 '_kui_vmu_native_heap_lease_valid': (28, '44371dce575ec7189d36e6ceb7c3f20689cf395b5179f77d4e67576f0effe735'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   'a554e97257bd053865bdbadb2aa8da310e12ddaf04ec11936fc757e9e52d4936'),
 '_kui_vmu_native_heap_preallocate': (36, '0acc99c998238bcac81d18127c0f98f306458c53acf02247e450943f95472708'),
 '_kui_vmu_native_stage_bottom_allocation_safe': (308,
                                                  '6ef0ce1782dcfa4e8e9ffe20e278b712f2a776cb18946208eb501c97832cd782'),
 '_kui_vmu_observe_stage_install': (416, 'af5b07e089a2b8e798ae5759046842cbaf533a83e04ac9843051b98e072060c5'),
 '_le32@vmu_native_heap_budget.c': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
 '_long_literal@vmu_native_heap_budget.c': (128,
                                            'f9191f11e0709a3e1aec4ae382b3810d30129f0c71804d5d9452088b2d18fbc5'),
 '_memcmp': (34, '3d7da64606fe412df2341ca8e1c2c56d14d3f4798465f1ec02f6d2994c6aefd9'),
 '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
 '_memset': (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a'),
 '_metadata@vmu_native_heap.c': (416, 'd1953828d01024fc65402fe5c67f11598f216693c454c93937b4acbfdc347d6a'),
 '_native_allocate_bottom@vmu_native_heap_probe_stage.c': (304,
                                                           '78496021ca91bc603124a55ecf91d37606a2f2567e10ed4c997bfec35a4d28b3'),
 '_native_allocator_unchanged@vmu_native_heap_probe_stage.c': (68,
                                                               '3c4223a2fa16bd9a4a16fb3084b736bf014382e2fae4955baa9c6a142632c6ba'),
 '_native_budget_prepare@vmu_native_heap_probe_stage.c': (92,
                                                          'b3cfc6b404435079cc7bc04f760b41df3813b01f083e95f16b99c7fa02f7d03e'),
 '_native_budget_publish@vmu_native_heap_probe_stage.c': (132,
                                                          'a9e1a499d197f1e09b0053aa31801744db7f00bea59aed6d600e381bab778798'),
 '_native_low_bindings@vmu_native_heap_probe_stage.c': (64,
                                                        '2d2bb4d75779cd4304e7a3f7224c3f703cd9a02a5f5d6f9cc7048985d490c679'),
 '_native_read@vmu_native_heap_probe_stage.c': (84,
                                                'b6bc33c6d4c36f9e6625ccf6afdc55b98e2e8f684d95194687fa946071a4804d'),
 '_overlaps.constprop.0@vmu_native_heap_budget.c': (124,
                                                    'f7d6c66f828af4369cdad5033761a578373155b9fc112aaa1225fc80d4873359'),
 '_ram@vmu_native_heap_budget.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_read_is@vmu_native_heap.c': (80, '0ebc9c562b0038f238b24bdb907915a8f3aba045f7cd4b2e268d34f651a23da8'),
 '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_span@vmu_native_stage_placement.c': (80, '18c883d6beb479d8b6967a1dda59fcbb6919bdc0015f79e7a55aaefecd8f492d'),
 '_word@vmu_native_heap_budget.c': (52, 'aa48f92d2d40d911e172fd18c1e14038c26b3c44b8e8c734cd6741f1bb9dac50')}
COLD_OBJECTS = {'_child_code@vmu_native_heap_budget.c': (100, '8b9249ed63eb68bec146752d114c643074f861aced0b0f03b07cb20cd90faa01'),
 '_heap_bottom_allocate_words@vmu_native_heap.c': (206,
                                                   '528ea52471bc42b01f1f4c4a40e7510f5346db70a539a35a40b8c97fbe8ba174'),
 '_heap_bottom_release_words@vmu_native_heap.c': (154,
                                                  '441e8180758616454557f02badc0b5b6a2b23b752ab88f5f6fb8e442ce84cb9e'),
 '_parent_code@vmu_native_heap_budget.c': (152,
                                           '07ce6930bce857d18e95a47261e637ebf8fbfc2f8ffb9624fd6baa36270eb1e1')}
WORKER_FUNCTIONS = {'_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_identity@vmu_native_heap.c': (412, '429249a54daf0e4254b165e5606256749144292c317652c841e0e16121ef7155'),
 '_index@vmu_native_heap.c': (100, '3d07d0df24cd95557c06caf44a47d8bfc37d8984c2f43516146b819d54b35797'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               '24a5163a2be5b05587d021311b6aab9be8b977f10e1a6199f97cc8788c281db9'),
 '_kui_vmu_native_heap_lease_valid': (28, 'c3ad4b0919d67cf5572dab60817a0e6d62d7ea70be2c7152047da9f4611f36b5'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   '35c5e1f489c033d33622fdf3c1c075b82163dd34465335702112c339fd919e83'),
 '_metadata@vmu_native_heap.c': (416, '3b1d2e561201d859aef5c7d5a66acd0110c3cfc4c2a76488cedce16a80ed3a08'),
 '_read_is@vmu_native_heap.c': (80, '2cd78026a669ee519ecd2a553c1f2e460c54f9c2fcc4319606dd82d3ec080c62'),
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
