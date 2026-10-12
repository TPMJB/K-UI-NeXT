#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Separate fail-closed linked admission for startup DATA scheduling.

Candidate discovery never grants hardware admission. Reviewed identities and
object/frame inventories pin independently inspected final SH target artifacts
from runtime source 143d51bf76dd23ab86a128ff30a492b4f5ad8bac. Older
trace/probe/SCI-next gates are not modified.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

import toy_loader_sci_next_audit as previous
import toy_loader_data_probe_audit as u
import toy_loader_trace_audit as trace
import toy_shared_sci_audit as shared
from check_loader_layout import inspect_elf
from check_retail_instructions import audit as instruction_audit
from package_cdda_toy_pilot import linked_symbol_sizes, pure_worker_leaf_audit, stack_rows
from toy_pilot_cache_audit import Linked, audit_cache_layout, physical

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASELINE = ROOT.parent / 'overnight-source/build/toy-u-reference'
LOW, LOW_LIMIT, HIGH, HIGH_LIMIT = previous.LOW, previous.LOW_LIMIT, previous.HIGH, previous.HIGH_LIMIT
STAGE, STAGE_LIMIT, P2 = previous.STAGE, previous.STAGE_LIMIT, previous.P2
REQUIRED_CONFIG = {**previous.REQUIRED_CONFIG, 'DATA_PAYLOAD_MODE': '0'}
MODES = {1: 'Y-one-sector', 2: 'Z-startup-async', 3: 'B-observational-control'}
# These tables were pinned after inspecting source and emitted instructions,
# never by an automatic bless/update option. The gate has no such option.
REVIEWED = {1: {'resident-sci': {'_kui_retail_toy_data_schedule_play': '2d0d7555a790d9d75f68326e1240a794d0a461e74d324732c335cd7e51956f40',
                      '_kui_toy_pilot_base_dispatch': '542f83ad96ea6a0da7c65ff5e3a27d8c9e5450f520f1607e08eb67b37a4dd87d',
                      '_step': 'e9693c754678156ddbf80dd007f1e4af42cddef1d2ae144414ea4b248cdfb3ca'},
     'stage': {},
     'worker': {'_data_probe_metric': '2c552970de2dc13e5734b5e95550b7bd03dfad8da63643817bf6b8ceb9125437',
                '_data_probe_payload': '3ca7e420eb930c799abe0b9b23aeff6b348115ea08a65531d63bffd5e7e54f26',
                '_data_probe_read': '860beaac1e185a0c1d4b3e6375502c66128dd9ef1ab07ba3113f10e906962af0',
                '_data_probe_sample': '3f835d931499c498b7804a2cc78347921aec312bfb8d1263b8e924951d59be13',
                '_dispatch_body': '04973f7e2a9002d589f909d71ec75388ecc7392eee0f8670f116377c02e373b2',
                '_kui_toy_loader_data_probe_begin': '736efe663971a4030c68a4dc94579546cf310031e26cfbfce73654abd775803a',
                '_kui_toy_loader_data_probe_end': '2840bdcc8f1c58d564ed104d0e7c9b92a1f6cb5cb7f72472c3f9a05787b14403',
                '_kui_toy_loader_data_probe_freeze': '396f6fde33d579bbeb881fd88b66f01881c557df19f4d450808be61145a174c2',
                '_kui_toy_loader_data_probe_words': '3a9aa2418bbdfe0a6fcd90d24af62cf6b28fd840aff265989d773824f3cffb0b',
                '_kui_toy_loader_schedule_report_begin': '8069cef989808c2ee28d871f07055cc30501a83eca967ee4a4e02892355b78d8',
                '_kui_toy_loader_schedule_report_end': '315f852a29c1b0e65d293238de02a2953e7f8581420997bdfac243eea6104fb3',
                '_kui_toy_loader_schedule_report_freeze': 'c8c3ec0e4f969b424e1021dc07953ed03270565e1db51efd1537bcb9fe5f63d2',
                '_kui_toy_loader_schedule_report_words': '2076cb2d85f7bec0200bf0abb09b7e1e3613b27fdfdf002f33f7f975e9e26c9f',
                '_kui_toy_loader_trace_report_capture': '7ef317a8fafd0f757b21fb9c6471ac7ce862c8b43f99a2d2dcd958ea0eb525ec',
                '_kui_toy_loader_trace_report_page': 'b25434148a25b5b3943dccbfa885a03535c7cf343876270f266e49ac3e9b2f80',
                '_kui_toy_loader_trace_terminal': 'ac7158f8ffaad050f8fb13d0d7f9433c7ff762cca6fa61bcb7d7e7aefd583bc6',
                '_kui_toy_pilot_gd_dispatch': '03427f0a1b7e2f825a3bdd3ec7b469074b29311fd6557b8415a99c919ca117f0',
                '_schedule_add': '2ca6a0fc5fefb7cc9d6321406567bcacd7d72e584bc6a2f85e46213af3f38d8f',
                '_schedule_clock_sample': '7e3c1c9d821156f105fc9039eed581afeac94b65a533c155b719960c4f836a83',
                '_schedule_initialize': '1d44a2ed640261a445fb11ac2072033248d9d13964beb1f48532fead6064f619'}},
 2: {'resident-sci': {'_kui_retail_toy_data_schedule_play': 'a691b0f95ac4b3eafcfda9319644e0f68de89ac9ea31daa722f3bb701f4ac872',
                      '_kui_toy_pilot_startup_sci_healthy': '377e5aa8320a0418d8fd943094f2f01b782c86b9e54813563b44949b43dba814'},
     'stage': {},
     'worker': {'_byte': '31c3ddec5319bda529f99a43dd790394a46fc2595dce4cff4f0425a62a10f57f',
                '_close_bus': 'aea3de16140033f0e186695410c8a8d2e59346761d8b7193efb8c645472559ca',
                '_command': '8b31e537cc75a82c517a1c6d3d11a6fd4d7cbe5ab302b716a02dc39d25850d62',
                '_data_probe_metric': '2c552970de2dc13e5734b5e95550b7bd03dfad8da63643817bf6b8ceb9125437',
                '_data_probe_payload': '3ca7e420eb930c799abe0b9b23aeff6b348115ea08a65531d63bffd5e7e54f26',
                '_data_probe_read': '860beaac1e185a0c1d4b3e6375502c66128dd9ef1ab07ba3113f10e906962af0',
                '_data_probe_sample': '3f835d931499c498b7804a2cc78347921aec312bfb8d1263b8e924951d59be13',
                '_data_work': 'a56b6f313e91de86d2b6ac16b682f6e59329e9c9680d60a2061d7774db299130',
                '_dispatch_body': 'a0499857b50c35879e1c98a3e5eed5e31d329ea234bca9fe8421e04841cbc2c1',
                '_finish': '11c112fd3c9f9fbf46b9685b906661588ca73d98216c0f04047bdb1771779307',
                '_guest': '1c9f2095b9d91990fd3f84c77d32677e2c703412b8aa7b1d93efcc4926965e81',
                '_hook': '0a076949c289271f41ccf4af8a75c7e12341199593812d72343d4a71a17d14ce',
                '_kui_retail_cursor_begin': '527681d5b77972ebde9390188dfdad88beda9e41618c47a71bc4d23752506e97',
                '_kui_retail_cursor_feed': '81ff614edc3370f9d3d465923014fe1fe1a797ed1f0b768f4ca97304e6eec86c',
                '_kui_retail_gd_dispatch': 'f56f47061a8b2d611dd810fd6c9a18dc2d9b37861b5536ab5b2165288b87ed69',
                '_kui_retail_gd_progress': 'b1ec7bb08471bc1f33b52d843d55894e4c1d7af46be50ca6615c2138f7fb97a5',
                '_kui_sci_sd_healthy': 'e9d09de7108d5110f1fd87bc2fa8c3be1dc40f7d6e86408374efda4ef0b1d86f',
                '_kui_sci_stream_busy': 'a3c4e41dbdb3b5382f7ca92564ac5597e05a615d70cc578941c48c06f6b7555c',
                '_kui_sci_stream_discard': '91abd7d2fa2986822f71620a9b78a3884387de80fe038122f5a6031dd5b84c71',
                '_kui_sci_stream_fetch': '1dd7c1eba302958c91fbf6a27a7a7897e4f6ca6f17a7e46de30f1fd18b25002d',
                '_kui_sci_stream_open': '3c37ecdf2bb3d06581f8ee661c9a47e58f390404b9f94438f37e7cea643b269c',
                '_kui_sci_stream_poll': '1e0478cad5012975cf5648db68f0e50e741e2697cab8046ba46d6895e373435b',
                '_kui_sci_stream_stats': '800f4dd7419d8a7c3eab1b1fe9bb779cf95886a70e7968ce0a6e61c5f00bbbaf',
                '_kui_sci_stream_stop': 'e8026d62ba551a950ca2aa958523cc3c657af8871f4263505670a4b0e67059c3',
                '_kui_sci_stream_take': 'a799b704b527bbbaf6a050e724d48b9a09006713734e2f6bd23d178cb31fed96',
                '_kui_sci_stream_token_budget': '72c59dcda6d22313c6f9c5c7fcf719caca565fdadda543c4ebb8fdbe6d9d15b3',
                '_kui_sci_stream_token_pending': '4e4f4328b23b1dd474901be16dabb01ea17acd9358ffbd183bb8230571723b73',
                '_kui_sci_stream_wait': '1804be3ce04b5202e1e98079417fb1877256f830fd76bf940e628ea68aac8f32',
                '_kui_toy_loader_data_probe_begin': '736efe663971a4030c68a4dc94579546cf310031e26cfbfce73654abd775803a',
                '_kui_toy_loader_data_probe_end': '2840bdcc8f1c58d564ed104d0e7c9b92a1f6cb5cb7f72472c3f9a05787b14403',
                '_kui_toy_loader_data_probe_freeze': '396f6fde33d579bbeb881fd88b66f01881c557df19f4d450808be61145a174c2',
                '_kui_toy_loader_data_probe_words': '3a9aa2418bbdfe0a6fcd90d24af62cf6b28fd840aff265989d773824f3cffb0b',
                '_kui_toy_loader_schedule_report_begin': '8069cef989808c2ee28d871f07055cc30501a83eca967ee4a4e02892355b78d8',
                '_kui_toy_loader_schedule_report_end': '315f852a29c1b0e65d293238de02a2953e7f8581420997bdfac243eea6104fb3',
                '_kui_toy_loader_schedule_report_freeze': 'ccf5421751af8050f44e83aeeaa3ef1bc9e11a5f123b7f27c35cdff6f45b2ca4',
                '_kui_toy_loader_schedule_report_words': '2076cb2d85f7bec0200bf0abb09b7e1e3613b27fdfdf002f33f7f975e9e26c9f',
                '_kui_toy_loader_trace_report_capture': '7ef317a8fafd0f757b21fb9c6471ac7ce862c8b43f99a2d2dcd958ea0eb525ec',
                '_kui_toy_loader_trace_report_page': 'b25434148a25b5b3943dccbfa885a03535c7cf343876270f266e49ac3e9b2f80',
                '_kui_toy_loader_trace_terminal': '51a4861bd187e86f4c2298da2ca50f16df7d455ab46c2c57f9af2feec04972c9',
                '_kui_toy_pilot_gd_dispatch': '03427f0a1b7e2f825a3bdd3ec7b469074b29311fd6557b8415a99c919ca117f0',
                '_kui_toy_pilot_sci_cancel': '01039169027775e6a7c7cd877de1545e39f5e65afec67d3bfafab066f85b15bd',
                '_kui_toy_pilot_sci_init': '67414a05ba2969a76e61500aa7e4b32d66a754eae434cc4d275df67fdc2d04e0',
                '_kui_toy_pilot_sci_irq': '3a984e60febfc7172472c13287b95fc9bf074ce9c6430db6faa5567e92f489d1',
                '_kui_toy_pilot_sci_pump': '5fd39d70ad1d6c5e96729b972a0ab1c29af815bf12719c1e838b413366b53686',
                '_kui_toy_pilot_sci_quiesce': 'd301cd9ba8d7910ed016fac239eec91ecae03fe1050b93bfcdf2c1769b5df7b8',
                '_kui_toy_pilot_sci_retire': '91724fcabb541e0b7e4fc97c092c7f0747715a1e73584a7ab40754c563e85940',
                '_kui_toy_pilot_sci_service': '9f245c23d22a9295587c71ff4e2944fd55e7d562c51e0dd3a6612597d2602b2f',
                '_kui_toy_pilot_sci_snapshot': 'c33d99ef2b86b8e12497edc9ba92bbaa12a166f43d7a46ec3452d9d65ada44b0',
                '_kui_toy_pilot_sci_startup_active': '5a2ac7963dbef6f0f5241ba37c6bc21706a5c52090ddb8867fd18d27adb6808f',
                '_kui_toy_pilot_sci_startup_snapshot': '33a2faa17eb88e96952403211bdd8f7731f4b60549b3c14d7f54d39d72da7248',
                '_kui_toy_pilot_worker_initialize': '0e8bcb17286d9aa81fa5d65a8dd74ed8964ac7e695aa39f9a611710e02d690df',
                '_live': '5f642094589cd275fad0582994fc08921ac24905d12d38bbac6d9605263b84a8',
                '_locate': '3b986bf2d21db16552be0929f2d904885f2cb908ce5ab37660b8379153769533',
                '_msf': '52364641d9531d0ec12723ab385f01fc491a7a6d678634ceb9036d0c5fb1c6fc',
                '_prepare': 'ed0e104beecb8c56b85f72272f6e797bef32de882db79f4a35e69a2b996b130d',
                '_retry': '4fedf27a3e02f081ab69abe3489c552f5404700c317c6b4c946ebd8ddd13de9f',
                '_reverse': '364e2caea75e90ef62a8d227f35c3ebf33d177c62b6772b2da0cd7b05216298f',
                '_schedule_add': '2ca6a0fc5fefb7cc9d6321406567bcacd7d72e584bc6a2f85e46213af3f38d8f',
                '_schedule_clock_sample': '7e3c1c9d821156f105fc9039eed581afeac94b65a533c155b719960c4f836a83',
                '_schedule_initialize': '71b386832a96a28860b07f62b109f299e5c64d0806b66277a192ffc6afb7bfb7',
                '_sci_startup_fail': '74a0e6377665def6961cc42fdbb3a89f8afee065f7bcb76c010285ff80b39359',
                '_sci_startup_report': 'ac4d11f5f286d1d01c6c6e57a37059657b68a419f3ab9774b379d9d194759ac4',
                '_select': 'e148dc7291ec593d664bc9e10acda7c6173151c91c9a01580027dc872ac8678e',
                '_start_dma': '44c97d304aaea6535a986f75458c8153dc7eb2c3de174177e0ac3b787e9d8b8f',
                '_stop_card': 'bdbdc505b43a43c44ce581a9c498dba92a5d4500d9c6a046e5be034a86ef4442',
                '_token': '85bf2b8eb505754cd8185ae14d79731644be41dcdbc71584db302e40de9c7a95',
                '_token_count': 'f1648b2765f31f093f4fb6a2d41a36c2ee3af336e6c1d7ec089b06e062b0eca3',
                '_unhook': 'da480cdb00ef3575fd6485a643c5bd64de41c67501bc8370c607e48435596007',
                '_write_out': 'acf68553ff72b83b9b99a309d08f2e5ee0ef5a873685a9503a4ccede0f8a8f6b'}},
 3: {'resident-sci': {'_kui_retail_toy_data_schedule_play': 'a691b0f95ac4b3eafcfda9319644e0f68de89ac9ea31daa722f3bb701f4ac872'},
     'stage': {},
     'worker': {'_data_probe_metric': '2c552970de2dc13e5734b5e95550b7bd03dfad8da63643817bf6b8ceb9125437',
                '_data_probe_payload': '3ca7e420eb930c799abe0b9b23aeff6b348115ea08a65531d63bffd5e7e54f26',
                '_data_probe_read': '860beaac1e185a0c1d4b3e6375502c66128dd9ef1ab07ba3113f10e906962af0',
                '_data_probe_sample': '3f835d931499c498b7804a2cc78347921aec312bfb8d1263b8e924951d59be13',
                '_dispatch_body': '04973f7e2a9002d589f909d71ec75388ecc7392eee0f8670f116377c02e373b2',
                '_kui_toy_loader_data_probe_begin': '736efe663971a4030c68a4dc94579546cf310031e26cfbfce73654abd775803a',
                '_kui_toy_loader_data_probe_end': '2840bdcc8f1c58d564ed104d0e7c9b92a1f6cb5cb7f72472c3f9a05787b14403',
                '_kui_toy_loader_data_probe_freeze': '396f6fde33d579bbeb881fd88b66f01881c557df19f4d450808be61145a174c2',
                '_kui_toy_loader_data_probe_words': '3a9aa2418bbdfe0a6fcd90d24af62cf6b28fd840aff265989d773824f3cffb0b',
                '_kui_toy_loader_schedule_report_begin': '8069cef989808c2ee28d871f07055cc30501a83eca967ee4a4e02892355b78d8',
                '_kui_toy_loader_schedule_report_end': '315f852a29c1b0e65d293238de02a2953e7f8581420997bdfac243eea6104fb3',
                '_kui_toy_loader_schedule_report_freeze': 'c8c3ec0e4f969b424e1021dc07953ed03270565e1db51efd1537bcb9fe5f63d2',
                '_kui_toy_loader_schedule_report_words': '2076cb2d85f7bec0200bf0abb09b7e1e3613b27fdfdf002f33f7f975e9e26c9f',
                '_kui_toy_loader_trace_report_capture': '7ef317a8fafd0f757b21fb9c6471ac7ce862c8b43f99a2d2dcd958ea0eb525ec',
                '_kui_toy_loader_trace_report_page': 'b25434148a25b5b3943dccbfa885a03535c7cf343876270f266e49ac3e9b2f80',
                '_kui_toy_loader_trace_terminal': 'ac7158f8ffaad050f8fb13d0d7f9433c7ff762cca6fa61bcb7d7e7aefd583bc6',
                '_kui_toy_pilot_gd_dispatch': '03427f0a1b7e2f825a3bdd3ec7b469074b29311fd6557b8415a99c919ca117f0',
                '_schedule_add': '2ca6a0fc5fefb7cc9d6321406567bcacd7d72e584bc6a2f85e46213af3f38d8f',
                '_schedule_clock_sample': '7e3c1c9d821156f105fc9039eed581afeac94b65a533c155b719960c4f836a83',
                '_schedule_initialize': '1ea3a51ddca706a0676b051b8bd307fe68758dbafa25f51f415309424357108c'}}}
