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
# Re-pinned after the reviewed early-controller split path and report gesture:
# only passive_split_submit (860 -> 916 bytes) and passive_complete (4000 ->
# 4110 bytes) change instructions. Against the clean 15e2e5fe merge build,
# all 115 other changed worker bodies contain only 534 typed relocations;
# all 111 changed stage bodies contain only 542 typed relocations. Complete
# anonymous constant regions preserve their contents after 8 worker / 6 stage
# pointer relocations; _chain's whole switch table is identical despite its
# compiler-local identifier changing from 353 to 357. SCI/Legacy resident and
# auxiliary function bodies are byte-identical. Existing mutation and linked
# execution checks remain mandatory; no expectations are learned at audit time.
# Re-pinned after the focused controller/relocation review of 26ed262974a8.
# Only passive_complete changes worker instructions: one duplicated TX scan
# is removed and early INFO/ALLINFO no longer dispatch optional reader TICK.
# All other 113 changed worker bodies preserve 482 typed pointer bindings; the
# complete 32-byte _chain switch table is identical (compiler ID 357 -> 355).
# The stage changes only the relocator's guarded aligned word access, its
# four-byte aligned32 leaf and compiler register allocation; 202 other stage
# bodies preserve 1020 typed pointer bindings. The complete anonymous constant
# tails preserve 7 worker/6 stage pointer targets. No audit learns pins.
STAGE_PINS = {
    'cache_gateway': {'_native_resolution_replay': (312, '7f3de02392f3fea08d76a848b0b7b73c47fa38210e3202a6dbf4dfb1f5dc3f4f')},
    'dual': {
        '_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
        '_native_dual_reader_bind': (68, 'dc4f26c035732ad827cd3cd705f18637f8db5d1fa3c9e291fd030d12d77b24bf'),
        '_select_resident': (272, 'e7103253250d7cfa46d58e30894c470a3d804552cf6f7499e9d30a1072fbdba9'),
    },
    'installer': frozenset({(2084, '0273b3a569a27a078150f4fc2c93e638b96e17d2870820c0b94a6549f78387c2')}),
    'read_ahead': {
        '_kui_vmu_native_background_map_address': (92, '14f07de519d8e507e8d2a6d69cdce033ce605aae4773114e5cbfb3fdad56163c'),
        '_native_readahead_bind': (1244, '119b65e6b9dbdf7d5f1433600f754adfef9c206862c9fb2796a14dbe58029b8d'),
    },
    'sdk': {
        '_fingerprint': (88, '3cd7bc500949746306fb63e8035c90db55cdb65fa2dd43a526087469fd96678e'),
        '_kui_native_sdk_sci_route_admit': (796, '766ccc65cdd710208962883d15c0e86e769995d1f8406269efd32cc9e1049488'),
        '_kui_native_sdk_sci_route_discover': (3264, 'c2113acedb426a052154f97d1dff39dbdd601f68140b2dcbdef552a9dc17c5ab'),
        '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
        '_live_words': (80, '71d7b13baaf6e21fa6329e94db086dfa03a60474cc64ab3ae825f4f955711e75'),
        '_native_sdk_sci_route_bind': (944, 'c02060e58942e8b054215f7fce0cadfc1aef367b2aa0b8c1cd3cdb5958315f11'),
        '_normalized': (196, '9d54f8d6223ac8b6e52e341c431a9e17a22b45feb4291c9ed1c6ca4bd504334d'),
        '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    },
    'timeline_binder': ('a042bafffa63f760a2a475fe5e3330bb77157f92d8b627b729b8af6c10bd426e', 180),
    'toggle': {
        '_native_passive_run': (2084, '0273b3a569a27a078150f4fc2c93e638b96e17d2870820c0b94a6549f78387c2'),
        '_native_persist_storage_init': (720, '8007df3eb70e88bf1d70d8af5fe3dd165f12fbd3873af57424628ca29d055c41'),
        '_native_runtime_diagnostics': (44, 'a555ac1c606cc4b2586cdc8c97a7920c1ee4adc93f4dcad564d4fdfbe68fb91c'),
        '_native_runtime_mode': (88, 'c25646f2ac77b5d75f3fa069202ecd9c9387642b14e6f97c17487c3f39a392fc'),
    },
}
WORKER_PINS = {'dual': {'_reader_capture_region': (100, 'c9afb4f461bee3a199b4d22885f938f75a1023392c8fb0a609db456ea422d037'),
          '_reader_cooperative_service': (248, '9f5ae247c19dc4f7ae60851096868cfe9e5562daab0b49015924a760ffcc6e71'),
          '_reader_streaming': (120, '8004471541243a9704b0f077814995e7d715d110fb5a6adcdd8883dcf7425868')},
 'live_boundary': (216, '111cf9f43455a4191773c5a2f6b52ec558b5550e85ac70c73d78a86dabe8c843'),
 'prepared': {'.rodata': ('4d4183a2bd743782c17d31f8d1b94dbbddc85643d39ac8ecab226ad8ac6abc73', 1176),
              '_chain': ('a6e698a83bf0309d2a08e5ed2953fcb0f9ea2f6685a8fdde85bb8eb17cf8ed29', 908),
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
              '_kui_vmu_native_passive_complete': ('65b20969445b657bab0915fe59742a813649348c216c9f77159fcf70ef2cccc1', 4100),
              '_kui_vmu_native_passive_split_submit': ('f31a1c5fcf82084b05106bc66fcc14a2e39c54f1dbb9f47a2c806d763d063984', 916),
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
 'read_ahead': {'_reader_capture_region': (100, 'c9afb4f461bee3a199b4d22885f938f75a1023392c8fb0a609db456ea422d037'),
                '_reader_cooperative_service': (248, '9f5ae247c19dc4f7ae60851096868cfe9e5562daab0b49015924a760ffcc6e71'),
                '_reader_streaming': (120, '8004471541243a9704b0f077814995e7d715d110fb5a6adcdd8883dcf7425868')},
 'toggle': {'_kui_vmu_native_passive_arm': (2620, 'c8ded11502b9abb74d3dde134bb7ec0e5f5a93ae43553ff6eb49b430bfde8765'),
            '_kui_vmu_native_passive_complete': (4100, 'e2d790865d23eb4255d79930f6a019c9697c3f9f91c31064dea9013f2c763691'),
            '_kui_vmu_native_passive_submit': (1272, 'e072762b2ad13a240841dfff71e7268d8bbd01d69bb5732972d2ca87e373d4dc'),
            '_kui_vmu_native_persistent_sd_transaction': (100, '3ea7f041feaaa649fc99f19426d699492472933928df2e215a06d1e2fd4063a3'),
            '_persistent_admit': (28, '4cada5f4e783405eacdf2a24457d5928a70cd843ba7ba4469da6080b3ca99a89'),
            '_persistent_handler': (28, '4354f4e10d42ebd2d842034732db0f09219f6a6b41a7d8a5b82e77c84cddff9f'),
            '_persistent_lease': (108, '1745803da9538486d77b72e97c94833e356256ed2c92be73de3185a8e835b231')},
 'witness': {'_controller_context_valid': (1336, '60c8b21816e2953e339a2aeaf1714eb650a8ea49f8f4f8027563f82f40e1c5fe'),
             '_failure_witness.isra.0': (16, '3540dd340c4e75f2962e36794cd7272d62c10fe124a91dfabc120073dfb9f415'),
             '_installed_hooks_valid': (128, 'ac3d6168bb4ef83172881e2c25911382e8c87faf90cd07698c38668ad08f0672'),
             '_kui_vmu_native_exit_shape_valid.constprop.0': (156, '487844d7a9c2d798272beda059f9c2d1341066bea7033a70907edf88a4384f3b'),
             '_kui_vmu_native_passive_complete': (4100, 'e2d790865d23eb4255d79930f6a019c9697c3f9f91c31064dea9013f2c763691'),
             '_kui_vmu_native_passive_submit': (1272, 'e072762b2ad13a240841dfff71e7268d8bbd01d69bb5732972d2ca87e373d4dc'),
             '_kui_vmu_native_patch_encode': (168, 'f8b8c3d8ceb7c0ba298ebb93b53e432ef37a41b0b7127ca894f574640a7118f3'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128, '4b6ba4b4f62b60a2f6a5c04fd0d380e53ce7736008a075420e3e07f968359295'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (108, '71e36e343a7d6f92a9b98d932bbacb628ab506afe9d5f3e8e91b21a4ad2cb6ee'),
             '_kui_vmu_native_timer_callback_owned': (92, '73378cb1488fccddd79453e2427d5d2da23983a81be95b62f792e945fbba2975'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_normalize_heap_dispatch': (276, '362423ea39f62032159bce5962095f459cdb3203d13da3aaaaf34c051ab905da'),
             '_normalize_heap_dispatch_if_routing': (40, '27ab1a111e48b0724ea5d87299a0ade15206dee342d46a80d76dff5846d7776f'),
             '_poll_lease_valid': (168, 'c0474f32f17b23c2b69c480049c6cdf8c123458c1e65407df5c00b2a76bd830b'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_reader_capture_region': (100, 'c9afb4f461bee3a199b4d22885f938f75a1023392c8fb0a609db456ea422d037'),
             '_reader_cooperative_service': (248, '9f5ae247c19dc4f7ae60851096868cfe9e5562daab0b49015924a760ffcc6e71'),
             '_reader_streaming': (120, '8004471541243a9704b0f077814995e7d715d110fb5a6adcdd8883dcf7425868'),
             '_readonly_rx.constprop.0': (76, '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, '614c85c738a86ddeb4e2412e70eeb432e6ab06f48e02d4a328cd98c151ccf96a'),
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
 '_allocation_safe@vmu_native_heap_probe_stage.c': (88, '8bdfa34b83422c5958fcf499c268a6815aca33549c70d9340ef7c5bc647eea63'),
 '_at@vmu_native_heap_budget.c': (92, 'b02b3fd852c348871a33429f96568363a69c5f9e58a69e54d6193bcd44a9cda2'),
 '_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_code_sha@vmu_native_heap_probe_stage.c': (88, '912eba2e4e3039a44c06946675630f85d961df0f2d8842efef313cfa96a950f5'),
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
 '_kui_vmu_native_heap_bottom_code_valid': (360, 'f2b54349b6ebae3e1bd9b5d9073fe388f1ddbaf3b78f12989fe3ad86a3aef283'),
 '_kui_vmu_native_heap_bottom_fresh': (148, '604aae4aeb3602887c9531571762c561dd4154badee6dbfc98f203acfdf3e908'),
 '_kui_vmu_native_heap_bottom_placeholder': (196,
                                             'b3ce4bb038c86339fb3552ebe2472282e2f804a0ef008ab256970396e7df29db'),
 '_kui_vmu_native_heap_bottom_ready': (144, 'c0803decc187dbce9ebc05df5f5c4fb3db3443df63a1bc87d6c5ee89183d81d8'),
 '_kui_vmu_native_heap_budget_prepare': (2370, 'bb6d44c7705886ccec4ba4d1bbe495227a3133bc72e025d6f7adbb47816b0c31'),
 '_kui_vmu_native_heap_fresh': (268, '2312f78c29f410eb80ad2308471fe164ffcc307851dc525731a79ef9f55a8dca'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               'b97fd6deac4953eb0ef57eccf22964cdf149d5f30006492f0837aa51e210b198'),
 '_kui_vmu_native_heap_lease_valid': (52, 'e7b19c1615783228321041f16897bd0adccfa199f38a0f8c4363db4e6803963d'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   'e914be1d8341e749ce26b49f9b6fa0cff1cb880a97c44ff64df17973ee5104a1'),
 '_kui_vmu_native_heap_preallocate': (36, '8b6eda6e40d6707edfdf28d200f9d827f333fe8e65b4cf1f63b5788b041adf24'),
 '_kui_vmu_native_stage_bottom_allocation_safe': (308,
                                                  '04cfb6c9d82461fcc8230ce8aa82c240245768935571ad379cf0f215effb1234'),
 '_kui_vmu_observe_stage_install': (444, 'a918426b79594bd643d6c5f4f29cd30ee3ad5490fb3317a42f39b597a6401359'),
 '_le32@vmu_native_heap_budget.c': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
 '_long_literal@vmu_native_heap_budget.c': (128,
                                            '0498f8644f268808b372a804fb98da1c89a06b2ec679f3ff85e5f4b05e767489'),
 '_memcmp': (34, '3d7da64606fe412df2341ca8e1c2c56d14d3f4798465f1ec02f6d2994c6aefd9'),
 '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
 '_memset': (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a'),
 '_metadata@vmu_native_heap.c': (416, '2d4f8a8cb476de0f799ecaa1661b6c64539afb018b8f29026a3f7d4a544b6c20'),
 '_native_allocate_bottom@vmu_native_heap_probe_stage.c': (304, '4fe6c6a971a2fb8b1150ca980589f2d16e9bac5a00be9651570e923e46cbafcc'),
 '_native_allocator_unchanged@vmu_native_heap_probe_stage.c': (68, '74095b677274f5cd031e8e7b73bf383cddbc1c963d0d3a933f8ac4d26db8db8b'),
 '_native_budget_prepare@vmu_native_heap_probe_stage.c': (92, 'da3374d648832dbe8ce35974b5030c2fc4cb46ec901f2c7a84e17513212894b6'),
 '_native_budget_publish@vmu_native_heap_probe_stage.c': (132, '97d4e09a5d81fa98838fd6cb538520155e1d9e31ceb57cd153611cc603982b07'),
 '_native_low_bindings@vmu_native_heap_probe_stage.c': (64, '05797e2f4eb09df3866feeb28e33b366830547f29a5e7cf00b45be7c1cad0dd2'),
 '_native_read@vmu_native_heap_probe_stage.c': (84,
                                                'b6bc33c6d4c36f9e6625ccf6afdc55b98e2e8f684d95194687fa946071a4804d'),
 '_overlaps.constprop.0@vmu_native_heap_budget.c': (124,
                                                    '065cd1ab46d438904e2b3c691cb9f235400030a81c39e7f4c2b4e30b79bddca3'),
 '_ram@vmu_native_heap_budget.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_read_is@vmu_native_heap.c': (80, '81cce0fd7757ba06a92615af6903a1998e93c28a590a617de969ca15235c0483'),
 '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_span@vmu_native_stage_placement.c': (80, 'df14d9f998c9e03e1aced8f68d6db855fdd61f28259da937cd345f32ad8d484f'),
 '_word@vmu_native_heap_budget.c': (52, '1ed0fa23d390b0c961ca0a635f020f7c58023e0f248087cc85f3c0fb743a5f9c')}
COLD_OBJECTS = {'_child_code@vmu_native_heap_budget.c': (100, '8b9249ed63eb68bec146752d114c643074f861aced0b0f03b07cb20cd90faa01'),
 '_heap_bottom_allocate_words@vmu_native_heap.c': (206,
                                                   '528ea52471bc42b01f1f4c4a40e7510f5346db70a539a35a40b8c97fbe8ba174'),
 '_heap_bottom_release_words@vmu_native_heap.c': (154,
                                                  '441e8180758616454557f02badc0b5b6a2b23b752ab88f5f6fb8e442ce84cb9e'),
 '_parent_code@vmu_native_heap_budget.c': (152,
                                           '07ce6930bce857d18e95a47261e637ebf8fbfc2f8ffb9624fd6baa36270eb1e1')}
WORKER_FUNCTIONS = {'_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_identity@vmu_native_heap.c': (412, '620a3a09663479e48be6869a52a45378a2b98d798e97318f4fce37f033f40956'),
 '_index@vmu_native_heap.c': (100, 'dbeb8cb4f53d695e3327cb97df9153112b6a2c81de5102c9780a143e22a16857'),
 '_kui_vmu_native_heap_lease_identity_valid': (76, '8ad8b85294e1cead36ed7cf2df7b571c9f964d064683b8c9e41739c94b3a39c2'),
 '_kui_vmu_native_heap_lease_valid': (52, 'a52ab5803160db40a82fb574fd72f112a3e6fba1c14f41f0b1d957cf5050b704'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512, 'd6bdfd89f9c66c675e2d327df7ab263193304eef18e6c6c97335cc6d3d3b1853'),
 '_metadata@vmu_native_heap.c': (416, '00968e5691bac123e2dca7b4396b9a8501e53e5bfde4bd05b6e5797e8b64c27d'),
 '_read_is@vmu_native_heap.c': (80, '131577a36ed99075791c6477db5e4c0fb443342f09f3782508b4b84090fb2822'),
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
