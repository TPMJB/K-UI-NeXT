#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Select separately reviewed cold-callback and terminal-observer code maps.

Historical complete pins stay in their original auditors. New maps are exact
whole bodies (or the existing typed-relocation signature), never patterns
learned from the artifact being checked.
"""

CALLBACK_MARKER = '__kui_native_dual_low_callbacks_stage'
RAW_OWNER_MARKER = '__kui_native_raw_owner_terminal_worker'
BOTTOM_STAGE_MARKER = '__kui_native_bottom_heap_stage'
BOTTOM_WORKER_MARKER = '__kui_native_bottom_heap_worker'
ABSOLUTE_ONE = (1, 0, 0, 0xfff1)

# Filled only from the independently reviewed complete linked candidate.
STAGE_PINS = {'dual': {'_select_resident': (272, '2761ce0b094f0dae5d6af71d80006ff9e9bdef4c165fb96d916ba423ff530611'),
          '_native_dual_reader_bind': (68,
                                       'da5aeff4a3291efb0239284b83410da3a8de67a0e669d628d2aa14d1fded6768'),
          '_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c')},
 'sdk': {'_native_sdk_sci_route_bind': (952,
                                        '0dd465927c460231a8710e4da35077ee0e3a0c9b593378ed6cddf3f5b87d17d8'),
         '_kui_native_sdk_sci_route_admit': (796,
                                             '173e7c8e40e668df055672165feb10ddbe1f625da156f1328379fcfdb112548c'),
         '_kui_native_sdk_sci_route_discover': (1856,
                                                '3dc7d5d244bfdc99f7c142349ca37752edb76c4585c0b43b2449c6a3c6d34b34'),
         '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
         '_fingerprint': (88, 'ff6efd555e693929eae9bc27124c5195894c808999e754dae819c60efc60ed3b'),
         '_live_words': (80, '75c13259ee1766df8e6c756f69f56336808a8c79dc819b5d460b360b8c6ea357'),
         '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
         '_normalized': (192, '1d215490b37c32de7c002b333040ed261f16a677ae0e28d564cc680306b3c2ab')},
 'read_ahead': {'_native_readahead_bind': (1260,
                                           'c0c1c618442132e3a2c0d6a2bbb85b43837bef571b1b835b3aae4442840e35c4'),
                '_kui_vmu_native_background_map_address': (92,
                                                           'cd9d8aba83db8374543f4850bf079245710ba3854dbf128c98f20c890a9753c8')},
 'timeline_binder': ('ca398027f2a09137e073d7e31357fcb35fe113419a6839873dab94edf3169797', 180),
 'installer': frozenset({(2004, '2076ad7dd2e491eb4b77348b31ffed32beed444584af899ffc4984e5838d6a0c')})}
WORKER_PINS = {'dual': {'_reader_readahead_exports': (200,
                                        '8b2b379ac0631957845d210d8d1e2c76fddc87d440bf23b869be3c40710d799f'),
          '_reader_capture_region': (100, '3c04ce83cadaf497cd72dc36959c8543ba935eb32627e7e34fc8b0b7c82b8b76'),
          '_reader_readahead_visit': (56,
                                      '277ea46f6d7d509e57e0be251e5f2e7f12e3193f8d9549d961cae44fc1b8bbb1')},
 'read_ahead': {'_reader_readahead_exports': (200,
                                              '8b2b379ac0631957845d210d8d1e2c76fddc87d440bf23b869be3c40710d799f'),
                '_reader_readahead_visit': (56,
                                            '277ea46f6d7d509e57e0be251e5f2e7f12e3193f8d9549d961cae44fc1b8bbb1'),
                '_reader_cooperative_service': (32,
                                                'cc22925c248e346f99c06943c5fff84e30668f842928e8b52d2820858b645243'),
                '_reader_readahead_failure': (172,
                                              'cb8117ca259981b85e0f2f6f67a1618179f7b988094bd2cde68b50906d68eb31')},
 'witness': {'_failure_witness.isra.0': (16,
                                         'fc122de5365755d2d5ef07aec1fd947acd4ba4a026c10e683b4b2639bea1ee47'),
             '_poll_lease_valid': (168, 'adfabc1a8a02b682d189fa1462a775135599846623d6b515b3a6cab9a0e1738d'),
             '_controller_context_valid': (1336,
                                           '53096b365d1efdc278a06655dfe373cb45b7e3b788e7c6c07017c469285a3a80'),
             '_kui_vmu_native_passive_complete': (3144,
                                                  'd88fa2c9db50f1f028447fd72e4d29c85aa3d36196a285702a686478e98c427f'),
             '_kui_vmu_native_passive_submit': (1428,
                                                '608b1af3b6de77cd9ea34e4d142b53147d48acd69c48461a7467bb0d53f8c62b'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_readonly_rx.constprop.0': (76,
                                          '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, '19156c1e098194e79f69c719ff07fcdadf3118a17ac04b5210426452e677f7dc'),
             '_installed_hooks_valid': (128,
                                        '0dacec7424ac69a33a3e65b4122a7c39ee290a5b7a57362cfc3c2fd4787670ee'),
             '_normalize_heap_dispatch': (276,
                                          'd3a876eef438731396f1aceef7b8ef83efffbeec161398f6bd98aa46d0f9fe39'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128,
                                                                    'ef7cd000848249887ff9870987edc53ab6cdc1f289be854a54200e20eb2d0884'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (100,
                                                                      '181646f26f1ae7eb74c3e662b67c05cab0c4005c0472c19804e33bc1e673778f'),
             '_kui_vmu_native_patch_encode': (168,
                                              '9996f3e52a020cd140afc96e36a5078af4ac22c4c7f7dfe286f10f3685d52f76'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_kui_vmu_native_trace_point': (1132,
                                             'd2092aa52e32d2606287722a403c356f9fb6ceda7810cf37450e9824e2ee998c'),
             '_reader_capture_region': (100,
                                        '3c04ce83cadaf497cd72dc36959c8543ba935eb32627e7e34fc8b0b7c82b8b76'),
             '_reader_cooperative_service': (32,
                                             'cc22925c248e346f99c06943c5fff84e30668f842928e8b52d2820858b645243'),
             '_reader_readahead_exports': (200,
                                           '8b2b379ac0631957845d210d8d1e2c76fddc87d440bf23b869be3c40710d799f'),
             '_reader_readahead_visit': (56,
                                         '277ea46f6d7d509e57e0be251e5f2e7f12e3193f8d9549d961cae44fc1b8bbb1'),
             '_kui_vmu_native_timer_callback_owned': (92,
                                                      '48a7911f84268444dc817b74d2e5c9bfceb3be8c79872d861abeb6301d3090dc'),
             '_timer_runtime_ram': (104, '4fb7919a6f74ec1c9aa3f434e446b352f7961eb7c565580f0c4e2e52a3eff415'),
             '_timer_runtime_in': (26, 'a0ff5d4d27ccbf7b073b8bc6f0e6b26659e6b729763f7b02d49aa124ea023824')},
 'live_boundary': (216, 'b460ad2843811984ac27a4687fe64490fffb6735226afb2d06967b0bc7aade1e'),
 'timeline_division': ('079afd27658837a46805adc55e7c9bc491d6be954b2370711cfd7610baca57dd', 144),
 'prepared': {'.rodata': ('a0b38d706394d3dcab54805134170714b4c3264bfbb42c2d03bc64626b2f2153', 3960),
              '_controller_context_valid': ('43d5b6f1aee195f92cd33672814eff6b8fff1bca07c86b921846b90c29204c2b',
                                            1336),
              '_diagnostic_begin': ('3be33ddc079ec9db0760339daf470b64f4903769d66dc830e14c2fcd0008ccc1', 40),
              '_diagnostic_complete': ('5433acef74fd525985ac8d0d4023b021fe8eda769a07801487300f67678aed21',
                                       3610),
              '_diagnostic_end': ('7cd314083d85f7299e6f540a6c294b82f54dc5e546fb0bc28492e9b866fc734e', 1492),
              '_diagnostic_gesture': ('798c8a977a6e0b1e49fca8135d6a944513c0e2c8cf5c9342f0dd0f5f291f828f', 34),
              '_diagnostic_pair_begin': ('1b958dcf67fba5c38737817051a4131f3d026618b78de2f4c90be5caca3208d2',
                                         504),
              '_diagnostic_phase_read': ('9be746502cbe97b305edc7c282c70dd68d3b6d43dfafea092fecd4d7b975fcfc',
                                         136),
              '_diagnostic_phase_row': ('ca25d712e430b3f140fec46bf465357a682ff172fe907efebcce9c4abe13c58c',
                                        16),
              '_diagnostic_sample': ('cb7e980cb399fa654e837a6896d3ec033fd9d844c8fd0a2c31c77399471411fe', 164),
              '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7',
                                          16),
              '_hook_word': ('69410002bf135878c32df63d2459603428947eadc760aa6309775f82b8801f9c', 132),
              '_installed_hooks_valid': ('5d07dd26ee0b66c99dc296095a7014c92f6e10d8ba8006ad0116ce4affaf4148',
                                         128),
              '_kui_native_loader_timeline_add': ('4d6b8073d0a1fd8de7359127083bbf20843381ed99df20a2d1c3c1f8b80d5b26',
                                                  856),
              '_kui_native_loader_timeline_reset': ('3e45ddcf599ecf09701486d6f30bc38a34bd5122e36349922416f6ee6d07ff0c',
                                                    24),
              '_kui_native_loader_timeline_visit': ('eb70c2499182c9ba9d638aa8c931cb768c8459630e56f057575d9e923e619f5c',
                                                    148),
              '_kui_vmu_native_fault_capture_install': ('db26024d38eaa6e0c440fdadf83df04c43a91a653ab21637e427502550726afb',
                                                        560),
              '_kui_vmu_native_lifetime_prepare': ('4691642d40ae532e2f81cfe46c3f81a019412c0da0e43fb4f7913d4a44b87312',
                                                   640),
              '_kui_vmu_native_passive_arm': ('65de0a633a9dd9646fade78613027d235407608268f0bb4f26b8539d43168d8e',
                                              2136),
              '_kui_vmu_native_passive_complete': ('3cb4e5dc0c8b9956124e38225f99aa7201eec730c48d623ee2fc6f3e7f92c629',
                                                   3144),
              '_kui_vmu_native_passive_submit': ('a04e49d3bf03cf65f9e1009ed6bc4e902d709eb6f99fbec063a3505ecbdfa2c6',
                                                 1428),
              '_kui_vmu_native_readonly_release_check': ('6454c1083bb5d050bb55f71ecc724696a09e00a061d6bbf6af949622f1a31f60',
                                                         728),
              '_kui_vmu_native_runtime_fault': ('172696a55694bc257d0155c40cf999641f1de6ef3b216479339f4f82b0d720c2',
                                                332),
              '_kui_vmu_native_sdk_coherence_prepare': ('52ca590c4ac48e91620888af088aa871c0dfa4476cc386a73a828fc4af94ccbc',
                                                        436),
              '_kui_vmu_native_terminal_dispatch_span.constprop.0': ('843a0460a5268fd47a94404e6a0df8e6269c0a7049aa71a6d32196e25c47d2ea',
                                                                     128),
              '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': ('1638ca28b7b5249584c59c425f10f78418c2740a45f325471b7eeee043512ca6',
                                                                       100),
              '_kui_vmu_native_trace_enabled': ('0a920c3cf23dad2c3aabfe188567a6d1425af67dd54b46b619555831e9748eac',
                                                16),
              '_kui_vmu_native_trace_failure': ('bc5de95cdeaf5c8bbb975302655c91ac13a6e8058655665651019a647c655931',
                                                120),
              '_kui_vmu_native_trace_frame_entry': ('24be2fc972ae425d21e9fd5a0b8ad551f1f64bc20c8d7cb9118c00818bba1111',
                                                    164),
              '_kui_vmu_native_trace_game': ('367d93d9d799bac69baf843535f58f139cac582df3d494ff5e4e4721ece346e3',
                                             112),
              '_kui_vmu_native_trace_image': ('e85462a3e477dee0a0124220436dc355360013e47136f21eb8576af465cdd9a9',
                                              60),
              '_kui_vmu_native_trace_init': ('cb04d77bf380406ced6994e9daf684e88aac772eaf5dbb91c93d6e2063ba07a8',
                                             96),
              '_kui_vmu_native_trace_irq': ('31b72b692190f12dabe3bee1767a9d43913918730d08b7c7c802bea816c8177e',
                                            14),
              '_kui_vmu_native_trace_point': ('47f46b3337fb28b3281e2b5d56eacd66476fd830abdf74f3a1f67dc1068fae0b',
                                              1132),
              '_kui_vmu_native_trace_report': ('b86c86895758f7040363c03ba794100a70c5d8e8ac1b689886aba23245093675',
                                               1040),
              '_kui_vmu_native_trace_wait_after': ('36746417db83b1f0060aacf65c6ad9fb76ce55aea985779d0d1244b70c6f30e6',
                                                   180),
              '_kui_vmu_native_trace_wait_before': ('e9b3f06c9f25ba26990d028291ee7c37f06e4fae6df7069ac0086d316321b5d9',
                                                    152),
              '_kui_vmu_native_trace_wait_loop': ('14823a20fee8b883e5f8e7ebd6e7e10af9715be40c12e4674d8fa89f2839adcd',
                                                  176),
              '_kui_vmu_readonly_overlay_complete': ('d4fc62020bef09ab9056b5b7a16c8a47c616c446142d770d6a9e801ecf677601',
                                                     1004),
              '_kui_vmu_readonly_overlay_complete_absent': ('569ef7dcb97f1904c68ce440dd9a6bc3c0d143da123c0b8d37dc8c55da4f0a60',
                                                            284),
              '_native_loader_snapshot': ('9592778ad157ebdd8b7f2ad0fd3efe92e05b17fc78c30d61ee6a0eba1c2410a0',
                                          368),
              '_native_loader_timeline_row': ('724cbf615ec27fc94cb0393f2a2639ff8478c50ae6f413d33328c98322ac575a',
                                              144),
              '_native_phase_span_from': ('9388e07ec7ac50ab32407f0403a654d8dae3fd66634f9fc2c16ec10fe178984e',
                                          128),
              '_normalize_heap_dispatch': ('4b106db6f1dab52944d90c8dab29473ea496292f4e92fbdfc229ae69dcdecc56',
                                           276),
              '_persistent_context_guard': ('c64d3f0b1e8a23c587834effc7697d01d17a20b0a2032a0fe4713d6b69bbdf77',
                                            128),
              '_persistent_lease': ('8658f150d0d4811335e46332a9a075f199fa8219edb5ccf1730223a4e34d78e3', 148),
              '_persistent_storage_guard': ('c51bd14af15bb22af8ad0a384c1c700a5c23a90fcd25e012ad977b7e7b77fa04',
                                            396),
              '_poll_lease_valid': ('e129f554365b5074909c9ebe421607bd74a96a0694cba17fbd1b9c2b488c8473', 168),
              '_prepare_hook_entry': ('e46707ed62da6217391c4aee61d074980abd67826655c1b508159548cabefd74',
                                      228),
              '_read_heap': ('b99a77eee8f04769ddb256eccf74192b7530bcd6cd856974d1fc48132299213f', 44),
              '_read_heap_raw': ('b102f5d502f2a2e45998b1555e7bd6e56d2b7048059ebb9bb37ed6ee415a58cd', 104),
              '_reader_capture_region': ('807f414ae76d5870a482e7952706e56e9f652c5168b3feb8db4316a4f827406c',
                                         100),
              '_reader_cooperative_service': ('893c8094dbf3c0c0924711f02b67c739ad8f69c7286c21a93cc60d347be90e68',
                                              32),
              '_readonly_overlay_increment': ('e3a6368a10593e4ae7510faf736d644a9d9e383e262070923241741b3f6893b1',
                                              14),
              '_readonly_overlay_le32': ('1daa47eecc292df8356826c0e5a683e3a2c27cec212f901db7b1f01cf3b897df',
                                         30),
              '_readonly_overlay_parent_response': ('d396e68edd3158708ba5a8c9ff5235821bb62b71cc9dc40650cadb9098eaeb12',
                                                    116),
              '_report': ('989b490b6f46bbfac1d4b00402439c05b750a5bec04acddff237844b4fe5d8b0', 440),
              '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
              '_trace_history': ('0faf72c76a352bc9c6cca0661ff88dc38b36a34f09361155811464fc6422f773', 164),
              '_trace_packets': ('54daa81d42d03a20ab72ae8d79ed39182bf17f1e7a5e1180b9efb859cad50448', 116),
              '_trace_row.constprop.0': ('9d55c81d38773acb609b68c1d6684dfc6be503622f4078799864c49148f5426d',
                                         92),
              '_transfer_context_valid': ('ee36620e038fadc8a93fe17c27727ff2ad7fd0a8c8a2f3d387f9e309072f5924',
                                          1180)}}

# The bottom-lease installer and worker keep the preceding observer/callback
# contracts. Its separately reviewed link moves typed literals in otherwise
# identical bodies. These maps are filled only after that complete comparison;
# the actual allocator transaction is a separately required instruction proof.
# Exact bottom-heap/private-pool/poll-gate linked maps. The actual predicates,
# new gate and compiler-rescheduled release wrapper have separate instruction
# evidence; unchanged bodies retain exact bytes or reviewed typed literals.
BOTTOM_STAGE_PINS = {'dual': {'_select_resident': (272, 'ff04cf036eba683ae9ffb331d76738639de7a046f532e635f86b9c2932acafe3'),
          '_native_dual_reader_bind': (68,
                                       '04ef3b1fdcdb4a7d0f9951209af9465ae516ca672125b37d8041a0395c33e23f'),
          '_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c')},
 'sdk': {'_native_sdk_sci_route_bind': (952,
                                        'aa4e5ba8c381a558face4224b74f57fe95336edf5555fd183d39522522a53df5'),
         '_kui_native_sdk_sci_route_admit': (796,
                                             'f99d69b4a8977d5dcf2cbc0fbc8bdfe4736ab14688f6ec338b66b2c3f24a9173'),
         '_kui_native_sdk_sci_route_discover': (1856,
                                                'd4d3025c655ff64d62be3cb99127236c52987e327c499d133cf8141a2b6bdc70'),
         '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
         '_fingerprint': (88, '6b308b9169903ffb9ba47a4ebdbfb017602db6571613133b82fb823ba5a73921'),
         '_live_words': (80, '086aa5c615fa7ceda4b39fc16df0ec561bdf56bbb4d739940b8325f84bc64a19'),
         '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
         '_normalized': (192, '859b984ecb6d62297c62b64a22f6fa88c11c04783e30d9fe9f91d26903ec370a')},
 'read_ahead': {'_native_readahead_bind': (1260,
                                           'dd404775cb1b3c05aa842327ce2457807b10cb70c1daad0ac0fb3ff242eb8792'),
                '_kui_vmu_native_background_map_address': (92,
                                                           'cd9d8aba83db8374543f4850bf079245710ba3854dbf128c98f20c890a9753c8')},
 'timeline_binder': ('743a44087cc4ba782cc765f32d463120bd7e3034dd65bc13ae1a92a1a97afd49', 180),
 'installer': frozenset({(2004, '45faee7a309e84b762aa3afed516f0683da1cc6370148d6018f09cccdf19bfaf')})}
BOTTOM_WORKER_PINS = {'dual': {'_reader_readahead_exports': (200,
                                        '13ca433d369112a4b3184b17ec96e025a76e142e537d65c8d3bba2c6e9ce4d5e'),
          '_reader_capture_region': (100, 'f46dc671c3f853a40f735778e6e4d841b869e7a84d2ba92be85aca4e1e3870ed'),
          '_reader_readahead_visit': (56,
                                      '277ea46f6d7d509e57e0be251e5f2e7f12e3193f8d9549d961cae44fc1b8bbb1')},
 'read_ahead': {'_reader_readahead_exports': (200,
                                              '13ca433d369112a4b3184b17ec96e025a76e142e537d65c8d3bba2c6e9ce4d5e'),
                '_reader_readahead_visit': (56,
                                            '277ea46f6d7d509e57e0be251e5f2e7f12e3193f8d9549d961cae44fc1b8bbb1'),
                '_reader_cooperative_service': (32,
                                                'ddc28cc212c9261a5c5261056a3bfca676663a46e347c0ea8e116acfa642148e'),
                '_reader_readahead_failure': (172,
                                              '5c6e08b53ee6155a6ddc698eb19bc2f84198265b1a13a44535e7711612b5781e')},
 'witness': {'_failure_witness.isra.0': (16,
                                         '9752e82987994a19f8731a90d3b4535ad753e1b665a1a545be8978d4ee8e2280'),
             '_poll_lease_valid': (168, '0f7e1c830251aa4def6d855319893a96289a625f98b3fe1faa2c9ed873138311'),
             '_controller_context_valid': (1336,
                                           'f16c718a47e91f5088b46953cffb8bd7aa50cce54d9ef3b23682369dda25c48a'),
             '_kui_vmu_native_passive_complete': (3144,
                                                  '42e4aedebd7c2d69e97a9219256f12827d57fb5c2185e7640124040a35385176'),
             '_kui_vmu_native_passive_submit': (1428,
                                                '7acadc803779a90ba75f9578672284473f83e09dac6a2654c95c0848ac8064b4'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_readonly_rx.constprop.0': (76,
                                          '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, 'a3bffefb714c3fd498142d7497a5604494489efe5ff57b29bd0df0c14763fc7f'),
             '_installed_hooks_valid': (128,
                                        '45d892df98fb230832b9ebfd61c585fa96712168a09eca763a6b73ec168cfd5f'),
             '_normalize_heap_dispatch': (276,
                                          'd44506592fed307c3ea37afb1019bd77045bb00993a088e30a699396300cc946'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128,
                                                                    '697c229576a1d5b73015e2f734462614c2d843c139ca64c0dae338a324f20cf2'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (100,
                                                                      '3b9061514a00b238b9ff8f275c8e411017b50592834bbdb73f180dd18963116c'),
             '_kui_vmu_native_patch_encode': (168,
                                              '9ab6c8f3a9c74bb0250b8ca3f931ea5a453f4215570a40ee5810153628c87e01'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_kui_vmu_native_trace_point': (1132,
                                             'a1efbe4acef96bfe9d7c31679e9c8fa36fdb5500f4987a3b5f2c490969614af0'),
             '_reader_capture_region': (100,
                                        'f46dc671c3f853a40f735778e6e4d841b869e7a84d2ba92be85aca4e1e3870ed'),
             '_reader_cooperative_service': (32,
                                             'ddc28cc212c9261a5c5261056a3bfca676663a46e347c0ea8e116acfa642148e'),
             '_reader_readahead_exports': (200,
                                           '13ca433d369112a4b3184b17ec96e025a76e142e537d65c8d3bba2c6e9ce4d5e'),
             '_reader_readahead_visit': (56,
                                         '277ea46f6d7d509e57e0be251e5f2e7f12e3193f8d9549d961cae44fc1b8bbb1'),
             '_kui_vmu_native_timer_callback_owned': (92,
                                                      'b6498e6100e639a321f46b4ebfe7e0df56af92051f0b2917b8a55df00ae2d33d'),
             '_timer_runtime_ram': (104, '4fb7919a6f74ec1c9aa3f434e446b352f7961eb7c565580f0c4e2e52a3eff415'),
             '_timer_runtime_in': (26, 'a0ff5d4d27ccbf7b073b8bc6f0e6b26659e6b729763f7b02d49aa124ea023824'),
             '_normalize_heap_dispatch_if_routing': (40,
                                                     '0cdc607ae34c612106a56685cd43cd903ea190c2e290eafce2ace2dab2825369')},
 'live_boundary': (216, '3038bff49df0d0bfc5d03f50fb294929b70cf4c6831cefb5646556909d87b7b4'),
 'timeline_division': ('7d62c701f21e400b67d3f1f3bda97d8cf24f02d9349d4ab2a94311fc09e1d4b1', 144),
 'raw_owner': {'_diagnostic_complete': (3610,
                                        '6e089704293ac65b92d1e5fd8c17404abd3fa0112152302d777b56f45c306def'),
               '_native_phase_span_from': (128,
                                           '2f2d811580469162b3f1b715592e0cf3c96f78a0ee86ac80b6bd7b9a1be09d1f'),
               '_diagnostic_phase_read': (136,
                                          '300072e2b8bad5d5aa1ac8e52af5bfe1c52c1b256541ff3281f449642641533a'),
               '_diagnostic_end': (1492, '74077b72ea7e69fe893dd02484b1a15b343d6001e5e3b10b82e0e595dd7140fa'),
               '_diagnostic_sample': (164,
                                      'd16961b66de244b6a2330a5f13018841d570addc52e2d5cca7f50bde1599e6f7'),
               '_memset': (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a'),
               '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634')},
 'prepared': {'.rodata': ('13b89fe6170b659c1165b2f6f9c44d1ad45b7ac572baf1cb1cafb64893bfc4b1', 3956),
              '_controller_context_valid': ('43d5b6f1aee195f92cd33672814eff6b8fff1bca07c86b921846b90c29204c2b',
                                            1336),
              '_diagnostic_begin': ('3be33ddc079ec9db0760339daf470b64f4903769d66dc830e14c2fcd0008ccc1', 40),
              '_diagnostic_complete': ('5433acef74fd525985ac8d0d4023b021fe8eda769a07801487300f67678aed21',
                                       3610),
              '_diagnostic_end': ('7cd314083d85f7299e6f540a6c294b82f54dc5e546fb0bc28492e9b866fc734e', 1492),
              '_diagnostic_gesture': ('798c8a977a6e0b1e49fca8135d6a944513c0e2c8cf5c9342f0dd0f5f291f828f', 34),
              '_diagnostic_pair_begin': ('1b958dcf67fba5c38737817051a4131f3d026618b78de2f4c90be5caca3208d2',
                                         504),
              '_diagnostic_phase_read': ('9be746502cbe97b305edc7c282c70dd68d3b6d43dfafea092fecd4d7b975fcfc',
                                         136),
              '_diagnostic_phase_row': ('ca25d712e430b3f140fec46bf465357a682ff172fe907efebcce9c4abe13c58c',
                                        16),
              '_diagnostic_sample': ('cb7e980cb399fa654e837a6896d3ec033fd9d844c8fd0a2c31c77399471411fe', 164),
              '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7',
                                          16),
              '_hook_word': ('69410002bf135878c32df63d2459603428947eadc760aa6309775f82b8801f9c', 132),
              '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56',
                                         128),
              '_kui_native_loader_timeline_add': ('4d6b8073d0a1fd8de7359127083bbf20843381ed99df20a2d1c3c1f8b80d5b26',
                                                  856),
              '_kui_native_loader_timeline_reset': ('3e45ddcf599ecf09701486d6f30bc38a34bd5122e36349922416f6ee6d07ff0c',
                                                    24),
              '_kui_native_loader_timeline_visit': ('eb70c2499182c9ba9d638aa8c931cb768c8459630e56f057575d9e923e619f5c',
                                                    148),
              '_kui_vmu_native_fault_capture_install': ('db26024d38eaa6e0c440fdadf83df04c43a91a653ab21637e427502550726afb',
                                                        560),
              '_kui_vmu_native_lifetime_prepare': ('4691642d40ae532e2f81cfe46c3f81a019412c0da0e43fb4f7913d4a44b87312',
                                                   640),
              '_kui_vmu_native_passive_arm': ('65de0a633a9dd9646fade78613027d235407608268f0bb4f26b8539d43168d8e',
                                              2136),
              '_kui_vmu_native_passive_complete': ('3cb4e5dc0c8b9956124e38225f99aa7201eec730c48d623ee2fc6f3e7f92c629',
                                                   3144),
              '_kui_vmu_native_passive_submit': ('a04e49d3bf03cf65f9e1009ed6bc4e902d709eb6f99fbec063a3505ecbdfa2c6',
                                                 1428),
              '_kui_vmu_native_readonly_release_check': ('c862e5603cc9bd47d2a169d76d18988184402ceeb86c453ad12059da5cc5e810',
                                                         712),
              '_kui_vmu_native_runtime_fault': ('172696a55694bc257d0155c40cf999641f1de6ef3b216479339f4f82b0d720c2',
                                                332),
              '_kui_vmu_native_sdk_coherence_prepare': ('52ca590c4ac48e91620888af088aa871c0dfa4476cc386a73a828fc4af94ccbc',
                                                        436),
              '_kui_vmu_native_terminal_dispatch_span.constprop.0': ('843a0460a5268fd47a94404e6a0df8e6269c0a7049aa71a6d32196e25c47d2ea',
                                                                     128),
              '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': ('1638ca28b7b5249584c59c425f10f78418c2740a45f325471b7eeee043512ca6',
                                                                       100),
              '_kui_vmu_native_trace_enabled': ('0a920c3cf23dad2c3aabfe188567a6d1425af67dd54b46b619555831e9748eac',
                                                16),
              '_kui_vmu_native_trace_failure': ('bc5de95cdeaf5c8bbb975302655c91ac13a6e8058655665651019a647c655931',
                                                120),
              '_kui_vmu_native_trace_frame_entry': ('24be2fc972ae425d21e9fd5a0b8ad551f1f64bc20c8d7cb9118c00818bba1111',
                                                    164),
              '_kui_vmu_native_trace_game': ('367d93d9d799bac69baf843535f58f139cac582df3d494ff5e4e4721ece346e3',
                                             112),
              '_kui_vmu_native_trace_image': ('e85462a3e477dee0a0124220436dc355360013e47136f21eb8576af465cdd9a9',
                                              60),
              '_kui_vmu_native_trace_init': ('cb04d77bf380406ced6994e9daf684e88aac772eaf5dbb91c93d6e2063ba07a8',
                                             96),
              '_kui_vmu_native_trace_irq': ('31b72b692190f12dabe3bee1767a9d43913918730d08b7c7c802bea816c8177e',
                                            14),
              '_kui_vmu_native_trace_point': ('47f46b3337fb28b3281e2b5d56eacd66476fd830abdf74f3a1f67dc1068fae0b',
                                              1132),
              '_kui_vmu_native_trace_report': ('b86c86895758f7040363c03ba794100a70c5d8e8ac1b689886aba23245093675',
                                               1040),
              '_kui_vmu_native_trace_wait_after': ('36746417db83b1f0060aacf65c6ad9fb76ce55aea985779d0d1244b70c6f30e6',
                                                   180),
              '_kui_vmu_native_trace_wait_before': ('e9b3f06c9f25ba26990d028291ee7c37f06e4fae6df7069ac0086d316321b5d9',
                                                    152),
              '_kui_vmu_native_trace_wait_loop': ('14823a20fee8b883e5f8e7ebd6e7e10af9715be40c12e4674d8fa89f2839adcd',
                                                  176),
              '_kui_vmu_readonly_overlay_complete': ('d4fc62020bef09ab9056b5b7a16c8a47c616c446142d770d6a9e801ecf677601',
                                                     1004),
              '_kui_vmu_readonly_overlay_complete_absent': ('569ef7dcb97f1904c68ce440dd9a6bc3c0d143da123c0b8d37dc8c55da4f0a60',
                                                            284),
              '_native_loader_snapshot': ('9592778ad157ebdd8b7f2ad0fd3efe92e05b17fc78c30d61ee6a0eba1c2410a0',
                                          368),
              '_native_loader_timeline_row': ('724cbf615ec27fc94cb0393f2a2639ff8478c50ae6f413d33328c98322ac575a',
                                              144),
              '_native_phase_span_from': ('9388e07ec7ac50ab32407f0403a654d8dae3fd66634f9fc2c16ec10fe178984e',
                                          128),
              '_normalize_heap_dispatch': ('4b106db6f1dab52944d90c8dab29473ea496292f4e92fbdfc229ae69dcdecc56',
                                           276),
              '_persistent_context_guard': ('c64d3f0b1e8a23c587834effc7697d01d17a20b0a2032a0fe4713d6b69bbdf77',
                                            128),
              '_persistent_lease': ('8658f150d0d4811335e46332a9a075f199fa8219edb5ccf1730223a4e34d78e3', 148),
              '_persistent_storage_guard': ('c51bd14af15bb22af8ad0a384c1c700a5c23a90fcd25e012ad977b7e7b77fa04',
                                            396),
              '_poll_lease_valid': ('ac832b82ea8d821dede21c5e9e825e9f7a4479be1dae662c525cdd4f92fb64a8', 168),
              '_prepare_hook_entry': ('e46707ed62da6217391c4aee61d074980abd67826655c1b508159548cabefd74',
                                      228),
              '_read_heap': ('c62b9300537310eef59a5369cd8283b59a64a55631de283ea33af52128069783', 44),
              '_read_heap_raw': ('b102f5d502f2a2e45998b1555e7bd6e56d2b7048059ebb9bb37ed6ee415a58cd', 104),
              '_reader_capture_region': ('807f414ae76d5870a482e7952706e56e9f652c5168b3feb8db4316a4f827406c',
                                         100),
              '_reader_cooperative_service': ('893c8094dbf3c0c0924711f02b67c739ad8f69c7286c21a93cc60d347be90e68',
                                              32),
              '_readonly_overlay_increment': ('e3a6368a10593e4ae7510faf736d644a9d9e383e262070923241741b3f6893b1',
                                              14),
              '_readonly_overlay_le32': ('1daa47eecc292df8356826c0e5a683e3a2c27cec212f901db7b1f01cf3b897df',
                                         30),
              '_readonly_overlay_parent_response': ('d396e68edd3158708ba5a8c9ff5235821bb62b71cc9dc40650cadb9098eaeb12',
                                                    116),
              '_report': ('989b490b6f46bbfac1d4b00402439c05b750a5bec04acddff237844b4fe5d8b0', 440),
              '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
              '_trace_history': ('0faf72c76a352bc9c6cca0661ff88dc38b36a34f09361155811464fc6422f773', 164),
              '_trace_packets': ('54daa81d42d03a20ab72ae8d79ed39182bf17f1e7a5e1180b9efb859cad50448', 116),
              '_trace_row.constprop.0': ('9d55c81d38773acb609b68c1d6684dfc6be503622f4078799864c49148f5426d',
                                         92),
              '_transfer_context_valid': ('ee36620e038fadc8a93fe17c27727ff2ad7fd0a8c8a2f3d387f9e309072f5924',
                                          1180),
              '_normalize_heap_dispatch_if_routing': ('54551c3ce68e045933e39c229955468e2bee6ead6fe9766f42935d1593309802',
                                                      40)}}


def marked(image, name):
    marker = image[4].get(name)
    if marker is not None and marker != ABSOLUTE_ONE:
        raise ValueError('Follow-up capability is not exact absolute one: ' + name)
    return marker is not None


def callback_profile(stage):
    return marked(stage, CALLBACK_MARKER)


def raw_owner_profile(worker):
    present = marked(worker, RAW_OWNER_MARKER)
    if present:
        from check_native_controller_failure_witness import owned_timer_profile
        if not owned_timer_profile(worker):
            raise ValueError('Terminal owner observer requires its owned-timer worker')
    return present


def bottom_stage_profile(stage):
    present = marked(stage, BOTTOM_STAGE_MARKER)
    if present and not callback_profile(stage):
        raise ValueError('Bottom-heap stage requires selected low-reader callbacks')
    return present


def bottom_worker_profile(worker):
    present = marked(worker, BOTTOM_WORKER_MARKER)
    from check_native_general_lean_profile import worker_profile
    if worker_profile(worker):
        if not present:
            raise ValueError('Lean General worker lacks its bottom-heap profile')
        return True
    if present and not raw_owner_profile(worker):
        raise ValueError('Bottom-heap worker requires its terminal owner observer')
    return present


def stage_pins(stage, role, historical):
    from check_native_general_lean_profile import stage_profile,stage_pins as lean_pins
    if stage_profile(stage):
        return lean_pins(stage,role,historical)
    if bottom_stage_profile(stage):
        selected = BOTTOM_STAGE_PINS.get(role)
        if not selected:
            raise ValueError('Bottom-heap stage lacks independently reviewed pins: ' + role)
        return selected
    if not callback_profile(stage):
        return historical
    selected = STAGE_PINS.get(role)
    if not selected:
        raise ValueError('Selected-callback stage lacks independently reviewed pins: ' + role)
    return selected


def worker_pins(worker, role, historical):
    from check_native_general_lean_profile import worker_profile,worker_pins as lean_pins
    if worker_profile(worker):
        return lean_pins(worker,role,historical)
    if bottom_worker_profile(worker):
        selected = BOTTOM_WORKER_PINS.get(role)
        if not selected:
            raise ValueError('Bottom-heap worker lacks independently reviewed pins: ' + role)
        return selected
    if not raw_owner_profile(worker):
        return historical
    selected = WORKER_PINS.get(role)
    if not selected:
        raise ValueError('Terminal owner observer lacks independently reviewed pins: ' + role)
    return selected