REVIEWED_OBJECTS = {1: {'resident-sci': {'_card': 72,
                      '_card_result': 4,
                      '_diagnostic': 64,
                      '_display': 56,
                      '_image': 544,
                      '_kui_retail_hook_active': 4,
                      '_kui_retail_hook_fault': 4,
                      '_kui_retail_hook_source': 4,
                      '_kui_retail_hook_sr': 4,
                      '_kui_retail_native_caller': 8,
                      '_kui_retail_original_menu': 4,
                      '_manifest': 1020,
                      '_pace': 44,
                      '_pacing': 8,
                      '_port': 20,
                      '_row': 4,
                      '_service': 204,
                      '_toy_data_play_accepted': 4,
                      '_toy_gd_timing': 24,
                      '_toy_scratch': 4},
     'worker': {'_attempts': 4,
                '_data_probe_read_state': 112,
                '_data_probe_report': 768,
                '_data_probe_scope': 36,
                '_diagnostic_service': 4,
                '_first_tick': 4,
                '_kui_toy_pilot_bridge_active': 4,
                '_kui_toy_pilot_bridge_skips': 4,
                '_kui_toy_pilot_native_diagnostics': 32,
                '_kui_toy_pilot_pause_detail': 4,
                '_kui_toy_pilot_pause_entries': 4,
                '_kui_toy_pilot_pause_max_attempts': 4,
                '_kui_toy_pilot_pause_pumps': 4,
                '_kui_toy_pilot_pause_retired': 4,
                '_kui_toy_pilot_pause_retries': 4,
                '_kui_toy_pilot_updater_entries': 4,
                '_kui_toy_pilot_updater_returns': 4,
                '_last_tick': 4,
                '_owner': 3968,
                '_page': 4,
                '_pilot_words': 512,
                '_report': 1664,
                '_request': 96,
                '_retry_context': 4,
                '_row': 4,
                '_schedule_report': 704,
                '_schedule_visit': 40,
                '_stopped_display': 56,
                '_trace_words': 1664,
                '_visit': 108}},
 2: {'resident-sci': {'_card': 72,
                      '_card_result': 4,
                      '_diagnostic': 64,
                      '_display': 56,
                      '_image': 544,
                      '_kui_retail_hook_active': 4,
                      '_kui_retail_hook_fault': 4,
                      '_kui_retail_hook_source': 4,
                      '_kui_retail_hook_sr': 4,
                      '_kui_retail_native_caller': 8,
                      '_kui_retail_original_menu': 4,
                      '_manifest': 1020,
                      '_pace': 44,
                      '_pacing': 8,
                      '_port': 20,
                      '_row': 4,
                      '_service': 204,
                      '_toy_gd_timing': 24,
                      '_toy_scratch': 4},
     'worker': {'_attempts': 4,
                '_data_probe_read_state': 112,
                '_data_probe_report': 768,
                '_data_probe_scope': 36,
                '_diagnostic_service': 4,
                '_e': 1312,
                '_first_tick': 4,
                '_kui_toy_pilot_bridge_active': 4,
                '_kui_toy_pilot_bridge_skips': 4,
                '_kui_toy_pilot_native_diagnostics': 32,
                '_kui_toy_pilot_pause_detail': 4,
                '_kui_toy_pilot_pause_entries': 4,
                '_kui_toy_pilot_pause_max_attempts': 4,
                '_kui_toy_pilot_pause_pumps': 4,
                '_kui_toy_pilot_pause_retired': 4,
                '_kui_toy_pilot_pause_retries': 4,
                '_kui_toy_pilot_sci_irq_fault': 4,
                '_kui_toy_pilot_sci_release': 56,
                '_kui_toy_pilot_sci_vectors': 1344,
                '_kui_toy_pilot_updater_entries': 4,
                '_kui_toy_pilot_updater_returns': 4,
                '_last_tick': 4,
                '_owner': 3968,
                '_page': 4,
                '_pilot_words': 512,
                '_report': 1664,
                '_request': 96,
                '_retry_context': 4,
                '_row': 4,
                '_s': 148,
                '_schedule_report': 704,
                '_schedule_visit': 40,
                '_stopped_display': 56,
                '_trace_words': 1664,
                '_visit': 108}},
 3: {'resident-sci': {'_card': 72,
                      '_card_result': 4,
                      '_diagnostic': 64,
                      '_display': 56,
                      '_image': 544,
                      '_kui_retail_hook_active': 4,
                      '_kui_retail_hook_fault': 4,
                      '_kui_retail_hook_source': 4,
                      '_kui_retail_hook_sr': 4,
                      '_kui_retail_native_caller': 8,
                      '_kui_retail_original_menu': 4,
                      '_manifest': 1020,
                      '_pace': 44,
                      '_pacing': 8,
                      '_port': 20,
                      '_row': 4,
                      '_service': 204,
                      '_toy_gd_timing': 24,
                      '_toy_scratch': 4},
     'worker': {'_attempts': 4,
                '_data_probe_read_state': 112,
                '_data_probe_report': 768,
                '_data_probe_scope': 36,
                '_diagnostic_service': 4,
                '_first_tick': 4,
                '_kui_toy_pilot_bridge_active': 4,
                '_kui_toy_pilot_bridge_skips': 4,
                '_kui_toy_pilot_native_diagnostics': 32,
                '_kui_toy_pilot_pause_detail': 4,
                '_kui_toy_pilot_pause_entries': 4,
                '_kui_toy_pilot_pause_max_attempts': 4,
                '_kui_toy_pilot_pause_pumps': 4,
                '_kui_toy_pilot_pause_retired': 4,
                '_kui_toy_pilot_pause_retries': 4,
                '_kui_toy_pilot_updater_entries': 4,
                '_kui_toy_pilot_updater_returns': 4,
                '_last_tick': 4,
                '_owner': 3968,
                '_page': 4,
                '_pilot_words': 512,
                '_report': 1664,
                '_request': 96,
                '_retry_context': 4,
                '_row': 4,
                '_schedule_report': 704,
                '_schedule_visit': 40,
                '_stopped_display': 56,
                '_trace_words': 1664,
                '_visit': 108}}}
