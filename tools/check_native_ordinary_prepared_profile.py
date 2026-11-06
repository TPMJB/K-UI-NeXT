#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepared closure for ordinary full General, from pristine 3a6 references.

The complete A1/A2 reference worker load images match the pre-existing ordinary
General workers. These fixed maps cover the prepared roots and all reachable
passive-file helper bodies, including outlined address, lease and context
guards. Source-qualified pointer identities/interiors remain in each digest.
Historical and session prepared maps remain separate.
"""

COMMON = {'_controller_context_valid': ('90ea99a592562009bd84ae6c6b365e5241513c56a5bfe132aab91d16b4f3e935', 72),
 '_controller_layout_valid': ('c35affd52b29c1602e2fc9640bc0279d5ee8d2c70849f05d1203f503b446aa1c', 748),
 '_controller_pair_valid': ('b7007b20806bea9a50bc5e694862b7ac72ee41043fda4a56d779705e075177b0', 196),
 '_controller_shutdown_literal': ('e204c1a4497ceabc9217bd374c025f56724c38f54ab47e17c1b13dfc221bd41a',
                                  132),
 '_controller_shutdown_mmio_state': ('e8bdc653a468992c0b56c065c1d45b78f87417da09be8117f5f84bbdee74f8df',
                                     88),
 '_controller_shutdown_source': ('b2235bcd3a6cc5010d978917fea149b52e7f39fbee4744ca64c1ec9e7f76e67c',
                                 520),
 '_controller_shutdown_source_read': ('6d9992e5f1101643fc738cafd453a5810b4380ff46eafc711b989331c6c14a86',
                                      32),
 '_controller_shutdown_span': ('40df857c87050b8955fb305817677e78b440765b9ec7adc9a02219fc6169ee87', 100),
 '_controller_suspended': ('10b770231b6b24405eeb7023f6ae4da6e9c69ec2bcd9bbca51d6b212f8ac783d', 380),
 '_fail': ('34efa939b712d6b1ddd0d9c59af160b6bbcac2bcbd411f4cfbf84bb26f2c7527', 28),
 '_failure_witness.isra.0': ('8fab6b82f679e0ba509dd4420916770f8a40bd5708e67a274c399d3dda2a81f7', 16),
 '_held_report_gesture': ('c37a4feec01d1d60de23ee9130e045c06faaf4b1cf8583041c9054416851d026', 36),
 '_hook_word': ('6c579e60911823ed1fbf6a57622bab2d7de2ebf46228c2c2c21d27f8fc144249', 112),
 '_installed_hooks_valid': ('7dea42814133fa311508f8c9441b300fef4f9b010d81a72e98378c8634bf7c56', 128),
 '_issued_physical': ('1cbca02d39df730690fdf4c8e3169902d1d45df68696fd366cdea547625452ef', 104),
 '_kui_native_loader_timeline_visit': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83',
                                       4),
 '_kui_vmu_native_exit_shape_valid.constprop.0': ('73f960510420f9114e16729f6b0f63303104db03ae2507cfecc87ada91e381ad',
                                                  132),
 '_kui_vmu_native_lifetime_prepare': ('51a7cf0ccf76c9ce4ab5d7395ee3fa12dad3e8a1435bf63cfed5e3d7896c2532',
                                      752),
 '_kui_vmu_native_passive_split_submit': ('e534ea1834eff1b69ba6208ee0d1dfedf4801d2a906c45791eb5ef1281cb22ca',
                                          884),
 '_kui_vmu_native_patch_encode': ('2ee6e8b4c1272c5916db0236959a09af2e07a5f4622d7c655982b4bd7eddc4dd',
                                  168),
 '_kui_vmu_native_readonly_release_check': ('0b7e53dfcc8c3b384e8b33b8d874ced89398c26ca02282fc23fb70411ad6c708',
                                            640),
 '_kui_vmu_native_runtime_fault': ('8f8a1f7ed293ad4e54b9c9178db239d79c0bf2485bc777209299c5c2170b75d4',
                                   308),
 '_kui_vmu_native_sdk_coherence_prepare': ('dc45cffbaf7faf192ee76278151f403e17e1946afe78c59cb3e03d14753699cc',
                                           420),
 '_kui_vmu_native_terminal_dispatch_span.constprop.0': ('843a0460a5268fd47a94404e6a0df8e6269c0a7049aa71a6d32196e25c47d2ea',
                                                        128),
 '_kui_vmu_native_terminal_dispatch_tagged.constprop.0': ('d045495217f025c5e362cc651783a2d791db46aab61bc9004d882d9635085d7b',
                                                          108),
 '_kui_vmu_native_timer_callback_owned': ('035d109aa67cbb6b585f263dd0d0b64d7679386bdf457ebbbbe84894abaabce6',
                                          92),
 '_kui_vmu_native_trace_frame_entry': ('24be2fc972ae425d21e9fd5a0b8ad551f1f64bc20c8d7cb9118c00818bba1111',
                                       164),
 '_kui_vmu_native_trace_game': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83', 4),
 '_kui_vmu_native_trace_irq': ('31b72b692190f12dabe3bee1767a9d43913918730d08b7c7c802bea816c8177e', 14),
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
 '_lease_valid': ('01cf615d1d2a16f67d88aab1345eaf5854fa1147c67fa4a51fa9a3cb2d714025', 24),
 '_live_boundary': ('73b14f62db52df70862faf3938b134343b14d54fb89558ba38d5867138106a1e', 216),
 '_memcpy': ('7fa8d1eee8135c7721a67c5ada882db55d17c24a41b305927c114c697ac5066d', 72),
 '_normalize_heap_dispatch': ('3419cac763b385db2a6f931e4df44172bb9dff4fbaba7ed25f3c5534524762ec', 276),
 '_normalize_heap_dispatch_if_routing': ('54551c3ce68e045933e39c229955468e2bee6ead6fe9766f42935d1593309802',
                                         40),
 '_ordinary_prepared_ram': ('0084512336c207d3ffe6225688c8058f1f87937c8d5ace66be5592e59b936320', 88),
 '_patch_ram': ('ee99409b66f8e0fff23f34fae0ef53e503429aeea4ce8117e1d584ea56ecb7c5', 80),
 '_persistent_context_guard': ('c64d3f0b1e8a23c587834effc7697d01d17a20b0a2032a0fe4713d6b69bbdf77', 128),
 '_persistent_lease': ('2cc5f048419d235babba86e995e68fa108301c09aa8822d858095d934da52bac', 108),
 '_persistent_storage_guard': ('5c520537d378c32b93f927645714db94244d6e22db32534aafbd617b5c477405', 384),
 '_poll_lease_valid': ('ac832b82ea8d821dede21c5e9e825e9f7a4479be1dae662c525cdd4f92fb64a8', 168),
 '_prepare_hook_entry': ('5d19b4a6dc4e4995fde5d613379bbc0cb8e28fa11854280a2686c357bdc5b582', 208),
 '_protect_range': ('fdab8c94cba19d50df3c78ce99b0e4b4da5def207c86189e6df7b6bb9aa28065', 84),
 '_read_heap': ('98154596efda0b82f86e858c35201c19f6d8773459cfc703474205cf1c2a998f', 48),
 '_read_value': ('114eb5e180ab1a3e10d2991991db4cd64069ca22b1c85a1f612a9cc5d9029f58', 36),
 '_read_value.part.0': ('1af38c39e6bb370c70947263231e5c742c989f186ac8af979a2654bafde206e2', 104),
 '_reader_capture_region': ('ba0fe03f2355bdee2e98166022692dab9fa03e444a7c52f890b3407c656a7165', 100),
 '_reader_cooperative_service': ('17975bf4c258a07eeaaf7638e5cab6b690aff3ef6c7cba40b2b47296152748bf',
                                 100),
 '_reader_readahead_exports': ('e180abcbd6e9a2144e4ae5e0cc25c009c49d65018c5815367873b5fb16d36ccd', 200),
 '_reader_streaming': ('6e039ba01afc9b34ddd3d82bdddac98662af88639e1c5e1a1989a1fcfd563759', 120),
 '_readonly_overlay_increment': ('e3a6368a10593e4ae7510faf736d644a9d9e383e262070923241741b3f6893b1', 14),
 '_readonly_overlay_le32': ('1daa47eecc292df8356826c0e5a683e3a2c27cec212f901db7b1f01cf3b897df', 30),
 '_readonly_overlay_parent_response': ('d396e68edd3158708ba5a8c9ff5235821bb62b71cc9dc40650cadb9098eaeb12',
                                       116),
 '_readonly_overlay_pointer_overlap': ('1ee60abf2148621140e7d53a347536afad5e8f7759627bce7da0d045d7ea0348',
                                       32),
 '_readonly_overlay_put32': ('04dba701f2b2b5a13739b4fde066b45ecceceb28684e620d217ff60cb1308163', 24),
 '_readonly_overlay_ram_span': ('5d29104681aa3d3a32c948c924049eb53df8a632f91ee767e9b237c9206c73ce', 56),
 '_readonly_overlay_target_response_bytes': ('4b38d95dfa7ff193118d30455a04e211fa54b7343e9beb12bdd72645835ad036',
                                             216),
 '_report': ('a854b68fa4fa84c3a2dd8506072c9e981e7deecbf8196ba7934e2a6331815931', 412),
 '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
 '_timer_runtime_in': ('7d3d7f5268d3a727c863c90fde2de8693613748f81e564271569b4ef3f4ecf8e', 26),
 '_timer_runtime_ram': ('2cd3fc97ffa9bd27467b7c6e12ae1bcebcdb41e591742863a7cac78ad34661dd', 104),
 '_transfer_context_valid': ('a562ecb51dc7c8f340c458353cc9e02af17c5796c81bcbfea79a077ba4af8580', 200),
 '_transfer_frame_valid': ('381582aca89800ccf5207d80f9e97c8273f22c971f6d27e98395317f2087dde8', 504),
 '_transfer_sdk_context_valid': ('1136d2be277a0644fc2726215a2a81dbc3ae4e91f30135890509dcccffd4acef',
                                 388)}
SLOT = {1: {'.rodata': ('eb05ec74cac5be59b7662e8e20d04148ed5ba7ff851913c935a486268d58ee8e', 1456),
     '_chain': ('2c7d822399646cca382bda8696d7a447934e29113fec0521f9e7586012c673c8', 808),
     '_kui_vmu_native_passive_arm': ('1e6bcc0dcdc90166912fbcc763a4802bc38fd2481fba5b8d31e148440815da5d',
                                     2248),
     '_kui_vmu_native_passive_complete': ('98bd7b2ce67f8adfc892dbad266f681eb7050f3ef786594a293a72b95bb8c297',
                                          3440),
     '_kui_vmu_native_passive_submit': ('41cb6535514de56e9b047c89e4ac61e8a2d2d5c9ea6438a2140de112f718b6e7',
                                        1408)},
 2: {'.rodata': ('fb1aba220ee4b58ec1efb9f5ee60a35da36c9f2265c84ed7a0788b9c2e8ba3ff', 1456),
     '_chain': ('473455b32cbcbfcc120f0dafb4c4e006f5ab582061d767f2f6cd6bb7cb215817', 808),
     '_kui_vmu_native_passive_arm': ('fb1818230bc364e62a44aa87f1f53055cc65d62d5d5dd8b3fe6dcbbaf6d6680a',
                                     2248),
     '_kui_vmu_native_passive_complete': ('2dbc8aa571f56dade367394370b9fc89d9fd3583f1c75e53b4c209002749a390',
                                          3436),
     '_kui_vmu_native_passive_submit': ('7ce6162578e7ae39e63c374665497355df58278f4e758753d242c70584f66051',
                                        1408)}}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def image(worker):
    from check_native_ordinary_general_profile import profile, aliases
    require(profile(worker) in SLOT, 'Missing strict ordinary General prepared profile')
    worker = aliases(worker)
    from check_vmu_native_persistent import prepared_symbol_records
    owned = [row for row in prepared_symbol_records(worker)
             if row[0] == '_ram@vmu_native_passive.c' and row[3] == 2]
    require(len(owned) == 1, 'Ordinary prepared RAM helper lacks unique FILE ownership')
    record = tuple(owned[0][1:])
    names = dict(worker[4])
    alias = '_ordinary_prepared_ram'
    require(alias not in names or names[alias] == record,
            'Ambiguous source-owned ordinary prepared helper')
    names[alias] = record
    return (*worker[:4], names)


def pins(worker):
    worker = image(worker)
    from check_native_ordinary_general_profile import profile, pinned
    slot = profile(worker)
    pinned(worker)
    return {**COMMON, **SLOT[slot]}

