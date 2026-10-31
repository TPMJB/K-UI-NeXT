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
STAGE_PINS = {
    'cache_gateway': {'_native_resolution_replay': (284,
        '8d093469928be74cad29fd153a5f632042e6bf240b563126bedf118560ee9dfd')},
    'dual': {
        '_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
        '_native_dual_reader_bind': (68, 'fa7ab32f86100edf17aa30471b61fbbbd72c98e096161f27f81706e66b924867'),
        '_select_resident': (272, '96e1040f9d0cd026d4d4b5e036e9a8f35215ad08aa05a7455be4a62755ca6a55'),
    },
    'installer': frozenset({(2068, 'a53927dc26e13ef08e15af5ff0db4136df70f34d49cb899d4bf2e73411e7095d')}),
    'read_ahead': {
        '_kui_vmu_native_background_map_address': (92, '14f07de519d8e507e8d2a6d69cdce033ce605aae4773114e5cbfb3fdad56163c'),
        '_native_readahead_bind': (1244, '54fd162b1790cd34bf459150f9b2d55981a162740a36ff8bc46c55a6eb9dfac3'),
    },
    'sdk': {
        '_fingerprint': (88, '9e50981a9f878826bcb6ab9e0aef91278c63319c680e81c164ff11b52923469d'),
        '_kui_native_sdk_sci_route_admit': (796, '2614a58e1b7fe1e55a6321d58466451416cdbca431d0097ad630b2e43a1cfeed'),
        '_kui_native_sdk_sci_route_discover': (2848, '7d9d709f2738d14608999cd15a856374d99e2896f5a2b4561e2631d370cfcdce'),
        '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
        '_live_words': (80, '7f9a9d1099b1032650a010c5bccef611968b6f967daf814fdc3a5a8396e27c2d'),
        '_native_sdk_sci_route_bind': (952, '5c42ce14d3009522a63864e200e66f5686539d74e0b99c2cd2b93ad0fb0959da'),
        '_normalized': (192, '3257e0f06b1f4d07436bed56fbfe36746b33aa83effa4694f8aaee1c024bc9d1'),
        '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    },
    'timeline_binder': ('69d76e28a24ed21b8e8a356683086eab775eca277983d8eac8133d3c426a91cc', 180),
    'toggle': {
        '_native_passive_run': (2068, 'a53927dc26e13ef08e15af5ff0db4136df70f34d49cb899d4bf2e73411e7095d'),
        '_native_persist_storage_init': (720, '9096cfdca3648ddf59facc801fb5155c6ea465f9335d13ddf637c6963fe3f637'),
        '_native_runtime_diagnostics': (44, 'a344c3cf9a8cbda49e18ce32789131b56a0769e746fbc6df3d440576afb29b62'),
        '_native_runtime_mode': (88, '991602f6ecb450c2a38ccf16db276c6b684161c0ec6f6b863a66c0a0757ded42'),
    },
}
WORKER_PINS = {'dual': {'_reader_capture_region': (100, '348d48974842bd4fd97259d535072d96babb744545d1fb9ac887beafba00d456'),
          '_reader_cooperative_service': (248, '940ce158ac039e9ad6e2003df0195bd57505bd5355d1a61ab7328f28093aed62'),
          '_reader_streaming': (120, '8587a92b2a7d3c237bf5f8f022595c2823c660a1db7534a03a9dfd75a6116aae')},
 'live_boundary': (216, 'b18b9961130e87275fa92b666d178c5451c0c5b1280e6055d670eb41370e7b1d'),
 'prepared': {'.rodata': ('4d4183a2bd743782c17d31f8d1b94dbbddc85643d39ac8ecab226ad8ac6abc73', 1176),
              '_chain': ('32ee04713adbef2163e718e068dfaa0f0d469785cd447f6f94cf58e9a4d71419', 908),
              '_controller_context_valid': ('43d5b6f1aee195f92cd33672814eff6b8fff1bca07c86b921846b90c29204c2b',
                                            1336),
              '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
              '_hook_word': ('69410002bf135878c32df63d2459603428947eadc760aa6309775f82b8801f9c', 132),
              '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
              '_kui_native_loader_timeline_visit': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83',
                                                    4),
              '_kui_vmu_native_lifetime_prepare': ('4691642d40ae532e2f81cfe46c3f81a019412c0da0e43fb4f7913d4a44b87312',
                                                   640),
              '_kui_vmu_native_passive_arm': ('f04f1efb8b413c82398b5f3b0b59e3957489bfebead2e5e9916466a8121777c6',
                                              2608),
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
              '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
              '_transfer_context_valid': ('f4d6f6fabdf3e2bfa6c1a6e2b1a5904b52adfbc4874c07b12dc87b6320c764fa',
                                          1208)},
 'read_ahead': {'_reader_capture_region': (100,
                                           '348d48974842bd4fd97259d535072d96babb744545d1fb9ac887beafba00d456'),
                '_reader_cooperative_service': (248,
                                                '940ce158ac039e9ad6e2003df0195bd57505bd5355d1a61ab7328f28093aed62'),
                '_reader_streaming': (120, '8587a92b2a7d3c237bf5f8f022595c2823c660a1db7534a03a9dfd75a6116aae')},
 'toggle': {'_kui_vmu_native_passive_arm': (2608,
                                            'd786aa7b629cc07831c1ca415516adccb88eef8147d51059d179dbc463cef7bd'),
            '_kui_vmu_native_passive_complete': (3828,
                                                 '3141da0e57c269fa52b6270b53eb69621b1a110ac68c58d7e8782e28ece779e6'),
            '_kui_vmu_native_passive_submit': (1272,
                                               '6c2785ff97671106b025ecb836ee7cef363c0811f307ceeb56d5aeadad267404'),
            '_kui_vmu_native_persistent_sd_transaction': (100,
                                                          'b7b96d26c46eaa652c480688a9f96b0c50b25ad4f66030cff57dcdd2210904ce'),
            '_persistent_admit': (28, '9af3abdd81b0698f83d7286959903bf1cd5a49df7472f2f6ca558af8b674570d'),
            '_persistent_handler': (28, '619a9015cbeed2e7c43638bc476477d63275953a0b79c850ba6f1351e673f183'),
            '_persistent_lease': (108, '511cf30b3ac46ae897c9e87d2d26f517f94d5fdfc2f09935f37230fa5f3ed4cd')},
 'witness': {'_controller_context_valid': (1336,
                                           '28a15a5de0313eb43beb95cf57e580539015b7a179fa78da18f6e7ec8fdc9ad0'),
             '_failure_witness.isra.0': (16, '82866f899fdef88f70e8632e669b695c9dd02b4b797bc4d7330eb6be95134bdf'),
             '_installed_hooks_valid': (128, '2babef2bf0ce988a706783e075ab52feae7048081585daae6787b80714e2b2b5'),
             '_kui_vmu_native_exit_shape_valid.constprop.0': (156,
                                                              'fe612b03ff13afa862e6f471ab7c7392cad80bc193d2e79e7c0911af6ca78ad8'),
             '_kui_vmu_native_passive_complete': (3828,
                                                  '3141da0e57c269fa52b6270b53eb69621b1a110ac68c58d7e8782e28ece779e6'),
             '_kui_vmu_native_passive_submit': (1272,
                                                '6c2785ff97671106b025ecb836ee7cef363c0811f307ceeb56d5aeadad267404'),
             '_kui_vmu_native_patch_encode': (168,
                                              'b11f3f34f416e96ea70cfeac08ed37711b5a865dcdef695969afeb3fef8e6cc2'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128,
                                                                    '232becd8a74c4f666780f52441a5e4732bcd2248c46317fb43778fa4e4736486'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (108,
                                                                      '7cec2124461f02e9dbbf27f78ecbb849e6de0fb6822e4f36af916850f032108c'),
             '_kui_vmu_native_timer_callback_owned': (92,
                                                      '06da399a6b677cdddfe117fe9e91eec648066ea8ce8eb4fd8dae05cb248aaafb'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_normalize_heap_dispatch': (276, 'a0effb91c0224ac011b99ff7742eed864841464561839acd6a232b32e9285477'),
             '_normalize_heap_dispatch_if_routing': (40,
                                                     '855e8d9b8a46f06c5fd89ce11741979911e7fa01b1fa2793529e1e83d9db4648'),
             '_poll_lease_valid': (168, '62055372d158939f93b937720767e031446df6f60e4a87113f12280e56c44912'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_reader_capture_region': (100, '348d48974842bd4fd97259d535072d96babb744545d1fb9ac887beafba00d456'),
             '_reader_cooperative_service': (248,
                                             '940ce158ac039e9ad6e2003df0195bd57505bd5355d1a61ab7328f28093aed62'),
             '_reader_streaming': (120, '8587a92b2a7d3c237bf5f8f022595c2823c660a1db7534a03a9dfd75a6116aae'),
             '_readonly_rx.constprop.0': (76, '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, 'dcdf7892a62ef1f7538d7232cb9f390c9b703c94f9b5fa4abba1107d9295ab16'),
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
                                                    '771e8ab653e3950bf667035ee97b61776d1a10bb0b66c0c80ecca9b6c4637c4b'),
 '_at@vmu_native_heap_budget.c': (92, 'e25b91e557630914f31039a157f937c46ce0d1b911673aa527237d1567ef492e'),
 '_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_code_sha@vmu_native_heap_probe_stage.c': (64,
                                             '1392ec35e45e4d964f7f006d0558e510eb5243e6f4831d4a80c954157ff5ced6'),
 '_control@vmu_native_heap_probe_stage.c': (28,
                                            '28b80e1d9c7a8b89650d5ce726b338e13f6160a8183af096c0f215337eb112a3'),
 '_excludes_stage@vmu_native_stage_placement.c': (64,
                                                  '74c9f79f5d6af5ccc3872d2c1efd5f85336a79205ef566c02a37e1fae31fb982'),
 '_fresh@vmu_native_heap.c': (116, '60fce225512af379c2423dd09e3119b3c3bb3d770a16326c755d33d538b96e94'),
 '_heap_bottom_code_span@vmu_native_heap.c': (88,
                                              'fee5d6ddaf12981d3613b02d1e3343e71bdfef104282b79e651478b5c66b9efb'),
 '_heap_bottom_halfword@vmu_native_heap.c': (58,
                                             'd66affe12d8e87985e0bbe094cc393649bad89f9e4494f55004afda5f193ab39'),
 '_identity@vmu_native_heap.c': (412, 'f9a29aa8d3355fd9e1d96f59dc1a508f172d37bd7efd4910ac1783a4726cc6bc'),
 '_index@vmu_native_heap.c': (100, '7345282e5897c23b7d09f7c2e521967c280af5449633fdbc2dc4a1a1abc0affb'),
 '_kui_vmu_native_heap_bottom_allocated': (172,
                                           'bf43af3c7d5fba689131b575ce6ce0a463b0f193fd1821d80409ec1d34a89c64'),
 '_kui_vmu_native_heap_bottom_code_valid': (360,
                                            '5de85f04c9187cfb38694a2e4073758905e291da6f1a21b956583cd8bf1bee2f'),
 '_kui_vmu_native_heap_bottom_fresh': (148, 'c570dc3dd9f4b32945968cbb33563b0e2271a4c9f071255f72ec6904fdcdf42a'),
 '_kui_vmu_native_heap_bottom_placeholder': (196,
                                             '7a864b7c7cd595a0a8c549df85e21b97abd5a202e299f3cf44225e44311ada6b'),
 '_kui_vmu_native_heap_bottom_ready': (144, '276dabc0527e3aa222d12516cfb7850e795964af2b0a821be1eb37f0888a39d2'),
 '_kui_vmu_native_heap_budget_prepare': (2370, '16b7fbe035701a1314c15ff6a0181cba73cdfaf3a84ba562aece96ef07dd8877'),
 '_kui_vmu_native_heap_fresh': (268, '66be773f1d27f01c31669d9cfd9af667360ed302eaf3652d9404a717b05ce59e'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               'caf29996760f65f169b7e615312517833c51d471c3c65320159f7cea5f4eb762'),
 '_kui_vmu_native_heap_lease_valid': (28, '445cdf16dcd8602771daf8ba193c35a8eb44ae82a27b0e2d2001936da354970c'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   'c0fc9ddd6adf1fd5843dcab35a637ff159ac2ef1a5effaabb24093b4855bb488'),
 '_kui_vmu_native_heap_preallocate': (36, '6100890e5b34d0b06c21ac197757e000370497c4c9383d85fcf4f2d7d2932c95'),
 '_kui_vmu_native_stage_bottom_allocation_safe': (308,
                                                  '765e286c4e21a37d41a5c42a26c92cd545054cd976579fe5281c4742f8211fd7'),
 '_kui_vmu_observe_stage_install': (416, '658ea169e8bd0f97f4bab96c7c430fb91564fab30ff89aec3d902d44cd766193'),
 '_le32@vmu_native_heap_budget.c': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
 '_long_literal@vmu_native_heap_budget.c': (128,
                                            '714996c60003a34cbc3c2732a0a219e9f41b86e5faed70b68183a76d868b56dd'),
 '_memcmp': (34, '3d7da64606fe412df2341ca8e1c2c56d14d3f4798465f1ec02f6d2994c6aefd9'),
 '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
 '_memset': (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a'),
 '_metadata@vmu_native_heap.c': (416, '408c189f0daab81a1a364210c683348a6415a79da51981f40776d90293b85319'),
 '_native_allocate_bottom@vmu_native_heap_probe_stage.c': (304,
                                                           'f68ad9ec537f9b7466b17da48465a8ce3cca80d5eafaebdce356b52a755b1535'),
 '_native_allocator_unchanged@vmu_native_heap_probe_stage.c': (68,
                                                               '6f73100164361b256c65fc78c7d0b61b2ac5c5cf4c0d5eb04aa55d867dbea9b5'),
 '_native_budget_prepare@vmu_native_heap_probe_stage.c': (92,
                                                          '1037d9d1d6ca3c44752830cc7953195e7a5f69668afb7e13fe7b91286f9559f9'),
 '_native_budget_publish@vmu_native_heap_probe_stage.c': (132,
                                                          '23544300d484c470a5bdd9b4d06c148c479c47660a31d1b688d96fa1076f75d2'),
 '_native_low_bindings@vmu_native_heap_probe_stage.c': (64,
                                                        '752bc642db1d292d0b1999bdcf88bfc29b5f8064aa5dbab561b08fa303a6cd97'),
 '_native_read@vmu_native_heap_probe_stage.c': (84,
                                                'b6bc33c6d4c36f9e6625ccf6afdc55b98e2e8f684d95194687fa946071a4804d'),
 '_overlaps.constprop.0@vmu_native_heap_budget.c': (124,
                                                    '3b4480a8fa64ab66e5ba5fc629d90b491a0c9b24007325a507d27f4df65d0be9'),
 '_ram@vmu_native_heap_budget.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_read_is@vmu_native_heap.c': (80, '4cfe98ab2062c5f205f006bac59e799d38568f1b813cbd99a998702acc88725e'),
 '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_span@vmu_native_stage_placement.c': (80, '1e1d97ad69f664e0ec0899bc2e7f277230a8b6d7452f1b778e3282f01eb62f3c'),
 '_word@vmu_native_heap_budget.c': (52, 'aa48f92d2d40d911e172fd18c1e14038c26b3c44b8e8c734cd6741f1bb9dac50')}
COLD_OBJECTS = {'_child_code@vmu_native_heap_budget.c': (100, '8b9249ed63eb68bec146752d114c643074f861aced0b0f03b07cb20cd90faa01'),
 '_heap_bottom_allocate_words@vmu_native_heap.c': (206,
                                                   '528ea52471bc42b01f1f4c4a40e7510f5346db70a539a35a40b8c97fbe8ba174'),
 '_heap_bottom_release_words@vmu_native_heap.c': (154,
                                                  '441e8180758616454557f02badc0b5b6a2b23b752ab88f5f6fb8e442ce84cb9e'),
 '_parent_code@vmu_native_heap_budget.c': (152,
                                           '07ce6930bce857d18e95a47261e637ebf8fbfc2f8ffb9624fd6baa36270eb1e1')}
WORKER_FUNCTIONS = {'_bottom_geometry@vmu_native_heap.c': (24, 'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_identity@vmu_native_heap.c': (412, '027635dcf6b58837e9154fd6c5a5887e3cfe58a254315682ab98105e5d546c76'),
 '_index@vmu_native_heap.c': (100, '5760f30763fa7eccda87c9c406ef8864b92a6b5a1db0f3f13da3a684a5a5a0ee'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               'd5fc54d8ed6fc81d31732ef3fbf5a77ccca67dc75bfd17994659778f51fd957f'),
 '_kui_vmu_native_heap_lease_valid': (28, '12599e82580e5f6359c22435d594e77aed53b41f6920300e724294ebf1955388'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   '4409a2e67d85d02c7bc71470bf5f15753164f52920279b7b766c0b510acd2ed3'),
 '_metadata@vmu_native_heap.c': (416, '18d3a076be0e87c54cb644c72405e595008f7509eb1472dcc3c174b8cc1db751'),
 '_read_is@vmu_native_heap.c': (80, 'f7519ef9ffdc8ba53cd0a6fff98709ae342c821c9751c306633c1186ddb77691'),
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