REVIEWED_STACK_ROWS = {1: {'sci/lto/resident-sci.elf.ltrans0.ltrans.su': 'c77cb0cee7b8f83a49f6ca37d26619ae6cc7ec93b0f68a4ea6bf37dd559b9502',
     'stage/src/core/ata.su': '178c891d8bd5d4b9c9d2c1014f86fffdb6fda981fad53e655deff7c0872ffcc8',
     'stage/src/core/hash.su': 'f43136e9635b1a3134cfb72eedf2ea4ae3cd5eaedb945781d4391a57d928bb9a',
     'stage/src/core/retail_image.su': '9b402a0b093fc0456f34270513010fa418b9906e4fc8c53f88449766d3142ce8',
     'stage/src/core/retail_observe.su': 'e78ec23dc9bfd3104497d7cee07130c0605fe8a5b1fb866e041651ef6e2ee1fa',
     'stage/src/dreamcast/ata_bus.su': 'd8c251cad54ee6ba63711d1b1eed8f32b9550496165da3f51a58bddf45950053',
     'stage/src/loader/minic.su': 'aee234f393f54b72c561d21ae9bbc05079733746748724f86ed5b878440bb471',
     'stage/src/loader/retail_display.su': '91800ba927951c42adbd563d8c7a8928423541c0a5fab639dec8873e1225aa79',
     'stage/src/loader/retail_sd.su': 'a68f39dfb6df67522668288144af464b84f61b8fabf9a2d82cbca7d700237087',
     'stage/src/loader/retail_storage.su': 'f0698dc22889dffac8e65e05975d865adfd118cecbf8289e0bb556bee63975a8',
     'stage/src/loader/sci_sd_bus.su': 'f49e78dd627082718590512dcbc23c091ea05ff27871a9154daded38de9ddf51',
     'stage/src/loader/sd_reader.su': '90bdad5b9fa7d63c702552638a50f94ead9115fb65a4f8f9c2180b1365df903c',
     'stage/src/loader/toy_pilot_stage.su': '8832b5a58b55fceb14a6b4ca98f42aaae25e11764145ed7c2e7f98bab8aaff65',
     'worker/src/core/hash.su': 'f43136e9635b1a3134cfb72eedf2ea4ae3cd5eaedb945781d4391a57d928bb9a',
     'worker/src/core/toy_pilot.su': '5a3e4b6994fcee5705056d476468902d39ed200f3a688bfd4bf62f3c72b881d7',
     'worker/src/loader/minic.su': 'aee234f393f54b72c561d21ae9bbc05079733746748724f86ed5b878440bb471',
     'worker/src/loader/retail_display.su': '91800ba927951c42adbd563d8c7a8928423541c0a5fab639dec8873e1225aa79',
     'worker/src/loader/toy_loader_data_probe.su': 'd0da3783d848cb68a6cc3d9c18e4bfc85a19d17cf06f6a92ca982f6c315edf09',
     'worker/src/loader/toy_loader_payload_control.su': '218e89415a5dd1799cbdb587cbd41b00eddc37eaeba9ba2b9f97fc9c056d6646',
     'worker/src/loader/toy_loader_schedule_report.su': 'd703f5fc4e3527e3c5fd7bce6dac6ce0dc042ea9f3945ddecb194da0bc375669',
     'worker/src/loader/toy_loader_trace.su': 'd27b751231d4c4f687afb04c2e09dc0c0bf10588c882d06a03ed71ced79ac887',
     'worker/src/loader/toy_loader_trace_report.su': '9c4426a075264c759372cd987b5dbfc3263fd454631443f5a9c6d23e34867c3c',
     'worker/src/loader/toy_pilot_bus.su': 'abd277304ccba2e6d77cc7cf12289eda4686100028e94c413e0f2f0937105819',
     'worker/src/loader/toy_pilot_gd.su': '56e079e8fb758382b8356c684211a3ce81ce3dd05a0b3c35ee106ef17590079a',
     'worker/src/loader/toy_pilot_lease.su': 'a738234f7af9f0514b59b230c0f33d4140b136495ac2f92111c807c8f0690dfa',
     'worker/src/loader/toy_pilot_pause.su': 'cc35e46ef5a9b2d4ee15ce85ca37999b2b7c61a4cfd782bcc4f50f046791eba7',
     'worker/src/loader/toy_pilot_worker.su': '2555c765510575508f9e4ea917f578ac90baba3acc389a4b0fd640e16ada668e'},
 2: {'sci/lto/resident-sci.elf.ltrans0.ltrans.su': 'cab5c3ac52c55ce5b005378727ee5b572af4b9b2c76d13d8870a80ef08240c5b',
     'stage/src/core/ata.su': '178c891d8bd5d4b9c9d2c1014f86fffdb6fda981fad53e655deff7c0872ffcc8',
     'stage/src/core/hash.su': 'f43136e9635b1a3134cfb72eedf2ea4ae3cd5eaedb945781d4391a57d928bb9a',
     'stage/src/core/retail_image.su': '9b402a0b093fc0456f34270513010fa418b9906e4fc8c53f88449766d3142ce8',
     'stage/src/core/retail_observe.su': 'e78ec23dc9bfd3104497d7cee07130c0605fe8a5b1fb866e041651ef6e2ee1fa',
     'stage/src/dreamcast/ata_bus.su': 'd8c251cad54ee6ba63711d1b1eed8f32b9550496165da3f51a58bddf45950053',
     'stage/src/loader/minic.su': 'aee234f393f54b72c561d21ae9bbc05079733746748724f86ed5b878440bb471',
     'stage/src/loader/retail_display.su': '91800ba927951c42adbd563d8c7a8928423541c0a5fab639dec8873e1225aa79',
     'stage/src/loader/retail_sd.su': 'a68f39dfb6df67522668288144af464b84f61b8fabf9a2d82cbca7d700237087',
     'stage/src/loader/retail_storage.su': 'f0698dc22889dffac8e65e05975d865adfd118cecbf8289e0bb556bee63975a8',
     'stage/src/loader/sci_sd_bus.su': 'da6f7753f4832482a4c098864b4832dec794f8e0a2f2db8b2c75ac3cda7b2839',
     'stage/src/loader/sd_reader.su': '90bdad5b9fa7d63c702552638a50f94ead9115fb65a4f8f9c2180b1365df903c',
     'stage/src/loader/toy_pilot_stage.su': '8832b5a58b55fceb14a6b4ca98f42aaae25e11764145ed7c2e7f98bab8aaff65',
     'worker/async_gd.su': 'f1d3c5e5daa63bf5a62c2758d266935828d5fb75f5dd396b9a8c89d5573f64b4',
     'worker/src/core/hash.su': 'f43136e9635b1a3134cfb72eedf2ea4ae3cd5eaedb945781d4391a57d928bb9a',
     'worker/src/core/retail_cursor.su': 'ae2703dc9cc923dfbcce34cea97270cf954e53ee7dc74267ddb8be7ae500bd0b',
     'worker/src/core/toy_pilot.su': '5a3e4b6994fcee5705056d476468902d39ed200f3a688bfd4bf62f3c72b881d7',
     'worker/src/loader/minic.su': 'aee234f393f54b72c561d21ae9bbc05079733746748724f86ed5b878440bb471',
     'worker/src/loader/retail_display.su': '91800ba927951c42adbd563d8c7a8928423541c0a5fab639dec8873e1225aa79',
     'worker/src/loader/sci_stream.su': '8e009c837426ee8cefbcfe51faf437c1031cd4bcc68b6891438d1639266e9955',
     'worker/src/loader/toy_loader_data_probe.su': 'd0da3783d848cb68a6cc3d9c18e4bfc85a19d17cf06f6a92ca982f6c315edf09',
     'worker/src/loader/toy_loader_payload_control.su': '218e89415a5dd1799cbdb587cbd41b00eddc37eaeba9ba2b9f97fc9c056d6646',
     'worker/src/loader/toy_loader_schedule_report.su': '974f2eb45f105c3daf5c9f9affe32165ff7e7e3181c100bcb2ce2b0f2bd6e904',
     'worker/src/loader/toy_loader_trace.su': 'd27b751231d4c4f687afb04c2e09dc0c0bf10588c882d06a03ed71ced79ac887',
     'worker/src/loader/toy_loader_trace_report.su': '9c4426a075264c759372cd987b5dbfc3263fd454631443f5a9c6d23e34867c3c',
     'worker/src/loader/toy_pilot_bus.su': 'abd277304ccba2e6d77cc7cf12289eda4686100028e94c413e0f2f0937105819',
     'worker/src/loader/toy_pilot_gd.su': '985008576d97d0148bd2d6add749f844bebc3653c5f57eb27121590c853e040b',
     'worker/src/loader/toy_pilot_lease.su': 'a738234f7af9f0514b59b230c0f33d4140b136495ac2f92111c807c8f0690dfa',
     'worker/src/loader/toy_pilot_pause.su': 'cc35e46ef5a9b2d4ee15ce85ca37999b2b7c61a4cfd782bcc4f50f046791eba7',
     'worker/src/loader/toy_pilot_sci.su': '4d747aff7144f22649501bc7bc47d3850587fac46d6992f9eed0386c712aed7a',
     'worker/src/loader/toy_pilot_worker.su': 'c6f39d01166e84930d9949903474064afa5b40446a4c7a5e5bf58f8f8520166f'},
 3: {'sci/lto/resident-sci.elf.ltrans0.ltrans.su': 'c77cb0cee7b8f83a49f6ca37d26619ae6cc7ec93b0f68a4ea6bf37dd559b9502',
     'stage/src/core/ata.su': '178c891d8bd5d4b9c9d2c1014f86fffdb6fda981fad53e655deff7c0872ffcc8',
     'stage/src/core/hash.su': 'f43136e9635b1a3134cfb72eedf2ea4ae3cd5eaedb945781d4391a57d928bb9a',
     'stage/src/core/retail_image.su': '9b402a0b093fc0456f34270513010fa418b9906e4fc8c53f88449766d3142ce8',
     'stage/src/core/retail_observe.su': 'e78ec23dc9bfd3104497d7cee07130c0605fe8a5b1fb866e041651ef6e2ee1fa',
     'stage/src/dreamcast/ata_bus.su': 'd8c251cad54ee6ba63711d1b1eed8f32b9550496165da3f51a58bddf45950053',
     'stage/src/loader/minic.su': 'aee234f393f54b72c561d21ae9bbc05079733746748724f86ed5b878440bb471',
     'stage/src/loader/retail_display.su': '91800ba927951c42adbd563d8c7a8928423541c0a5fab639dec8873e1225aa79',
     'stage/src/loader/retail_sd.su': 'a68f39dfb6df67522668288144af464b84f61b8fabf9a2d82cbca7d700237087',
     'stage/src/loader/retail_storage.su': 'f0698dc22889dffac8e65e05975d865adfd118cecbf8289e0bb556bee63975a8',
     'stage/src/loader/sci_sd_bus.su': 'f49e78dd627082718590512dcbc23c091ea05ff27871a9154daded38de9ddf51',
     'stage/src/loader/sd_reader.su': '90bdad5b9fa7d63c702552638a50f94ead9115fb65a4f8f9c2180b1365df903c',
     'stage/src/loader/toy_pilot_stage.su': '8832b5a58b55fceb14a6b4ca98f42aaae25e11764145ed7c2e7f98bab8aaff65',
     'worker/src/core/hash.su': 'f43136e9635b1a3134cfb72eedf2ea4ae3cd5eaedb945781d4391a57d928bb9a',
     'worker/src/core/toy_pilot.su': '5a3e4b6994fcee5705056d476468902d39ed200f3a688bfd4bf62f3c72b881d7',
     'worker/src/loader/minic.su': 'aee234f393f54b72c561d21ae9bbc05079733746748724f86ed5b878440bb471',
     'worker/src/loader/retail_display.su': '91800ba927951c42adbd563d8c7a8928423541c0a5fab639dec8873e1225aa79',
     'worker/src/loader/toy_loader_data_probe.su': 'd0da3783d848cb68a6cc3d9c18e4bfc85a19d17cf06f6a92ca982f6c315edf09',
     'worker/src/loader/toy_loader_payload_control.su': '218e89415a5dd1799cbdb587cbd41b00eddc37eaeba9ba2b9f97fc9c056d6646',
     'worker/src/loader/toy_loader_schedule_report.su': 'd703f5fc4e3527e3c5fd7bce6dac6ce0dc042ea9f3945ddecb194da0bc375669',
     'worker/src/loader/toy_loader_trace.su': 'd27b751231d4c4f687afb04c2e09dc0c0bf10588c882d06a03ed71ced79ac887',
     'worker/src/loader/toy_loader_trace_report.su': '9c4426a075264c759372cd987b5dbfc3263fd454631443f5a9c6d23e34867c3c',
     'worker/src/loader/toy_pilot_bus.su': 'abd277304ccba2e6d77cc7cf12289eda4686100028e94c413e0f2f0937105819',
     'worker/src/loader/toy_pilot_gd.su': '56e079e8fb758382b8356c684211a3ce81ce3dd05a0b3c35ee106ef17590079a',
     'worker/src/loader/toy_pilot_lease.su': 'a738234f7af9f0514b59b230c0f33d4140b136495ac2f92111c807c8f0690dfa',
     'worker/src/loader/toy_pilot_pause.su': 'cc35e46ef5a9b2d4ee15ce85ca37999b2b7c61a4cfd782bcc4f50f046791eba7',
     'worker/src/loader/toy_pilot_worker.su': '2555c765510575508f9e4ea917f578ac90baba3acc389a4b0fd640e16ada668e'}}
