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
# Re-pinned after the a25580bc residual-lag review. The early driver sends
# TICK arg1 for one complete optional block; arg0 retains the timed pair.
# The shared 200-byte auxiliary-header getter is now outlined and explicitly
# pinned/executed. Only cooperative service and terminal report change worker
# instructions; 115 other bodies preserve 591 typed pointer bindings. Stage
# instructions are unchanged (543 typed relocations); the auxiliary changes
# only its producer loop bound and event dispatcher (17 other typed pointers).
# The terminal adds one authored 24-byte caption. Existing ownership, CRC,
# stream guards, private layouts and historical noncompact pins stay exact.
# Re-pinned after the dc9fc5e4 copy/maximum-context review. The worker
# changes only its held-report field load and same-sized caption; all other
# 174 worker functions are byte-identical. All 400 stage functions preserve
# their instructions (538 typed pointer relocations). The auxiliary changes
# only cache_append's guarded directional word copy and dispatch's diagnostic
# maximum-context store; nine other bodies preserve 15 typed pointers.
# Actual linked copies and context updates remain mandatory proof premises.
# Re-pinned after the 33db22c6 direct syMalloc 1.35 compatibility review.
# Worker semantic changes are limited to metadata, identity and cold lifetime
# preparation; 109 other changed bodies preserve 518 typed pointer bindings.
# All 1200 previous constant bytes preserve seven pointer targets, with only
# the independently authored 16-byte shape table added. Bottom geometry is
# now inlined: the compact closure keeps eight functions plus that exact table;
# historical noncompact nine-function closures remain unchanged. The stage
# adds direct discovery/code guards and exact control geometry, preserves the
# prior dispatch-only executable reclamation boundary, and verifies bounded
# post-RTS pointer pools. Full linked comparisons bind all other instructions
# and complete constant bytes. Original startup-derived compact/disc leases,
# linked guards, table mutations and the complete-audit refusal stay mandatory.
# Re-pinned after the 24216904 preheap-clear lifetime review. Only the
# normal heap publisher's installer selection and relay's prepare/arm calls
# change existing stage instructions. All 286 other shifted stage bodies keep
# 1528 typed pointer bindings; all 12200 previous constant bytes keep 21 pointer
# targets, with 76 authored path/helper/digest bytes added and exactly two zero
# alignment bytes moved. Six new stage-only functions implement the guarded
# split clear and tail retirement. Worker and both resident loadable payloads
# remain byte-identical; no retained geometry/state grows. The new closed
# preheap map requires actual original+linked execution and refusal evidence.
# Re-pinned after the shared early/terminal SDK expansion. Independent complete
# linked comparisons preserve 102 worker functions byte-for-byte and 61 through
# 152 typed relocations; ten source-owned semantic changes and the exact 20-byte
# descriptor table are separately reviewed. The stage preserves 114 functions
# byte-for-byte and 276 through 1361 typed relocations; all prior constant bytes
# retain their roles, with 384 authored additions and two relocated zero bytes.
# Two preclear span literals bind the actual stage start/BSS-end pair. All worker,
# cache, stack and lease sizes remain fixed. New descriptor/table and pointer-pool
# companions supplement the mandatory full original-eight and preclear proofs.
STAGE_PINS = {
    'cache_gateway': {'_native_resolution_replay': (312, '6b4dffdf71c478bc8e4ccae622984a78938ba632eb5fabb42e5b003b32860de7')},
    'dual': {
        '_kui_native_dual_u32': (30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
        '_native_dual_reader_bind': (68, 'da7ebf01a29924abb4677c4504551ad70b3be465a48a08b95a5e82d384cb9458'),
        '_select_resident': (272, '7acdd4cca82c3258c57b41fa3d88a88574ed7995d484eafd95f41588fed74f3c'),
    },
    'installer': frozenset({(2084, '329eb484ab743952f5e2a82df21f278948351173ffd6b1b98ce362a365ad68b9')}),
    'read_ahead': {
        '_kui_vmu_native_background_map_address': (92, '14f07de519d8e507e8d2a6d69cdce033ce605aae4773114e5cbfb3fdad56163c'),
        '_native_readahead_bind': (1244, 'eb683d26f999b7f068a0a6c86fccf0fc69731a9d510b5ba692ea930f296dfdb9'),
    },
    'sdk': {
        '_fingerprint': (88, 'f1848a598997ad838a6f5cdf99e73687fed5dd5797ffd86204e748a99b347f6c'),
        '_kui_native_sdk_sci_route_admit': (796, '7af6b1afbfadbc5a5b13df199c56e748c67a8c96f3df08bf71d7a233b0b93ed7'),
        '_kui_native_sdk_sci_route_discover': (3264, '61b283477ceac1122f26ac9a896a91b61bcbc9f18975084de1105fb985604724'),
        '_literal_at': (62, '1712d6126cb969c7f2f21fb2de718960809b1782d1b39a06f772a21af78e16fe'),
        '_live_words': (80, '27ee713952c69e9f6ae0b4c9c07b043b342eb7973a4824b3fc85efb8854bdf74'),
        '_native_sdk_sci_route_bind': (944, 'eecf5d00238a07071ae63837a3d4507a1224b86d5925e2456b6bf059b15df515'),
        '_normalized': (196, 'a875be734097c3fb21ad0df7ec9e4add00a95c1a02a3de6e2fef46d851748255'),
        '_u32': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
    },
    'timeline_binder': ('d5f9efcb455cf3a783276277e193121041cb455a8f99f6ffdc5a31ed7f8bc055', 180),
    'toggle': {
        '_native_passive_run': (2084, '329eb484ab743952f5e2a82df21f278948351173ffd6b1b98ce362a365ad68b9'),
        '_native_persist_storage_init': (720, 'dfbf996835a7e6584b8d9a870b571777cd1c49c941e496fb3457c456fbdaa0bb'),
        '_native_runtime_diagnostics': (44, 'c9a61f703e171beef525675a5240072c91571c92a1646a7365d7c5e37f7f7be1'),
        '_native_runtime_mode': (88, '492499f13bb7cfd84e2a9dc9a0052c5ff0be71a60ada43c98b9d16c8695b0d85'),
    },
}
WORKER_PINS = {'dual': {'_reader_readahead_exports': (200, '44b87dfb956f27acbbb9f02aa1e86d96720acc7c3262286ffa95ae9eec8ab5ec'),
              '_reader_capture_region': (100, '8397abc7e9de325430e8fd5b0a90d086c1059686cc8a3ecbe035cb19981f8dd7'),
          '_reader_cooperative_service': (100, 'f6296ec62984e431f02e875bedb9b95748e17cb3ada78af911d89716c2093067'),
          '_reader_streaming': (120, 'cf5e1151bbc936b7798bcbc51e64f3d39dca78d24f3db85aaa7a3646516f4a0b')},
 'live_boundary': (216, '370bea26b5df1f7cb072f087c7d05d97cd42fe1caeacf27f6abe7c087fb9db10'),
 'prepared': {'_reader_readahead_exports': ('49e15d97e9fe67abc588055ead17661c09756ae58447ffe6123db3f8231ba587', 200),
              '.rodata': ('ebb4fff35b9c2fcdf970706d3030db4409e37619f9c61fc89a1b31852130f94c', 1236),
              '_chain': ('a6e698a83bf0309d2a08e5ed2953fcb0f9ea2f6685a8fdde85bb8eb17cf8ed29', 908),
              '_controller_context_valid': ('43d5b6f1aee195f92cd33672814eff6b8fff1bca07c86b921846b90c29204c2b',
                                            1336),
              '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
              '_hook_word': ('69410002bf135878c32df63d2459603428947eadc760aa6309775f82b8801f9c', 132),
              '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
              '_kui_native_loader_timeline_visit': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83',
                                                    4),
              '_kui_vmu_native_lifetime_prepare': ('51a7cf0ccf76c9ce4ab5d7395ee3fa12dad3e8a1435bf63cfed5e3d7896c2532', 752),
              '_kui_vmu_native_passive_arm': ('efbc849773b91b9b07067775904ffedc1b6d08b01967c846be7cc59a2c998550', 2644),
              '_kui_vmu_native_passive_complete': ('f1c0d46e5916f3fa6baad0290fe3ab87b4ef4fdc53a5d38e592ab89bb2cf296c', 4108),
              '_kui_vmu_native_passive_split_submit': ('66b64139d18ceb7c56a646c6bdcdf45ca2349f6fb15038552ef8a689be38488b', 916),
              '_kui_vmu_native_passive_submit': ('08332141fa19ba049c32ae75f83b7b6936d303ee25357d4ffd6d623dd90b17d9',
                                                 1272),
              '_kui_vmu_native_readonly_release_check': ('0b7e53dfcc8c3b384e8b33b8d874ced89398c26ca02282fc23fb70411ad6c708',
                                                         640),
              '_kui_vmu_native_runtime_fault': ('8f8a1f7ed293ad4e54b9c9178db239d79c0bf2485bc777209299c5c2170b75d4', 308),
              '_kui_vmu_native_sdk_coherence_prepare': ('dc45cffbaf7faf192ee76278151f403e17e1946afe78c59cb3e03d14753699cc', 420),
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
              '_reader_cooperative_service': ('17975bf4c258a07eeaaf7638e5cab6b690aff3ef6c7cba40b2b47296152748bf', 100),
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
              '_report': ('a854b68fa4fa84c3a2dd8506072c9e981e7deecbf8196ba7934e2a6331815931', 412),
              '_stack_valid': ('dd348cfbf4d1379d905dcd08aed813db8e2df61b1ce3cf76edfee604bc5d5d26', 116),
              '_transfer_context_valid': ('aab411111978626c85df1395d22f1895a5eca778cb3f07c7b31b44cf272810ff', 1216)},
 'read_ahead': {'_reader_readahead_exports': (200, '44b87dfb956f27acbbb9f02aa1e86d96720acc7c3262286ffa95ae9eec8ab5ec'),
              '_reader_capture_region': (100, '8397abc7e9de325430e8fd5b0a90d086c1059686cc8a3ecbe035cb19981f8dd7'),
                '_reader_cooperative_service': (100, 'f6296ec62984e431f02e875bedb9b95748e17cb3ada78af911d89716c2093067'),
                '_reader_streaming': (120, 'cf5e1151bbc936b7798bcbc51e64f3d39dca78d24f3db85aaa7a3646516f4a0b')},
 'toggle': {'_kui_vmu_native_passive_arm': (2644, '2afb8d5e27d7282adfba9465233532eafec60f10f7ad984bea36bbd55d2f2fe7'),
            '_kui_vmu_native_passive_complete': (4108, '7aa9004380633a1b895c01dba91ea4862a095058e4f252d6c58c677c91e2a6df'),
            '_kui_vmu_native_passive_submit': (1272, '981cf611097ba6ceea3eaec7dccf2e08abd1b693823d648677cb5093f9188cdd'),
            '_kui_vmu_native_persistent_sd_transaction': (100, '3778b0fd5cf00ddd36c77a7f48a5cdba33d7faba68549b1d15715fa56f3a88d1'),
            '_persistent_admit': (28, '97ba276bf3ca272aad553b24713ac9b33f8d2aa19b1cb1e88b2736c1d9eceeb6'),
            '_persistent_handler': (28, '2241afd6852db5227eb529e54c2eec96aba7bca8907ddf9a198c43feaaae633e'),
            '_persistent_lease': (108, '379dd45c355a03b92af0270c6553baf23ca90ffbf889a998901295bbf0c6783f')},
 'witness': {'_reader_readahead_exports': (200, '44b87dfb956f27acbbb9f02aa1e86d96720acc7c3262286ffa95ae9eec8ab5ec'),
             '_patch_ram': (80, '1327b2f301be78737ee539a4af7e5def8f100d6f05d998534324da10e1755f03'),
              '_controller_context_valid': (1336, '223bf4750f7fdcd8217eeef6f0c9c176eba331a32165f16e93f06514cd674ea9'),
             '_failure_witness.isra.0': (16, '192c9b1ef56e6cedbb31ecebe8de68dd4d4c0be25e270f057cb628f20e85fa89'),
             '_installed_hooks_valid': (128, '582faf212d9f5e7cebd8dd4b5812e761f8ada15982651dea8a1da0d103ff8acf'),
             '_kui_vmu_native_exit_shape_valid.constprop.0': (132, 'fc7c0c23cf1857dcd610ca4e460930a984d1e4e69262edcf0aca23cd562441ee'),
             '_kui_vmu_native_passive_complete': (4108, '7aa9004380633a1b895c01dba91ea4862a095058e4f252d6c58c677c91e2a6df'),
             '_kui_vmu_native_passive_submit': (1272, '981cf611097ba6ceea3eaec7dccf2e08abd1b693823d648677cb5093f9188cdd'),
             '_kui_vmu_native_patch_encode': (168, 'f83d84658d6629a32c5f259c338720c0c404522597f678d752de78857f1c40bc'),
             '_kui_vmu_native_terminal_dispatch_span.constprop.0': (128, '58a9cbc0e5b20cadd6163f18df70da4c74a8097a938952ed7f9c9fad771c4743'),
             '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': (108, '870640b1cbbd7622945531c4ded5a1e112a0f0a8b22a5a1bd89869e16a5875a6'),
             '_kui_vmu_native_timer_callback_owned': (92, '609df1c77b2fbf1e1e68267a8d47b312c859b00676824888bb335d4fe060781b'),
             '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
             '_normalize_heap_dispatch': (276, '3094ce20120344fcf1ffaa0dda2feb83faab402fda7c7cd7371f3f2d60363642'),
             '_normalize_heap_dispatch_if_routing': (40, '838ead82ca2753054d3d597cee76fb0b9787b82ea9393a41f490692470ca5b85'),
             '_poll_lease_valid': (168, 'e7c43259eeab30ad5b7f32af1ed2a7b4e2d777a4e0c4bebe912e04e2fd3c746a'),
             '_read_heap_raw': (104, 'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
             '_read_value': (180, '2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
             '_reader_capture_region': (100, '8397abc7e9de325430e8fd5b0a90d086c1059686cc8a3ecbe035cb19981f8dd7'),
             '_reader_cooperative_service': (100, 'f6296ec62984e431f02e875bedb9b95748e17cb3ada78af911d89716c2093067'),
             '_reader_streaming': (120, 'cf5e1151bbc936b7798bcbc51e64f3d39dca78d24f3db85aaa7a3646516f4a0b'),
             '_readonly_rx.constprop.0': (76, '8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
             '_stack_valid': (116, '41986e82fe131006c9a2345a399cb028b24ad6f4a28f863d3e8ed5e051336aa0'),
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
# The guarded auxiliary retains the exact context, geometry and TICK budget.
# Its aligned/directional cache copies and diagnostic maximum-context store
# are independently executed. All other instructions or typed pointer targets
# are unchanged. The subsequent endpoint optimization retains the complete
# wide validation pass and every overlap pair; 66 actual SH differential
# cases cover its narrow sums. Other auxiliary bodies are unchanged except
# four independently bound memcpy/memset pointer relocations. No expected
# pin is derived during artifact admission.
AUX_CODE_PIN = (6304, 'a7d86a4351018eaa34b0d4ea1af473586590f918332983c916d1d237de4e1cbb')
COLD_FUNCTIONS = {'_alias@vmu_native_stage_placement.c': (32,
                                         '5d5b341944aa4da08467cfca39b78f276224218bd3ed9fd094c99456668b974d'),
 '_allocation_safe@vmu_native_heap_probe_stage.c': (88, '091ae308940e352f4afd3d4906d1828ae02a95670714f1506b7f9dd0249ca64d'),
 '_allocator_state_excludes_stage@vmu_native_stage_placement.c': (144, '7dcb313bfc319d0eea56afdf845ba2241a35efed654390ba116cb49663f2a44f'),
 '_at@vmu_native_heap_budget.c': (92, '9fc319637475d7e299701bb831749ed08ec4fc27dada5b25c4a6ee6fc2446c90'),
 '_code_sha@vmu_native_heap_probe_stage.c': (104, '1f5e4099534f8a16221ddba0ed69696fe70d120e0babc6f0d8442e46787e873a'),
 '_control@vmu_native_heap_probe_stage.c': (28,
                                            '4f620eb71c257c62795afd94380b211231e6f90e5704bb14a15d2ebb75b74c45'),
 '_excludes_stage@vmu_native_stage_placement.c': (64, '5df1c12fb1d3cafebccfc20640b467c1eb71bb15099d4ffc3229f39e1c0de841'),
 '_fresh@vmu_native_heap.c': (116, '98d201297fb6247e983d74d8f4b2a052a786c642cc24f7ef70e09f73d7784bb5'),
 '_heap_bottom_code_span@vmu_native_heap.c': (88, '73876e20d034b1d2d8ba55be98ca30198304bb010525ed5cc51310ed21353a5c'),
 '_heap_bottom_halfword@vmu_native_heap.c': (58,
                                             'd66affe12d8e87985e0bbe094cc393649bad89f9e4494f55004afda5f193ab39'),
 '_identity@vmu_native_heap.c': (444, '6d4085e8ba2cae855d0579ff7b9f22979c46c62f7cdede61ee5811176bf11926'),
 '_index@vmu_native_heap.c': (100, '8a651ae66a002b0931e2d78ccebb94c851fe77c67b6121674a869b65a4563587'),
 '_kui_vmu_native_heap_bottom_allocated': (172, '0102661301396d186fdf29aa7a458eae8ab61d5e5180af1278e4f7dd7bdd80c0'),
 '_kui_vmu_native_heap_bottom_code_valid': (772, '855d803ae889b97122cfc201aca3086a8e9fae3371b41fbab30776cf8df65853'),
 '_kui_vmu_native_heap_bottom_fresh': (148, '6c6c56aea65e24f333dc97fd2a94b2e7acc4636f980d257601fd4330f1174bdf'),
 '_kui_vmu_native_heap_bottom_placeholder': (188, 'c3a66add8470a32d1da5dde1fe1e248f51d7c9712fa1848a209fb97ac9eab7db'),
 '_kui_vmu_native_heap_bottom_ready': (144, 'dd9a8f63cf282b4a66e1179b7156d64f297a9b7cd6653e2354b143ad6eef265c'),
 '_kui_vmu_native_heap_budget_prepare': (2370, '5978a090985cd9e33ead76b1c2c24400f9b659869fd3454909953be73c145e58'),
 '_kui_vmu_native_heap_fresh': (268, '4ee8f36a6886103e9d1ef899f354188b77e4a8cdb532dece0f9b3166447a8fb4'),
 '_kui_vmu_native_heap_lease_identity_valid': (76, 'bdc192599618b5d52049043b695ba6c9519d4df03082773b7e37e524d1f952a6'),
 '_kui_vmu_native_heap_lease_valid': (52, '53215d07ff3d39727d5f586de4dbbcf3dbd7a7c8b99a8699c39e591139d9d9e3'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512, '5d00db0193bd7ec585f3ae97ef6c7152fcedc257b1d45adc82e643a714a383c6'),
 '_kui_vmu_native_heap_metadata_valid': (12, 'a8b6b8f7ad8b8dcc1dad0bb970c014dddc6736b21266180c939bb68db4d8c139'),
 '_kui_vmu_native_heap_preallocate': (36, 'ce70e386b3998185117e5d2a4a1285e6f4231659f586dcc9b709707f19f1056b'),
 '_kui_vmu_native_stage_bottom_allocation_safe': (292, 'e8b03d63f534a2c920f5ebb924c27cfad7b8eb12433d20cdd58b66b9296eec2e'),
 '_kui_vmu_observe_stage_install': (444, '31ac75b9d37de89bbc2b38e618d3ce84aa003bc0a9fe64e8844a6b4d730be7b1'),
 '_le32@vmu_native_heap_budget.c': (30, '6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334'),
 '_long_literal@vmu_native_heap_budget.c': (128, '67d1d836715db6e9df3e8e6ec8e60844daf2b1145beca8f43400dd7c960894c9'),
 '_memcmp': (34, '3d7da64606fe412df2341ca8e1c2c56d14d3f4798465f1ec02f6d2994c6aefd9'),
 '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
 '_memset': (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a'),
 '_metadata@vmu_native_heap.c': (436, '2478dbd08aab2af1facc93fec1837143cbd0580512ee1ce193b22ed383e97eb4'),
 '_native_allocate_bottom@vmu_native_heap_probe_stage.c': (304, '4a2ee80dfd015659d3584276c24dce39aa39e1112f65c3bc8f30db7fa36de7d3'),
 '_native_allocator_unchanged@vmu_native_heap_probe_stage.c': (68, '838dba5320059f162ffd662e49bc5d969de154ee7da01011112a35c090501d1b'),
 '_native_budget_prepare@vmu_native_heap_probe_stage.c': (144, '4ab67b8babf0b75f7e10efb40359fa01a7865b3c3b2f0ac0d3844a757d63476d'),
 '_native_budget_publish@vmu_native_heap_probe_stage.c': (132, '0288b84e2fd89cadf444d844273ea7c894d201dc33e81005deff75620b63c153'),
 '_native_low_bindings@vmu_native_heap_probe_stage.c': (64, '3dfa8ed3d369f4a45357533b82fcf790e306900d66f45a1ede9595cbd64b4c51'),
 '_native_read@vmu_native_heap_probe_stage.c': (84,
                                                'b6bc33c6d4c36f9e6625ccf6afdc55b98e2e8f684d95194687fa946071a4804d'),
 '_overlaps.constprop.0@vmu_native_heap_budget.c': (124, 'a6df026af48f0210ec080c786da4a6142a8b222fde7983ce3a90a27af4f2947a'),
 '_ram@vmu_native_heap_budget.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_read_is@vmu_native_heap.c': (80, '9c29bdbbe69ef9be2252a01d915d29fb26621294ff98fc45c3a0c60e72e1ce34'),
 '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_span@vmu_native_stage_placement.c': (80, 'e373b8ac65eb9ed97efbc2f5783a056a498790eef6f9f5b0147a249b5ae84f23'),
 '_word@vmu_native_heap_budget.c': (52, '7b345c9cfc2f6f73694ad2d177fbb60f2d4e6a577a69dcb50222fd8e20a75d97')}
COLD_OBJECTS = {'_child_code@vmu_native_heap_budget.c': (100,
                                          '8b9249ed63eb68bec146752d114c643074f861aced0b0f03b07cb20cd90faa01'),
 '_heap_bottom_allocate_words@vmu_native_heap.c': (206,
                                                   '528ea52471bc42b01f1f4c4a40e7510f5346db70a539a35a40b8c97fbe8ba174'),
 '_heap_bottom_release_words@vmu_native_heap.c': (154,
                                                  '441e8180758616454557f02badc0b5b6a2b23b752ab88f5f6fb8e442ce84cb9e'),
 '_heap_shapes@vmu_native_heap.c': (16, 'fcf8e4fc85390977b00ad86a99a683d47e5e633fff6d62b0cb5d93837803e2c0'),
 '_parent_code@vmu_native_heap_budget.c': (152,
                                           '07ce6930bce857d18e95a47261e637ebf8fbfc2f8ffb9624fd6baa36270eb1e1')}
WORKER_FUNCTIONS = {'_identity@vmu_native_heap.c': (444, '4db2ac42d4e670b25d09c6329bc13471ae94160d1e1ba202add9bb19c50111f2'),
 '_index@vmu_native_heap.c': (100, 'd2ff273cc18b27689da7ea9b8cbb370259eb5866150f23017bd3a796b7b9b01b'),
 '_kui_vmu_native_heap_lease_identity_valid': (76, 'd09e753d969e7c193890a2e62fe89b41045d35ecfb44d8eb9b49f5eedc1e7a34'),
 '_kui_vmu_native_heap_lease_valid': (52, '7cb44759d9dc9cc44a9845c52140811035c680be74b3d04c8b5b3bd1f143344a'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512, 'f4e035c68c7fa0dc82f416335b83ffec9339046aefd503103d4dc03bc3c69599'),
 '_metadata@vmu_native_heap.c': (436, '182e84ad585e5966705ad62d3de77ffbd956d1c3e12e899e1a24a43e9e4cd7ec'),
 '_read_is@vmu_native_heap.c': (80, '6cb7c1e07a084dcdcd855a039115cbcfab3e10dc78c1538c092aa9644741696f'),
 '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305')}

# Authored two-row {extent, allocate delta, release delta, initializer mask}
# contract. The complete typed immutable table and metadata's exact pointer
# binding were independently reviewed against the linked SH implementation.
WORKER_OBJECTS = {'_heap_shapes@vmu_native_heap.c':
    (16, 'fcf8e4fc85390977b00ad86a99a683d47e5e633fff6d62b0cb5d93837803e2c0')}


# Closed additional preheap-clear stage functions and immutable owner contracts.
PRECLEAR_FUNCTIONS = {'_kui_native_preclear_finish': (292, 'f1eb73380f95039d3726c90ff160b412cf9c1f76f04bcf84d01a3f0109eba007'),
 '_kui_native_preclear_stage_install': (48, '955c81d5defa27503852fb4120c70dd3b700166bd6f210014f9f3144e6af3fba'),
 '_kui_vmu_native_heap_preclear_plan': (1276, 'b1dab9cf427d81bb73e14dd451cb5cfa0311be275c400f2c9e1ffa0dc0a64d0b'),
 '_native_arm_heap_hook@vmu_native_heap_probe_stage.c': (208, '3c43b5f6215ff7aac4e72889ce871c587e14d960a1372278f8deeb2ad01c932f'),
 '_native_preclear_arm@vmu_native_heap_probe_stage.c': (124, '439bf01d06fe39fd99afb7cea1d91570b4d7a5414c8b56ef2dcd107403bdf17b'),
 '_native_preclear_hook@vmu_native_heap_probe_stage.c': (332, 'b49554883ed1a46159817acde3e0daf6d1edcaf36d40593d38b7146d967adbc2'),
 '_native_preclear_prepare@vmu_native_heap_probe_stage.c': (252, 'f0c9a61304070d5902045ca234540c47b25a720a702728492bc29ca1462aebf9'),
 '_overlaps@vmu_native_heap.c': (120, 'e52ae890aafe08b995cb750efdeeb2c8d26f9b475090139673132ad294349d96')}
PRECLEAR_OBJECTS = {'_digest.2@vmu_native_heap.c': (32, '8b816dc944b22e82394832d9f40d824a78c54fc75ffd49e66525502accc2bce4'),
 '_helper.0@vmu_native_heap.c': (24, '5ad3e0d54fc4dd851554f442557da8a4e985f67c4f753bca372b415771537b61'),
 '_path.1@vmu_native_heap.c': (20, '284fcb26ca610bc73aabe951079ba53c239cf3502c35605c20a2f91a182f06b3')}

# Reviewed ten-family descriptor predicate and its complete policy table.
# The worker predicate is specialized to config.exit; the stage predicate is
# inlined into schema. Both table pointers and the worker descriptor base
# retain exact typed bindings. Heap metadata remains in its separate closure.
EXIT_SHAPE_FUNCTIONS = {
    'worker': {'_kui_vmu_native_exit_shape_valid.constprop.0@vmu_native_passive.c':
        (132, 'fc7c0c23cf1857dcd610ca4e460930a984d1e4e69262edcf0aca23cd562441ee')},
    'stage': {'_schema@vmu_native_resolution_cache.c':
        (548, '4310f574c8b9601d1bcd7d8b113bbfa6f635801196fb674adbe52c938b69ad6c')}}
EXIT_SHAPE_OBJECTS = {
    'worker': {'_kui_vmu_native_exit_shape_words.0@vmu_native_passive.c':
        (20, '0c2739935977ee2e41b9e95fe755c166010340fa71bd8b5924f16de2c5ea3e8f')},
    'stage': {'_kui_vmu_native_exit_shape_words.0@vmu_native_resolution_cache.c':
        (20, '0c2739935977ee2e41b9e95fe755c166010340fa71bd8b5924f16de2c5ea3e8f')}}
EXIT_SHAPE_BINDINGS = {
    'worker': (
        ('_kui_vmu_native_exit_shape_valid.constprop.0@vmu_native_passive.c', 0x7c,
         '_config@vmu_native_passive.c', 0x8bc),
        ('_kui_vmu_native_exit_shape_valid.constprop.0@vmu_native_passive.c', 0x80,
         '_kui_vmu_native_exit_shape_words.0@vmu_native_passive.c', 0)),
    'stage': (
        ('_schema@vmu_native_resolution_cache.c', 0x1a8,
         '_kui_vmu_native_exit_shape_words.0@vmu_native_resolution_cache.c', 0),
        ('_schema@vmu_native_resolution_cache.c', 0x220,
         '_kui_vmu_native_heap_metadata_valid', 0))}

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
