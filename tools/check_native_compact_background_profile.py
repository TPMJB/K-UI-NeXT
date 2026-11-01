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
        '3f50ba8630ccdf77c74fa267ffac35ebe9fe9a82d1929b62720d865ee850118e')},
    'dual': {
        '_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
        '_native_dual_reader_bind': (68, 'c7c1a1009e24d56c597f69d6eb6fd926b2e9c97b24be877e6b6e1fafb1001d9b'),
        '_select_resident': (272, '00e89b2f638ac24cf2b60044e9f7d1459ecc5d7ec745f0676d636f301631d61f'),
    },
    'installer': frozenset({(2084, 'b7e8e8669300b8bfec185e1d41157916f6899e1c302d04d0a1cd28d26959784c')}),
    'read_ahead': {
        '_kui_vmu_native_background_map_address': (92, '14f07de519d8e507e8d2a6d69cdce033ce605aae4773114e5cbfb3fdad56163c'),
        '_native_readahead_bind': (1244, '353e4b55fb5964d3e93e4d7b8d864d5bc150e3bbbc391e474515577cbe130697'),
    },
    'sdk': {
        '_fingerprint': (88, '3cd7bc500949746306fb63e8035c90db55cdb65fa2dd43a526087469fd96678e'),
        '_kui_native_sdk_sci_route_admit': (796, '38e73f06330556bec917731df079c9fa85a10a13e2349b15b0aa0fae063265a5'),
        '_kui_native_sdk_sci_route_discover': (3264, '3b63d8ee990456b50d8f461ee34633402a248e125a4b80be92dbddc990e71b59'),
        '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
        '_live_words': (80, 'fe326f8b3b180f42d28abb774b6d2845e00128e1abf3f60c3a49261f8cdcf2f9'),
        '_native_sdk_sci_route_bind': (944, 'bbdfb56df8d277792b2ec360e934c3a9d08538805b7eae378ee17b5667283b12'),
        '_normalized': (196, 'c495e0ef7b3e5518c56a812c649c3f4c55f7c2ef9b3c074ed683747bd48cb6b8'),
        '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    },
    'timeline_binder': ('a042bafffa63f760a2a475fe5e3330bb77157f92d8b627b729b8af6c10bd426e', 180),
    'toggle': {
        '_native_passive_run': (2084, 'b7e8e8669300b8bfec185e1d41157916f6899e1c302d04d0a1cd28d26959784c'),
        '_native_persist_storage_init': (720, 'a120daef4f5385ad618b0ac69e3c50c9d7caf9e4eef520077ca170fcd2a71d72'),
        '_native_runtime_diagnostics': (44, 'c3c8a95202b2ad16ff80857fb5357ba6bffa30130fa781878041b25bf9113337'),
        '_native_runtime_mode': (88, '8399379213391385e975e91efcb62517cbc08dbdc08e4bc55246b5ee3c6bb368'),
    },
}
WORKER_PINS = {'dual': {'_reader_capture_region': (100, '7bd742d45268867fdf0dff04ea8b2345a921e1a6021389926b924b186ad2ef8b'),
          '_reader_cooperative_service': (248, 'b1dbcdf17f36fb86070ad3d90524cc4a33c265d5b28416e83d0107911a15b1ca'),
          '_reader_streaming': (120, 'c4c8370f581d79fe166b3ee018b6a8d9535cbbbbcfd5c46bb974e73be0cda78d')},
 'live_boundary': (216, '096b5af096de6e7b8339d797fb5e882c8ba85b6538733ffd04659d8d3741451e'),
 'prepared': {'.rodata': ('4d4183a2bd743782c17d31f8d1b94dbbddc85643d39ac8ecab226ad8ac6abc73', 1176),
              '_chain': ('fb3446a0ff123f4e19ea506887a1eea104464530775abbd0023f8b55243a7fc8', 908),
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
              '_kui_vmu_native_passive_complete': ('6a6b08a09e76e0cebeab98c0d53a6a205ee5ee042291a4d2479f9ca0dbf8e727',
                                                   4000),
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
                                           '7bd742d45268867fdf0dff04ea8b2345a921e1a6021389926b924b186ad2ef8b'),
                '_reader_cooperative_service': (248,
                                                'b1dbcdf17f36fb86070ad3d90524cc4a33c265d5b28416e83d0107911a15b1ca'),
                '_reader_streaming': (120, 'c4c8370f581d79fe166b3ee018b6a8d9535cbbbbcfd5c46bb974e73be0cda78d')},
 'toggle': {'_kui_vmu_native_passive_arm': (2620,
                                            'ab388c31126c7b2cf774bea3dbfacc1bc95b55f3617c5738b5e5cdd0f11ca599'),
            '_kui_vmu_native_passive_complete': (4000,
                                                 '4c634b95fb684abfd6d4a98ab5c6519529b56a2d27547e65e514d63e8789dc54'),
            '_kui_vmu_native_passive_submit': (1272,
                                               'b299e25f374d16da2c9ca6f11bfeb14ac6c5d31c5c1ae18b6b7ce78c7d567da2'),
            '_kui_vmu_native_persistent_sd_transaction': (100,
                                                          '47aba71c01758ba2ecafd7856c1f8952a2163650b33dbe592b75eddcbd83faad'),
            '_persistent_admit': (28, 'bb448ba7784b628e991d5c6de72cb8f54694cabc182b1fb82f4864dadb712a3f'),
            '_persistent_handler': (28, '411aec10d62b99632d4a4cae8438b32080ae6e5464624b39a37ff62ceb360db7'),
            '_persistent_lease': (108, 'f6ce5ec62c5a072d9fcfc2421b2a4b0cdaed34b98d1787e55e75404a733e864a')},
 'witness': {'_controller_context_valid': (1336,
                                           '13235154fb1e00a9986b7ae538cbf6d4aca639bdefb32cdf178be1294c116519'),
             '_failure_witness.isra.0': (16, '60bea49945b6cec40c1dc8f8c8db906331d4024b2add2f562d9b42bd613bfc79'),
             '_installed_hooks_valid': (128, '76597a6230d4c09fa9cdca9751393772c1c00aa11b8e0648950cfe7abd0530ce'),
             '_kui_vmu_native_exit_shape_valid.constprop.0': (156,
                                                              '883867117ae097de92d3d883a38bafe904ec48095392aa0e3a3bf60f33b6e8d0'),
             '_kui_vmu_native_passive_complete': (4000,
                                                  '4c634b95fb684abfd6d4a98ab5c6519529b56a2d27547e65e514d63e8789dc54'),
             '_kui_vmu_native_passive_submit': (1272,
                                                'b299e25f374d16da2c9ca6f11bfeb14ac6c5d31c5c1ae18b6b7ce78c7d567da2'),
             '_kui_vmu_native_patch_encode': (168,
                                              'bc1c8a51bce1e7c69d097dacf13a3c62174eb0fe1c51af040dabbe43c01f6e2a'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128,
                                                                    'caafa945127bb3c6a461799ed5707733e788df45f4f032d7b0e5dd798f4ff716'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (108,
                                                                      '2629f0fdcdb537c99da9aa6a775e14f44c52a42ed7cab0dac761511c56f530f0'),
             '_kui_vmu_native_timer_callback_owned': (92,
                                                      '934af92d66d0d0747e5768b8f4e8a44e5d91cd1d602a1c959b0ef574d53f4940'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_normalize_heap_dispatch': (276, '681f4880fa6486fb117b832e182cc3a1c40f09b94932bfd31641b9e5cdf4eff8'),
             '_normalize_heap_dispatch_if_routing': (40,
                                                     'e235f27cb2c3124cd309a15752236c5f3dbc422f29686c64dcf16ef28026e9de'),
             '_poll_lease_valid': (168, 'b4d869887ca5de3eee852fba4df2076c61bed14cf1ce80e1069f3ec21b0d7f95'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_reader_capture_region': (100, '7bd742d45268867fdf0dff04ea8b2345a921e1a6021389926b924b186ad2ef8b'),
             '_reader_cooperative_service': (248,
                                             'b1dbcdf17f36fb86070ad3d90524cc4a33c265d5b28416e83d0107911a15b1ca'),
             '_reader_streaming': (120, 'c4c8370f581d79fe166b3ee018b6a8d9535cbbbbcfd5c46bb974e73be0cda78d'),
             '_readonly_rx.constprop.0': (76, '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, 'a6fb8b998363524ce7e43f3fc187640b386cdd184925104b6fc0c6a27827cad7'),
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
                                                    '20a252cc309a9d04a773b0540b31dd8871601c4048e835af4d2deb3e58473bdf'),
 '_at@vmu_native_heap_budget.c': (92, 'e25b91e557630914f31039a157f937c46ce0d1b911673aa527237d1567ef492e'),
 '_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_code_sha@vmu_native_heap_probe_stage.c': (88,
                                             '73b0c7c2b3bfa2ab93dcd37642cee52c75b72d62065e0f7d767a2b43b34bc389'),
 '_control@vmu_native_heap_probe_stage.c': (28,
                                            '0f855a783d8dbf24c0b484b0066b7bd1f2854a5edc1d8d33bb17748e118b9ee6'),
 '_excludes_stage@vmu_native_stage_placement.c': (64,
                                                  '8540d2ae017cf21f499a277b2f32e03c6d21fee0865a347eff1c4c8bf36c5fe1'),
 '_fresh@vmu_native_heap.c': (116, 'd54fadf31fe16a93fd66370afdc5dccd2fe0fe85394a4fd6383c9a282427dcb2'),
 '_heap_bottom_code_span@vmu_native_heap.c': (88,
                                              'dbc822036064a68e06616872b2f38ac1e020e73d6a7c9ec29b0e3979c67f315d'),
 '_heap_bottom_halfword@vmu_native_heap.c': (58,
                                             'd66affe12d8e87985e0bbe094cc393649bad89f9e4494f55004afda5f193ab39'),
 '_identity@vmu_native_heap.c': (412, '86a986dfc4f32474965a416cb593222d735dce55b9d5b1376f8c39df8daa6120'),
 '_index@vmu_native_heap.c': (100, 'b412ae78cbfd65c1a1b10658ce17165212d296d9de64f5eee9eccbb5a1a0e7b5'),
 '_kui_vmu_native_heap_bottom_allocated': (172,
                                           'ef57b4c32b6daac233742762dde5c38d95186b4aeb9e90e31624bf9bb608c453'),
 '_kui_vmu_native_heap_bottom_code_valid': (360,
                                            'cf1b6823d8c888803967ea9f204f97ccb3db5ddc8fceb13362e27ecfab85b5be'),
 '_kui_vmu_native_heap_bottom_fresh': (148, '604aae4aeb3602887c9531571762c561dd4154badee6dbfc98f203acfdf3e908'),
 '_kui_vmu_native_heap_bottom_placeholder': (196,
                                             'b3ce4bb038c86339fb3552ebe2472282e2f804a0ef008ab256970396e7df29db'),
 '_kui_vmu_native_heap_bottom_ready': (144, 'c0803decc187dbce9ebc05df5f5c4fb3db3443df63a1bc87d6c5ee89183d81d8'),
 '_kui_vmu_native_heap_budget_prepare': (2370, '3aa8023859e1f8ef4abc457939be91ae7350e7dbb869cb972e2550731f288b23'),
 '_kui_vmu_native_heap_fresh': (268, '2312f78c29f410eb80ad2308471fe164ffcc307851dc525731a79ef9f55a8dca'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               'b97fd6deac4953eb0ef57eccf22964cdf149d5f30006492f0837aa51e210b198'),
 '_kui_vmu_native_heap_lease_valid': (52, 'e7b19c1615783228321041f16897bd0adccfa199f38a0f8c4363db4e6803963d'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   'e914be1d8341e749ce26b49f9b6fa0cff1cb880a97c44ff64df17973ee5104a1'),
 '_kui_vmu_native_heap_preallocate': (36, '8b6eda6e40d6707edfdf28d200f9d827f333fe8e65b4cf1f63b5788b041adf24'),
 '_kui_vmu_native_stage_bottom_allocation_safe': (308,
                                                  '04cfb6c9d82461fcc8230ce8aa82c240245768935571ad379cf0f215effb1234'),
 '_kui_vmu_observe_stage_install': (444, '198636e7de9ae56291c8259c5ec1f5ebce98815a12d62e8d817b62f038b49ca7'),
 '_le32@vmu_native_heap_budget.c': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
 '_long_literal@vmu_native_heap_budget.c': (128,
                                            '0498f8644f268808b372a804fb98da1c89a06b2ec679f3ff85e5f4b05e767489'),
 '_memcmp': (34, '3d7da64606fe412df2341ca8e1c2c56d14d3f4798465f1ec02f6d2994c6aefd9'),
 '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
 '_memset': (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a'),
 '_metadata@vmu_native_heap.c': (416, '2d4f8a8cb476de0f799ecaa1661b6c64539afb018b8f29026a3f7d4a544b6c20'),
 '_native_allocate_bottom@vmu_native_heap_probe_stage.c': (304,
                                                           'cddded766e28f7cbb343c499b8aeb9a1fb4419ed7c1a6698d55f19a1a8bec17e'),
 '_native_allocator_unchanged@vmu_native_heap_probe_stage.c': (68,
                                                               '996a1a3450c4546bbcde22a0ef8f296012175f2a863a818110fc1812b011a7be'),
 '_native_budget_prepare@vmu_native_heap_probe_stage.c': (92,
                                                          'ab6f317040a39467f4fed51b9f06b84a0d7b909a5453c9a9394afe61df8e1f76'),
 '_native_budget_publish@vmu_native_heap_probe_stage.c': (132,
                                                          'f79e7a3c8242a8adc70afcc9c6083de1648f0fe82feec1ad90862fb1f07c0684'),
 '_native_low_bindings@vmu_native_heap_probe_stage.c': (64,
                                                        '6f4d2de9317ce03d9d40c33d9ff60e6f53599d0088451afe93739fdf988beb8e'),
 '_native_read@vmu_native_heap_probe_stage.c': (84,
                                                'b6bc33c6d4c36f9e6625ccf6afdc55b98e2e8f684d95194687fa946071a4804d'),
 '_overlaps.constprop.0@vmu_native_heap_budget.c': (124,
                                                    '065cd1ab46d438904e2b3c691cb9f235400030a81c39e7f4c2b4e30b79bddca3'),
 '_ram@vmu_native_heap_budget.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_read_is@vmu_native_heap.c': (80, '81cce0fd7757ba06a92615af6903a1998e93c28a590a617de969ca15235c0483'),
 '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_span@vmu_native_stage_placement.c': (80, 'df14d9f998c9e03e1aced8f68d6db855fdd61f28259da937cd345f32ad8d484f'),
 '_word@vmu_native_heap_budget.c': (52, 'aa48f92d2d40d911e172fd18c1e14038c26b3c44b8e8c734cd6741f1bb9dac50')}
COLD_OBJECTS = {'_child_code@vmu_native_heap_budget.c': (100, '8b9249ed63eb68bec146752d114c643074f861aced0b0f03b07cb20cd90faa01'),
 '_heap_bottom_allocate_words@vmu_native_heap.c': (206,
                                                   '528ea52471bc42b01f1f4c4a40e7510f5346db70a539a35a40b8c97fbe8ba174'),
 '_heap_bottom_release_words@vmu_native_heap.c': (154,
                                                  '441e8180758616454557f02badc0b5b6a2b23b752ab88f5f6fb8e442ce84cb9e'),
 '_parent_code@vmu_native_heap_budget.c': (152,
                                           '07ce6930bce857d18e95a47261e637ebf8fbfc2f8ffb9624fd6baa36270eb1e1')}
WORKER_FUNCTIONS = {'_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_identity@vmu_native_heap.c': (412, 'b7bca4b2ad693aad51482b882f0ab5cb3a48a4627b08881d15de8e8f008ef78a'),
 '_index@vmu_native_heap.c': (100, 'c6ed491134adc1c5eec5fa34931766894e3493cde1e6cfb7005bc66dd8f7d59f'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               '09e1ab3a298dadd60cb7c7478509f4c589353435a951cda1449f0e9b4946acdf'),
 '_kui_vmu_native_heap_lease_valid': (52, 'b417b11aed6df7ea28b6ebd5ad13cd6a4d078c962d4ee0663f35b739baea43af'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   '1f9893598ed80bc99a914c32d61d4e61067dc0af69dddf6b260b6d7fe771ac01'),
 '_metadata@vmu_native_heap.c': (416, 'd49e53bb628103af10c60c0f88d2e18c989612750ffb0edad5ff7a0ec6e48ed9'),
 '_read_is@vmu_native_heap.c': (80, '317d91d5a9c401243076f2aaaadce7aea3ec35c138b9a78073be794a4333f1c2'),
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
    # Consecutive held-report gesture transfers (the operator's failure page).
    '_held_report@vmu_native_passive.c': (8, 4),
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