REVIEWED_RUNTIME_SHA256 = {1: '0f0a362345c43ff35cfd8960728dce19f72b24b04f1e253b958458245eacf9a3',
 2: 'f58f8dad7f51233f01088f68eb09fa78a7228aea3489ce8b62c12d691a77a1ef',
 3: 'bb2d25eb99b3ee6b75db87b643239bce7ef755b3f6bd017ca5525a4ebf6e6a97'}
CHANGED_LOW = {1: {'_step', '_kui_toy_pilot_base_dispatch'}, 2: set(), 3: set()}
CHANGED_HIGH = {'_dispatch_body', '_kui_toy_pilot_gd_dispatch',
                '_kui_toy_loader_trace_report_capture',
                '_kui_toy_loader_trace_report_page', '_kui_toy_loader_trace_terminal'} | set(u.REVIEWED_DATA)
CHANGED_STAGE = set()
FROZEN_SOURCES = {name: digest for name, digest in previous.FROZEN_SOURCES.items()
                  if name not in {'src/loader/retail_resident.c',
                                  'src/loader/toy_pilot_gd.c',
                                  'src/loader/toy_loader_trace_report.c',
                                  'src/loader/toy_pilot_worker.c',
                                  'src/loader/toy_pilot_resident.c'}}


def changed_functions(mode, image_name):
    if image_name == 'resident-sci':
        return CHANGED_LOW[mode]
    if image_name == 'worker':
        return CHANGED_HIGH | ({'_kui_toy_pilot_worker_initialize'} if mode == 2 else set())
    return CHANGED_STAGE


