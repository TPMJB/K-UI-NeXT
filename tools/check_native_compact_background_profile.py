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
STAGE_PINS = {
    'cache_gateway': {'_native_resolution_replay': (284,
        '7d718a7053c1044abf36ef202e3f0075529fa3b36ca73c5278771197eefd63ba')},
    'dual': {
        '_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
        '_native_dual_reader_bind': (68, '7950a644b11a9a55b5a06ad5b04c3196f92ba31ca874926fde75e4a4ac9e7a9e'),
        '_select_resident': (272, '87f2eb2481cbfe888c5dbb2d824f8f9cd2e5509fcbfaf5b50b9b1696f4b2251a'),
    },
    'installer': frozenset({(2068, 'fe382c8dafe2b84164b6c07e267706f6c890197370169c13d64c0f8c66acdf33')}),
    'read_ahead': {
        '_kui_vmu_native_background_map_address': (92, '14f07de519d8e507e8d2a6d69cdce033ce605aae4773114e5cbfb3fdad56163c'),
        '_native_readahead_bind': (1244, '83908f1db8d1ad2e8088d7376b05dad4bae60adad2c5b441c2df15622540b0bc'),
    },
    'sdk': {
        '_fingerprint': (88, '3879ed3e05b61f88136b7ed3b485c92c495cdf8d05bd93fea478f2b387925529'),
        '_kui_native_sdk_sci_route_admit': (796, '27b7faeb0df257dde6dfc0906098fb848514646d2ffdfe305bed8adedd1b2b35'),
        '_kui_native_sdk_sci_route_discover': (2848, 'dfac05add6ce90dbb5a197a60b5896e618d0fc1cb8fbcddb8e530ba54c352417'),
        '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
        '_live_words': (80, '86d0609aafb0d937f1079927d47cc17582506b2be16f36167bf6b623d784a643'),
        '_native_sdk_sci_route_bind': (952, 'e06b54714e28b55ff204c45d0e8c10295bc2a0a209f4feeb49d207977b9915be'),
        '_normalized': (192, '7ad4c030f74d86685dac95666c364d2a09657b5f8bfd4ec836f4c840c5812c24'),
        '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    },
    'timeline_binder': ('69d76e28a24ed21b8e8a356683086eab775eca277983d8eac8133d3c426a91cc', 180),
    'toggle': {
        '_native_passive_run': (2068, 'fe382c8dafe2b84164b6c07e267706f6c890197370169c13d64c0f8c66acdf33'),
        '_native_persist_storage_init': (720, '038fc11a3dd4846e0a44396ccf93528ba7793b8cc43a144979d914c30412b697'),
        '_native_runtime_diagnostics': (44, 'add6c791caad87eb01520cf55770b845ee9a12dbf9d9ab7ecbc93b6d1677d4b4'),
        '_native_runtime_mode': (88, '45e90a3448b94e61d0d07edc67e152fcfbdf8b02c7ccf32147552b934d93f745'),
    },
}
WORKER_PINS = {'dual': {'_reader_capture_region': (100, '5d8a0818db57289bfb5f2a37ca17f13738c8ac9fe19aafb6ca620cfe22a5c1aa'),
          '_reader_cooperative_service': (248, '5e7b1a7600f51ff9437759d9dfed94b109b324991c6a4d76417d60b2d3513185'),
          '_reader_streaming': (120, 'f92f15a0ef26429436026ebf71e3dd5454a70397a25e79fb0e11efa959f23e6e')},
 'live_boundary': (216, 'b0669a761198274a39e512a4e083e0f322919c1060c84c68ee98443e34c4c8b7'),
 'prepared': {'.rodata': ('4d4183a2bd743782c17d31f8d1b94dbbddc85643d39ac8ecab226ad8ac6abc73', 1176),
              '_chain': ('15508e497f63dddd608b3bd2b74d0aa67b2114c481b1470025d97d2c7d237fff', 908),
              '_controller_context_valid': ('43d5b6f1aee195f92cd33672814eff6b8fff1bca07c86b921846b90c29204c2b',
                                            1336),
              '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
              '_hook_word': ('69410002bf135878c32df63d2459603428947eadc760aa6309775f82b8801f9c', 132),
              '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
              '_kui_native_loader_timeline_visit': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83',
                                                    4),
              '_kui_vmu_native_lifetime_prepare': ('4691642d40ae532e2f81cfe46c3f81a019412c0da0e43fb4f7913d4a44b87312',
                                                   640),
              '_kui_vmu_native_passive_arm': ('589b2ede5629daa5d2ecbfed97b21cbfe557aaed1c2b5530a5b326273e19282c',
                                              2584),
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
              '_transfer_context_valid': ('f4d6f6fabdf3e2bfa6c1a6e2b1a5904b52adfbc4874c07b12dc87b6320c764fa',
                                          1208)},
 'read_ahead': {'_reader_capture_region': (100,
                                           '5d8a0818db57289bfb5f2a37ca17f13738c8ac9fe19aafb6ca620cfe22a5c1aa'),
                '_reader_cooperative_service': (248,
                                                '5e7b1a7600f51ff9437759d9dfed94b109b324991c6a4d76417d60b2d3513185'),
                '_reader_streaming': (120, 'f92f15a0ef26429436026ebf71e3dd5454a70397a25e79fb0e11efa959f23e6e')},
 'toggle': {'_kui_vmu_native_passive_arm': (2584,
                                            '447c2d7b473d7240fe5f2ddcd93b09efb89a19bd521c9c8efb222da89d034eb4'),
            '_kui_vmu_native_passive_complete': (3828,
                                                 'a957b427b811a6ebc753fc2bd7ef70f3ac92c48d6d26c0b11b23541b66d2073b'),
            '_kui_vmu_native_passive_submit': (1272,
                                               '94667e9b2cc672ce831651619902f99edca18a50671c21cac8abd3525174abdc'),
            '_kui_vmu_native_persistent_sd_transaction': (100,
                                                          '9ffe17f70ad8866235a264e94e90e457b6490f6cf032b4b9d06c18bf813c1b59'),
            '_persistent_admit': (28, 'a3c10c7e4b1fb9555ab36c3f9c8976fec4fd26de5a5a808b39481031a807ebf0'),
            '_persistent_handler': (28, '4046bdfb3178a9691ba641242851a876fd1a5fce2622d7ada3c5b69355c848db'),
            '_persistent_lease': (108, 'cc48343c6d1d144bd5acad65378a8c1ad257624fc47fb34349796dd2bbda77fc')},
 'witness': {'_controller_context_valid': (1336,
                                           'e5414295c9e86bb734d5cc0837f7d17333311a00908f4ace453b87021dda8160'),
             '_failure_witness.isra.0': (16, 'efd2ee1b5ed453660985ddc3fa2da27e2635bb4a15b7dc4d9a36a0f3ee3cc593'),
             '_installed_hooks_valid': (128, '3737ad587908a3fd28a2d0c88c2347bc44bf6467a44f742380f7fba220a3d7d1'),
             '_kui_vmu_native_exit_shape_valid.constprop.0': (128,
                                                              '39c8a1aeff1e8063f5507d3b0bd3fdf608b77ed8742fcf1c73e32b096f4399b2'),
             '_kui_vmu_native_passive_complete': (3828,
                                                  'a957b427b811a6ebc753fc2bd7ef70f3ac92c48d6d26c0b11b23541b66d2073b'),
             '_kui_vmu_native_passive_submit': (1272,
                                                '94667e9b2cc672ce831651619902f99edca18a50671c21cac8abd3525174abdc'),
             '_kui_vmu_native_patch_encode': (168,
                                              'f83364b642ab1100fae54dc141bcadc5dcce726476b7e1af68faae171ed70480'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128,
                                                                    '12cca05680280d4c0b1b3e54e0dea86c271fbac67434c6bbfe4aa6cbdc995e2e'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (108,
                                                                      '8ba7a4c00eb259d08e7e9d768464930a26d4d51a76f86e2d1b6286b83b4e8170'),
             '_kui_vmu_native_timer_callback_owned': (92,
                                                      '258845de87754d9836d928060bd4211c8619420ba5239dbe7810ca61cedf0963'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_normalize_heap_dispatch': (276, 'c2657356706e8dc9860080a8267c132a756e11b6d26099e8f383fc5a032c4651'),
             '_normalize_heap_dispatch_if_routing': (40,
                                                     'd80b5ef7c7bd853bd1ac3636947d20a6696bef59463f634f7e034a0166fa92b0'),
             '_poll_lease_valid': (168, '6724592a8ac154964a59dc2fc26a667e9af2f43702b74b2ee39f30ecc2694243'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_reader_capture_region': (100, '5d8a0818db57289bfb5f2a37ca17f13738c8ac9fe19aafb6ca620cfe22a5c1aa'),
             '_reader_cooperative_service': (248,
                                             '5e7b1a7600f51ff9437759d9dfed94b109b324991c6a4d76417d60b2d3513185'),
             '_reader_streaming': (120, 'f92f15a0ef26429436026ebf71e3dd5454a70397a25e79fb0e11efa959f23e6e'),
             '_readonly_rx.constprop.0': (76, '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, 'adf57477d04724fbf70c3d2d8ae9e65b72c22d331632f70d9b4efc17b1f5da81'),
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
                                                    'b1c5832b869133347639d8e158d5e585fe155462a4cf4a02c423016f5a3ad683'),
 '_at@vmu_native_heap_budget.c': (92, 'e25b91e557630914f31039a157f937c46ce0d1b911673aa527237d1567ef492e'),
 '_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_code_sha@vmu_native_heap_probe_stage.c': (64,
                                             '4aea7a8c618f671e559b07582c250d6b58c357f51d259731f8e4d56c15bcfff1'),
 '_control@vmu_native_heap_probe_stage.c': (28,
                                            '28b80e1d9c7a8b89650d5ce726b338e13f6160a8183af096c0f215337eb112a3'),
 '_excludes_stage@vmu_native_stage_placement.c': (64,
                                                  '2ce80eba35db4804644eb88bd2248b711338b0e7c8c6e4de398ac2cd39d6bbf7'),
 '_fresh@vmu_native_heap.c': (116, '33a9a4e1b8f22d3d86e9de5471d18300c301205d79757ed23af3294f0ec85a30'),
 '_heap_bottom_code_span@vmu_native_heap.c': (88,
                                              '4263b8ad8345cf884ab5aa237b744f01c569f3502fd1aaeb892748730bc1791f'),
 '_heap_bottom_halfword@vmu_native_heap.c': (58,
                                             'd66affe12d8e87985e0bbe094cc393649bad89f9e4494f55004afda5f193ab39'),
 '_identity@vmu_native_heap.c': (412, '073fb27fedf6a423bd1541f105eda444e8c5ba913bd8cba5d9dfc72f607fa223'),
 '_index@vmu_native_heap.c': (100, '88c010b1e936b4985c82c8588b7a2c000b3a1f122541c1952cee28a548132cdd'),
 '_kui_vmu_native_heap_bottom_allocated': (172,
                                           'ac8eae644962aa4091fea700859efffadd710e453b4fc5ef1c57eafc29b7a57c'),
 '_kui_vmu_native_heap_bottom_code_valid': (360,
                                            'be027f23cc7449b139cb81f5e431c129d4d18b2e469d53e0f10beb79b45bfab1'),
 '_kui_vmu_native_heap_bottom_fresh': (148, '2d843a456a88162a10bde70abe580f6c0e23a104fe6e4ac5b0e0397de59662f3'),
 '_kui_vmu_native_heap_bottom_placeholder': (196,
                                             '2f5fce8275658ebb702ac35b87952aa814b2430a32750de118f63f5e77363a17'),
 '_kui_vmu_native_heap_bottom_ready': (144, '2eb9e66c7645088f5d747bbbd8bb8efe15dc85395b825dcea893a4c6e6fc3528'),
 '_kui_vmu_native_heap_budget_prepare': (2370, '1dbce8dc78df167f5b4c9c967d623a85ef700bdb1b7a3fec9871fd4e6f354d61'),
 '_kui_vmu_native_heap_fresh': (268, '8d12a5196cea1d863fb3829b32fe7b8b4c24bcd49c7063a4cde11791ba2aac16'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               '66786d63e291a3db9a57c890f351ef5eaff04598e89369bdecd64bfae2c19306'),
 '_kui_vmu_native_heap_lease_valid': (28, 'd86493a0a6c96a20361d02f174dc18d865d7a9900a5c84eb4292061ddbcb27ab'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   'a394349928c07d9491d4f7d206ee19ad65541008107dda1f572138fefeb9555c'),
 '_kui_vmu_native_heap_preallocate': (36, 'ee3a93e3f5f420fa5edc737c32ed9f034faba88a561051768a82fb1f563d8499'),
 '_kui_vmu_native_stage_bottom_allocation_safe': (308,
                                                  '358b17848074e42ad2a46d16a655832cb1a58c5a55e7918b22523fc26037c51b'),
 '_kui_vmu_observe_stage_install': (416, '2ca65e019b0fbe8a8110aa85c54abb4a94ede04f7621fe3d98e03c63637cce46'),
 '_le32@vmu_native_heap_budget.c': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
 '_long_literal@vmu_native_heap_budget.c': (128,
                                            '9a8b3ef16337229f0110370e9de4cc092f25141ecb183780f5104e1645bf1ca2'),
 '_memcmp': (34, '3d7da64606fe412df2341ca8e1c2c56d14d3f4798465f1ec02f6d2994c6aefd9'),
 '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
 '_memset': (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a'),
 '_metadata@vmu_native_heap.c': (416, '129f78e8ff7ff5aa7e0480ac5472b59e10d6727b908d2fc2044df2f6b47ad0b0'),
 '_native_allocate_bottom@vmu_native_heap_probe_stage.c': (304,
                                                           '21a511786d3ccea01daca818284c2d6f7d4a2f25c01eac12228bd294881fcc32'),
 '_native_allocator_unchanged@vmu_native_heap_probe_stage.c': (68,
                                                               '2b8ca36c15a7a8f372f438c408ab2455027b2a2dbec7c85b2f1d2634bedfe836'),
 '_native_budget_prepare@vmu_native_heap_probe_stage.c': (92,
                                                          '6e2d200fd558b78ce3d62904ab66e81b94409883af807dfb7902ec00c8dcd591'),
 '_native_budget_publish@vmu_native_heap_probe_stage.c': (132,
                                                          'a71345114351040a5a5e70be9657c562078bc7a27c57280e255a8d58b0bf3ab2'),
 '_native_low_bindings@vmu_native_heap_probe_stage.c': (64,
                                                        '915812d16c3a903d9449a3141e27090a3573b3e3df8a06efde96355aaf4cc988'),
 '_native_read@vmu_native_heap_probe_stage.c': (84,
                                                'b6bc33c6d4c36f9e6625ccf6afdc55b98e2e8f684d95194687fa946071a4804d'),
 '_overlaps.constprop.0@vmu_native_heap_budget.c': (124,
                                                    '8a58b795108eca7aaaa30ed69f7e130ce23b2c09e2e8093c44e4bef182e091a3'),
 '_ram@vmu_native_heap_budget.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_read_is@vmu_native_heap.c': (80, '02a080a8425dfffcd0b2d69d263d094215b3398dbdbbebf9f09bde5d73419c8d'),
 '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_span@vmu_native_stage_placement.c': (80, '1e3811b14732ebfcdf943706f4c47dcb58538f683aa919b09bb2399d52df2a57'),
 '_word@vmu_native_heap_budget.c': (52, 'aa48f92d2d40d911e172fd18c1e14038c26b3c44b8e8c734cd6741f1bb9dac50')}
COLD_OBJECTS = {'_child_code@vmu_native_heap_budget.c': (100, '8b9249ed63eb68bec146752d114c643074f861aced0b0f03b07cb20cd90faa01'),
 '_heap_bottom_allocate_words@vmu_native_heap.c': (206,
                                                   '528ea52471bc42b01f1f4c4a40e7510f5346db70a539a35a40b8c97fbe8ba174'),
 '_heap_bottom_release_words@vmu_native_heap.c': (154,
                                                  '441e8180758616454557f02badc0b5b6a2b23b752ab88f5f6fb8e442ce84cb9e'),
 '_parent_code@vmu_native_heap_budget.c': (152,
                                           '07ce6930bce857d18e95a47261e637ebf8fbfc2f8ffb9624fd6baa36270eb1e1')}
WORKER_FUNCTIONS = {'_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_identity@vmu_native_heap.c': (412, '83f853396f65414dd7288638ca84d8137a4a105330404def30168bd93f638cb7'),
 '_index@vmu_native_heap.c': (100, '64e295e1019759427f2bdcfe0575df6b54030f9aa8e4593e9a8a1a61bfb8720a'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               'f6fd77635c7c67434e4160adb2acef30dfa9c3697be003cbc839ad78db05b3f2'),
 '_kui_vmu_native_heap_lease_valid': (28, '13c53cff86c2db8876780f803fd0c629a06384e2981644574ef6cc88977b0f9a'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   '6ef6c326bc5db7d426e286e89c7ad12e84b7c5768908a059aa5c867ca96dcf60'),
 '_metadata@vmu_native_heap.c': (416, '9c3d1c8425c5f4f99c0732d40dcaabba672730ce0d089db21c2f897f3f92e75b'),
 '_read_is@vmu_native_heap.c': (80, 'f266146a9e3e988e6d46c88f7ced1c3a82c87e5776d47984c2cc1dcece85152a'),
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