def read_config(directory, mode=None):
    config = {}
    for line in (Path(directory) / 'build-config').read_text().splitlines():
        if '=' not in line:
            raise ValueError('Malformed scheduling configuration')
        key, value = line.split('=', 1)
        if key in config:
            raise ValueError('Duplicate scheduling configuration: ' + key)
        config[key] = value
    if set(config) != set(REQUIRED_CONFIG) | {'BUILD', 'DATA_SCHEDULE_MODE'}:
        raise ValueError('Scheduling configuration has missing/unreviewed keys')
    if any(config.get(key) != value for key, value in REQUIRED_CONFIG.items()):
        raise ValueError('Scheduling must retain the exact synchronous CDDA/cache configuration')
    if config['DATA_SCHEDULE_MODE'] not in tuple(str(i) for i in MODES):
        raise ValueError('Scheduling mode must be 1, 2 or 3')
    if mode is not None and config['DATA_SCHEDULE_MODE'] != str(mode):
        raise ValueError('Scheduling mode differs from linked configuration')
    if not re.fullmatch(r'[0-9a-f]{12}', config.get('BUILD', '')):
        raise ValueError('Scheduling build must be twelve hexadecimal digits')
    return config


def load_profile(directory, mode=None):
    directory = Path(directory).resolve()
    config = read_config(directory, mode)
    objdump = shutil.which('sh-elf-objdump')
    if not objdump:
        raise ValueError('SH objdump is required for scheduling admission')
    images, dis = {}, {}
    for name, base, limit in (('resident-sci', LOW, LOW_LIMIT),
                             ('worker', HIGH, HIGH_LIMIT), ('stage', STAGE, STAGE_LIMIT)):
        raw = (directory / (name + '.elf')).read_bytes()
        # The retained installer already contains duplicate local names from
        # its independent ATA/SCIF/SCI units. Gameplay images must be unique;
        # the installer keeps its exact reviewed publication and immutable
        # runtime binding, without changing any prior gate's handling.
        if name != 'stage':
            reject_ambiguous_symbols(raw)
        options = {'private_p2': True} if name != 'stage' else {}
        if name == 'worker':
            options.update(entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
        image = inspect_elf(raw, base, limit, **options)
        image['symbol_sizes'] = linked_symbol_sizes(raw)
        image['readonly_ranges'] = u.readonly_ranges(raw)
        image['sized_symbol_records'] = sized_symbol_records(raw)
        if (directory / (name + '.bin')).read_bytes() != image['payload']:
            raise ValueError(name + '.bin differs from actual scheduling ELF bytes')
        images[name] = image
        dis[name] = subprocess.check_output([objdump, '-d', str(directory / (name + '.elf'))], text=True)
    attach_linked_symbols(images)
    return config, images, dis


def reject_ambiguous_symbols(raw):
    """Local names from separate C units must never overwrite safety bindings."""
    header = u.EH.unpack_from(raw)
    sections = [u.SH.unpack_from(raw, header[6] + i * header[11]) for i in range(header[12])]
    names_seen = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = u.region(raw, strings[4], strings[5], 'scheduling symbol names')
        for offset in range(section[4], section[4] + section[5], section[9]):
            name_offset, address, size, info, _, index = u.SYM.unpack_from(raw, offset)
            if not name_offset or not size or not index:
                continue
            end = names.find(b'\0', name_offset)
            if end < 0:
                raise ValueError('Unterminated scheduling symbol name')
            name = names[name_offset:end].decode('ascii')
            identity = (address, size, info & 15)
            if name in names_seen and names_seen[name] != identity:
                raise ValueError('Ambiguous independently owned scheduling symbol: ' + name)
            names_seen[name] = identity
    return len(names_seen)


def sized_symbol_records(raw):
    header = u.EH.unpack_from(raw)
    sections = [u.SH.unpack_from(raw, header[6] + i * header[11]) for i in range(header[12])]
    records, occurrences = [], {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = u.region(raw, strings[4], strings[5], 'scheduling sized symbols')
        for offset in range(section[4], section[4] + section[5], section[9]):
            no, at, size, info, _, index = u.SYM.unpack_from(raw, offset)
            if not no or not size or not index:
                continue
            finish = names.find(b'\0', no)
            if finish < 0:
                raise ValueError('Unterminated sized scheduling symbol')
            name = names[no:finish].decode('ascii')
            occurrence = occurrences.get(name, 0); occurrences[name] = occurrence + 1
            records.append({'name': name, 'address': at, 'bytes': size, 'type': info & 15,
                            'occurrence': occurrence})
    return records


def bounded_function(code, name):
    begin, end, rows = trace.exact_function(code, name)
    # Some retained low LTO .size entries extend into the following C entry.
    # Do not attribute that sibling's instructions to the preceding function.
    following = [row['address'] for row in code.image.get('sized_symbol_records', [])
                 if row['type'] == 2 and begin < row['address'] < end]
    instruction_addresses = {row[0] for row in code.rows if row[0] not in code.literals}
    following.extend(address for symbol, address in code.symbols.items()
                     if begin < address < end and address in instruction_addresses
                     and not symbol.startswith(('.', 'KUI_')))
    if following:
        end = min(following)
        rows = [row for row in rows if row[0] < end]
    return begin, end, rows


def attach_linked_symbols(images):
    atlas = [(image_name, r) for image_name, image in images.items()
             for r in image.get('sized_symbol_records', [])]
    anchors = [(image_name, name, at) for image_name, image in images.items()
               for name, at in image['symbols'].items() if not name.startswith(('.', 'KUI_'))]
    for image in images.values():
        image['linked_symbol_records'] = atlas
        image['linked_symbol_anchors'] = anchors


def _string_identity(code, value, build):
    """Only exact bounded immutable strings may relocate anonymously.

    Numeric literals do not become arbitrary in-image aliases. Binary rodata
    must have a named exact owned object; an anonymous non-string fails closed
    as an unchanged scalar rather than gaining a permissive alias identity.
    """
    for begin, end in code.image.get('readonly_ranges', []):
        if begin <= value < end:
            raw = code.data(value, min(end - value, 512))
            terminator = raw.find(b'\0')
            if terminator <= 0:
                return None
            raw = raw[:terminator + 1]
            if any(byte < 32 or byte > 126 for byte in raw[:-1]):
                return None
            if build.encode() in raw:
                if raw.count(build.encode()) != 1:
                    raise ValueError('Ambiguous relocated build string')
                raw = raw.replace(build.encode(), b'<BUILD>')
            return ('readonly_string', raw.hex())
    return None


def literal_identity(code, value, build):
    symbols, sizes = code.symbols, code.image['symbol_sizes']
    records = code.image.get('sized_symbol_records', [])
    exact_records = [r for r in records if r['address'] == value and not r['name'].startswith(('.', 'KUI_'))]
    if exact_records:
        record = min(exact_records, key=lambda r: (r['name'], r['bytes'], r['occurrence']))
        return ('typed_symbol', record['name'], record['occurrence'])
    string = _string_identity(code, value, build)
    if string is not None:
        return string
    exact_anchors = [(image_name, name) for image_name, name, at in code.image.get('linked_symbol_anchors', [])
                     if at == value]
    if exact_anchors:
        return ('exact_linked_anchor', *min(exact_anchors))
    inside_records = [r for r in records if r['address'] < value < r['address'] + r['bytes']
                      and not r['name'].startswith(('.', 'KUI_'))]
    if inside_records:
        record = min(inside_records, key=lambda r: (r['bytes'], r['name'], r['occurrence']))
        return ('typed_inside', record['name'], record['occurrence'], value - record['address'])
    if 0x8C000000 <= value < 0x8D000000 or 0xAC000000 <= value < 0xAD000000:
        candidates = [(image_name, r) for image_name, r in code.image.get('linked_symbol_records', [])
                      if physical(r['address']) <= physical(value) < physical(r['address']) + r['bytes']
                      and not r['name'].startswith(('.', 'KUI_'))]
        if candidates:
            image_name, record = min(candidates, key=lambda item: (item[1]['bytes'], item[1]['name'], item[1]['occurrence']))
            alias = 'P2' if value >= 0xAC000000 else 'P1'
            return ('linked_alias', image_name, record['name'], record['occurrence'],
                    physical(value) - physical(record['address']), alias)
        anchors = [(image_name, name) for image_name, name, at in code.image.get('linked_symbol_anchors', [])
                   if physical(at) == physical(value)]
        if anchors:
            return ('linked_anchor', *min(anchors), 'P2' if value >= 0xAC000000 else 'P1')
    exact = sorted(name for name, at in symbols.items() if at == value
                   and not name.startswith(('.', 'KUI_')))
    if exact:
        return ('symbol', exact[0])
    contained = [(size, name, value - symbols[name]) for name, size in sizes.items()
                 if size and symbols.get(name, 0) < value < symbols.get(name, 0) + size
                 and not name.startswith(('.', 'KUI_'))]
    if contained:
        _, name, offset = min(contained)
        return ('inside', name, offset)
    return ('value', value)


def function_identity(code, name, build, *, retained=True):
    """Retain all opcodes while resolving only real relocation operands.

    PC-relative literal displacement and external BSR displacement change when
    independently owned objects move. Their width/register, exact target and
    immutable string contents stay part of the identity. Local control-flow
    opcodes are kept unchanged. Instruction rows are checked against load bytes
    so stale/mutated disassembly cannot hide changed machine instructions.
    """
    begin, end, rows = bounded_function(code, name)
    result = []
    for at, opcode, mnemonic, _ in rows:
        if code.half(at) != opcode:
            raise ValueError('Disassembly differs from actual linked instruction: ' + name)
        if opcode & 0xF000 in (0xD000, 0x9000) and mnemonic in ('mov.l', 'mov.w'):
            width = 4 if opcode & 0xF000 == 0xD000 else 2
            target, value = code.literal(at, opcode >> 8 & 15, width)
            if begin <= target < end and target not in code.literals:
                raise ValueError('Retained instruction overlaps literal pool')
            stack_rule = u.REVIEWED_STACK_USES.get(name, {}).get(at - begin) if retained else None
            ro_rule = u.REVIEWED_RODATA_USES.get(name, {}).get(at - begin) if retained else None
            if retained and code.base == STAGE and (name, at - begin) in (
                    ('_kui_retail_stage_relay',0xba),('_kui_toy_pilot_stage_install',0xc2)):
                if width != 4 or value != code.symbols.get('__toy_pilot_worker_blob_end'):
                    raise ValueError('Retained installer lost its exact embedded worker endpoint')
                identity = ('embedded_worker_endpoint',)
            elif retained and code.base == LOW and name == '_kui_retail_resident_init' and at - begin == 0x11e:
                if width != 4 or not any(a <= value < value + 28 <= b for a, b in code.image.get('readonly_ranges', [])):
                    raise ValueError('Original low GD callback tuple left exact readonly bounds')
                words = [code.word(value + offset) for offset in range(0, 28, 4)]
                identity = ('low_original_callback_tuple', [literal_identity(code, word, build) for word in words])
            elif retained and code.base == LOW and name == '_kui_retail_gd_dispatch' and at - begin == 0x798:
                expected = bytes.fromhex('4744432056657273696f6e20312e313020313939392d30332d33310200')
                if width != 4 or code.data(value, len(expected)) != expected:
                    raise ValueError('Original GD GET_VERS immutable protocol bytes changed')
                identity = ('GD_GET_VERS', expected.hex())
            elif retained and code.base == STAGE and name == '_kui_retail_stage_main' and at - begin in (0xd2,0x10a,0x116,0x11e):
                if width != 4 or value != STAGE + len(code.payload):
                    raise ValueError('Stage relay literal lost exact initialized payload endpoint')
                identity = ('stage_initialized_payload_end',)
            elif retained and code.base == STAGE and name == '_kui_retail_stage_main' and at - begin == 0x6f0:
                expected = bytes.fromhex('b000008cb400008cb800008ce000008c')
                if width != 4 or code.data(value, len(expected)) != expected:
                    raise ValueError('Retained stage native vector addresses changed')
                identity = ('stage_native_vector_addresses', expected.hex())
            elif ro_rule:
                register, key = ro_rule
                expected = b'BUILD ' + build.encode() + b'\0' if key == 'BUILD' else u.RODATA_BYTES[key]
                if (opcode >> 8 & 15 != register or width != 4 or
                        not any(a <= value < value + len(expected) <= b for a, b in code.image.get('readonly_ranges', [])) or
                        code.data(value, len(expected)) != expected):
                    raise ValueError('Retained scoped readonly contents changed: ' + name)
                identity = ('reviewed_rodata', key)
            elif stack_rule:
                register, symbol, delta, canonical = stack_rule
                expected = code.symbols.get(symbol, 0) + delta
                if canonical:
                    expected = physical(expected)
                expected &= 0xFFFFFFFF
                if opcode >> 8 & 15 != register or width != 4 or value != expected:
                    raise ValueError('Retained independently reviewed stack-boundary literal changed: ' + name)
                identity = ('reviewed_stack', symbol, delta, canonical)
            else:
                identity = literal_identity(code, value, build)
            result.append((at - begin, opcode & 0xFF00, width, identity))
        elif opcode & 0xF000 in (0xA000, 0xB000):
            target = at + 4 + trace._signed(opcode & 0xFFF, 12) * 2
            if begin <= target < end:
                result.append((at - begin, opcode))
            else:
                result.append((at - begin, 'external_bsr' if opcode & 0xF000 == 0xB000 else 'external_bra', literal_identity(code, target, build)))
        else:
            result.append((at - begin, opcode))
    return hashlib.sha256(json.dumps((end - begin, result), separators=(',', ':')).encode()).hexdigest()


def frozen_sources(root=ROOT):
    observed = {}
    for name, expected in FROZEN_SOURCES.items():
        raw = (Path(root) / name).read_bytes()
        if name == 'src/loader/sci_sd_bus.c':
            # One new noipa export reads the original private health bits;
            # remove only that exact guarded addition before comparing the
            # complete original low SCI implementation to its published SHA.
            addition = re.compile(rb'^#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2\n/\* Startup-only typed export\.[\s\S]*?^#endif\n', re.M)
            matches = list(addition.finditer(raw))
            if len(matches) != 1 or b'bool __attribute__((noipa)) kui_toy_pilot_startup_sci_healthy(void)' not in matches[0].group():
                raise ValueError('Missing/ambiguous separately reviewed startup health export')
            raw = addition.sub(b'', raw)
        digest = hashlib.sha256(raw).hexdigest()
        if digest != expected:
            raise ValueError('Changed frozen low reader/CDDA source: ' + name)
        observed[name] = digest
    return observed


def audit_schedule_schema(root=ROOT):
    header = (Path(root) / 'include/kui/toy_loader_schedule_report.h').read_text()
    for name, value in (('KUI_TOY_SCHEDULE_REPORT_WORDS', 176), ('KUI_TOY_SCHEDULE_RECORDS', 8)):
        if re.findall(r'^#define\s+' + name + r'\s+(\d+)u\b', header, re.M) != [str(value)]:
            raise ValueError('Scheduling chronology schema constant changed: ' + name)
    return {'magic': '0x4c534331', 'version': 1, 'words': 176,
            'header_words': 16, 'engine_words': 32, 'records': 8, 'record_words': 16,
            'record_start': 48, 'publication_checksum': 'first credited sector CRC32'}


def compare_retained(directory, config, images, dis, baseline, *, discover=False):
    old_config, old_images, old_dis, binding = previous.audit_reference(baseline)
    for image_name in old_images:
        old_images[image_name]['sized_symbol_records'] = sized_symbol_records((Path(baseline) / (image_name + '.elf')).read_bytes())
    attach_linked_symbols(old_images)
    mode = int(config['DATA_SCHEDULE_MODE'])
    changed = {name: changed_functions(mode, name) for name in ('resident-sci', 'worker', 'stage')}
    names_by_image, identities, differences, new_names = {}, {}, {}, {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        old_code, code = Linked(old_images[image_name], base, old_dis[image_name]), Linked(images[image_name], base, dis[image_name])
        old_names = u.function_symbols((Path(baseline) / (image_name + '.elf')).read_bytes())
        names = u.function_symbols((Path(directory) / (image_name + '.elf')).read_bytes())
        missing = old_names - names
        if missing:
            raise ValueError('Scheduling removed retained functions: ' + ', '.join(sorted(missing)))
        names_by_image[image_name] = names
        new_names[image_name] = sorted(names - old_names)
        identities[image_name], differences[image_name] = {}, {}
        for name in sorted(old_names - changed[image_name]):
            old_begin, old_end, _ = bounded_function(old_code, name)
            new_begin, new_end, _ = bounded_function(code, name)
            old_bytes, new_bytes = old_code.data(old_begin, old_end - old_begin), code.data(new_begin, new_end - new_begin)
            if old_bytes == new_bytes:
                before = after = hashlib.sha256(b'byte-identical-function\0' + old_bytes).hexdigest()
            else:
                before = function_identity(old_code, name, previous.U_BUILD)
                after = function_identity(code, name, config['BUILD'])
            identities[image_name][name] = after
            if before != after:
                differences[image_name][name] = {'U': before, 'candidate': after}
        # Retained writable objects cannot silently change their sizes. Their
        # locations may move only within the already checked image lease.
        for name, size in old_images[image_name]['symbol_sizes'].items():
            address = old_images[image_name]['symbols'].get(name, 0)
            if size and base + P2 <= address < base + (HIGH_LIMIT - HIGH if image_name == 'worker' else LOW_LIMIT - LOW) + P2:
                expected_size = (112 if image_name == 'worker' and name == '_data_probe_read_state' else size)
                if images[image_name]['symbol_sizes'].get(name) != expected_size:
                    raise ValueError('Retained private object size changed: ' + name)
    if not discover and any(differences.values()):
        raise ValueError('Scheduling changed retained opcode/literal identity: ' + json.dumps(differences, sort_keys=True))
    return {'baseline_build': previous.U_BUILD, 'baseline_runtime': binding,
            'function_identities': identities, 'retained_mismatches': differences,
            'new_functions': new_names, 'functions': names_by_image}


def stack_inventory(directory):
    directory = Path(directory)
    found = {}
    for group in ('worker', 'stage', 'sci/lto'):
        for path in (directory / group).rglob('*.su'):
            found[path.relative_to(directory).as_posix()] = [[row['function'], row['bytes']]
                                                          for row in previous.static_rows(path)]
    if not found:
        raise ValueError('Missing scheduling static stack evidence')
    return found


def stack_signatures(inventory):
    return {name: hashlib.sha256(json.dumps(rows,separators=(',',':')).encode()).hexdigest()
            for name, rows in inventory.items()}


def audit_owned_objects(image, expected, prefix, base, limit):
    if not expected:
        raise ValueError('Missing reviewed scheduling BSS objects')
    begin = image['symbols'].get(prefix + '_bss_begin', 0)
    end = image['symbols'].get(prefix + '_bss_end', 0)
    if not base + P2 <= begin <= end <= limit + P2:
        raise ValueError('Scheduling BSS escaped its P2 lease')
    spans = []
    for name, size in expected.items():
        at = image['symbols'].get(name, 0)
        if image['symbol_sizes'].get(name) != size or not begin <= at < at + size <= end:
            raise ValueError('Scheduling object lost its exact P2 size/ownership: ' + name)
        spans.append((at, at + size, name))
    spans.sort()
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
        raise ValueError('Scheduling owned objects overlap')
    return {name: {'address': hex(at), 'bytes': end_at - at} for at, end_at, name in spans}


def audit_emitted_frames(directory, images, dis, inventory):
    """Check compiler frames against target prologues, including delay slots.

    The independently pinned complete inventory closes dropped helper rows.
    Authored assembly has separate exact bridge/IRQ checks and allowances.
    """
    result = {}
    for image_name, base, group in (('worker', HIGH, 'worker/'),
                                   ('resident-sci', LOW, 'sci/lto/')):
        code = Linked(images[image_name], base, dis[image_name])
        for report, rows in inventory.items():
            if not report.startswith(group):
                continue
            for name, expected in rows:
                symbol = '_' + name
                if symbol not in images[image_name]['symbols']:
                    # Only linked function rows have emitted instructions;
                    # their presence or absence is independently pinned.
                    continue
                frame = emitted_frame(code, symbol)
                if expected != frame:
                    raise ValueError('Scheduling .su differs from actual linked frame: ' + name)
                result[image_name + ':' + name] = frame
    return result


def emitted_frame(code, name):
    """Maximum authored SP allocation along all local control-flow paths.

    Conditional prologues must not be mistaken for zero-sized frames. Calls
    preserve SP by the checked integer ABI; callees are independently counted
    by the conservative source-unit budget. Unknown writes to SP refuse the
    build. Register values merge conservatively at joins.
    """
    begin, end, rows = trace.exact_function(code, name)
    rowmap = {row[0]: row for row in rows}
    initial = [None] * 16; initial[15] = 0
    states, pending, maximum = {begin: tuple(initial)}, [begin], 0

    def merge(at, registers):
        if at not in rowmap:
            raise ValueError('Stack control-flow leaves emitted function: ' + name)
        old = states.get(at)
        if old is not None and old[15] != registers[15]:
            raise ValueError('Inconsistent authored SP at control-flow join: ' + name)
        joined = tuple(registers) if old is None else tuple(a if a == b else None for a, b in zip(old, registers))
        if joined != old:
            states[at] = joined; pending.append(at)

    def effect(at, registers):
        nonlocal maximum
        _, op, mnemonic, operands = rowmap[at]
        n, m = op >> 8 & 15, op >> 4 & 15
        wrote_sp = False
        if op & 0xFF0F == 0x2F06 or (mnemonic in ('sts.l', 'stc.l') and '@-r15' in operands):
            registers[15] -= 4; wrote_sp = True
        elif (op & 0xF00F == 0x6006 and m == 15) or (mnemonic in ('lds.l', 'ldc.l') and '@r15+' in operands):
            registers[15] += 4; wrote_sp = True
            if mnemonic == 'mov.l': registers[n] = None
        elif op & 0xF000 == 0xE000:
            registers[n] = trace._signed(op & 255, 8); wrote_sp = n == 15
        elif op & 0xF000 == 0x7000:
            registers[n] = None if registers[n] is None else registers[n] + trace._signed(op & 255, 8)
            wrote_sp = n == 15
        elif op & 0xF00F == 0x6003:
            registers[n] = registers[m]; wrote_sp = n == 15
        elif op & 0xF00F in (0x300C, 0x3008):
            registers[n] = (None if registers[n] is None or registers[m] is None else
                            registers[n] + registers[m] if op & 15 == 12 else registers[n] - registers[m])
            wrote_sp = n == 15
        elif op & 0xF000 in (0xD000, 0x9000) and mnemonic in ('mov.l', 'mov.w'):
            width = 4 if op & 0xF000 == 0xD000 else 2
            registers[n] = code.literal(at, n, width)[1]
            if width == 2: registers[n] = trace._signed(registers[n], 16)
            wrote_sp = n == 15
        elif mnemonic not in ('cmp/eq', 'cmp/hs', 'cmp/hi', 'cmp/ge', 'cmp/gt', 'cmp/pl', 'cmp/pz', 'tst',
                                'bt', 'bf', 'bt.s', 'bf.s', 'bra', 'bsr', 'jsr', 'jmp', 'rts', 'nop'):
            destination = re.search(r'(?:^|,)r(\d+)$', operands.split('!', 1)[0].strip())
            if destination:
                destination = int(destination[1]); registers[destination] = None
                wrote_sp = destination == 15
        if registers[15] is None or (wrote_sp and registers[15] > 0):
            raise ValueError('Unproved emitted SP modification: ' + name)
        maximum = max(maximum, -registers[15])
        return registers

    while pending:
        at = pending.pop(); registers = list(states[at])
        _, op, mnemonic, _ = rowmap[at]
        delayed = (op & 0xF000 in (0xA000, 0xB000) or op & 0xFF00 in (0x8D00, 0x8F00)
                   or op & 0xF0FF in (0x400B, 0x402B) or op == 0x000B)
        if delayed:
            if at + 2 not in rowmap:
                raise ValueError('Missing emitted stack delay slot: ' + name)
            registers = effect(at + 2, registers)
            if op == 0x000B:
                if registers[15] != 0:
                    raise ValueError('Authored return leaves an allocated stack frame: ' + name)
                continue
            if op & 0xF000 == 0xA000:
                target = at + 4 + trace._signed(op & 0xFFF, 12) * 2
                if begin <= target < end:
                    merge(target, registers)
                elif registers[15] != 0:
                    raise ValueError('Authored external tail branch retains its stack frame: ' + name)
            elif op & 0xF000 == 0xB000 or op & 0xF0FF == 0x400B:
                registers[:8] = [None] * 8; merge(at + 4, registers)
            elif op & 0xF0FF == 0x402B:
                if registers[15] != 0:
                    raise ValueError('Authored tail call leaves an allocated stack frame: ' + name)
            else:
                merge(at + 4 + trace._signed(op & 255, 8) * 2, registers)
                merge(at + 4, registers)
        elif op & 0xFF00 in (0x8900, 0x8B00):
            merge(at + 4 + trace._signed(op & 255, 8) * 2, registers)
            merge(at + 2, registers)
        else:
            merge(at + 2, effect(at, registers))
    return maximum


def reviewed_shapes(config, images, dis, retained):
    mode = int(config['DATA_SCHEDULE_MODE'])
    if not REVIEWED[mode] or not REVIEWED_OBJECTS[mode] or not REVIEWED_STACK_ROWS[mode]:
        raise ValueError('Missing independently reviewed scheduling identities/objects/frames')
    result = {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        code = Linked(images[image_name], base, dis[image_name])
        expected = REVIEWED[mode].get(image_name, {})
        permitted = set(expected) | set(retained['function_identities'][image_name])
        if retained['functions'][image_name] != permitted:
            raise ValueError('Scheduling gained missing/unreviewed functions in ' + image_name)
        actual = {name: function_identity(code, name, config['BUILD'], retained=False) for name in expected}
        if actual != expected:
            raise ValueError('Changed independently reviewed scheduling body: ' + image_name)
        result[image_name] = actual
    return result


class P2SharedLinked(shared.Linked):
    """Reuse the separately proved integer IRQ/vector templates with P2 BSS."""
    def half(self, address):
        offset = physical(address) - HIGH
        if offset < 0 or offset + 2 > len(self.payload):
            raise ValueError('Scheduling IRQ read escaped its actual initialized worker')
        return int.from_bytes(self.payload[offset:offset + 2], 'little')


def audit_startup_irq(directory, worker, mode):
    forbidden = ('_kui_toy_pilot_sci_audio_acquire', '_kui_toy_pilot_sci_audio_release',
                 '_kui_toy_pilot_sci_audio_read')
    if any(name in worker['symbols'] for name in forbidden):
        raise ValueError('Startup scheduler gained an audio transport helper')
    if mode != 2:
        if any(name in worker['symbols'] for name in ('_kui_toy_pilot_sci_irq_entry',
                 '__toy_sci_irq_stack_bottom', '_kui_toy_pilot_sci_vectors')):
            raise ValueError('Synchronous schedule/control unexpectedly gained an SCI vector engine')
        return {'present': False, 'CDDA_transport_helpers': False}
    symbols = worker['symbols']
    bottom, top = (symbols.get(name, 0) for name in
                   ('__toy_sci_irq_stack_bottom', '__toy_sci_irq_stack_top'))
    if (top - bottom != 2048 or bottom % 32 or top % 32 or
            not symbols['__toy_pilot_bss_begin'] <= bottom < top <= symbols['__toy_pilot_bss_end'] or
            not HIGH + P2 <= bottom < top <= HIGH_LIMIT + P2):
        raise ValueError('Startup IRQ lost its independent guarded 2KiB P2 stack')
    linked = P2SharedLinked(worker)
    vector = shared.irq_vectors(linked)
    publication = shared.vector_publisher(linked)
    low_rows = stack_rows(list((Path(directory) / 'sci/lto').glob('*.ltrans*.su')))
    high_rows = stack_rows(list((Path(directory) / 'worker').rglob('*.su')))
    units = {'toy_pilot_sci.su','sci_stream.su','retail_cursor.su','async_gd.su','retail_gd.su','minic.su'}
    engine = [row for row in high_rows if Path(row['report']).name in units or row['function']=='kui_sci_sd_healthy']
    low_common = [row for row in low_rows if row['function'] not in ('kui_retail_resident_init','kui_toy_pilot_read_raw')]
    used = sum(row['bytes'] for row in engine + low_common) + 44
    if used > 2048 - 80:
        raise ValueError(f'Startup IRQ conservative participating-frame sum {used} exceeds 1968')
    stacks = {'conservative_bytes': used, 'available_bytes':1968,
              'assembly_allowance':44, 'all_engine_units_and_shared_low_callbacks_counted':True}
    return {'present': True, 'IRQ_P2_stack_bottom': hex(bottom), 'IRQ_P2_stack_top': hex(top),
            'integer_vector_handoff': vector, 'vector_cache_publication': publication,
            'independent_stacks': stacks, 'CDDA_transport_helpers': False}


def audit_schedule_stacks(directory, images, dis):
    """Bound each independent entry with its compiled participating units.

    GD reaches the audio owner only through three independently proven no-call
    integer leaves. Audio refill/SDK lifecycle/SHA driver verification cannot
    be nested beneath that entry. Count every other GD/TRACE/DATA/SCHED/engine
    frame, including mutually exclusive calls, plus all shared low callbacks.
    This avoids counting the entire unrelated audio worker on the GD stack.
    Audio still receives the original all-frame conservative bound.
    """
    directory = Path(directory)
    low_rows = stack_rows(list((directory/'sci/lto').glob('*.ltrans*.su')))
    high_rows = stack_rows(list((directory/'worker').rglob('*.su')))
    pure = pure_worker_leaf_audit(images['worker'], dis['worker'])
    leaf_names = {name[1:] for name in pure}
    audio_only = {'toy_pilot_worker.su','toy_pilot_pause.su','toy_pilot_bus.su',
                  'toy_pilot_lease.su','hash.su','toy_pilot.su'}
    gd_rows = [row for row in high_rows if Path(row['report']).name not in audio_only
               or row['function'] in leaf_names or row['function']=='kui_sci_sd_healthy']
    excluded_low = {'kui_retail_resident_init','kui_toy_pilot_read_raw'}
    low_common = [row for row in low_rows if row['function'] not in excluded_low]
    low_sum = sum(row['bytes'] for row in low_common) + 256
    gd_sum = sum(row['bytes'] for row in gd_rows + low_common) + 256
    audio_sum = sum(row['bytes'] for row in high_rows + low_common) + 256 + sum(row['bytes'] for row in low_rows if row['function']=='kui_toy_pilot_read_raw')
    limits = {'low':(low_sum,1232),'high_GD':(gd_sum,4000),'audio_worker':(audio_sum,8096)}
    for name,(used,available) in limits.items():
        if used>available:
            raise ValueError(f'Scheduling {name} participating-frame sum {used} exceeds {available}')
    return {'conservative_sums':{name:{'bytes':used,'available':available} for name,(used,available) in limits.items()},
            'GD_audio_access':pure,'GD_all_telemetry_engine_and_low_callback_frames_counted':True,
            'GD_excluded_audio_only_units':sorted(audio_only),
            'audio_retains_original_all_frame_bound':True,'assembly_allowance_bytes':256,
            'GD_participating_rows':gd_rows}


def audit_schedule_bridges(worker, disassembly, mode):
    if mode != 2:
        return trace.audit_bridges(worker,disassembly)
    code = Linked(worker,HIGH,disassembly);s=worker['symbols']
    gd=s['_kui_toy_pilot_gd_bridge']
    code.sequence(gd,(0x60F2,0x4F22,0x2F86,0x2F96,0x68F3,None,0x6212,None,0x3320,
        0x8B09,None,0x2F06,None,0x400B,9,None,0x6212,None,0x3320,0x8900,0xE0FF,
        0x6F83,0x69F6,0x68F6,0x4F26,0x000B,9))
    for off,reg,value in ((10,1,s['__toy_pilot_gd_stack_bottom']),(14,3,0xA55A4AA5),
        (20,15,s['__toy_pilot_gd_stack_top']),(24,0,s['_kui_toy_pilot_gd_dispatch']),
        (30,1,s['__toy_pilot_gd_stack_bottom']),(34,3,0xA55A4AA5)):
        if code.literal(gd+off,reg)[1]!=value:
            raise ValueError('Schedule GD bridge lost exact mask/argument/guard binding')
    if any(mn in ('ldc','ldc.l') and 'sr' in operands for _,_,mn,operands in trace.exact_function(code,'_kui_toy_pilot_gd_bridge')[2]):
        raise ValueError('Schedule GD bridge changed inherited SR')
    terminal=s['_kui_toy_loader_trace_terminal_bridge']
    code.sequence(terminal,(0x0102,None,0x221B,0x420E,0x2F16,0x4F22,0x2F86,0x2F96,
        0x68F3,None,0x6212,None,0x3320,0x8B03,None,None,0x400B,9,0x6F83,0x69F6,
        0x68F6,0x4F26,0x61F6,0x410E,0x000B,9))
    for off,reg,value in ((2,2,0x100000F0),(18,1,s['__toy_pilot_gd_stack_bottom']),
        (22,3,0xA55A4AA5),(28,15,s['__toy_pilot_gd_stack_top']),
        (30,0,s['_kui_toy_loader_trace_terminal'])):
        if code.literal(terminal+off,reg)[1]!=value:
            raise ValueError('Schedule terminal bridge lost exact SR/guard/owned entry')
    # The original guard loop and its operands are retained. Its forward/back
    # displacements alone span the extra independently reviewed SCI init call.
    init=code.find('_kui_toy_pilot_worker_initialize',
        (None,0x1126,None,None,0x3236,0x3128,0x7103,0x4109,0x7101,0x8B00,0xE101,0x4110,0x8B3A))
    for off,reg,value in ((0,7,0xA55A4AA5),(4,2,s['__toy_pilot_gd_stack_bottom']),
                          (6,1,s['__toy_pilot_gd_stack_top'])):
        if code.literal(init+off,reg)[1]!=value:
            raise ValueError('Startup initializer lost actual guarded GD stack')
    target=init+24+4+trace._signed(0x3A,8)*2
    code.sequence(target,(0x2272,0xAFC0,0x7204))
    return {'gd_incoming_SR_preserved':True,'gd_fifth_argument_copied_before_save':True,
        'gd_pre_and_post_guard_checks':True,'terminal_original_SR_PR_SP_and_r8_r9_restored':True,
        'terminal_mask_before_first_save':'0x100000f0','gd_stack_owned_guard_initialization':True,
        'initializer_extra_SCI_call_only_changes_guard_loop_branch_displacements':True}


def candidate(builddir, baseline=DEFAULT_BASELINE, mode=None):
    directory = Path(builddir).resolve()
    config, images, dis = load_profile(directory, mode)
    retained = compare_retained(directory, config, images, dis, baseline, discover=True)
    evidence = {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        code = Linked(images[image_name], base, dis[image_name])
        changed = changed_functions(int(config['DATA_SCHEDULE_MODE']), image_name)
        selected = set(retained['new_functions'][image_name]) | set(retained['retained_mismatches'][image_name]) | changed
        evidence[image_name] = {}
        for name in sorted(selected):
            if name not in retained['functions'][image_name]:
                continue
            begin, end, rows = trace.exact_function(code, name)
            evidence[image_name][name] = {'identity': function_identity(code, name, config['BUILD'], retained=False),
                'bytes': end - begin, 'instructions': [[hex(at), hex(op), mn, arg] for at, op, mn, arg in rows],
                'literals': [{'instruction': hex(at), 'value': hex(code.literal(at, op >> 8 & 15, 4 if op & 0xF000 == 0xD000 else 2)[1])}
                             for at, op, mn, _ in rows if op & 0xF000 in (0xD000, 0x9000) and mn in ('mov.l', 'mov.w')]}
    retained.pop('functions')
    return {'admitted': False, 'independent_review_required': True, 'config': config,
            'runtime': previous.bind_runtime(directory, config, images), 'retained': retained,
            'functions': evidence, 'stack_rows': stack_inventory(directory),
            'BSS_objects': {image_name: {name: {'address': hex(at), 'bytes': images[image_name]['symbol_sizes'].get(name)}
                for name, at in images[image_name]['symbols'].items() if base + P2 <= at < limit + P2 and images[image_name]['symbol_sizes'].get(name)}
                for image_name, base, limit in (('resident-sci', LOW, LOW_LIMIT), ('worker', HIGH, HIGH_LIMIT))}}


def audit_schedule(builddir, baseline=DEFAULT_BASELINE, mode=None):
    directory = Path(builddir).resolve()
    config, images, dis = load_profile(directory, mode)
    selected = int(config['DATA_SCHEDULE_MODE'])
    runtime = previous.bind_runtime(directory, config, images)
    if not REVIEWED_RUNTIME_SHA256[selected] or runtime['sha256'] != REVIEWED_RUNTIME_SHA256[selected]:
        raise ValueError('Scheduling runtime is not the independently reviewed immutable artifact')
    retained = compare_retained(directory, config, images, dis, baseline)
    reviewed = reviewed_shapes(config, images, dis, retained)
    inventory = stack_inventory(directory)
    if stack_signatures(inventory) != REVIEWED_STACK_ROWS[selected]:
        raise ValueError('Scheduling emitted static stack inventory changed or missing')
    for name in images:
        try:
            instruction_audit(name, dis[name])
        except SystemExit as exc:
            raise ValueError(str(exc)) from exc
    low, worker, stage = (images[name] for name in ('resident-sci', 'worker', 'stage'))
    objects = {}
    for image_name, expected in REVIEWED_OBJECTS[selected].items():
        base, limit, prefix = ((HIGH, HIGH_LIMIT, '__toy_pilot') if image_name == 'worker'
                               else (LOW, LOW_LIMIT, '__retail_resident'))
        objects[image_name] = audit_owned_objects(images[image_name], expected, prefix, base, limit)
    retained.pop('functions')
    return {'admitted': True, 'profile': MODES[selected], 'build': config['BUILD'],
            'runtime': runtime, 'retained': retained,
            'reviewed_functions': reviewed, 'frozen_sources': frozen_sources(), 'owned_objects': objects,
            'schedule_schema': audit_schedule_schema(),
            'retained_TRACE_graph': trace.audit_trace_graph(Linked(worker, HIGH, dis['worker'])),
            'retained_DATA_probe': {'schema': previous.audit_schema(),
                'bindings': previous.audit_bindings(directory, low, Linked(worker, HIGH, dis['worker']), 0),
                'passive_callgraph': previous.audit_probe_graph(Linked(worker, HIGH, dis['worker']), low, 0)},
            'exports': trace.audit_exports(low, worker),
            'bridges': audit_schedule_bridges(worker, dis['worker'],selected),
            'publication': trace.audit_boot_publication(low, worker, stage, dis['resident-sci'], dis['stage']),
            'cache_audio_heap': audit_cache_layout(low, worker, stage, dis['resident-sci'], dis['worker'], dis['stage']),
            'stacks': audit_schedule_stacks(directory, images, dis),
            'emitted_frames': audit_emitted_frames(directory, images, dis, inventory),
            'startup_IRQ': audit_startup_irq(directory, worker, selected),
            'hardware_behavior_verified': False, 'audio_or_video_improvement_proven': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builddir', type=Path)
    parser.add_argument('--baseline', type=Path, default=DEFAULT_BASELINE)
    parser.add_argument('--mode', type=int, choices=tuple(MODES))
    parser.add_argument('--candidate', action='store_true')
    args = parser.parse_args()
    print(json.dumps((candidate if args.candidate else audit_schedule)(args.builddir, args.baseline, args.mode), indent=2, sort_keys=True))
