#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the actual relocated native persistent worker, low SCI loan and source.

Integer gateway/ABI models establish the authored control transfers; they do
not establish console execution or universal game/reset compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import random
import shutil
import struct
import subprocess
import tempfile

import retail_package as layout
import check_retail_loader_layout as resident_layout
from check_vmu_native_heap_probe import (
    LOW, HIGH, LIMIT, typed_function, check_bss, inspect_dynamic_hook, regression,
)
from check_vmu_native_readonly import (ReadonlyGateway, relocated, model_hooks, model_mutations)
from check_vmu_native_worker_gateway import (
    MASK, P1, P2, literal_aware_instructions, model_gateway, read_elf, symbol_bytes,
    Gateway as StageGateway, ENTRY as P2_ENTRY, TOP, CONFIG, RESULT,
)
from check_loader_layout import inspect_elf, padded, region, SYM
from check_retail_instructions import audit as instruction_audit
from runtime_package import verify
from package_vmu_maple_observe import (linked_stack_rows, _function_base, check_pio_callgraph, linked_function_names)
from check_retail_stack import EDGE, NODE, load as load_graph
from vmu_maple_observe_symbols import sd_gateway_span
from check_vmu_pio_write import check_pio_entry
from check_vmu_native_cache_seed import audit as audit_cache_seed
from check_vmu_native_cache_gateway import check as check_cache_gateway
from check_vmu_native_irq_rearm import check as check_irq_rearm
from check_vmu_native_loader_timeline import check as check_loader_timeline
from check_vmu_native_sdk_sci_route import check as check_sdk_sci_route,pio_companion,pio_pair_companion
from check_vmu_native_stage_stack import check as check_stage_stack
from check_vmu_native_stage_stack import Machine
from check_vmu_native_mode import audit as audit_runtime_mode
from check_vmu_native_trace import check as audit_trace_gates, GATES as TRACE_GATES
from check_vmu_native_map_refusal import check as audit_map_refusal
from check_vmu_native_fault_capture import audit as audit_fault_capture
from check_vmu_native_fault_capture import GATEWAY as FAULT_GATEWAY, PUBLISH as FAULT_PUBLISH, STATE as FAULT_STATE
from vmu_native_relocations import generate, validate_loads
from test_armada_ce_maple_hook import CPU

ROOT = Path(__file__).resolve().parents[1]
SPAN = 0x20000
SDK_COHERENCE_GATES = ("_kui_vmu_native_sdk_coherence_uniform_gate",
                       "_kui_vmu_native_sdk_coherence_variable_gate")
SDK_COHERENCE_STATE = "_kui_vmu_native_sdk_coherence_state"
SDK_COHERENCE_PREPARE = "_kui_vmu_native_sdk_coherence_prepare"

# The prepared kernels are reviewed linked SH instructions, including their
# literal pools. Only ELF-proven pointer words are normalized; their exact
# typed object/function, local source owner and interior offset remain in the
# signature. This admits relocation/layout changes, not new code or targets.
# The A2 revision changes only reviewed selected-address calls, physical A1
# validation, the full DMA allocation and captured-frame stride. The SDK
# initializer target still requires a valid RAM alias of its proved address.
PREPARED_PINS = {
 '_kui_vmu_native_sdk_coherence_prepare': ('52ca590c4ac48e91620888af088aa871c0dfa4476cc386a73a828fc4af94ccbc', 436),
 '_kui_vmu_native_fault_capture_install': ('db26024d38eaa6e0c440fdadf83df04c43a91a653ab21637e427502550726afb', 560),
 '_read_heap': ('b102f5d502f2a2e45998b1555e7bd6e56d2b7048059ebb9bb37ed6ee415a58cd', 104),
 '_stack_valid': ('f5e81a00f19db27641f67958161041e8a865061f49fd71fbe114b165b2666db6', 116),
 '_native_phase_span': ('eb173e6c2fcbdabc829a622ae3cf7f3430ecae15cc95e2bab92314eb885ee2f2', 136),
 '_diagnostic_phase_read': ('9be746502cbe97b305edc7c282c70dd68d3b6d43dfafea092fecd4d7b975fcfc', 136),
 '_diagnostic_gesture': ('798c8a977a6e0b1e49fca8135d6a944513c0e2c8cf5c9342f0dd0f5f291f828f', 34),
 '_diagnostic_phase_row': ('9d55c81d38773acb609b68c1d6684dfc6be503622f4078799864c49148f5426d', 92),
 '_diagnostic_sample': ('cb7e980cb399fa654e837a6896d3ec033fd9d844c8fd0a2c31c77399471411fe', 164),
 '_diagnostic_begin': ('3be33ddc079ec9db0760339daf470b64f4903769d66dc830e14c2fcd0008ccc1', 40),
 '_native_phase_word.constprop.0': ('1e94b1f6d66b2667c76e878575a2f3b2fa57f04d0d65e0e735be7b88e2b54704', 56),
 '_diagnostic_pair_begin': ('c579977d4365a6b01f990fc9802e6db3a3bf75ad3b9fcb10ee2ac69c10ceecde', 584),
 '_diagnostic_end': ('6f5f038044a4d92b72a929a0a55d6f3c06740fc336e4d94c1aa49282ea4fbf57', 272),
 '_diagnostic_complete': ('aa64408dc10b6fa647159977dee5cd8deb9230ceee7eb19eba82f19f605c9129', 966),
 '_poll_lease_valid': ('b67c432db445f780a4b2e93871f7907b7f7de1fd8c0795faf112f30fe0673302', 64),
 '_installed_hooks_valid': ('819cc9127506f6790fa189371f453b3474bd2b3f9e39752d43a5d8501dd2e286', 76),
 '_kui_vmu_native_readonly_release_check': ('87433dd479877714f7773607b751f56b629a27792417dbb31ccfa430499e1474', 588),
 '_hook_word': ('69410002bf135878c32df63d2459603428947eadc760aa6309775f82b8801f9c', 132),
 '_prepare_hook_entry': ('e46707ed62da6217391c4aee61d074980abd67826655c1b508159548cabefd74', 228),
 '_kui_vmu_native_lifetime_prepare': ('4691642d40ae532e2f81cfe46c3f81a019412c0da0e43fb4f7913d4a44b87312', 640),
 '_kui_vmu_native_passive_arm': ('abdfa26b375d1bfbfc96c611aaaaabd02c66059e66b3d1b00d260f413cb20058', 2096),
 '_persistent_storage_guard': ('c51bd14af15bb22af8ad0a384c1c700a5c23a90fcd25e012ad977b7e7b77fa04', 396),
 '_persistent_lease': ('8658f150d0d4811335e46332a9a075f199fa8219edb5ccf1730223a4e34d78e3', 148),
 '_kui_vmu_native_passive_submit': ('6648f29a9c2be60b35c930d062a16d100353cbac12e485cf039f319bd742885d', 1384),
 '_kui_vmu_native_passive_complete': ('481728bfca6efd2ec8bf4663948e3b6c8f15dba21a0cbecf886467d9c1c323eb', 2704),
 '_controller_context_valid': ('e8e1eee80b1bce155a8a68c9c91229c138e017daf5bd2d880d7d51195efde2a0', 1032),
 '_kui_vmu_readonly_overlay_complete_absent': ('569ef7dcb97f1904c68ce440dd9a6bc3c0d143da123c0b8d37dc8c55da4f0a60', 284),
 '_kui_vmu_readonly_overlay_complete': ('d4fc62020bef09ab9056b5b7a16c8a47c616c446142d770d6a9e801ecf677601', 1004),
 '_readonly_overlay_parent_response': ('d396e68edd3158708ba5a8c9ff5235821bb62b71cc9dc40650cadb9098eaeb12', 116),
 '_readonly_overlay_increment': ('e3a6368a10593e4ae7510faf736d644a9d9e383e262070923241741b3f6893b1', 14),
 '_readonly_overlay_le32': ('1daa47eecc292df8356826c0e5a683e3a2c27cec212f901db7b1f01cf3b897df', 30),
 '_transfer_context_valid': ('6878e558790e8a9fa5a38a734e005735180821fb7f95ad98c6f30e2cc336de78', 1236),
 '_persistent_context_guard': ('7d7b44453ed058c85b22f3624a96001a93f68e06bd6dc581757cbff364c96df7', 132),
 '_kui_vmu_native_runtime_fault': ('602715b3a63ceb4cee060f7c4d3ddffd11d4158fadcf0b7f1602a10057a0bc44', 332),
 '.rodata': ('d8e3e0ee22d651048fd7853b6232536eb41c4045407aff6bf97a85b07490caea', 3864),
 '_kui_vmu_native_trace_enabled': ('0a920c3cf23dad2c3aabfe188567a6d1425af67dd54b46b619555831e9748eac', 16),
 '_kui_vmu_native_trace_image': ('e85462a3e477dee0a0124220436dc355360013e47136f21eb8576af465cdd9a9', 60),
 '_kui_vmu_native_trace_point': ('47f46b3337fb28b3281e2b5d56eacd66476fd830abdf74f3a1f67dc1068fae0b', 1132),
 '_kui_vmu_native_trace_wait_loop': ('14823a20fee8b883e5f8e7ebd6e7e10af9715be40c12e4674d8fa89f2839adcd', 176),
 '_kui_vmu_native_trace_init': ('cb04d77bf380406ced6994e9daf684e88aac772eaf5dbb91c93d6e2063ba07a8', 96),
 '_kui_vmu_native_trace_wait_before': ('e9b3f06c9f25ba26990d028291ee7c37f06e4fae6df7069ac0086d316321b5d9', 152),
 '_kui_vmu_native_trace_frame_entry': ('24be2fc972ae425d21e9fd5a0b8ad551f1f64bc20c8d7cb9118c00818bba1111', 164),
 '_kui_vmu_native_trace_report': ('d1f939d33649583417dc3a83df4775ee0ada70f1dbee6326b998305ace6ef070', 1120),
 '_kui_vmu_native_trace_wait_after': ('36746417db83b1f0060aacf65c6ad9fb76ce55aea985779d0d1244b70c6f30e6', 180),
 '_kui_vmu_native_trace_game': ('367d93d9d799bac69baf843535f58f139cac582df3d494ff5e4e4721ece346e3', 112),
 '_trace_history': ('707a8e97e89cde6ff13821edf89b71fd1791571defc72156990d7f12959c0eb6', 164),
 '_report': ('b598ee84327026939e1788074d3cc6d1cec220f8e0618e9c2d07e7def344bf52', 440),
 '_kui_vmu_native_trace_failure': ('bc5de95cdeaf5c8bbb975302655c91ac13a6e8058655665651019a647c655931', 120),
 '_trace_row.constprop.0': ('9d55c81d38773acb609b68c1d6684dfc6be503622f4078799864c49148f5426d', 92),
 '_trace_packets': ('95fdb20909abe914630c4c450ddce61102706f6689bd02fb776d8ee5f99f0f81', 116),
 '_kui_vmu_native_trace_irq': ('31b72b692190f12dabe3bee1767a9d43913918730d08b7c7c802bea816c8177e', 14)}


# Separate reviewed diagnostic image. Scheduling, storage and owner guards
# remain pinned; these additions bind RAM capture, its private state and new
# text/typed string offsets. The ordinary worker retains its original pins.
STALL_PREPARED_PINS = {
 '_diagnostic_end': ('c88c8dc8002887cdd643729f4469438081017b4b1273efd02175da143afa3b57', 1248),
 '_diagnostic_complete': ('b24624ba519db5e96771cc52668edf744d9c63f87b57ac91d27ef0485759813b', 1528),
 '_transfer_context_valid': ('864afeda319c05fad5f87de8222c254541d296bc34cd81513aa95c90d712d8b9', 1236),
 '_kui_vmu_native_runtime_fault': ('585ee839a421456d7aef53dca77e9558b6652d7b5c5198b67141cffac4352e79', 332),
 '.rodata': ('8fcde010e837b8b43b6112473d3191684e4651747591ae81921471f1c7de2305', 4420),
 '_kui_vmu_native_trace_report': ('06a3b3f1f6e3caf7e4a052c0232b45a5d01dd1ce8f2fc26bb6b9313059734f38', 1120),
 '_trace_history': ('0326ba7ea22d4d60253a55cc1950f8d4db51a17945bd4f56c21441b844831523', 164),
 '_report': ('5d48af4f97d25f73c0680f7c9a3f3589ed62e36e4373c551494ac65f1958cbb7', 440),
 '_trace_packets': ('7fc54ea8dcae371c4c35092c6baf4fe758b4002ce0bd75769c5aebb1d3b88e28', 116),
 '_reader_capture_region': ('13f1c4bb467b40f2f40ac8b27de13c73e5b0d90f098caec2c5b57d2b6bf95cfd', 100),
}


COOPERATIVE_PREPARED_PINS = {
 '_diagnostic_end': ('0dc0630080b686611a6798d79bbcac0f6f6445770fda330da07ec57692cfa6c2', 1372),
 '_diagnostic_complete': ('3dd34836554fc7fcf878b8f0df32bfd5202d0c1b380a3d8584bc5557fb1f07c0', 1636),
 '_kui_vmu_native_passive_complete': ('95a9e3ed44a3cdf947f2510c04d2382cc9c95423d53350610c2a5cff2579e5e3', 2712),
 '_transfer_context_valid': ('db4d82d003b435ec6f434203ab6e5e774cd2d0333ecad34f6130077e0b5b8dc3', 1236),
 '_kui_vmu_native_runtime_fault': ('959aa6c0a741aa03b179c7818a44b83292a82d3e72232b25a35a8d1dde47da85', 332),
 '.rodata': ('4042119b1c701edb28b921c75c41e07cbff6b3db2607363595953f4a15f32ec4', 4440),
 '_kui_vmu_native_trace_report': ('63dacf440db2a06109f497a5a5837680c7375129c45d3c98403b5e4a65a734db', 1120),
 '_trace_history': ('2dbd1fece83f2bf345714001904a2ea8a50bcd6712114d402fa7019e6c08e86b', 164),
 '_report': ('aea31ae8abe199a187fade98bda0110ea26b9eebaed17b290045dfbc142af017', 440),
 '_trace_packets': ('856815ab57af0c7101d9e36bfe8a01ad5e7e76486958c3d70516fa60d519e542', 116),
 '_reader_cooperative_service': ('bea9cb6d7bc3577b9bfe2ee4ae7072083c945c0cbc7b27fad426e9b9be38667e', 200),
}

TIMED_PREPARED_PINS = {
 '_diagnostic_complete': ('ee4030db7c1ba544f3b94174f26f720a5dbb4dd068fa783cb89d762570aafa5c', 1660),
 '_transfer_context_valid': ('c0643890f7423a7d2dc3a3978c063bb51431296ee2fd3527d013acb9edea1c4c', 1236),
 '_kui_vmu_native_runtime_fault': ('46ca6d85e1e795284808228ae2190599b518254284aec54cb6dc5e447bed5600', 332),
 '.rodata': ('8abb79edefa93fc768da07069005c8668970eb0fa235e5b4944e0ce00cd4f6b0', 4396),
 '_kui_vmu_native_trace_report': ('72e77855533038e2b36eaffe49c3c30b4849b66a9167db950db180e89111587f', 1120),
 '_trace_history': ('508ed02675ed370b486988d3f8494a5258a55cf393c73f501abb728d7416309c', 164),
 '_report': ('d3041ea663d14edc888423ca13380461c4e86ba7f6f450f6dfdd58461504d656', 440),
 '_trace_packets': ('e3e6dc04111ec2dceb78c4be4d100964a9cc3f5b81c99f1c8e266412a1b1ff33', 116),
 '_reader_cooperative_service': ('ad10663ade0ab6bde347e07ca31df0691a8f2f1c7ae05c8bb2b6b3cd4e51465a', 380),
 '_reader_window_clock': ('c416d56570b536794b03df8effe197babf770dd655364793aae27e1072304924', 60),
}

# Controller service is a pinned RTS/NOP in this experiment. Request timing
# and its report remain; pointer/string offsets stay independently bound.
PACED_PREPARED_PINS = {
 '_diagnostic_complete': ('b82324e75590de63308be5e79b759ed5972736e4064bc80e7bc597c4d0a95944', 1604),
 '_transfer_context_valid': ('ba4f7070be29872139d6b266cb4b83e2da696678828f1e8f0fa6c9aaccf91905', 1236),
 '_kui_vmu_native_runtime_fault': ('40b69a31d59ab52d6f64b97b53880e1a4190931a8e72734ab191ec1c8bd80b2e', 332),
 '.rodata': ('d9c1723c0db628355c2c9436cc6a29636749dade4519b48bdacab2c3b942a4f9', 4488),
 '_kui_vmu_native_trace_report': ('b1c88399d075a00257be669f190aff9c73687dfc1b4e1a5221e4328be0240797', 1120),
 '_trace_history': ('0ca4a7a7b2377a95a136ee07eb355767b6acf086f921e6ad21994ddc83fb820b', 164),
 '_report': ('99fbb78034fb36d000c0865e703e04d6b8564863d1d5713ffe2d5f8bacc97cb6', 440),
 '_trace_packets': ('cd06c541ee0b07d7bf0f6223d8f1b4ee70229edad46db9d09062850d6f47de1f', 116),
 '_reader_cooperative_service': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83', 4),
}

TIMELINE_PREPARED_PINS = {
 '_diagnostic_end': ('e42bc56b72fbbbf375b29b50d3ccd9a12a884248e2743dfc20ceae875f00f3b7', 1432),
 '_diagnostic_complete': ('c9917fa2da939d407c8655b9b2b58c968fbcb7db1899d1dabdf447cca448e765', 2820),
 '_transfer_context_valid': ('e8dfc67165fff7d12a1df60ac8e5785bf83ea16b0aa2574dd6450244698112a0', 1236),
 '_kui_vmu_native_runtime_fault': ('4bdc586daa750bebacdcf191fe21a7c30125c2a3ed34453e565280f6a1468a35', 332),
 '.rodata': ('6a88a132d4eab4019c9d02539abc1ece85cd3de726af74a300784689ea2aa31d', 5108),
 '_kui_vmu_native_trace_report': ('f62e00328dff4479fa2705d7ec1126cf8077d3d706c9baa1809e28b8ea434306', 1120),
 '_trace_history': ('2634123aeadfeec9b8d97b75568f50c0854067dbe5535244354ca62977c67a55', 164),
 '_report': ('495b4faeea4abb6ae6aa2b447e47a53d83cbf80e5c55970f4051008d2e31b813', 440),
 '_trace_packets': ('c3fa31e8b526e3936a387bbfc3d3debcecc696fab33613f3a6b8eee1f269cdfd', 116),
 '_kui_native_loader_timeline_visit': ('eb70c2499182c9ba9d638aa8c931cb768c8459630e56f057575d9e923e619f5c', 148),
 '_native_loader_snapshot': ('9592778ad157ebdd8b7f2ad0fd3efe92e05b17fc78c30d61ee6a0eba1c2410a0', 368),
 '_kui_native_loader_timeline_add': ('4d6b8073d0a1fd8de7359127083bbf20843381ed99df20a2d1c3c1f8b80d5b26', 856),
 '_kui_native_loader_timeline_reset': ('3e45ddcf599ecf09701486d6f30bc38a34bd5122e36349922416f6ee6d07ff0c', 24),
}

IRQ_REARM_PREPARED_PINS = {
 '_diagnostic_end': ('f47baec4099deef08b1556d8c453e9e472d9c9b6df765c9e963f437ba0780288', 1372),
 '_diagnostic_complete': ('bc3ea96b7a3d1e71c8a68a5df444b3ff72d4897372dc622b9859e1af1f16b644', 1680),
 '_transfer_context_valid': ('30af5fbde597dd166cee33474c913adf8b995f8abc4913ecc66d71062cf26a10', 1236),
 '_kui_vmu_native_runtime_fault': ('2acc36c6ec2d0f6cfb86c9b1cb030bbda4dae74ecb28a0b24f0e0fa115a00fca', 332),
 '.rodata': ('c9bbbf8def8e112c7617e7dd0e2452b62104ac6313672e128e7e3ff16fb7279e', 4448),
 '_kui_vmu_native_trace_report': ('4317d7bb82b66e969a4aaed7a5a687d7f76500e4f2a2eb399c9050f8e4ed88aa', 1120),
 '_trace_history': ('0519eb281371e21450042625f62da6cffdd9b88075c2b9684e19c9752fb7d19c', 164),
 '_report': ('2151b6aa708e94cec04567b87885a839042a894c88f3f9bcc78e0aa321a0fcee', 440),
 '_trace_packets': ('a47219521702e4e08577e3b7a0b7d37c9bee05186695875bb9a841c4f4489a0f', 116),
 '_reader_cooperative_service': ('501ea5b89ab4d8ff1b044d90f0e7b770df4b0bc60a561f96ec3ca179d28f6c83', 4),
}


def prepared_symbol_records(worker):
    data, _, _, sections, _ = worker
    records = []
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = region(data, strings[4], strings[5], "prepared symbol strings")
        source = ""
        for at in range(section[4], section[4] + section[5], SYM.size):
            name, value, size, info, _, owner = SYM.unpack(region(data, at, SYM.size, "prepared symbol"))
            name = names[name:].split(b"\0", 1)[0].decode("ascii")
            if info & 15 == 4:
                source = name
            elif name and (info & 15 in (1, 2) or name in
                           ("__vmu_native_worker_stack_bottom", "__vmu_native_worker_stack_top")):
                qualified = name + ("@" + source if not info >> 4 else "")
                records.append((qualified, value, size, info & 15, owner))
    strings = sections[worker[1][13]]
    names = region(data, strings[4], strings[5], "prepared section names")
    for owner, section in enumerate(sections):
        # GCC's anonymous switch tables have no STT_OBJECT. Their complete
        # constant section is separately pinned, including relocations into
        # exact function offsets. The separately bounded stack below is the
        # only other synthetic section target admitted here.
        if names[section[0]:].split(b"\0", 1)[0] == b".rodata" and section[2] & 7 == 2:
            records.append((".rodata", section[3], section[5], 3, owner))
    # Failure diagnostics can materialize stack_bottom+256. Retain the exact
    # zero-size linker-bound labels above, and admit an interior reference only
    # through this unique, allocated, private P2 NOBITS stack section. Its
    # complete4KiB extent must match both bound symbols and the worker lease.
    # Runtime callbacks use its P1 alias for ephemeral frames; only this exact
    # owned section and its two boundaries gain typed cached views. Persistent
    # mutable objects must still resolve at their original P2 addresses.
    bottom = worker[4]["__vmu_native_worker_stack_bottom"]
    top = worker[4]["__vmu_native_worker_stack_top"]
    require(bottom[1:3] == top[1:3] == (0, 0) and bottom[3] == top[3] and
            bottom[3] < len(sections), "prepared private stack boundary symbols differ")
    owner = bottom[3]
    stack = sections[owner]
    require(names[stack[0]:].split(b"\0", 1)[0] == b".stack" and
            stack[1] == 8 and stack[2] & 7 == 3 and
            bottom[0] == stack[3] == P2 + SPAN - 4096 and
            top[0] == bottom[0] + stack[5] == P2 + SPAN and stack[5] == 4096 and
            not (bottom[0] & 31) and not (top[0] & 31),
            "prepared private stack lacks exact P2 NOBITS ownership")
    require(sum(names[s[0]:].split(b"\0", 1)[0] == b".stack" for s in sections) == 1,
            "prepared private stack section is ambiguous")
    records.append((".stack", stack[3], stack[5], 3, owner))
    records.append((".stack.cached", stack[3] - 0x20000000, stack[5], 3, owner))
    for name, record in (("__vmu_native_worker_stack_bottom", bottom),
                         ("__vmu_native_worker_stack_top", top)):
        records.append((name + ".cached", record[0] - 0x20000000, 0, 0, owner))
    # The optional cold installer compares exact private BSS ownership
    # boundaries, including the P1 form of the lower bound. This does not
    # create a cached view of mutable BSS interiors.
    begin=worker[4]["__vmu_native_worker_bss_begin"]
    end=worker[4]["__vmu_native_worker_bss_end"]
    require(begin[1:3]==end[1:3]==(0,0) and begin[3]==end[3] and begin[3]<len(sections),
            "prepared private BSS boundary symbols differ")
    bss=sections[begin[3]]
    require(names[bss[0]:].split(b"\0",1)[0]==b".bss" and bss[1]==8 and bss[2]&7==3 and
            bss[3]==begin[0] and begin[0]+bss[5]==end[0] and
            P2+80<=begin[0]<end[0]<=bottom[0] and not(begin[0]&31) and not(end[0]&31),
            "prepared private BSS lacks exact P2 NOBITS ownership")
    require(sum(names[s[0]:].split(b"\0",1)[0]==b".bss" for s in sections)==1,
            "prepared private BSS section is ambiguous")
    records.append((".bss",begin[0],bss[5],3,begin[3]))
    for name,record in (("__vmu_native_worker_bss_begin",begin),("__vmu_native_worker_bss_end",end)):
        records.append((name,record[0],0,0,record[3]))
        records.append((name+".cached",record[0]-0x20000000,0,0,record[3]))
    exports=worker[4]["_kui_vmu_native_worker_exports"]
    require(exports[:3]==(P2,80,1) and exports[3]<len(sections),
            "prepared header boundary lacks exact immutable exports")
    header=sections[exports[3]]
    require(header[1]==1 and header[2]&7==2 and header[3]==P2 and header[5]==80,
            "prepared header boundary lacks exact immutable exports")
    records.append(("__vmu_native_worker_header_end.cached",P1+80,0,0,exports[3]))
    coherence=sdk_coherence_metadata(worker)
    if coherence["available"]:
        # These are exact function-entry identities, never a P2 code span.
        # Global relocation admission still requires the independently
        # executed cold-gateway proof and the two exact descriptor sites.
        for name in SDK_COHERENCE_GATES:
            address,_,_,owner=worker[4][name]
            records.append((name+".uncached",address+0x20000000,0,2,owner))
    return records


def prepared_signature(worker, binary, wire, name, build=None):
    """Closed signature over all body bytes and typed relocation bindings."""
    records = prepared_symbol_records(worker)
    if name == ".rodata":
        strings = worker[3][worker[1][13]]
        names = region(worker[0], strings[4], strings[5], "prepared section names")
        sections = [s for s in worker[3] if names[s[0]:].split(b"\0", 1)[0] == b".rodata" and
                    s[1] == 1 and s[2] & 7 == 2 and s[3] & 0xff000000 == 0x8c000000]
        require(len(sections) == 1, "prepared worker lacks unique cached constant section")
        section = sections[0]
        address, body = section[3], bytearray(region(worker[0], section[4], section[5], name))
        # Exactly one authored diagnostic label carries the independently
        # verified module/entry build identity. Normalize only its twelve
        # lowercase hexadecimal bytes; prefix, position and NUL stay pinned.
        require(isinstance(build, str) and re.fullmatch(r"[0-9a-f]{12}", build),
                "prepared constant signature needs verified runtime build identity")
        label = b"BUILD " + build.encode("ascii") + b"\0"
        require(body.count(b"BUILD ") == 1 and body.count(label) == 1,
                "prepared build identity differs from verified runtime")
        at = body.index(label) + 6
        body[at:at + 12] = bytes(12)
    else:
        address, raw, section = symbol_bytes(worker, name, 2)
        body = bytearray(raw)
    first = physical(address) - physical(P1)
    references = []
    for i in range(struct.unpack_from("<I", wire, 28)[0]):
        at, value = struct.unpack_from("<II", wire, 72 + 8 * i)
        if not first <= at < first + len(body):
            continue
        offset = at - first
        require(offset % 4 == 0 and offset + 4 <= len(body), "prepared pointer crosses body")
        require(struct.unpack_from("<I", body, offset)[0] == value, "prepared pointer differs from ELF body")
        found = [r for r in records if r[1] <= value < r[1] + r[2] or
                 not r[2] and r[1] == value]
        require(found, "prepared pointer has no typed target: " + hex(value))
        # Existing actual objects/functions retain their typed identities if
        # a BSS boundary coincides with their first byte. Synthetic BSS views
        # fill only the boundary/padding references with no prior typed owner.
        ordinary=[r for r in found if r[0]!=".bss" and not r[0].startswith("__vmu_native_worker_bss_")]
        if ordinary:found=ordinary
        target = min(found, key=lambda r: (r[2], r[0]))
        require(target[4] < len(worker[3]) and worker[3][target[4]][2] & 2,
                "prepared pointer target is not allocated")
        references.append((offset, target[0], value - target[1], target[3]))
        body[offset:offset + 4] = bytes(4)
    value = {"body": body.hex(), "references": references}
    digest = hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return digest, len(body), references


class AbsentCompletion(Machine):
    """Execute the complete absent publisher and its closed integer callees.

    The caller's independent prepare/DMA proofs supply a valid frame array.
    No backend is substituted: any call outside these four actual linked
    functions is rejected, including handler, admit and SD callbacks.
    """
    helper = '_kui_vmu_readonly_overlay_complete_absent'
    closure = (helper, '_readonly_overlay_parent_response',
               '_readonly_overlay_le32', '_readonly_overlay_increment')

    def __init__(self, worker, seed, command, count, targets, outcome, *, body=None):
        super().__init__(worker, self.helper, seed, 15, 0xAC11FFE0, body=body)
        self.functions = [symbol_bytes(worker, name, 2)[:2] for name in self.closure]
        if body is not None:
            self.functions[0] = self.functions[0][0], body
        self.data, self.external_stores, self.outcome = {}, [], outcome
        self.overlay = worker[4]['_overlay'][0]
        require(worker[4]['_overlay'][1:3] == (17824, 1), 'absent model needs exact private overlay object')
        self.r[4] = 0 if outcome == 'null' else self.overlay
        self.before_regs = self.r.copy(), self.pr, self.sr, self.fpscr, self.fr.copy()
        self.overlay_end = self.overlay + 17824
        for i in range(17824):
            self.data[self.overlay + i] = (i * 13 + seed) & 255
        self.put(self.overlay + 4, MASK if seed & 1 else 17)
        self.put(self.overlay + 36, MASK if seed & 1 else 19)
        self.put(self.overlay + 60, 0 if outcome == 'backend' else 0x8C155000)
        self.put(self.overlay + 68, 0 if outcome == 'magic' else 0x56524F31)
        self.put(self.overlay + 72, int(outcome != 'admitted'))
        self.put(self.overlay + 76, int(outcome != 'pending'))
        self.put(self.overlay + 80, 2); self.put(self.overlay + 84, count); self.put(self.overlay + 88, count - 1); self.put(self.overlay + 92, targets)
        self.rx = []
        for i in range(count):
            rx = 0xAC200000 + 1024 * i
            destination = 32 if i == count - 1 else (2 if i < targets else 1 if i == targets else 96)
            header = (0x01000000 if command == 9 else 0) | destination << 8 | command
            frame = self.overlay + 96 + 28 * i
            self.put(frame, header); self.put(frame + 12, rx & 0x1FFFFFFF); self.put(frame + 20, rx)
            for j in range(1024): self.data[rx + j] = (i + j * 19 + seed) & 255
            self.rx.append((rx, destination))
        parent = self.rx[-1][0]
        response = (28 if command == 1 else 48 if command == 2 else 3) << 24 | 33 << 16 | (5 if command == 1 else 6 if command == 2 else 8)
        if outcome == 'parent': response ^= 1
        if outcome == 'conflict': response |= 2 << 16
        self.put(parent, response); self.put(parent + 4, 0x01000000)
        self.before_data = self.data.copy()

    def put(self, address, value):
        for i in range(4): self.data[address + i] = value >> (8 * i) & 255

    def code(self, address, size):
        for first, body in self.functions:
            if first <= address < address + size <= first + len(body):
                return body[address - first:address - first + size]
        raise ValueError('absent completion leaves its exact linked integer closure: ' + hex(address))

    def read(self, address):
        require(not address & 3, 'unaligned absent word read')
        if all(address + i in self.data for i in range(4)):
            return sum(self.data[address + i] << (8 * i) for i in range(4))
        return struct.unpack('<I', self.code(address, 4))[0]

    def byte(self, address):
        require(address in self.data, 'absent byte read escapes prepared buffers')
        value = self.data[address]
        return (value - 256 if value & 128 else value) & MASK

    def store(self, address, value, width):
        if not self.start_sp - 256 <= address < address + width <= self.start_sp:
            require(self.overlay <= address < address + width <= self.overlay_end or
                    any(rx <= address < address + width <= rx + 1024 for rx, _ in self.rx),
                    'absent publisher writes outside overlay and prepared RX')
            self.external_stores.append((address, width))
        for i in range(width): self.data[address + i] = value >> (8 * i) & 255

    def write(self, address, value):
        require(not address & 3, 'unaligned absent word store'); self.store(address, value, 4)

    def plain(self, word, pc):
        n, m = word >> 8 & 15, word >> 4 & 15
        if word & 0xF00F == 0x6000:
            self.r[n] = self.byte(self.r[m]); return
        if word & 0xFF00 == 0x8400:
            self.r[0] = self.byte(self.r[m] + (word & 15)); return
        if word & 0xF00F == 0x2000:
            self.store(self.r[n], self.r[m], 1); return
        if word & 0xFF00 == 0x8000:
            self.store(self.r[m] + (word & 15), self.r[0], 1); return
        if word & 0xF00F == 0x600C:
            self.r[n] = self.r[m] & 255; return
        if word & 0xF00F == 0x6008:
            self.r[n] = self.r[m] & 0xFFFF0000 | (self.r[m] & 255) << 8 | (self.r[m] >> 8 & 255); return
        if word & 0xF0FF in (0x4018, 0x4028):
            self.r[n] = self.r[n] << (8 if word & 0xFF == 0x18 else 16) & MASK; return
        if word & 0xF00F == 0x0007:
            self.macl = self.r[n] * self.r[m] & MASK; return
        if word & 0xF0FF == 0x001A:
            self.r[n] = self.macl; return
        if word & 0xF00F == 0x600A:
            value = -self.r[m] - (self.sr & 1); self.r[n] = value & MASK; self.t(value < 0); return
        if word >> 8 == 0xCB:
            self.r[0] |= word & 255; return
        super().plain(word, pc)

    def check(self):
        r, pr, sr, fpscr, fr = self.before_regs
        require(self.r[8:] == r[8:] and self.pr == pr and self.sr & ~1 == sr & ~1 and
                self.fpscr == fpscr and self.fr == fr, 'absent publisher changes owner ABI state')
        result = 0 if self.outcome == 'ok' else -1 if self.outcome == 'null' else -6 if self.outcome == 'parent' else -7 if self.outcome == 'conflict' else -2
        require(self.r[0] == result & MASK, 'absent publisher changes refusal/return meaning')
        expected = self.before_data.copy()
        def old(offset):
            return sum(self.before_data[self.overlay + offset + i] << (8 * i) for i in range(4))
        def put(offset, value):
            for i in range(4): expected[self.overlay + offset + i] = value >> (8 * i) & 255
        if self.outcome in ('ok', 'parent', 'conflict'):
            parent = self.rx[-1][0]
            frame = self.overlay + 96 + 28 * (len(self.rx) - 1)
            for offset, value in ((44, self.read(frame)), (48, self.read(parent)), (52, parent & 0x1FFFFFFF)):
                put(offset, value)
        if self.outcome == 'ok':
            put(4, min(MASK, old(4) + 1)); put(76, 0)
            for index, (rx, destination) in enumerate(self.rx):
                if destination == 2:
                    for j in range(4): expected[rx + j] = 255
                    for offset, value in ((44, self.read(self.overlay + 96 + 28 * index)),
                                          (48, MASK), (52, rx & 0x1FFFFFFF)):
                        put(offset, value)
        elif self.outcome != 'null':
            put(72, 0); put(76, 0); put(36, min(MASK, old(36) + 1))
            put(40, old(40) | (64 if self.outcome == 'parent' else 128 if self.outcome == 'conflict' else 2))
        require(all(self.data[a] == value for a, value in expected.items()),
                'absent publisher changes a payload/physical response/private state or refuses after an RX store')
        for offset in (8, 12, 16):
            require(self.read(self.overlay + offset) == sum(self.before_data[self.overlay + offset + i] << (8 * i) for i in range(4)),
                    'absent publisher reports a virtual advertisement/reply/backend call')
        if self.outcome == 'ok':
            require(self.read(self.overlay + 76) == 0 and self.read(self.overlay + 72) == 1,
                    'absent completion loses its prepared lifecycle')
        require(all(self.overlay <= a < self.overlay_end or (width == 1 and
                    any(destination == 2 and rx <= a < rx + 4 for rx, destination in self.rx))
                    for a, width in self.external_stores), 'absent publisher has an unexpected store target')


def audit_absent_completion(worker, *, self_test=False):
    cases = 0
    for seed in range(2):
        for command in (1, 2, 9):
            for count, targets in ((1, 0), (2, 1), (4, 0), (4, 2), (32, 31)):
                for outcome in ('ok', 'null', 'magic', 'admitted', 'pending', 'backend', 'parent', 'conflict'):
                    AbsentCompletion(worker, seed, command, count, targets, outcome).run().check(); cases += 1
    # Execute the actual linked selector, with the config's exact last word.
    base, body, _ = symbol_bytes(worker, '_kui_vmu_native_passive_complete', 2)
    words = struct.unpack('<' + 'H' * (len(body) // 2), body)
    positions = [i for i in range(1,len(words)-10) if words[i]==0xe201 and words[i+2]&0xff0f==0x510f and
                 words[i+4:i+7]==(0x71fe,0x3126,0x8b00) and words[i+8:i+10]==(0x400b,9)]
    require(len(positions) == 1, 'missing unique actual hooks-only completion selector')
    i = positions[0]; config, size, kind, _ = worker[4]['_config']
    require((size, kind) == (2300, 1), 'hooks-only config word escaped its unchanged ABI')
    config_register=(words[i+2]>>4)&15
    require(words[i-1]&0xff00==0xd000|(config_register<<8),
            'completion selector lacks its exact config-base load')
    literal=((base+2*(i-1)+4)&~3)+4*(words[i-1]&255)
    require(struct.unpack_from('<I',body,literal-base)[0]==config+size-64,
            'completion selector config-base literal escaped the exact final word')
    routes = 0
    for seed in range(16):
        for mode in (0, 1, 2, 3, 4):
            m = Machine(worker, '_kui_vmu_native_passive_complete', seed, 15, 0xAC11FFE0)
            m.pc = base + 2 * i; m.r[config_register] = config + size - 64
            m.mem[(config & ~0x20000000) + size - 4] = mode
            m.stop_pc = base + 2 * (i + 10)
            def call(target, m=m, mode=mode):
                name = '_kui_vmu_readonly_overlay_complete_absent' if mode in (2,3) else '_kui_vmu_readonly_overlay_complete'
                require(target == worker[4][name][0] and m.r[4] == worker[4]['_overlay'][0],
                        'hooks-only selector changed mode or exact completion target')
                m.pc = m.stop_pc; m.calls.append(name); return True
            m.call = call; m.run(); require(len(m.calls) == 1, 'completion selector did not choose exactly one path'); routes += 1
    rejected = 0
    if self_test:
        # Exact source closure and typed pins catch arbitrary changes; this
        # independently exercises a changed header store and backend target.
        _, original, _ = symbol_bytes(worker, AbsentCompletion.helper, 2)
        mutations = [(struct.pack('<2H', 0xE2FF, 0x7160), struct.pack('<2H', 0xE200, 0x7160)),
                     (struct.pack('<H', 0x8033), struct.pack('<H', 0x8034)),
                     (struct.pack('<I', worker[4]['_readonly_overlay_le32'][0]),
                      struct.pack('<I', worker[4]['_kui_vmu_native_persistent_sd_transaction'][0]))]
        for old, new in mutations:
            require(original.count(old) == 1, 'missing unique absent header/callback mutation site')
            changed = original.replace(old, new)
            try: AbsentCompletion(worker, 0, 1, 2, 1, 'ok', body=changed).run().check()
            except (ValueError, AssertionError, KeyError): rejected += 1
            else: raise ValueError('absent model accepted changed no-response/header extent')
        changed = bytearray(body); struct.pack_into('<H', changed, 2 * (i + 4), 0x71fd)
        m = Machine(worker, '_kui_vmu_native_passive_complete', 0, 15, 0xAC11FFE0, body=bytes(changed))
        m.pc = base + 2 * i; m.r[config_register] = config + size - 64
        m.mem[(config & ~0x20000000) + size - 4] = 2; m.stop_pc = base + 2 * (i + 10)
        def reject_route(target):
            require(target == worker[4]['_kui_vmu_readonly_overlay_complete_absent'][0],
                    'wrong mode reached ordinary virtual publication')
            m.pc = m.stop_pc; return True
        m.call = reject_route
        try: m.run()
        except (ValueError, AssertionError, KeyError): rejected += 1
        else: raise ValueError('hooks-only selector accepted a profile/mode swap')
    return dict(helper_instruction_cases=cases, selector_instruction_cases=routes,
                mutation_rejections=rejected, actual_callees_only=True,
                no_backend_or_sd_call=True, no_parent_advertisement=True,
                physical_a1_replies_preserved=True, virtual_address=2,
                only_filtered_a2_four_byte_headers_changed=True)


class RetryCompletion(AbsentCompletion):
    """Run the linked publisher with an explicitly authored backend reply.

    This substitutes only the protocol handler, not any publisher predicate,
    helper, store or return. It proves wire acceptance/publication, not SD
    ownership, durable commit, original SDK resubmission or hardware timing.
    """
    helper='_kui_vmu_readonly_overlay_complete'
    closure=(helper,'_readonly_overlay_parent_response','_readonly_overlay_le32',
             '_readonly_overlay_increment','_readonly_overlay_put32',
             '_readonly_overlay_target_response_bytes')

    def __init__(self,worker,seed,command,outcome,*,body=None):
        super().__init__(worker,seed,9,3,1,'ok',body=body)
        self.command,self.outcome=command,outcome
        self.callback=worker[4]['_persistent_handler'][0]
        self.context=worker[4]['_durable_card'][0]
        self.put(self.overlay+56,self.callback);self.put(self.overlay+64,self.context)
        self.put(self.overlay+60,0 if outcome=='unfiltered' else worker[4]['_persistent_admit'][0])
        self.target=self.rx[0][0];self.parent=self.rx[-1][0]
        self.put(self.target,MASK)
        self.phase=1 if outcome=='sync-phase-one' else 4
        words=34 if command==12 else 2
        self.header=(words<<24)|(2<<8)|command
        self.location=0x2a000000|((self.phase<<8) if command==13 else 0)
        self.put(self.overlay+96,self.header);self.put(self.overlay+100,0x02000000)
        self.put(self.overlay+104,self.location)
        if command==12:
            request=self.overlay+1056+384
            self.put(request,self.header);self.put(request+4,0x02000000);self.put(request+8,self.location)
        for i in range(3):self.put(self.overlay+96+28*i+16,1024)
        self.before_data=self.data.copy();self.calls=[]

    def call(self,target):
        if target!=self.callback:return False
        require(self.r[4]==self.context and self.r[6]==(140 if self.command==12 else 12),
                'retry publisher changes authored handler context/request length')
        request,response=self.r[5],self.r[7]
        require(self.read(request)==self.header and self.read(request+4)==0x02000000 and
                self.read(request+8)==self.location and response==self.overlay+1056,
                'retry publisher changes immutable request or private staging buffer')
        require(all(self.data[rx+i]==self.before_data[rx+i] for rx,_ in self.rx for i in range(1024)),
                'retry publisher stores external RX before handler completion')
        self.calls.append('authored-handler')
        packet=bytearray((0xfc,0,2,0))
        if self.outcome=='success':
            packet=bytearray(struct.pack('<I',0x82020008)+struct.pack('<II',0x02000000,self.location)+
                bytes((i*7+3)&255 for i in range(512))) if self.command==11 else bytearray((7,0,2,0))
        if self.outcome=='host':packet[1]=1
        if self.outcome=='source':packet[2]=1
        if self.outcome=='words':packet[3]=1
        count=8 if self.outcome=='length' else 0 if self.outcome=='empty' else len(packet)
        require(self.read(self.r[15])==(384 if self.command==12 else 524),
                'retry handler changes exact private staged capacity')
        count_pointer=self.read(self.r[15]+4)
        require(self.start_sp-256<=count_pointer<count_pointer+4<=self.start_sp,
                'retry publisher moved its reply-length result outside its private C frame')
        for i,value in enumerate(packet):self.store(response+i,value,1)
        self.write(count_pointer,count)
        self.packet=bytes(packet);self.abi_return(1);return True

    def plain(self,word,pc):
        n,m=word>>8&15,word>>4&15
        if word&0xF0FF in (0x4019,0x4029):
            self.r[n]>>=8 if word&255==0x19 else 16;return
        if word&0xF00F==0x6001:
            address=self.r[m];value=self.data[address]|self.data[address+1]<<8
            self.r[n]=(value-65536 if value&0x8000 else value)&MASK;return
        if word&0xF00F==0x2001:self.store(self.r[n],self.r[m],2);return
        if word&0xF00F==0x000C:self.r[n]=self.byte(self.r[0]+self.r[m]);return
        if word&0xF00F==0x0004:self.store(self.r[0]+self.r[n],self.r[m],1);return
        if word&0xF00F==0x600D:self.r[n]=self.r[m]&65535;return
        if word&0xF00F==0x600B:self.r[n]=(-self.r[m])&MASK;return
        super().plain(word,pc)

    def run(self):
        # The successful524-byte response's actual byte loop exceeds the
        # stack-wrapper interpreter's1200-step slice. Retain a finite8-slice bound.
        for _ in range(8):
            try:return super().run()
            except AssertionError as error:
                if str(error)!='unbounded native stack instruction path':raise
        raise ValueError('unbounded linked retry publisher')

    def check(self):
        regs,pr,sr,fpscr,fr=self.before_regs
        require(self.r[8:]==regs[8:] and self.pr==pr and self.sr&~1==sr&~1 and
                self.fpscr==fpscr and self.fr==fr,'retry publisher changes owner ABI')
        ok=self.outcome in ('again','success')
        require(self.calls==['authored-handler'] and self.r[0]==(0 if ok else -8)&MASK,
                'retry publisher changes handler count or refusal meaning')
        for rx,_ in self.rx:
            expected=bytearray(self.before_data[rx+i] for i in range(1024))
            if ok and rx==self.target:expected[:len(self.packet)]=self.packet
            if ok and rx==self.parent:expected[2]|=2
            require(bytes(self.data[rx+i] for i in range(1024))==expected,
                    'retry publisher changes payload/physical A1 or publishes malformed reply')
        require(self.read(self.overlay+76)==0 and self.read(self.overlay+72)==int(ok),
                'retry completion changes prepared lifecycle')
        if not ok:require(self.read(self.overlay+40)&256,'malformed retry lacks handler-fault flag')
        return self

    def next_success(self):
        self.outcome='success';self.put(self.overlay+76,1)
        self.put(self.parent,0x03210008)
        self.pc=self.base;self.r[4]=self.overlay;self.r[15]=self.start_sp;self.pr=self.stop_pc
        self.before_regs=self.r.copy(),self.pr,self.sr,self.fpscr,self.fr.copy()
        self.before_data=self.data.copy();self.calls=[];self.external_stores=[]
        return self.run().check()


def audit_retry_completion(worker,*,self_test=False):
    cases=successes=0
    for seed in range(2):
        for command in (11,12,13):
            for outcome in ('again','host','source','words','length','empty','unfiltered'):
                model=RetryCompletion(worker,seed,command,outcome).run().check();cases+=1
                if outcome=='again':model.next_success();successes+=1
        RetryCompletion(worker,seed,13,'sync-phase-one').run().check();cases+=1
    rejected=0
    if self_test:
        _,original,_=symbol_bytes(worker,RetryCompletion.helper,2)
        mutations=[(struct.pack('<H',0x88fc),struct.pack('<H',0x8800),'again'),
            (struct.pack('<4H',0x84c1,0x2008,0x8b07,0x84c2),
             struct.pack('<4H',0x84c1,0x2008,0x0009,0x84c2),'host'),
            (struct.pack('<6H',0x84c2,0x51f9,0x600c,0x3010,0x8b02,0x84c3),
             struct.pack('<6H',0x84c2,0x51f9,0x600c,0x3010,0x0009,0x84c3),'source'),
            (struct.pack('<2H',0x60b3,0x8804),struct.pack('<2H',0x60b3,0x8808),'length')]
        for old,new,outcome in mutations:
            require(original.count(old)==1,'missing unique compiled retry predicate: '+outcome)
            changed=original.replace(old,new)
            try:RetryCompletion(worker,0,11,outcome,body=changed).run().check()
            except (ValueError,AssertionError,KeyError):rejected+=1
            else:raise ValueError('retry model accepts changed predicate: '+outcome)
    return dict(linked_instruction_cases=cases,retry_then_success_cases=successes,
        mutation_rejections=rejected,virtual_address=2,physical_a1_unchanged=True,
        retry_header_only_publication=True,exact_endpoint_length_and_command_gates=True,
        authored_backend_handler=True,storage_or_sdk_retry_scheduling_proved=False)


# The isolated read-ahead profile keeps every existing prepared gate. Its
# additional pins cover the larger owned lease in inline boundary predicates,
# the guarded heartbeat/capture-start calls and new post-stop report strings.
# Reporting-only references keep exact .rodata interior offsets; those shifts
# have their own pins rather than widening normalization for older profiles.
READ_AHEAD_PREPARED_PINS={
 '_diagnostic_complete':('ae4069a48558e8995d80b570f3a7f0e8747a4fbd776afbe784f908a0bb3fd50e',3044),
 '_kui_vmu_native_passive_arm':('03bca3f62c624f712f345b926605c8ae79d117ab24958689f57a7d79cd061c47',2096),
 '_transfer_context_valid':('3157605dd3abcd27c7df23dd9af52d678928548522c59a0f2b419bed8e910a59',1236),
 '_kui_vmu_native_runtime_fault':('97749aebb4133556ea1d7eb392451303664f11999406132c07d33b790bb2c461',332),
 '.rodata':('5329e541b5b4590b225003323326d1be9257bed891554e41cc2f8af2bd18c096',5388),
 '_kui_vmu_native_trace_report':('8b2733ebb0d989a6323a1dbf327c9681b960ef9fb37ff8943274aea135e563eb',1120),
 '_trace_history':('10756711968849a6ddeb810da3940ddd1e3b0829a814519c56b78205041f7a2c',164),
 '_report':('196717881c1f5eecd933a54d1b2c0b9306d1a5e67015cdbb29116bee4b5bdf26',440),
 '_trace_packets':('a8c24c3e20dab055719807a4e9f58a65fc1b02db34dd0198122e7be3bed8b326',116),
 '_reader_capture_region':('807f414ae76d5870a482e7952706e56e9f652c5168b3feb8db4316a4f827406c',100),
 '_reader_cooperative_service':('893c8094dbf3c0c0924711f02b67c739ad8f69c7286c21a93cc60d347be90e68',32),
}
# Exact diagnostic-only report expansion and shifted immutable string/table
# bindings. The previous read-ahead profile retains every preceding pin.
READ_AHEAD_DIAGNOSTIC_PREPARED_PINS={
 '_diagnostic_complete':('f5f751f8fc3c7ed8aab3864d8a58ac1fedbd6752d2bfd2fd0fe1fc259a920ea1',3284),
 '_kui_vmu_native_passive_arm':('8b1ad7a9ecd8a05a9cf8695d8f421c6ec8604fc8334ba29642656666574c716d',2096),
 '_transfer_context_valid':('3d4e8964a9bc123656fc7c5d77fe4c9e02c58889aa791babf6e694535d63d6fc',1236),
 '_kui_vmu_native_runtime_fault':('28ef621a29fd378b88096fe1ad97a1cef2d267a282f0219dda421d3bfcdb966a',332),
 '.rodata':('fc5fc124c7cd547100c38fea783b6a2318b057b0fbc5b606a0a11db7a2fc3813',5800),
 '_kui_vmu_native_trace_report':('3d3285dff3f6de74024330eb8ae906a36943b264289a1fb85638c28b3387894e',1120),
 '_trace_history':('37927a1eb4b01f1c35bfb46a0fa8071f58a0f606f71b9ed77646c9f874acd84e',164),
 '_report':('4706198f8c69896b415aefbde23a3b0ddcae7331af212a17263d04288110f032',440),
 '_trace_packets':('6231888cd04b42e7e0490458c2fb33b9a198b4f2cc775c5dce1f7b607c9855d6',116),
}
# The isolated one-block PIO report adds one three-value row. Keep the
# preceding diagnostic body's exact pins, including its string offsets.
READ_AHEAD_PIO_PREPARED_PINS={
 '_diagnostic_complete':('d1e5db3c76fc36728892a540a6df423ae10afdd908fab2081efdac358745f9d2',3296),
 '_transfer_context_valid':('dbdef3280d3f1ead790f01e9dd58b8c85115104e310d556ec73f7b5e2457c635',1236),
 '_kui_vmu_native_runtime_fault':('e81b98a7e3203847f5ec37a15588db47452d83d994ef6a4de418088cedd44e4b',332),
 '.rodata':('7f4cbc0711524ba59b87b401aea50960c6de3ec04518df804e046c9239bf67f8',5824),
 '_kui_vmu_native_trace_report':('28eebbd4da06fc5c8dbe2cae1663a38bcdb976e1b04b06f10166e6a67e2bfc87',1120),
 '_trace_history':('c2ba15d88253f65d2cf9dce747012648b70051eca90e084ff526d828eff55d8d',164),
 '_report':('496bd5eb4dbfbb986320ccb5ade6e3eef97a39e2e8a278b042e3bbd2fa03e97b',440),
 '_trace_packets':('39babcd833d67ea240255738a48e2b3e59da709964f252e4b8b2799619b99cda',116),
}
READ_AHEAD_PIO_PAIR_PREPARED_PINS={
 '_diagnostic_complete':('fbe80a816bbf48bbef120f04eb806d331de87a1ce5bf3d5121993cb38cbd040e',3312),
 '_transfer_context_valid':('f7723db064e47a871812211754deb4eadd35f43b3c103d376741c6abfe80f3e0',1236),
 '_kui_vmu_native_runtime_fault':('19d5b8dd56b7f2850ab065cded240db783ed9b7a63e87b9fe11d28397b6eb40c',332),
 '.rodata':('4a9fe349145125e9ded984d76538c83b9025a447d2b188f2ec97f18f59afd42d',5844),
 '_kui_vmu_native_trace_report':('157922428e1b3091cdf82322a1abb5a8d6191de5204eea6d7554f9cbde7ac9fa',1120),
 '_trace_history':('f64fc4c7572f10c9fb3feb18031c0864148c89b1093a80eb63e232265a23fa27',164),
 '_report':('46c412fb6ae18186ad63ed87985e26d74e5567f8d22a930146a96d9168f67c37',440),
 '_trace_packets':('75312cc024c9d433b226c53b54eb92dbd1363579102b5dc1a9d83c3be19dda9b',116),
}

def check_prepared_lifetime(directory, worker, binary, wire, self_test=False, *, build=None,
                            read_ahead_proof=None):
    """Keep the legacy proof, or require the complete prepared proof family."""
    if "_lifetime_plan" not in worker[4]:
        _, _, section = symbol_bytes(worker, "_kui_vmu_native_lifetime_release_safe_ranges", 2)
        require(section[2] & 7 == 6, "missing legacy lifetime admission")
        return {"prepared": False, "legacy_full_release_admission": True}
    if build is None:
        build = verify((directory / "retail-maple-observe.kui").read_bytes())["build"]
    stall_marker=worker[4].get('__kui_native_reader_stall_capture')
    if stall_marker is not None:
        require(stall_marker==(1,0,0,0xfff1),'invalid reader stall diagnostic marker')
    cooperative=worker[4].get('__kui_native_cooperative_reader')
    if cooperative is not None:
        require(stall_marker and cooperative==(1,0,0,0xfff1),'invalid cooperative reader marker')
        require(COOPERATIVE_PREPARED_PINS,'missing reviewed cooperative reader pins')
    timed=worker[4].get('__kui_native_timed_reader')
    if timed is not None:
        require(cooperative and timed==(1,0,0,0xfff1),'invalid timed reader marker')
        require(TIMED_PREPARED_PINS,'missing reviewed timed reader pins')
    paced=worker[4].get('__kui_native_paced_reader')
    if paced is not None:
        require(cooperative and not timed and paced==(1,0,0,0xfff1),'invalid paced reader marker')
    irq_rearm=worker[4].get('__kui_native_irq_rearm_reader')
    if irq_rearm is not None:
        require(cooperative and not paced and not timed and irq_rearm==(1,0,0,0xfff1),
                'invalid IRQ rearm reader marker')
        require(IRQ_REARM_PREPARED_PINS,'missing reviewed IRQ rearm reader pins')
    timeline=worker[4].get('__kui_native_loader_timeline_worker')
    if timeline is not None:
        require(irq_rearm and timeline==(1,0,0,0xfff1),'invalid loader timeline marker')
        require(TIMELINE_PREPARED_PINS,'missing reviewed timeline pins')
    read_ahead=worker[4].get('__kui_native_readahead_worker')
    if read_ahead is not None:
        require(timeline and read_ahead==(1,0,0,0xfff1),'invalid prepared read-ahead marker')
    read_ahead_diagnostic=worker[4].get('__kui_native_readahead_diagnostic_worker')
    require(bool(read_ahead_diagnostic)==bool(read_ahead_proof and read_ahead_proof.get('diagnostic_capture')),
            'prepared read-ahead diagnostic worker/auxiliary proof differs')
    if read_ahead_diagnostic:
        require(read_ahead and read_ahead_diagnostic==(1,0,0,0xfff1) and
                read_ahead_proof.get('enabled') and read_ahead_proof.get('paired_markers') and
                read_ahead_proof.get('schema')=='kui.native.read-ahead.audit.v1' and
                read_ahead_proof.get('artifact_sha256',{}).get('worker')==hashlib.sha256(worker[0]).hexdigest() and
                read_ahead_proof.get('diagnostics',{}).get('enabled') and
                read_ahead_proof.get('diagnostics',{}).get('schema')=='kui.native.read-ahead.diagnostics.audit.v1',
                'prepared diagnostic pins require the complete exact auxiliary proof')
    read_ahead_pio=pio_companion(worker,read_ahead_proof)
    read_ahead_pio_pair=pio_pair_companion(worker,read_ahead_proof)
    require(not read_ahead_pio or read_ahead_diagnostic,'prepared PIO profile lacks its diagnostic worker')
    pins={**PREPARED_PINS,**(STALL_PREPARED_PINS if stall_marker else {}),
          **(COOPERATIVE_PREPARED_PINS if cooperative else {}),
          **(TIMED_PREPARED_PINS if timed else {}),**(PACED_PREPARED_PINS if paced else {}),
          **(IRQ_REARM_PREPARED_PINS if irq_rearm else {}),
          **(TIMELINE_PREPARED_PINS if timeline else {}),
          **(READ_AHEAD_PREPARED_PINS if read_ahead else {}),
          **(READ_AHEAD_DIAGNOSTIC_PREPARED_PINS if read_ahead_diagnostic else {}),
          **(READ_AHEAD_PIO_PREPARED_PINS if read_ahead_pio else {}),
          **(READ_AHEAD_PIO_PAIR_PREPARED_PINS if read_ahead_pio_pair else {})}
    objects=[("_lifetime_plan", 616), ("_hook_witness", 384),
                       ("_hook_witness_count", 4), ("_hook_witness_ready", 4),
                       ("_protected_ranges", 512), ("_protected_count", 4),
                       ("_controller_pair", 188), ("_diagnostic_phase", 976), ("_diagnostic", 144)]
    if FAULT_STATE in worker[4]:objects.append((FAULT_STATE,32))
    if stall_marker:objects.append(('_reader_capture',508))
    if cooperative:objects.append(('_reader_cooperative',40 if timed else 24))
    for name,size in objects:
        address, actual, kind, owner = worker[4][name]
        section = worker[3][owner]
        require(kind == 1 and actual == size and address % 4 == 0 and
                section[2] & 7 == 3 and section[1] == 8 and
                section[3] <= address < address + actual <= section[3] + section[5] and
                P2 <= address < address + actual <= worker[4]["__vmu_native_worker_stack_bottom"][0],
                "prepared object lacks exact private P2 BSS ownership: " + name)
    functions = {}
    for name, expected in pins.items():
        signature, size, references = prepared_signature(worker, binary, wire, name, build)
        require((signature, size) == expected, "prepared linked instructions/targets changed: " + name)
        functions[name] = {"normalized_sha256": signature, "bytes": size,
                           "typed_pointer_bindings": len(references)}
    require(pins, "missing reviewed prepared linked signatures")
    graph = (directory / "native/src/loader/vmu_native_passive.ci").read_text()
    edges = [(a, b) for a, b, _ in EDGE.findall(graph)]
    # GCC emits unlabeled edges for its split read_value.part.0 helper.
    # Retain those real emitted calls rather than guessing a source location.
    edges += re.findall(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)" \}', graph)
    local = "src/loader/vmu_native_passive.c:"
    for caller, required in (
            ("kui_vmu_native_passive_arm", ("kui_vmu_native_lifetime_prepare", local + "prepare_hook_entry", local + "hook_word")),
            ("kui_vmu_native_passive_submit", (local + "installed_hooks_valid", local + "poll_lease_valid",
                "kui_vmu_volatile_chain_build_address", "kui_vmu_volatile_chain_verify")),
            ("kui_vmu_native_passive_complete", (local + "installed_hooks_valid", local + "poll_lease_valid")),
            ("kui_vmu_native_readonly_release_check", (local + "installed_hooks_valid",)),
            (local + "controller_context_valid", (local + "transfer_context_valid",)),
            (local + "transfer_context_valid", (local + "stack_valid",)),
            (local + "persistent_storage_guard", ("kui_vmu_native_heap_lease_valid", local + "stack_valid",
                local + "installed_hooks_valid", "kui_vmu_native_layout_bind_checked", "kui_vmu_native_runtime_check",
                local + "chain", "kui_vmu_volatile_chain_verify"))):
        require(all((caller, target) in edges for target in required),
                "prepared admission loses required emitted call: " + caller)
    # The address/width/alias checks are compiled into these two complete
    # pinned bodies. Reintroducing an out-of-line reader would move those
    # predicates outside their reviewed byte signatures and restore the
    # measured call/spill cost. The linked reader model below independently
    # executes the same full predicate for4B RAM/MMIO boundaries.
    for caller in (local+"controller_context_valid",local+"transfer_context_valid"):
        require(not any(a==caller and b.startswith((local+"read_value",local+"ram"))
                        for a,b in edges),"prepared context address checks are not inlined")
    if SDK_COHERENCE_STATE in worker[4]:
        require(("kui_vmu_native_passive_arm","kui_vmu_native_sdk_coherence_prepare") in edges,
                "SDK cold coherency state preparation is missing from worker arm")
    require(edges.count((local + "persistent_lease", local + "persistent_storage_guard")) == 2,
            "storage loan must retain full guards at both boundaries")
    rejected = 0
    if self_test:
        # Independent code and valid-but-wrong pointer-target mutations. The
        # signatures bind the loop bounds, fail branches and every literal.
        for name in pins:
            signature, size, references = prepared_signature(worker, binary, wire, name, build)
            if name == ".rodata":
                continue
            address, body, _ = symbol_bytes(worker, name, 2)
            for at in (0, size - 2):
                changed = bytearray(worker[0])
                owner = worker[3][worker[4][name][3]]
                changed[owner[4] + address - owner[3] + at] ^= 1
                mutant = (bytes(changed),) + worker[1:]
                try:
                    different = prepared_signature(mutant, binary, wire, name)[:2] != (signature, size)
                except ValueError:
                    different = True
                require(different,
                        "prepared signature accepts instruction mutation: " + name)
                rejected += 1
            if references:
                changed = bytearray(wire)
                changed_elf = bytearray(worker[0])
                first = physical(address) - physical(P1)
                offset = references[0][0]
                for i in range(struct.unpack_from("<I", wire, 28)[0]):
                    at, value = struct.unpack_from("<II", wire, 72 + i * 8)
                    if at == first + offset:
                        struct.pack_into("<I", changed, 76 + i * 8, value + 4)
                        owner = worker[3][worker[4][name][3]]
                        struct.pack_into("<I", changed_elf, owner[4] + address - owner[3] + offset, value + 4)
                        break
                try:
                    mutant = (bytes(changed_elf),) + worker[1:]
                    different = prepared_signature(mutant, binary, bytes(changed), name)[:2] != (signature, size)
                except ValueError:
                    different = True
                require(different, "prepared signature accepts changed typed pointer: " + name)
                rejected += 1
    return {"prepared": True, "exact_linked_kernels": functions, "build_identity": build,
            "read_ahead_diagnostic_companion_required":bool(read_ahead_diagnostic),
            "read_ahead_pio_companion_required":read_ahead_pio,
            "read_ahead_pio_pair_companion_required":read_ahead_pio_pair,
            "constant_build_identity_bound_to_verified_runtime": True, "identity_words": 8,
            "protected_interval_limit": 64, "hook_word_limit": 32, "free_node_limit": 1024,
            "full_dynamic_free_list_each_release": True, "full_storage_guards_before_and_after_loan": True,
            "emitted_callgraph_bound": True, "mutation_rejections": rejected,
            "controller_fastlane": {"source_word_limit": 32, "main_controller_limit": 4,
                "private_pair_bytes": 188, "original_dma_unchanged": True,
                "mixed_main_queries": "B-D INFO/ALLINFO; A controller GETCOND required",
                "profile_uses_same_fast_lane": True,
                "optional_phase_private_bytes": 976, "phase_ring_records": 16,
                "publication": "one parent-A presence byte after whole-batch validation",
                "full_storage_guards_unchanged": True,
                "synthetic_read_count_is_not_hardware_time": True},
            "hooks_only": audit_absent_completion(worker, self_test=self_test),
            "retry_publication":audit_retry_completion(worker,self_test=self_test)}

# Whole integer trampoline, including its P2 resume literal and zero padding.
TRAMPOLINE_PREFIX = bytes.fromhex(
    "062f162f262f362f462f562f662f762f862f962fa62fb62fc62fd62fe62f224f"
    "034f234f134f024f124f0200f0cb0e4001d02b4009000900")


def resume_template(cached, relay):
    """Authored complete game_resume instruction body and all literal words."""
    ops = [0x68f3, 0, 0, 0x6902, 0, 0, 0x00a3, 0x7020, 0x3012, 0x8bfb,
           0, 0, 0x2012] + [9] * 8 + [0x6483, 0x6593, 0, 0x400b, 9,
           0, 0x209b, 0, 0x2102] + [9] * 8 + [0x6f83, 0x4f16, 0x4f06,
           0x4f17, 0x4f27, 0x7f04, 0x4f26] + [0x60f6 | r << 8 for r in range(14, 0, -1)] + \
          [0x60f3, 0x70c0, 0x6002, 0x400e, 0, 0x402b, 0x60f6]
    assert len(ops) == 66
    values = [0x8cff0000, 0xff00001c, 0x8c000000, 0x8d000000, 0x808]
    if cached:
        values.append(0x908)
    values += [relay, 0xac010000]
    for index, reg, word in ((1, 15, 0), (2, 0, 1), (4, 0, 2), (5, 1, 3),
                              (10, 0, 1), (11, 1, 5 if cached else 4),
                              (23, 0, 6 if cached else 5), (26, 0, 4),
                              (28, 1, 1), (63, 0, 7 if cached else 6)):
        at = 132 + 4 * word
        delta = at - ((2 * index + 4) & ~3)
        assert delta >= 0 and delta % 4 == 0
        ops[index] = 0xd000 | reg << 8 | delta // 4
    return struct.pack("<66H", *ops) + struct.pack("<" + "I" * len(values), *values)


class ResumeGateway(CPU):
    """Execute actual pinned assembly, folding only its proven RAM purge loop."""
    def __init__(self, seed, mask, ccr, resume, blob, trampoline, relay, cached):
        super().__init__(seed, resume, 1)
        self.resume, self.blob, self.trampoline, self.relay = resume, blob, trampoline, relay
        self.cached = cached
        self.r[15] = 0x8c00ed00
        self.start_sp = self.r[15]
        self.sr = (self.sr & ~0xf0) | mask << 4
        self.vbr = random.Random(seed + 83).getrandbits(32)
        self.owner_ccr, self.ccr = ccr, ccr
        self.pc, self.mem, self.called, self.purged = 0xac010000, {}, 0, False
        self.ccr_writes, self.wait = [], 0
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                        self.vbr, self.sr, self.fpscr, self.fr.copy())

    def code(self, address, size):
        if 0xac010000 <= address < address + size <= 0xac010000 + len(self.trampoline):
            return self.trampoline[address - 0xac010000:address - 0xac010000 + size]
        assert self.resume + 0x20000000 <= address < address + size <= self.resume + 0x20000000 + len(self.blob), \
            "cache-control relay must execute through P2"
        offset = address - self.resume - 0x20000000
        return self.blob[offset:offset + size]

    def read(self, address):
        if address == 0xff00001c:
            return self.ccr
        if address in self.mem:
            return self.mem[address]
        return struct.unpack("<I", self.code(address, 4))[0]

    def write(self, address, value):
        if address == 0xff00001c:
            assert self.pc >= self.resume + 0x20000000 and self.purged
            assert not self.wait
            self.ccr_writes.append(value & MASK)
            expected = (0x908 if self.cached else 0x808) if len(self.ccr_writes) == 1 else self.owner_ccr | 0x808
            assert value == expected and len(self.ccr_writes) <= 2, "unexpected CCR mode or owner-state loss"
            # ICI and OCI are command bits; reads retain the enable/mode bits.
            self.ccr, self.wait = value & ~0x808, 8
            return
        assert address % 4 == 0 and self.start_sp - 84 <= address < self.start_sp
        assert not self.ccr_writes, "relay writes original owner frame after publication"
        self.mem[address] = value & MASK

    def plain(self, word, pc):
        if self.wait:
            assert word == 9, "CCR access lacks eight following NOPs"
            self.wait -= 1
        if word in (0x0002,):
            self.r[0] = self.sr; return
        if word == 0x4f03:
            self.r[15] -= 4; self.write(self.r[15], self.sr); return
        if word in (0x4f23, 0x4f27):
            if word == 0x4f23:
                self.r[15] -= 4; self.write(self.r[15], self.vbr)
            else:
                self.vbr = self.read(self.r[15]); self.r[15] += 4
            return
        if word & 0xf00f == 0x200b:
            self.r[word >> 8 & 15] |= self.r[word >> 4 & 15]; return
        super().plain(word, pc)

    def relay_call(self):
        assert not self.wait and self.purged and self.called == 0
        assert self.r[4:6] == [self.start_sp - 84, self.owner_ccr]
        assert self.r[15] == 0x8cff0000 and self.sr & 0xf0 == 0xf0
        assert self.ccr == (0x100 if self.cached else 0) and not self.ccr & 1
        self.called += 1
        for i in range(8):
            self.r[i] = (0x92345678 + 0x1234567 * i) & MASK
        self.mach ^= 0x12345678; self.macl ^= 0x98765432
        self.gbr ^= 0x13572468; self.vbr ^= 0x87654321
        self.sr ^= 1
        self.pc = self.pr

    def run(self):
        for _ in range(200):
            pc = self.pc
            # Exact four-op loop is byte-pinned below. Folding preserves the
            # final R0/T and proves every aligned line in the 16MiB RAM range.
            if pc == self.resume + 0x20000000 + 12:
                assert self.code(pc, 8) == struct.pack("<4H", 0x00a3, 0x7020, 0x3012, 0x8bfb)
                assert self.r[:2] == [0x8c000000, 0x8d000000]
                self.r[0] = self.r[1]; self.t(True); self.purged = True
                self.pc += 8
                continue
            word = struct.unpack("<H", self.code(pc, 2))[0]
            if word & 0xf0ff in (0x402b, 0x400b):
                target = self.r[word >> 8 & 15]
                if word & 0xf0ff == 0x400b:
                    self.pr = pc + 4
                self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                if target == self.relay:
                    self.relay_call()
                elif target == 0xac010000:
                    self.pc = target
                    return self
                else:
                    self.pc = target
            else:
                self.plain(word, pc); self.pc += 2
        raise AssertionError("unbounded cache publication gateway")

    def check(self):
        r, pr, mach, macl, gbr, vbr, sr, fpscr, fr = self.initial
        assert self.r == r and self.pc == 0xac010000 and self.called == 1
        assert (self.pr, self.mach, self.macl, self.gbr, self.vbr, self.sr, self.fpscr, self.fr) == \
               (pr, mach, macl, gbr, vbr, sr, fpscr, fr)
        assert self.ccr_writes == [0x908 if self.cached else 0x808, self.owner_ccr | 0x808]
        assert self.ccr == self.owner_ccr & ~0x808 and not self.wait


def check_resume_publication(stage, cached, self_test=False):
    symbols = stage[4]
    resume = symbols["_kui_retail_game_resume"][0]
    relay, _, _ = symbol_bytes(stage, "_kui_retail_stage_relay", 2)
    require(resume % 4 == 0 and HIGH <= resume < LIMIT and HIGH <= relay < LIMIT,
            "unbounded cache relay symbols")
    expected = resume_template(cached, relay)
    image = inspect_elf(stage[0], HIGH, LIMIT)
    require(region(image["payload"], resume - HIGH, len(expected), "cache resume") == expected,
            "linked game_resume cache mode/publication/owner restoration changed")
    trampoline = symbols["__retail_trampoline_start"][0]
    require(symbols["__retail_trampoline_end"][0] == trampoline + 128 and
            symbols["__retail_trampoline_code_end"][0] == trampoline + 60,
            "owner trampoline span changed")
    template = TRAMPOLINE_PREFIX + struct.pack("<I", resume + 0x20000000) + bytes(68)
    require(region(image["payload"], trampoline - HIGH, 128, "cache trampoline") == template,
            "linked owner trampoline/P2 resume changed")
    cases = 0
    for seed in range(40):
        for mask in range(16):
            for ccr in (0, 1, 4, 0x20, 0x80, 0x100, 0x105, 0x1a7):
                ResumeGateway(seed, mask, ccr, resume, expected, template, relay, cached).run().check()
                cases += 1
    rejected = 0
    if self_test:
        # Coherent instruction/literal changes must fail the closed model too.
        mutations = [(12, 0x0009), (26, 0xe000), (60, 0xe000), (124, 0x0009),
                     (148, 0x0809), (156 if cached else 152, 0x0009)]
        if cached:
            mutations += [(152, 0x0808), (152, 0x0909)]
        for at, value in mutations:
            changed = bytearray(expected); struct.pack_into("<H", changed, at, value)
            try:
                ResumeGateway(0, 3, 0x100, resume, bytes(changed), template, relay, cached).run().check()
            except (AssertionError, struct.error):
                rejected += 1
            else:
                raise ValueError("resume gateway model accepted mutation at" + str(at))
    pure_cached='__kui_native_cached_extent' in symbols
    return {"instruction_cache_enabled_for_scan": cached, "operand_cache_disabled_for_scan": not pure_cached,
            "operand_cache_off_except_scoped_pure_extent":pure_cached,
            "scan_ccr": "0x00000908" if cached else "0x00000808", "owner_ccr_restored": True,
            "full_ram_purge_bytes": 0x1000000, "cache_write_nops": [8, 8],
            "owner_integer_frame_bytes": 84, "gateway_model_cases": cases,
            "gateway_mutation_rejections": rejected, "complete_linked_assembly_pinned": True}


def check_worker_publication(stage, cached, disassembly, self_test=False, *, background_tail=None):
    """Pin the P2 publisher and witness initialization before its first arm."""
    image = inspect_elf(stage[0], HIGH, LIMIT)
    symbols = stage[4]
    address = symbols["_kui_native_passive_publish"][0]
    ops = [0xd001, 0x402b, 9, 9] + list(struct.unpack("<2H", struct.pack("<I", address + 12 + 0x20000000))) + \
          [0xe0e0, 0x2409, 0x3452, 0x8903, 0x04a3, 0x7420, 0xaffa, 9,
           0xd007, 0x6102, 0xd207, 0x212b, 0x2012] + [9] * 8 + [0x000b, 9, 9] + \
          list(struct.unpack("<4H", struct.pack("<2I", 0xff00001c, 0x800)))
    expected = struct.pack("<34H", *ops)
    require(address % 4 == 0 and region(image["payload"], address - HIGH, len(expected), "I-cache publisher") == expected,
            "linked P2 code publisher cache purge/ICI/owner CCR preservation changed")
    report = {"publisher_p2": hex(address + 12 + 0x20000000), "publisher_bytes": len(expected),
              "exact_linked_publisher_pinned": True, "ici_only_preserves_ccr": True,
              "post_ccr_write_nops": 8, "worker_prearm_publication_required": cached}
    if not cached:
        return report
    begin, body, _ = symbol_bytes(stage, "_native_passive_run", 2)
    words = list(struct.unpack("<" + "H" * (len(body) // 2), body))
    mode_config = "_native_runtime_mode" in symbols
    background = '_native_background_boot_region' in symbols
    def literal(index):
        op = words[index]
        at = ((begin + index * 2 + 4) & ~3) + 4 * (op & 255)
        # This candidate scan also crosses embedded pool data. A data word
        # resembling MOV.L is not a witness if its supposed literal escapes
        # the function. Every accepted block still binds all exact targets.
        if not begin <= at <= begin + len(body) - 4:
            return None
        return struct.unpack("<I", region(body, at - begin, 4, "installer literal"))[0]
    candidates = []
    for i in range(len(words) - 20):
        # Original configuration layout: the publisher uses callee-saved R14.
        old = (words[i] & 0xff00 == 0xde00 and literal(i) == address and
               words[i + 1:i + 10] ==
               [0x6593, 0x57b7, 0x358c, 0x2372, 0x57be, 0x53ad, 0x2372, 0x4e0b, 0x1f23] and
               words[i + 10] & 0xff00 == 0xd000 and
               literal(i + 10) == symbols["_kui_vmu_native_worker_call"][0] and
               words[i + 11:i + 20] ==
               [0x67f3, 0x52f3, 0x7720, 0x55aa, 0x66f3, 0x54a3, 0x7630, 0x400b, 0x1f28])
        if old:
            require(words[i - 1] & 0xff00 == 0xdb00 and
                    literal(i - 1) == symbols["_native_driver"][0], "publisher uses a different driver witness")
            candidates.append((i, 8, 18, 0))
        # Prepared-plan configuration: exactly the same two dynamic writes,
        # [load,load+worker_bytes) publication and immediate arm; R2 carries
        # the publisher and the result/config stack offsets increase by8B.
        # The raw-lease alias check makes GCC exchange the cached load and
        # worker byte-count registers (R8/R9). Pin both complete witnesses;
        # the dynamic stores and their CFG domination remain identical.
        prepared_setups = (
            [0x6593, 0x358c, 0x53b7, 0x2232, 0x53be, 0x52ad, 0x2232],
            [0x6583, 0x359c, 0x53b7, 0x2232, 0x53be, 0x52ad, 0x2232])
        new = (words[i] & 0xff00 == 0xdb00 and literal(i) == symbols["_native_driver"][0] and
               words[i + 1:i + 8] in prepared_setups and
               words[i + 8] & 0xff00 == 0xd200 and literal(i + 8) == address and
               words[i + 9:i + 11] == [0x420b, 0x1f24 if mode_config else 0x1f23] and words[i + 11] & 0xff00 == 0xd000 and
               literal(i + 11) == symbols["_kui_vmu_native_worker_call"][0] and
               words[i + 12:i + 20] == [0x67f3, 0x55aa, 0x7728, 0x54a3, 0x66f3, 0x1fea, 0x400b, 0x7638])
        if new:
            candidates.append((i, 9, 18, prepared_setups.index(words[i + 1:i + 8])))
        # Persistent v2 validates two additional executable exports. GCC
        # retains P1 load inR9, bytes inR10 and P2 header inR8, loading the
        # driver inR13/publisher inR1. Pin the complete new argument/store
        # block rather than accepting an arbitrary publisher call.
        diagnostic = (mode_config and words[i] & 0xff00 == 0xdd00 and
            literal(i) == symbols["_native_driver"][0] and words[i + 1] & 0xff00 == 0xd100 and
            literal(i + 1) == address and words[i + 2:i + 11] ==
            [0x6593, 0x53d7, 0x35ac, 0x2232, 0x53de, 0x528d, 0x2232, 0x410b, 0x1f14] and
            words[i + 11] & 0xff00 == 0xd000 and
            literal(i + 11) == symbols["_kui_vmu_native_worker_call"][0] and
            words[i + 12:i + 20] == [0x67f3, 0x558a, 0x7728, 0x5483, 0x66f3, 0x1fca, 0x400b, 0x7644])
        if diagnostic:
            require(i >= 4 and words[i - 4:i] == [0x8d02, 0x6493, 0xae4d, 0xe503],
                    "v2 publisher lost unconditional delay-slot P1 span start")
            candidates.append((i, 9, 18, 2))
        traced = (mode_config and words[i]&0xff00==0xde00 and literal(i)==symbols['_native_driver'][0] and
            words[i+1]&0xff00==0xdc00 and literal(i+1)==address and
            words[i+2:i+10]==[0x65a3,0x53e7,0x359c,0x2232,0x53ee,0x528d,0x4c0b,0x2232] and
            words[i+10]&0xff00==0xd000 and literal(i+10)==symbols['_kui_vmu_native_worker_call'][0] and
            words[i+11:i+19]==[0x67f3,0x558a,0x772c,0x5483,0x66f3,0x1fdb,0x400b,0x7648])
        if traced:
            require(i>=4 and words[i-4:i]==[0x8d02,0x64a3,0xae4a,0xe503],
                    'v3 trace publisher lost exact unconditional P1 start')
            candidates.append((i,8,17,3))
        # The separately admitted Background stage retains the worker length
        # in its fixed stack slot while copying/binding the owned async tail.
        # Its final publication still writes both exact dynamic cells and
        # immediately enters the identical worker ABI.
        bg = (background and words[i]&0xff00==0xdd00 and literal(i)==symbols['_native_driver'][0] and
            words[i+1]&0xff00==0xdc00 and literal(i+1)==address and
            words[i+2:i+10]==[0x53d7,0x55f7,0x2232,0x53de,0x359c,0x528d,0x4c0b,0x2232] and
            words[i+10]&0xff00==0xd000 and literal(i+10)==symbols['_kui_vmu_native_worker_call'][0] and
            words[i+11:i+19]==[0x67f3,0x558a,0x772c,0x5483,0x66f3,0x1fbb,0x400b,0x7648])
        if bg:
            require(i>=4 and words[i-4:i]==[0x8d02,0x6493,0xadf3,0xe503],
                    'Background publisher lost exact unconditional P1 start')
            candidates.append((i,8,17,4))
    require(len(candidates) == 1, "missing unique linked worker-publication/first-arm block")
    first, publish_at, arm_at, register_family = candidates[0]
    # This bounded block loads the worker's own two dynamic target cells,
    # publishes exactly [load,load+worker_bytes), and immediately calls arm.
    publish, arm = begin + (first + publish_at) * 2, begin + (first + arm_at) * 2
    relocate = symbols["_kui_vmu_native_relocate"][0]
    relocations = []
    for i, op in enumerate(words[:-12]):
        if op & 0xff00 != 0xd000 or literal(i) != relocate:
            continue
        # Alias-normalized installation uses R8 for its destination and
        # explicitly copies its R9 byte-count into argument R5. Legacy
        # installation carries its byte-count in R8 and destination in R9.
        for extra, destination_store in ((0, 0x1f92), (1, 0x1f82)):
            j = i + extra
            if extra and words[i + 1] != 0x6593:
                continue
            if (words[j + 1] & 0xff00 == 0xd200 and literal(j + 1) == SPAN and
                words[j + 2] == 0x67e3 and words[j + 3] & 0xff00 == 0xd600 and
                literal(j + 3) == symbols["__native_worker_blob_end"][0] and
                words[j + 4] == 0x37d8 and words[j + 5] & 0xff00 == 0xd400 and
                literal(j + 5) == symbols["__native_worker_blob_begin"][0] and
                words[j + 6:j + 11] == [destination_store, 0x1f21, 0x2fa2, 0x400b, 0x1f23]):
                relocations.append((i, j + 9, extra, False))
        # The mode/diagnostics configuration retains R13 as the exact span
        # and moves the P2 destination store into the relocation delay slot.
        # Pin the entire argument block; a new callback literal alone is not
        # an installation/publication witness.
        if (mode_config and words[i + 1] == 0x6593 and
            words[i + 2] & 0xff00 == 0xdd00 and literal(i + 2) == SPAN and
            words[i + 3] == 0x37e8 and words[i + 4] & 0xff00 == 0xd600 and
            literal(i + 4) == symbols["__native_worker_blob_end"][0] and
            words[i + 5] & 0xff00 == 0xd400 and literal(i + 5) == symbols["__native_worker_blob_begin"][0] and
            words[i + 6:i + 10] == [0x1f82, 0x1fd1, 0x400b, 0x2fa2]):
            relocations.append((i, i + 8, 1, True))
        if (mode_config and words[i + 1] == 0x65a3 and
            words[i + 2] & 0xff00 == 0xdb00 and literal(i + 2) == SPAN and
            words[i + 3] == 0x37e8 and words[i + 4] & 0xff00 == 0xd600 and
            literal(i + 4) == symbols["__native_worker_blob_end"][0] and
            words[i + 5] & 0xff00 == 0xd400 and literal(i + 5) == symbols["__native_worker_blob_begin"][0] and
            words[i + 6:i + 10] == [0x1f92, 0x1fb1, 0x400b, 0x2f82]):
            relocations.append((i, i + 8, 2, True))
        if (mode_config and words[i+1]==0x6593 and words[i+2]&0xff00==0xdb00 and literal(i+2)==SPAN and
            words[i+3]==0x37e8 and words[i+4]&0xff00==0xd600 and literal(i+4)==symbols['__native_worker_blob_end'][0] and
            words[i+5]&0xff00==0xd400 and literal(i+5)==symbols['__native_worker_blob_begin'][0] and
            words[i+6:i+10]==[0x1fa2,0x1fb1,0x400b,0x2f82]):
            relocations.append((i,i+8,3,True))
        if (background and words[i+1]==0x67e3 and words[i+2]&0xff00==0xda00 and literal(i+2)==SPAN and
            words[i+3:i+5]==[0x37b8,0x55f7] and
            words[i+5]&0xff00==0xd600 and literal(i+5)==symbols['__native_worker_blob_end'][0] and
            words[i+6]&0xff00==0xd400 and literal(i+6)==symbols['__native_worker_blob_begin'][0] and
            words[i+7:i+11]==[0x1f92,0x1fa1,0x400b,0x2f82]):
            relocations.append((i,i+9,4,True))
    zero_patterns = (
        [0x56f3, 0x6ba3, 0x3b8c, 0x3688, 0x64b3, 0x4c0b, 0xe500],
        [0x56f3, 0x6ba3, 0x3b9c, 0x3698, 0x64b3, 0x4c0b, 0xe500],
        [0x6ba3, 0x3b9c, 0xe500, 0x66d3, 0x64b3, 0x4c0b, 0x3698],
        [0x6e83, 0x3eac, 0xe500, 0x66b3, 0x64e3, 0x4c0b, 0x36a8],
        [0x6e83,0x3e9c,0xe500,0x66b3,0x64e3,0x4c0b,0x3698],
        [0x5bf7,0xe500,0x51f7,0x66a3,0x3b8c,0x3618,0x4d0b,0x64b3])
    zero_families = (0, 1, 1, 2, 3, 4)
    zeroes = [(i,zero_families[j],j) for j,pattern in enumerate(zero_patterns)
              for i in range(len(words)-len(pattern)) if words[i:i+len(pattern)]==pattern and
              (j!=5 or background)]
    releases = [i for i in range(len(words) - 3) if words[i:i + 3] ==
                ([0x52aa,0x7214,0x2022] if register_family==4 else
                 [0x52ba, 0x7214, 0x2022] if register_family in (2,3) else [0x53ca, 0x7314, 0x2032])]
    require(len(relocations) == len(zeroes) == len(releases) == 1,
            "missing linked relocation/BSS/release-cell initialization witnesses")
    require(relocations[0][2] == zeroes[0][1] == register_family,
            "installer relocation/BSS/publication use inconsistent load/length registers")
    require(relocations[0][3] == mode_config and (zeroes[0][2] in (2, 3, 4, 5)) == mode_config,
            "installer mode configuration uses an unreviewed relocation/zero-fill ABI")
    relocate_pc = begin + relocations[0][1] * 2
    zero_pc = begin + (zeroes[0][0] + (6 if register_family==4 else 5)) * 2
    release_pc = begin + (releases[0] + 2) * 2
    # The compiler keeps memset in callee-saved R12 until this BSS call.
    # Pin its initializer and reject any intervening write to R12.
    memset_reg=13 if register_family==4 else 12
    memset_loads = [i for i in range(zeroes[0][0]) if words[i] & 0xff00 == 0xd000|(memset_reg<<8) and
                    literal(i) == symbols["_memset"][0]]
    require(len(memset_loads) == 1, "BSS zeroing has no exact linked memset target")
    decoded = {pc: (op, mnemonic, operands.split("!", 1)[0].strip())
               for pc, op, mnemonic, operands in literal_aware_instructions(disassembly)
               if begin <= pc < begin + len(body)}
    for pc, (_, mnemonic, operands) in decoded.items():
        if begin + 2 * memset_loads[0] < pc < zero_pc and mnemonic not in ("cmp/eq", "cmp/hi", "cmp/hs", "tst"):
            require(not re.search(r"(?:^|,)\s*r"+str(memset_reg)+r"$", operands), "BSS memset target register is overwritten")
    # A conservative intraprocedural CFG treats every C call as returning.
    # The initialization witnesses must dominate the publication and arm;
    # byte order alone would allow a branch to bypass publication.
    edges, todo, seen = {}, [begin], set()
    while todo:
        pc = todo.pop()
        if pc in seen:
            continue
        require(pc in decoded, "installer branch enters non-instruction/literal bytes")
        seen.add(pc); op = decoded[pc][0]
        delay = op >> 12 in (0xa, 0xb) or op & 0xf0ff in (0x400b, 0x402b) or op == 0x000b or op >> 8 in (0x8d, 0x8f)
        if delay:
            require(pc + 2 in decoded, "installer delay slot is not an instruction")
        if op >> 12 == 0xa:
            d = op & 4095; d -= 4096 if d & 2048 else 0
            targets = [pc + 4 + 2 * d]
        elif op >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
            d = op & 255; d -= 256 if d & 128 else 0
            targets = [pc + 4 + 2 * d, pc + (4 if delay else 2)]
        elif op == 0x000b:
            targets = []
        else:
            require(op & 0xf0ff != 0x402b, "unmodeled installer indirect tail jump")
            targets = [pc + (4 if delay else 2)]
        edges[pc] = targets; todo.extend(targets)
    predecessors = {pc: set() for pc in seen}
    for pc, targets in edges.items():
        for target in targets:
            predecessors[target].add(pc)
    dom = {pc: ({pc} if pc == begin else set(seen)) for pc in seen}
    for _ in range(len(seen)):
        changed = False
        for pc in seen - {begin}:
            value = {pc} | set.intersection(*(dom[p] for p in predecessors[pc]))
            if value != dom[pc]:
                dom[pc] = value; changed = True
        if not changed:
            break
    # v3 puts the consumer-cell store in the publisher JSR delay slot.
    # The exact block above pins that store; the JSR node represents both
    # instructions in this CFG, before control reaches the publisher.
    dynamic_stores = (4,8) if register_family==4 else (5, 8) if register_family in (2, 3) else (4, 7)
    witnesses = {relocate_pc, zero_pc, release_pc,
                 *(begin + (first + offset) * 2 for offset in dynamic_stores)}
    if register_family in (2, 3, 4):witnesses.add(begin + (first - 4) * 2)
    require(witnesses <= dom[publish] and publish in dom[arm],
            "installer can enter P1 arm before initialization/publication")
    require(begin + 2 * memset_loads[0] in dom[zero_pc],
            "BSS memset target can be bypassed")
    diagnostics_getter = None
    background_prefix_mutations=[]
    if '_native_runtime_diagnostics' in symbols:
        # Bind the noipa getter's canonical 0/1/2/3 return to the exact last
        # config word. v3 adds four stack bytes ahead of the configuration.
        # The store is the next call's delay slot, before that call may
        # clobber R0. Both getter and store must dominate the first arm.
        config_start, config_base = (0x48, 0x904) if register_family in (3,4) else (0x44, 0x900)
        alias_add = 0x38AC if register_family == 3 else 0x389C
        choices = [j for j in range(len(words) - 9) if
            words[j] & 0xFF00 == 0xD000 and literal(j) == symbols['_native_runtime_diagnostics'][0] and
            words[j + 1] & 0xFF00 == 0xD800 and literal(j + 1) == 0x20000000 and
            words[j + 2] & 0xFF00 == 0xDE00 and literal(j + 2) == symbols['__native_worker_blob_end'][0] and
            words[j + 3:j + 5] == [0x400B, alias_add] and
            words[j + 5] & 0xFF00 == 0xD100 and literal(j + 5) == symbols['_retail_display_restore'][0] and
            words[j + 6] & 0xFF00 == 0xD400 and literal(j + 6) == symbols['_display'][0] and
            words[j + 7:j + 9] == [0x410B, 0x1D0F]]
        if register_family==4:
            choices=[j for j in range(len(words)-7) if
                words[j]&0xff00==0xd000 and literal(j)==symbols['_native_runtime_diagnostics'][0] and
                words[j+1:j+7]==[0x400b,0x0009,0x51c8,0x31a0,0x8f09,0x1b0f]]
        require(len(choices) == 1, 'missing exact runtime diagnostics return-to-config store')
        diagnostics_getter = choices[0]
        bases = []
        for j in range(diagnostics_getter - 3):
            config_register=11 if register_family==4 else 13
            if words[j] & 0xFF00 != 0x9000|(config_register<<8) or words[j + 1] != 0xE630 or words[j + 3] != 0x300c|(config_register<<8)|0xf0:
                continue
            at = begin + 2 * j + 4 + 2 * (words[j] & 255)
            if struct.unpack('<h', region(body, at - begin, 2, 'diagnostics config stack offset'))[0] == config_base:
                bases.append(j)
        require(len(bases) == 1 and config_base + 60 - config_start == 0x8F8,
                'diagnostics store no longer names exact final config word')
        getter_pc = begin + 2 * (diagnostics_getter + (1 if register_family==4 else 3))
        store_call_pc = begin + 2 * (diagnostics_getter + (5 if register_family==4 else 7))
        base_pc = begin + 2 * (bases[0] + 3)
        require(base_pc in dom[getter_pc] and getter_pc in dom[store_call_pc] and store_call_pc in dom[arm],
                'runtime mode/config store can be bypassed before worker arm')
        for pc, (_, mnemonic, operands) in decoded.items():
            if base_pc < pc < store_call_pc and mnemonic not in ('cmp/eq', 'cmp/hi', 'cmp/hs', 'tst'):
                require(not re.search(r'(?:^|,)\s*r'+str(config_register)+r'$', operands), 'diagnostics config base is overwritten')
            if base_pc < pc < arm:
                require(not re.search(r'(?:^|,)\s*r15$', operands) and '@-r15' not in operands and '@r15+' not in operands,
                        'config stack moves between diagnostics assignment and arm')
    if background:
        require(background_tail is not None,
                'Background publication needs its independently executed compiled tail-copy proof')
        tail_entry=int(background_tail['entry'],16)
        tail={name:int(value,16) for name,value in background_tail['witnesses'].items()}
        require(set(tail)=={'copy_map','copy_region','publish','bind','bind_success','relocate'} and
                tail['relocate']==relocate_pc,'Background tail replay names a different worker relocation')
        ordered=[tail_entry,*(tail[name] for name in
            ('copy_map','copy_region','publish','bind','bind_success','relocate')),publish,arm]
        require(all(before in dom[after] for before,after in zip(ordered,ordered[1:])),
                'Background installer can bypass/reorder the complete tail-copy/bind/publication sequence')
        # Close the replay's incoming callee-preserved register assumptions
        # against the whole linked prefix. The lease is admitted before its
        # raw payload is normalized to P1; the exact memcpy target, selected
        # lease constant and config stack base dominate the replay entry.
        normalizers=[j for j in range(13,len(words)-11) if
            words[j]&0xff00==0xd100 and literal(j)==0x1fffffff and
            words[j+1]&0xff00==0xdd00 and literal(j+1)==symbols['_memset'][0] and
            words[j+2]==0x64f3 and words[j+4]==0x2919 and
            words[j+5]&0xff00==0xd100 and literal(j+5)==0x80000000 and
            words[j+6:j+11]==[0x7448,0x6683,0xeb01,0x4d0b,0x291b]]
        require(len(normalizers)==1,'Background prefix lacks its exact P1 payload normalization')
        normal=normalizers[0]
        require(words[normal-12]==0x5cf8 and words[normal-10]&0xff00==0xd000 and
                literal(normal-10)==symbols['_kui_vmu_native_heap_lease_valid'][0] and
                words[normal-9:normal-7]==[0xe600,0x7c40] and
                words[normal-7]&0xff00==0xd500 and literal(normal-7)==symbols['_native_read'][0] and
                words[normal-6:normal]==[0x59ca,0x400b,0x54f8,0x2008,0x8deb,0xe500],
                'Background prefix does not bind the loaded payload to its admitted native lease')
        memcpy_loads=[j for j in range(normal,(tail_entry-begin)//2) if
            words[j]&0xff00==0xd800 and literal(j)==symbols['_memcpy'][0]]
        selected_lease=0x60000 if '__kui_native_readahead_stage' in symbols else 0x30000
        require(background_tail.get('lease_bytes')==selected_lease,
                'Background tail replay selected a different owned lease size')
        lease_constants=[j for j in range(normal,(tail_entry-begin)//2) if
            words[j]&0xff00==0xda00 and literal(j)==selected_lease]
        require(len(memcpy_loads)==len(lease_constants)==1,
                'Background prefix needs exact unique memcpy/lease-size register initializers')
        memcpy_pc=begin+2*memcpy_loads[0];lease_pc=begin+2*lease_constants[0]
        norm_and=begin+2*(normal+4);norm_or=begin+2*(normal+9)
        prefix={begin+2*(normal-5),begin+2*(normal-2),begin+2*(normal-6),
                norm_and,norm_or,memcpy_pc,lease_pc,base_pc}
        require(prefix<=dom[tail_entry],
                'Background tail can be entered without lease admission/P1 normalization/exact callback setup')
        stable=((9,norm_or+4,tail['relocate']),
                (8,memcpy_pc+2,tail['copy_region']),
                (10,lease_pc+2,tail_entry),(11,base_pc+2,tail_entry),
                (12,begin+2*(normal-8)+2,tail_entry))
        for register,first_pc,last_pc in stable:
            for pc,(_,mnemonic,operands) in decoded.items():
                if first_pc<=pc<last_pc and mnemonic not in ('cmp/eq','cmp/hi','cmp/hs','tst'):
                    require(not re.search(r'(?:^|,)\s*r'+str(register)+r'$',operands),
                            'Background replay incoming callee-preserved register is overwritten: r'+str(register))
        report['background_tail_copy_and_prefix']={
            'whole_function_cfg_domination':True,'ordered_witnesses':[hex(pc) for pc in ordered],
            'lease_validation_dominates_copy':True,'normalized_p1_payload_dominates_copy':True,
            'exact_memcpy_and_lease_size_setup':True,'callee_preserved_registers_stable':True}
        background_prefix_mutations=[((normal+4,0x0009),),((normal+10,0x0009),),
                                     ((memcpy_loads[0],0x0009),),((lease_constants[0],0x0009),)]
    report.update(worker_relocation_before_publication=True, worker_bss_zero_before_publication=True,
                  dynamic_cells_before_publication=True, publication_dominates_first_arm=True,
                  first_worker_publish=hex(publish), first_worker_arm=hex(arm),
                  mode_configuration_compiler_witness=mode_config,
                  runtime_diagnostics_return_stored_in_exact_config=diagnostics_getter is not None,
                  runtime_diagnostics_store_dominates_arm=diagnostics_getter is not None,
                  linked_installer_cfg_nodes=len(seen))
    if self_test:
        # An individually recognized register-family block must not be mixed
        # with the other family's relocation/zero-fill/publication. Mutate the
        # actual ELF instruction bytes, keeping its typed geometry unchanged.
        alternate = 0 if register_family in (2, 3, 4) else 1 - register_family
        mutations = [((zeroes[0][0] + 2, zero_patterns[alternate][2]),
                      (zeroes[0][0] + 3, zero_patterns[alternate][3])),
                     ((relocations[0][0] + 1, 0x0009),)]
        mutations.extend(background_prefix_mutations)
        if register_family==4:
            mutations.extend([((first+3,0x0009),),((first+6,0x35ac),),
                              ((first+9,0x0009),),((first-3,0x64a3),),((first-2,0x0009),)])
        elif register_family == 3:
            mutations.extend([((first + 4, 0x35ac),), ((first + 9, 0x0009),),
                              ((first - 3, 0x6493),), ((first - 2, 0x0009),)])
        elif register_family == 2:
            mutations.append(((first + 4, 0x359c),))
            mutations.extend([((first - 3, 0x6483),), ((first - 2, 0x0009),)])
        elif publish_at == 9:
            mutations.append(((first + 1, prepared_setups[1 - register_family][0]),
                              (first + 2, prepared_setups[1 - register_family][1])))
        if diagnostics_getter is not None:
            getter_literal = (((begin + 2 * diagnostics_getter + 4) & ~3) +
                              4 * (words[diagnostics_getter] & 255) - begin) // 2
            wrong_getter = symbols['_native_runtime_mode'][0]
            store_index=diagnostics_getter+(6 if register_family==4 else 8)
            mutations.extend([((store_index, 0x0009),),
                              ((store_index, 0x1B0E if register_family==4 else 0x1D0E),),
                              ((diagnostics_getter, 0xE000),),
                              ((getter_literal, wrong_getter & 0xFFFF),
                               (getter_literal + 1, wrong_getter >> 16))])
        _, _, section = symbol_bytes(stage, "_native_passive_run", 2)
        offset = section[4] + begin - section[3]
        rejected = 0
        for changes in mutations:
            changed = bytearray(stage[0])
            for index, value in changes:
                struct.pack_into("<H", changed, offset + index * 2, value)
            try:
                check_worker_publication((bytes(changed), *stage[1:]), cached, disassembly,
                                         background_tail=background_tail)
            except (ValueError, AssertionError, KeyError, struct.error):
                rejected += 1
            else:
                raise ValueError("worker publication accepted inconsistent initialization witness")
        report["initialization_witness_mutation_rejections"] = rejected
    return report


def require(value, message):
    if not value:
        raise ValueError(message)


def physical(address):
    return address & 0x1FFFFFFF


class CachedGateway(ReadonlyGateway):
    """Actual gateways use P1 code and ephemeral stack; cells remain P2."""
    def __init__(self, *args):
        super().__init__(*args)
        self.private_top -= 0x20000000

    def code(self, address, size):
        assert address & 0xFF000000 == 0x8C000000, "gateway fetched uncached code/constant"
        offset = physical(address) - physical(self.base)
        assert 0 <= offset < offset + size <= len(self.blob)
        return self.blob[offset:offset + size]


class CachedStackGuard(Machine):
    """Execute the linked leaf; retain raw aliases instead of coalescing RAM."""
    def __init__(self, worker, armed, mask, stack, bad_guard=None, *, body=None):
        super().__init__(worker, "_stack_valid", 7, mask, stack, body=body)
        self.armed, self.bad_guard = armed, bad_guard
        self.owner = worker[4]["_state"][0]
        self.bottom = worker[4]["__vmu_native_worker_stack_bottom"][0]
        self.expected_bottom = self.bottom - (0x20000000 if armed else 0)
        self.raw_guards = []

    def read(self, address):
        if address == self.owner:
            return self.armed
        if physical(self.bottom) <= physical(address) < physical(self.bottom) + 64:
            index = (physical(address) - physical(self.bottom)) // 4
            assert address == self.expected_bottom + index * 4, "stack guard sampled stale alternate alias"
            self.raw_guards.append(address)
            return 0 if index == self.bad_guard else 0x4b535431
        return struct.unpack("<I", self.code(address, 4))[0]

    def write(self, address, value):
        raise AssertionError("stack guard is not a read-only leaf")

    def plain(self, word, pc):
        if word & 0xf00f == 0x000e:
            self.r[word >> 8 & 15] = self.read((self.r[word >> 4 & 15] + self.r[0]) & MASK)
            return
        super().plain(word, pc)

    def check(self, expected):
        self.run()
        assert self.r[0] == int(expected), "stack guard admitted wrong lifecycle/alias/boundary"
        regs, pr, mach, macl, gbr, sr, fpscr, fr = self.before
        assert self.r[8:] == regs[8:] and self.pr == pr
        assert (self.mach, self.macl, self.gbr, self.sr & ~1, self.fpscr, self.fr) == (mach, macl, gbr, sr & ~1, fpscr, fr)
        count = 16 if expected else self.bad_guard + 1 if self.bad_guard is not None and self.raw_guards else 0
        assert self.raw_guards == [self.expected_bottom + i * 4 for i in range(count)]
        return self


def check_cached_stack_guard(worker, self_test=False):
    """Admit only cold P2 and armed P1, including every actual canary load."""
    prepared_symbol_records(worker)  # Independently establish the complete section ownership.
    bottom=worker[4]["__vmu_native_worker_stack_bottom"][0]
    cases=0
    for armed in (0, 1):
        for mask in range(16):
            for alias in (bottom, bottom-0x20000000, physical(bottom)):
                for offset in (0, 252, 256, 260, 2048, 4092, 4096, 4100):
                    valid = mask==15 and alias==bottom-(0x20000000 if armed else 0) and 256<=offset<4096
                    CachedStackGuard(worker,armed,mask,alias+offset).check(valid);cases+=1
        for bad in range(16):
            CachedStackGuard(worker,armed,15,bottom-(0x20000000 if armed else 0)+2048,bad).check(False);cases+=1
    rejected=0
    if self_test:
        _,body,_=symbol_bytes(worker,"_stack_valid",2)
        mask=struct.pack("<I",0x1fffffff)
        require(body.count(mask)==1,"stack guard lacks unique physical-address mask")
        # Preserving bit29 forces armed canary reads through P2 while both
        # aliases contain good guard values. Raw-address checking must refuse.
        changed=body.replace(mask,struct.pack("<I",0x3fffffff))
        try:CachedStackGuard(worker,1,15,bottom-0x20000000+2048,body=changed).check(True)
        except (ValueError,AssertionError,KeyError,struct.error):rejected+=1
        else:raise ValueError("stack guard accepted stale alternate-alias canary sampling")
    return {"instruction_cases":cases,"mutation_rejections":rejected,
            "cold_arm_alias":"P2","armed_runtime_alias":"P1","raw_canary_alias_checked":True,
            "guard_words":16,"guard_border_bytes":256,"owner_writes":0}


class CompiledHeapReader(Machine):
    """Execute the actual emitted4B reader, trapping disallowed source loads."""
    def __init__(self,worker,address,valid,*,body=None):
        super().__init__(worker,"_read_heap",11,15,0x8c11fff0,body=body)
        self.address,self.valid=address,valid
        self.output=self.start_sp-64
        self.r[4:7]=[0,address,self.output]
        self.source_reads,self.output_writes=[],[]

    def read(self,address):
        if address==self.address:
            assert self.valid,"compiled reader dereferenced an unadmitted source"
            self.source_reads.append(address);return 0x5a73c129
        return struct.unpack("<I",self.code(address,4))[0]

    def write(self,address,value):
        assert self.valid and address==self.output,"compiled reader changed unexpected memory"
        self.output_writes.append((address,value))

    def check(self):
        self.run()
        assert self.r[0]==int(self.valid)
        assert self.source_reads==([self.address] if self.valid else [])
        assert self.output_writes==([(self.output,0x5a73c129)] if self.valid else [])
        regs,pr,mach,macl,gbr,sr,fpscr,fr=self.before
        assert self.r[8:]==regs[8:] and self.pr==pr
        assert (self.mach,self.macl,self.gbr,self.sr&~1,self.fpscr,self.fr)==(mach,macl,gbr,sr&~1,fpscr,fr)


def check_compiled_heap_reader(worker,self_test=False):
    mmio={0xa05f6c04,0xa05f6c10,0xa05f6c14,0xa05f6c18}
    addresses={area|offset for area in range(0,0x100000000,0x20000000)
               for offset in (0,0xbfffffc,0xc000000,0xc000001,0xc000002,0xc000003,
                              0xc000004,0xc800000,0xcfffff8,0xcfffffc,0xcfffffd,0xcfffffe,
                              0xcffffff,0xd000000,0x1ffffffc)}
    addresses|={base+offset for base in (0x005f6c00,0x805f6c00,0xa05f6c00,0xe05f6c00)
                for offset in range(0,0x24)}
    for address in sorted(addresses):
        valid=(address in mmio or address&0xe0000000 in (0,0x80000000,0xa0000000) and
               not address&3 and 0xc000000<=physical(address)<=0xcfffffc)
        CompiledHeapReader(worker,address,valid).check()
    rejected=0
    if self_test:
        _,body,_=symbol_bytes(worker,"_read_heap",2)
        edits=((struct.pack("<H",0xc803),struct.pack("<H",0xc800),0x8c200001),
               (struct.pack("<I",0x60000000),bytes(4),0xec200000))
        for before,after,address in edits:
            require(body.count(before)==1,"compiled reader lacks unique address predicate mutation site")
            try:CompiledHeapReader(worker,address,False,body=body.replace(before,after)).check()
            except (ValueError,AssertionError,KeyError,struct.error):rejected+=1
            else:raise ValueError("compiled reader accepted weakened address predicate")
    return {"instruction_cases":len(addresses),"mutation_rejections":rejected,
            "width":4,"source_loaded_only_after_full_predicate":True,"mmio_addresses":sorted(mmio)}


def model_cached_hooks(binary, wire, symbols):
    cases = 0
    for destination in (P2, 0xAC38A880, 0xAC700000):
        blob = relocated(binary, wire, destination)
        mapped = {name: address + destination - P2 for name, address in symbols.items()}
        for seed in range(40):
            for mask in range(16):
                for mode in ("submit", "complete", "terminal", "release"):
                    CachedGateway(seed, mask, destination, blob, mapped, mode).run().check()
                    cases += 1
    return cases


class CachedStageGateway(StageGateway):
    def __init__(self, seed, mask, base, blob, **options):
        super().__init__(seed, mask, base, blob, entry=options.pop("entry", P2_ENTRY - 0x20000000), **options)
        self.cache_entry = P2_ENTRY - 0x20000000
    def run(self):
        steps = 0
        while self.pc != self.initial[1]:
            steps += 1;assert steps < 200
            pc = self.pc;word = struct.unpack("<H", self.code(pc, 2))[0]
            if word >> 12 == 0xA or word & 0xF0FF == 0x400B or word == 0xB:
                if word >> 12 == 0xA:
                    displacement = word & 4095
                    displacement -= 4096 if displacement & 2048 else 0
                    target = (pc + 4 + displacement * 2) & MASK
                elif word & 0xF0FF == 0x400B:
                    target = self.r[word >> 8 & 15];self.pr = pc + 4
                else:
                    target = self.pr
                self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                if target == self.cache_entry:
                    self.worker()
                else:
                    self.pc = target
            elif word >> 8 in (0x89, 0x8B):
                displacement = word & 255
                displacement -= 256 if displacement & 128 else 0
                take = bool(self.sr & 1) == (word >> 8 == 0x89)
                self.pc = (pc + 4 + displacement * 2 if take else pc + 2) & MASK
            else:
                self.plain(word, pc);self.pc = pc + 2
        return self


def model_cached_stage(base, blob):
    cases = 0
    for seed in range(80):
        for mask in range(16):
            CachedStageGateway(seed, mask, base, blob).run().check(int(mask == 15)); cases += 1
    entry = P2_ENTRY - 0x20000000
    invalid = (("entry", entry + 1), ("entry", P2_ENTRY), ("top", TOP + 4),
               ("top", TOP - 0x20000000),
               ("top", 0xAD000020), ("top", 0xAC000FE0), ("config", 0), ("result", 0))
    for field, value in invalid:
        CachedStageGateway(1, 15, base, blob, **{field: value}).run().check(0); cases += 1
    CachedStageGateway(1, 15, base, blob, top=0xAD000000).run().check(1)
    return cases + 1

def model_cached_mutations(binary, symbols):
    """Reject control-flow/FPU changes without relying on ELF byte mismatch."""
    tests = []
    begin = physical(symbols["_kui_vmu_native_passive_submit_hook"]) - physical(P2)
    changed = bytearray(binary)
    struct.pack_into("<H", changed, begin, 0xF000)
    tests.append((changed, "submit", "FPU in gateway"))
    # Change the referenced callback literal in situ. Its wrong target is a
    # real known callback, so rejection must come from the frame/control-flow
    # model, rather than merely an out-of-bounds instruction read.
    for mode, target_name, wrong_name in (
            ("submit", "_kui_vmu_native_passive_submit", "_kui_vmu_native_passive_complete"),
            ("complete", "_kui_vmu_native_passive_complete", "_kui_vmu_native_passive_submit"),
            ("terminal", "_kui_vmu_native_passive_report", "_kui_vmu_native_passive_submit"),
            ("release", "_kui_vmu_native_readonly_release_check", "_kui_vmu_native_passive_complete")):
        function = symbols["_kui_vmu_native_readonly_release_hook"] if mode == "release" else symbols["_kui_vmu_native_passive_" + ("terminal" if mode == "terminal" else mode + "_hook")]
        matches = []
        # Authored hook functions are all shorter than 256 bytes. Inspect
        # PC-relative MOV.L operands, not arbitrary pointer-looking words.
        for offset in range(physical(function) - physical(P2), min(physical(function) - physical(P2) + 256, len(binary)) - 1, 2):
            word = struct.unpack_from("<H", binary, offset)[0]
            if word >> 12 != 0xD:
                continue
            literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
            if (0 <= literal <= len(binary) - 4 and
                    struct.unpack_from("<I", binary, literal)[0] == symbols[target_name]):
                matches.append(literal)
        require(matches, "missing actual gateway callback literal for mutation")
        changed = bytearray(binary)
        struct.pack_into("<I", changed, matches[0], symbols[wrong_name])
        tests.append((changed, mode, "wrong callback target"))
    # The completion must dereference its own consumer cell, not a stage
    # address, callback, or submission resume cell.
    complete = physical(symbols["_kui_vmu_native_passive_complete_hook"]) - physical(P2)
    matches = []
    for offset in range(complete, min(complete + 256, len(binary)) - 1, 2):
        word = struct.unpack_from("<H", binary, offset)[0]
        if word & 0xFF00 != 0xD300:
            continue
        literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
        if (0 <= literal <= len(binary) - 4 and
                struct.unpack_from("<I", binary, literal)[0] == symbols["_kui_vmu_native_passive_consumer"]):
            matches.append(literal)
    require(len(matches) == 1, "missing unique dynamic consumer-cell load")
    changed = bytearray(binary)
    struct.pack_into("<I", changed, matches[0], symbols["_kui_vmu_native_passive_resume"])
    tests.append((changed, "complete", "wrong consumer cell"))
    # Move inherited SR restoration one frame pop too early. This must fail
    # independently of final register equality, since a same-priority IRQ may
    # arrive as soon as the original mask is restored.
    pattern = struct.pack("<4H", 0x63F6, 0x61F6, 0x60F6, 0x430E)
    at = binary.find(pattern, complete, min(complete + 256, len(binary)))
    require(at >= 0, "missing complete-frame final restore sequence")
    changed = bytearray(binary)
    struct.pack_into("<2H", changed, at + 4, 0x430E, 0x60F6)
    tests.append((changed, "complete", "premature inherited SR restoration"))
    changed = bytearray(binary)
    struct.pack_into("<H", changed, at + 8, 0x6883)  # MOV R8,R8 is otherwise harmless.
    tests.append((changed, "complete", "missing SR latency instruction"))
    # Replay the original MDST store twice; the linked model admits that
    # exact address but requires precisely one store during submission.
    submit = physical(symbols["_kui_vmu_native_passive_submit_hook"]) - physical(P2)
    store = binary.find(struct.pack("<H", 0x12C6), submit, min(submit + 256, len(binary)))
    require(store >= 0, "missing original MDST replay")
    changed = bytearray(binary)
    struct.pack_into("<H", changed, store + 2, 0x12C6)
    tests.append((changed, "submit", "duplicate MDST replay"))
    # Apply the same lifetime-entry restoration mutation to the allocator
    # wrapper. Its C callback may execute in contexts different from Maple.
    release = physical(symbols["_kui_vmu_native_readonly_release_hook"]) - physical(P2)
    pattern = struct.pack("<5H", 0x60F6, 0x61F6, 0x6703, 0xCBF0, 0x400E)
    at = binary.find(pattern, release, min(release + 256, len(binary)))
    require(at >= 0, "missing release-frame final restore sequence")
    changed = bytearray(binary)
    struct.pack_into("<2H", changed, at + 2, 0x400E, 0x61F6)
    tests.append((changed, "release", "premature release SR restoration"))
    # Removing the OR before the first post-callback SR load produces the
    # same final state after the original mask-establishment sequence, yet
    # transiently permits IRQs to invalidate the just-checked free list.
    changed = bytearray(binary)
    struct.pack_into("<H", changed, at + 6, 0x0009)
    tests.append((changed, "release", "transient release IRQ admission"))
    # Runtime code and ephemeral frames must consistently use P1; mutable
    # consumer cells remain P2. A P2 runtime stack after a previous P1 call
    # could be corrupted by later dirty cached-frame eviction, so exercise
    # the wrong-stack mutation at every returning and terminal gateway.
    for mode, hook, value, alias, delta in (
            ("submit", "_kui_vmu_native_passive_submit_hook", "_kui_vmu_native_passive_submit", 0, 0x20000000),
            ("complete", "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_complete", 0, 0x20000000),
            ("terminal", "_kui_vmu_native_passive_terminal", "_kui_vmu_native_passive_report", 0, 0x20000000),
            ("release", "_kui_vmu_native_readonly_release_hook", "_kui_vmu_native_readonly_release_check", 0, 0x20000000),
            ("complete", "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_consumer", 0, -0x20000000),
            *((mode, hook, "__vmu_native_worker_stack_top", -0x20000000, 0x20000000)
              for mode, hook in (("submit", "_kui_vmu_native_passive_submit_hook"),
                                 ("complete", "_kui_vmu_native_passive_complete_hook"),
                                 ("release", "_kui_vmu_native_readonly_release_hook"),
                                 ("terminal", "_kui_vmu_native_passive_terminal")))):
        begin = physical(symbols[hook]) - physical(P2)
        references = []
        for offset in range(begin, min(begin + 256, len(binary)) - 1, 2):
            word = struct.unpack_from("<H", binary, offset)[0]
            if word >> 12 != 0xD: continue
            literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
            if 0 <= literal <= len(binary) - 4 and struct.unpack_from("<I", binary, literal)[0] == symbols[value] + alias:
                references.append(literal)
        require(references, "missing actual cached gateway alias literal")
        changed = bytearray(binary)
        struct.pack_into("<I", changed, references[0], symbols[value] + alias + delta)
        tests.append((changed, mode, "wrong cached code or mutable-state alias"))
    cases = 0
    for blob, mode, description in tests:
        try:
            CachedGateway(7, 6, P2, blob, symbols, mode).run().check()
        except (AssertionError, struct.error):
            cases += 1
        else:
            raise ValueError("gateway model accepted " + description)
    return cases


def check_runtime_fault_gateways(worker, binary, wire, self_test=False):
    """Execute the exact relocated failure-only ABI; owner RAM is read only."""
    symbols = {name: value[0] for name, value in worker[4].items()}
    runtime = "_kui_vmu_native_runtime_fault_hook"
    allocation = "_kui_vmu_native_allocation_failure_hook"
    report = "_kui_vmu_native_runtime_fault"
    stack_delta = 0x20000000 if symbols["_kui_vmu_native_passive_arm"] & 0xff000000 == 0x8c000000 else 0
    def authored(image):
        for name, prefix, targets in (
                (runtime, (0x0002, 0xcbf0, 0x400e, 0xdf02, 0xd002, 0x402b, 0x0009, 0x0009),
                 (symbols["__vmu_native_worker_stack_top"] - stack_delta, symbols[report])),
                (allocation, (0x65e3, 0x56f1, 0x67f3, 0xe401, 0xd001, 0x402b, 0x0009, 0x0009),
                 (symbols[runtime],))):
            address, body, section = symbol_bytes(image, name, 2)
            require(section[2] & 7 == 6 and address % 4 == 0 and
                    body == struct.pack("<" + "H" * len(prefix) + "I" * len(targets), *prefix, *targets),
                    "failure gateway integer capture/mask/private-stack transfer changed: " + name)
    authored(worker)
    cases = 0
    for destination in (P2, 0xAC38A880, 0xAC700000):
        blob = relocated(binary, wire, destination)
        mapped = {name: address + destination - P2 for name, address in symbols.items()}
        def read_code(address, size):
            require(address & 0xff000000 == mapped[runtime] & 0xff000000,
                    "failure gateway used wrong instruction alias")
            offset = physical(address) - physical(destination)
            require(0 <= offset < offset + size <= len(blob), "failure gateway escaped initialized code")
            return blob[offset:offset + size]
        for seed in range(40):
            rng = random.Random(seed)
            for mask in range(16):
                for mode in ("allocation", "runtime"):
                    r = [rng.getrandbits(32) for _ in range(16)]
                    sp = 0x8c00f100;request = (rng.randrange(1, 0x100000) + 31) & ~31
                    caller = 0x8c010000 + 2 * rng.randrange(0x100000)
                    r[15], r[14] = sp, request
                    inherited_sr = (rng.getrandbits(32) & ~0xf0) | 0x40000000 | mask << 4
                    sr = inherited_sr
                    r[4:8] = [2, rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)]
                    expected = [1, request, caller, sp] if mode == "allocation" else r[4:8].copy()
                    # These gateways contain no instruction touching PR, MAC,
                    # GBR, FPSCR or any FR/FPUL; the closed opcode vocabulary
                    # and authored byte proof exclude such side effects.
                    pc = mapped[allocation if mode == "allocation" else runtime]
                    owner_reads = []
                    for step in range(24):
                        if pc == mapped[report]: break
                        op, = struct.unpack("<H", read_code(pc, 2))
                        n, m = op >> 8 & 15, op >> 4 & 15
                        if op == 0x0002:r[0] = sr
                        elif op == 0xcbf0:r[0] |= 0xf0
                        elif op == 0x400e:sr = r[0]
                        elif op & 0xf00f == 0x6003:r[n] = r[m]
                        elif op >> 12 == 0xe:r[n] = ((op & 255) - (256 if op & 128 else 0)) & MASK
                        elif op >> 12 == 0x5:
                            at = (r[m] + 4 * (op & 15)) & MASK
                            require(mode == "allocation" and at == sp + 4,
                                    "failure shim followed an unproved owner address")
                            owner_reads.append(at);r[n] = caller
                        elif op >> 12 == 0xd:
                            at = ((pc + 4) & ~3) + 4 * (op & 255)
                            r[n], = struct.unpack("<I", read_code(at, 4))
                        elif op == 0x402b:
                            require(read_code(pc + 2, 2) == b"\x09\x00", "failure gateway delay slot changed")
                            pc = r[0];continue
                        elif op != 9:raise ValueError("unadmitted instruction/store in failure gateway")
                        pc += 2
                    else:raise ValueError("failure gateway did not terminate in bounded report")
                    require(r[4:8] == expected and r[15] == mapped["__vmu_native_worker_stack_top"] - stack_delta and
                            sr == inherited_sr | 0xf0 and owner_reads == ([sp + 4] if mode == "allocation" else []),
                            "failure gateway lost arguments, private SP or IRQ exclusion")
                    cases += 1
    rejected = 0
    if self_test:
        for name in (runtime, allocation):
            address, body, section = symbol_bytes(worker, name, 2)
            for offset in range(0, len(body), 2):
                changed = bytearray(worker[0]);changed[section[4] + address - section[3] + offset] ^= 1
                try:authored((bytes(changed),) + worker[1:])
                except ValueError:rejected += 1
                else:raise ValueError("failure gateway accepted a changed authored instruction/literal")
    return {"modeled_cases": cases, "mutation_rejections": rejected,
            "owner_data_writes": 0, "allocation_owner_reads": 1,
            "runtime_report_is_noreturn": True}


def worker_load_policy(worker, binary):
    data, _, segments, sections, _ = worker
    loads = validate_loads(data, binary, P1, SPAN, sections)
    if len(segments) == 1:
        kind, offset, va, pa, size, memory, flags, _ = segments[0]
        require((kind, va, pa, size, memory, flags) == (1, P2, P1, len(binary), SPAN, 7),
                "uncached worker differs from its exact legacy P2 load")
        require(binary == region(data, offset, size, "worker load"), "legacy flat worker differs from ELF")
        return {"cached_code": False, "code_alias": P2, "loads": loads}
    require(len(segments) == len(loads) == 3, "cached worker needs exactly3 physical loads")
    header, code, mutable = loads
    require((header["virtual"], header["physical"], header["bytes"], header["memory"], header["flags"]) ==
            (P2, P1, 80, 80, 4), "cached worker header must be immutable P2/80B")
    require(code["virtual"] == code["physical"] and code["virtual"] & 0xFF000000 == 0x8C000000 and
            code["flags"] == 5 and code["bytes"] == code["memory"] and code["bytes"] and
            not (code["first"] + code["bytes"]) & 31 and
            code["first"] in (physical(P1) + 80, (physical(P1) + 80 + 31) & ~31),
            "cached code/const load must own readonly P1 bytes through an aligned end")
    require(mutable["virtual"] == mutable["physical"] + 0x20000000 and mutable["flags"] == 6 and
            mutable["first"] == code["first"] + code["memory"] and not mutable["first"] & 31 and
            mutable["first"] + mutable["memory"] == physical(P1) + SPAN,
            "mutable worker load must be P2 and disjoint from cached code/constants")
    shstrings = sections[worker[1][13]]
    section_names = region(data, shstrings[4], shstrings[5], "section names")
    for section in sections:
        if not section[2] & 2 or not section[5]: continue
        if section[2] & 1:
            require(section[3] & 0xFF000000 == 0xAC000000 and
                    section[2] & 7 == 3, "cached worker has writable P1/code state")
        elif section[3] == P2:
            require(section[1] == 1 and section[5] == 80 and section[2] & 7 == 2,
                    "P2 header contains unexpected code/state")
        elif section[3] & 0xFF000000 == 0xAC000000:
            name = section_names[section[0]:].split(b"\0", 1)[0]
            require(name == b".image_align" and section[1] == 1 and section[2] & 7 == 2 and
                    0 < section[5] <= 32 and not any(region(data, section[4], section[5], "mutable tail padding")),
                    "P2 immutable bytes are neither header nor zero tail padding")
        else:
            require(section[3] & 0xFF000000 == 0x8C000000 and section[2] & 7 in (2, 6),
                    "code/const bytes do not exclusively use cached P1")
    # ELF reservation/export bounds stay P2. The only additional cached
    # writable alias is the independently bounded ephemeral private stack;
    # runtime gateways prove that alias at every entry below.
    stack=next(r for r in prepared_symbol_records(worker) if r[0]==".stack.cached")
    return {"cached_code": True, "code_alias": P1, "loads": loads,
            "cold_arm_stack_alias": P2, "runtime_stack_alias": P1,
            "runtime_stack_begin": stack[1], "runtime_stack_end": stack[1]+stack[2],
            "persistent_mutable_alias": P2}


def inspect_base(directory, *, self_test=False, objdump="sh-elf-objdump"):
    """Reuse exact bootstrap/layout checks while admitting the one SCI loan."""
    images, elfs = {}, {}
    low_elf = read_elf(directory / "resident-sci.elf")
    background = "_kui_vmu_native_background_manifest_bind" in low_elf[4]
    low_limit = 0x8c007ba0 if background else 0x8c007800
    background_audit = None
    if background:
        # The wider layout is admitted only by the separate exact Background
        # profile proof, including its typed binder, fifth init argument,
        # owned async region, source flags and low-stack call graph.
        from check_vmu_native_background import check as audit_background
        background_audit = audit_background(directory, self_test=self_test, require_stage=True)
    for name, base, limit in (("resident-sci", LOW, low_limit),
                              ("stage", HIGH, LIMIT), ("entry", layout.EXEC_ADDRESS, 0x8C112000)):
        elfs[name] = (directory / (name + ".elf")).read_bytes()
        images[name] = inspect_elf(elfs[name], base, limit)
        if name != "entry":
            require((directory / (name + ".bin")).read_bytes() == images[name]["payload"],
                    name + " binary differs from ELF")
    resident, stage, entry = (images[name] for name in ("resident-sci", "stage", "entry"))
    check_bss(resident, LOW, "__retail_resident")
    check_bss(stage, HIGH, "__retail_stage")
    for name in ("resident-sci", "stage"):
        require(not any(symbol.startswith(("_kui_vmu_maple_observer_", "__vmu_observe_worker_blob_"))
                        for symbol in images[name]["symbols"]), "old fixed-address VMU worker linked")
    require(not any(symbol.startswith(("_kui_vmu_durable_", "_kui_vmu_persist_", "_kui_sci_sd_write",
                                       "_kui_sci_sd_sync")) for symbol in resident["symbols"]),
            "low reader contains a high-only persistent writer")
    for name in ("_kui_vmu_native_heap_discover", "_kui_vmu_native_heap_call_discover", "_kui_vmu_native_heap_fresh",
                 "_kui_vmu_native_heap_preallocate", "_kui_vmu_native_heap_postallocate",
                 "_kui_vmu_native_heap_lease_valid", "_kui_vmu_observe_stage_install",
                 "_kui_vmu_persist_stage_admit", "_kui_vmu_persist_stage_prepare",
                 "_kui_vmu_persist_stage_handoff_matches"):
        typed_function(elfs["stage"], stage, name, HIGH)
    for transport in ("scif", "sci", "ide", "scia"):
        prefix = "__retail_resident_" + transport + "_blob_"
        begin, end = (stage["symbols"].get(prefix + suffix, 0) for suffix in ("start", "end"))
        require(region(stage["payload"], begin - HIGH, end - begin, "embedded resident") ==
                padded(resident["payload"]), "stage embeds different resident: " + transport)
    es = entry["symbols"]
    require(es.get("__retail_map") == layout.EXEC_ADDRESS + layout.MAP_OFFSET and
            es.get("__retail_stage_blob_start") == layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET and
            es.get("__retail_stage_blob_end") == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["memory_end"] == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["payload"][layout.STAGE_BLOB_OFFSET:] == padded(stage["payload"]),
            "entry relocation/stage bytes differ from ELF")
    require(not any(region(entry["payload"], layout.MAP_OFFSET, layout.MAP_BYTES, "blank manifest")),
            "launch manifest must ship blank")
    require(region(entry["payload"], layout.HEADER_OFFSET, layout.HEADER_BYTES, "relocation header") ==
            layout.relocation_header(len(padded(stage["payload"])), False, True),
            "persistent probe must select the low native reader ABI")
    module = (directory / "retail-maple-observe.kui").read_bytes()
    runtime = verify(module)
    require(module[64:] == entry["payload"] and runtime["memory_bytes"] == entry["memory_end"] - layout.EXEC_ADDRESS,
            "module differs from actual entry ELF")
    instructions = {}
    for name in images:
        disassembly = subprocess.check_output([objdump, "-d", str(directory / (name + ".elf"))], text=True)
        instructions[name] = instruction_audit(name, disassembly)
    gateway = check_loan_gateway(directory, resident, elfs["resident-sci"])
    map_refusal = audit_map_refusal(directory / "resident-sci.elf",self_test=self_test,objdump=objdump)
    report = {"schema": "kui.native.persistent.bootstrap.v1", "runtime": runtime,
              "module_sha256": hashlib.sha256(module).hexdigest(),
              "resident_memory_end": f"0x{resident['memory_end']:08x}",
              "stage_memory_end": f"0x{stage['memory_end']:08x}",
              "dynamic_bootstrap": inspect_dynamic_hook(elfs["resident-sci"], resident,low_limit=low_limit),
              "integer_instruction_counts": instructions, "sd_lease_gateway": gateway,
              "gd_map_refusal_terminal":map_refusal,
              "old_fixed_worker_linked": False, "console_verified": False,
              "reader_profile": "background" if background else "standard"}
    if background:report["background_reader"]=background_audit
    if self_test:
        report["dynamic_bootstrap_mutation_rejections"] = regression(elfs["resident-sci"], resident,low_limit=low_limit)
    return report


def check_loan_gateway(directory, resident, elf):
    address, size, declared = sd_gateway_span(elf, resident)
    reports = sorted((directory / "resident/lto").glob("*.ltrans*.ci"))
    if not reports:
        reports = sorted(directory.glob("resident-sci.elf*.ci"))
    nodes = set()
    require(reports, "missing final-LTO SCI loan callgraph proof")
    for report in reports:
        data = report.read_text()
        nodes.update(_function_base(title) for title, _ in NODE.findall(data))
        require(not any(_function_base(target) == "kui_vmu_observe_sd_lease"
                        for _, target, _ in EDGE.findall(data)), "resident call enters worker-stack-only loan")
    require("kui_vmu_observe_sd_lease" in nodes, "final-LTO graph omits SCI loan")
    for offset in range(0, len(resident["payload"]) - 3, 4):
        at = LOW + offset
        if address <= at < address + size:
            continue
        value = struct.unpack_from("<I", resident["payload"], offset)[0]
        if value & 0xFF000000 in (0x0C000000, 0x8C000000, 0xAC000000):
            canonical = value & 0x1FFFFFFF | 0x80000000
            require(not address <= canonical < address + size, "low resident literal enters worker-stack-only loan")
    return {"address": f"0x{address:08x}", "bytes": size, "declared_symbol_bytes": declared,
            "incoming_resident_edges": 0, "resident_pointer_references": 0,
            "callgraph_reports": [report.name for report in reports]}


def check_pio_writers(directory, worker):
    results = {}
    for image_name, image, base, folder, adapter in (
            ("worker", worker, P1 if worker[4]["_kui_vmu_native_passive_arm"][0] < P2 else P2,
             "native", "durable_card_pio_write"),
            ("stage", read_elf(directory / "stage.elf"), HIGH, "stage", "native_persist_pio_write")):
        elf = image[0]
        binary = (directory / ("native-worker.bin" if image_name == "worker" else "stage.bin")).read_bytes()
        address, body, _ = symbol_bytes(image, "_" + adapter, 2)
        writer, _, _ = symbol_bytes(image, "_kui_sci_sd_write", 2)
        report = check_pio_entry({"payload": binary}, address, len(body), writer, base=base)
        report.update(check_pio_callgraph(directory / folder, adapter, linked_function_names(elf, {})))
        results[image_name] = report
    return results


def check_source(directory, runtime):
    raw = (directory / "entry.compile.json").read_bytes()
    record = json.loads(raw)
    require(isinstance(record, dict) and isinstance(record.get("commit"), str) and
            re.fullmatch(r"[0-9a-f]{40}", record["commit"]) and type(record.get("source_dirty")) is bool and
            record.get("elf_sha256") == hashlib.sha256((directory / "entry.elf").read_bytes()).hexdigest() and
            runtime["build"] == record["commit"][:12], "source provenance differs from compiled entry")
    return {"commit": record["commit"], "source_dirty": record["source_dirty"],
            "compile_provenance_sha256": hashlib.sha256(raw).hexdigest(),
            "elf_sha256": record["elf_sha256"]}


def sdk_coherence_metadata(worker):
    """Establish exact descriptor, code-entry and private-state ownership."""
    names={*SDK_COHERENCE_GATES,SDK_COHERENCE_STATE,SDK_COHERENCE_PREPARE}
    present=names&worker[4].keys()
    if not present:return {"available":False}
    require(present==names,"partial SDK cache-coherence implementation")
    table,body,section=symbol_bytes(worker,"_kui_vmu_native_trace_exports",1)
    require(section[2]&7==2 and table&0xff000000==0x8c000000 and
            not table&3 and len(body)==40,
            "SDK coherence descriptor lacks exact40B immutable P1 ownership")
    state,size,kind,owner=worker[4][SDK_COHERENCE_STATE]
    require(owner<len(worker[3]),"SDK coherence state has no section owner")
    bss=worker[3][owner]
    begin=worker[4]["__vmu_native_worker_bss_begin"]
    end=worker[4]["__vmu_native_worker_bss_end"]
    require(size==48 and kind==1 and not state&3 and
            state&0xff000000==0xac000000 and owner==begin[3]==end[3] and
            bss[1]==8 and bss[2]&7==3 and
            begin[0]<=state<state+size<=end[0] and
            bss[3]<=state<state+size<=bss[3]+bss[5],
            "SDK coherence scratch lacks exact48B private P2 BSS ownership")
    for name,(other,count,other_kind,_) in worker[4].items():
        if name==SDK_COHERENCE_STATE or other_kind!=1 or not count:continue
        require(physical(other)+count<=physical(state) or physical(state)+size<=physical(other),
                "SDK coherence scratch overlaps another typed object: "+name)
    targets=[]
    for name in SDK_COHERENCE_GATES:
        address,gate,code=symbol_bytes(worker,name,2)
        require(code[2]&7==6 and address&0xff000000==0x8c000000 and
                not address&1 and gate,
                "SDK cold gate lacks an exact typed cached-code entry")
        targets.append(address+0x20000000)
    expected=(0x4b545231,2,40,*(worker[4][name][0] for name,*_ in TRACE_GATES),*targets,state)
    require(struct.unpack("<10I",body)==expected,
            "SDK coherence descriptor differs from exact typed entries/state")
    return {"available":True,"descriptor_address":table,"descriptor_bytes":40,
            "state_address":state,"state_bytes":48,
            "p2_references":[[physical(table)-physical(P1)+28+4*i,value]
                             for i,value in enumerate(targets)],
            "verified_cold_owner_stack_gateways":[name[1:] for name in SDK_COHERENCE_GATES]}


def sdk_coherence_admission(worker,self_test=False):
    """Execute cold handoff before authorizing its two P2 entry pointers."""
    metadata=sdk_coherence_metadata(worker)
    if not metadata["available"]:return metadata
    from check_vmu_native_sdk_coherence import audit
    result=audit(worker,self_test=self_test)
    require(result.get("instruction_cases",0)>0 and
            result.get("owner_stack_accesses_before_purge")==0 and
            result.get("purge_before_first_tas") is True and
            result.get("no_private_stack_use") is True and
            result.get("forced_lock_writes")==0 and
            result.get("uniform_replay_stack_bytes")==8 and
            result.get("variable_replay_stack_bytes")==24,
            "SDK coherence gates lack the complete pre-acquisition/stack proof")
    result.update(metadata)
    return result


def fault_capture_admission(worker,self_test=False):
    """Run the exact fault gateway/publisher before granting narrow exceptions."""
    names={FAULT_GATEWAY,FAULT_PUBLISH,FAULT_STATE,"_kui_vmu_native_fault_capture_install"}
    present=names&worker[4].keys()
    if not present:return {"available":False}
    require(present==names,"partial fault-capture implementation cannot acquire special admission")
    address,size,kind,owner=worker[4][FAULT_STATE]
    section=worker[3][owner]
    require(size==32 and kind==1 and not address&3 and address&0xff000000==0xac000000 and
            owner==worker[4]["__vmu_native_worker_bss_begin"][3] and
            section[3]<=address<address+size<=section[3]+section[5],
            "fault capture scratch lacks exact32B private P2 BSS ownership")
    prepared_symbol_records(worker)
    result=audit_fault_capture(worker,self_test)
    require(result["instruction_cases"] and result["publication_cases"]==5 and
            result["publication_from_p2_after_two_operand_cache_purges"] and
            result["interrupted_stack_accesses"]==0,
            "fault capture lacks its complete stack/publication proof")
    address,body,section=symbol_bytes(worker,FAULT_PUBLISH,2)
    require(section[2]&7==6 and address&0xff000000==0x8c000000 and
            len(body)>=16 and struct.unpack_from("<I",body,8)[0]==address+12+0x20000000,
            "fault publisher lacks the exact unique P2 island literal")
    result.update(available=True,mode="PROFILE; exact Evolution revision only",
                  publisher_p2_reference_offset=physical(address)-physical(P1)+8,
                  publisher_p2_reference_value=address+12+0x20000000,
                  verified_zero_stack_assembly=[FAULT_GATEWAY[1:],FAULT_PUBLISH[1:]])
    installer,body,_=symbol_bytes(worker,"_kui_vmu_native_fault_capture_install",2)
    result.update(installer_first=physical(installer)-physical(P1),installer_bytes=len(body),
                  cached_bss_boundaries=[worker[4][name][0]-0x20000000 for name in
                      ("__vmu_native_worker_bss_begin","__vmu_native_worker_bss_end")],
                  cached_header_end=P1+80)
    return result


def check_worker_pointer_aliases(wire,policy,fault_capture,coherence=None):
    """Admit only the independently proved island and ownership comparisons."""
    relocations=struct.unpack_from("<I",wire,28)[0]
    fault_islands=0
    coherence=coherence or {"available":False}
    cold_sites={tuple(row) for row in coherence.get("p2_references",[])}
    require(len(cold_sites)==(2 if coherence["available"] else 0),
            "SDK cold gates require exactly two distinct descriptor references")
    cold_seen=[]
    for i in range(relocations):
        at,value=struct.unpack_from("<II",wire,72+i*8)
        area=value&0xff000000
        require(area in ((0x8c000000,0xac000000) if policy["cached_code"] else (0xac000000,)),
                "internal relocated pointer uses an unadmitted alias")
        if not policy["cached_code"]:continue
        code=policy["loads"][1]
        if area==0x8c000000:
            boundary=(fault_capture["available"] and
                value in fault_capture["cached_bss_boundaries"]+[fault_capture["cached_header_end"]] and
                fault_capture["installer_first"]<=at<fault_capture["installer_first"]+fault_capture["installer_bytes"])
            require(code["first"]<=physical(value)<=code["first"]+code["memory"] or
                    policy["runtime_stack_begin"]<=value<=policy["runtime_stack_end"] or boundary,
                    "cached pointer aliases persistent mutable P2 state")
        elif code["first"]<=physical(value)<code["first"]+code["memory"]:
            if (at,value) in cold_sites:
                cold_seen.append((at,value))
            else:
                require(fault_capture["available"] and at==fault_capture["publisher_p2_reference_offset"] and
                        value==fault_capture["publisher_p2_reference_value"],
                        "uncached pointer aliases immutable cached code/constants")
                fault_islands+=1
    require(fault_islands==int(fault_capture["available"]),
            "fault publisher must own exactly one audited P2 island reference")
    require(len(cold_seen)==len(cold_sites) and set(cold_seen)==cold_sites,
            "SDK cold descriptor must own exactly two audited P2 gate references")
    return relocations


def worker_stack_paths(directory, worker, resident, fault_proof=None, coherence_proof=None,
                       *, terminal_proof=None,terminal_gateway_cases=0,read_ahead_proof=None):
    """Walk source-qualified emitted graphs with finite authored callbacks."""
    report_sets = {"worker": sorted((directory / "native").rglob("*.ci")),
                   "low": sorted(directory.glob("resident-sci.elf*.ltrans*.ci"))}
    if not report_sets["low"]:
        report_sets["low"] = sorted((directory / "resident/lto").glob("*.ltrans*.ci"))
    images = {"worker": worker[4], "low": resident["symbols"]}
    background="_kui_vmu_native_background_manifest_bind" in images["low"]
    descriptor=None
    if background:
        low=read_elf(directory/'resident-sci.elf')
        address,body,section=symbol_bytes(low,'_bus',1)
        require(len(body)==28 and section[2]&7==2,
                'Background loan needs its exact immutable seven-word descriptor')
        select,transfer,ticks=(symbol_bytes(low,name,2)[0]
            for name in ('_select_card','_transfer','_loan_ticks'))
        require(struct.unpack('<7I',body)==(0,0,0,select,transfer,ticks,0),
                'Background loan bus callbacks/NULL block callback differ')
        descriptor={'address':hex(address),'bytes':28,'ctx_begin_end_null':True,
                    'select':'select_card','transfer':'transfer','ticks':'loan_ticks',
                    'transfer_block_null':True}
    frames, calls, origins = {}, {}, {}
    for domain, reports in report_sets.items():
        require(reports, "missing emitted callgraph: " + domain)
        f, c = load_graph(reports, qualified=True, scope=domain)
        live = {_function_base(name) for name in images[domain]}
        f = {name: value for name, value in f.items() if _function_base(name) in live}
        frames.update(f); calls.update(c)
        for report in reports:
            for title, label in NODE.findall(report.read_text()):
                pieces = label.split(r"\n")
                if len(pieces) >= 2:
                    origins[domain + ":" + title] = Path(pieces[1].rsplit(":", 2)[0]).name
    # This authored leaf has no compiler .su/.ci file. Admit zero stack only
    # after proving all seven actual instructions: three control-register
    # reads, three stores through R4 and RTS. It neither pushes nor calls.
    _, reader, owner = symbol_bytes(worker, "_kui_vmu_native_trace_irq", 2)
    require(owner[2] & 7 == 6 and reader == bytes.fromhex("420002243200011422000b000214"),
            "saved exception reader is not the exact zero-stack read-only leaf")
    frames["worker:kui_vmu_native_trace_irq"] = 0
    calls["worker:kui_vmu_native_trace_irq"] = []
    if read_ahead_proof and read_ahead_proof.get('enabled'):
        require(read_ahead_proof.get('paired_markers') and
                read_ahead_proof.get('gateway',{}).get('parked_owner_stack_bytes')==20,
                'read-ahead worker call lacks its actual20B guarded stack switch')
        frames['worker:verified_read_ahead_stack_switch']=20
        calls['worker:verified_read_ahead_stack_switch']=[]
    if fault_proof is None:fault_proof=fault_capture_admission(worker)
    fault_leaves=fault_proof.get("verified_zero_stack_assembly",[])
    if coherence_proof is None:coherence_proof=sdk_coherence_admission(worker)
    cold_gates=coherence_proof.get("verified_cold_owner_stack_gateways",[])
    for name in fault_leaves:
        # The proof above executed these authored assembly bodies with
        # unusable owner stacks; neither pushes a frame or calls C.
        frames["worker:"+name]=0;calls["worker:"+name]=[]
    by_base = {}
    for name in frames:
        by_base.setdefault((name.split(":", 1)[0], _function_base(name)), []).append(name)
    def resolve(domain, function, owner=None):
        exact = domain + ":" + function
        found = [exact] if exact in frames else by_base.get((domain, _function_base(function)), [])
        if owner is not None:
            found = [name for name in found if origins.get(name) == owner]
        require(found, "missing emitted callback frame: " + domain + ":" + function)
        return found
    def high(*names):
        return [node for name in names for node in resolve("worker", name)]
    def bus(*names):
        return [node for name in names for node in resolve("low",
                "loan_ticks" if background and name=="ticks" else name,
                "sci_sd_bus.c")]
    memo, paths, visiting, indirect_sites, null_sites = {}, {}, set(), set(), set()
    terminal_escapes=set()
    def indirect(name, label):
        domain, function = name.split(":", 1)[0], _function_base(name)
        source = Path(label.split(":", 1)[0]).name
        if read_ahead_proof and read_ahead_proof.get('enabled') and (
            (domain,function,source)==('worker','reader_readahead_visit','vmu_native_reader_capture.inc') or
            (domain,function,source)==('low','kui_vmu_observe_sd_lease','retail_async.c')):
            return ['worker:verified_read_ahead_stack_switch']
        if domain == "low":
            if (function,source)==("kui_vmu_observe_sd_lease",
                    "vmu_native_background_resident.c" if background else "vmu_sd_lease.inc"):
                return high("kui_vmu_native_persistent_sd_transaction")
            if background and (function,source) in (("byte","sci_stream.c"),("select","sci_stream.c")):
                return bus("transfer" if function=="byte" else "select_card")
            if background and (function,source) in (('deliver','retail_cursor.c'),('kui_retail_cursor_feed','retail_cursor.c')):
                return resolve('low','write_out','retail_async.c')
            if background and (function,source)==('write_out','retail_async.c'):
                return resolve('low','native_background_map')
            if background and (function,source)==('kui_vmu_observe_low_failure','vmu_native_background_resident.c'):
                # The low map-refusal instruction proof binds this target to
                # the relocated terminal; its actual gateway replaces SP
                # before entering C. That report's root is walked separately
                # below, not added to the abandoned caller's current stack.
                require(terminal_proof and terminal_proof.get('schema')=='kui.native.gd-map-refusal.v1' and
                        terminal_proof.get('cached_worker_terminal') and
                        terminal_proof.get('mutation_rejections',0)>0 and terminal_gateway_cases>=7680,
                        'cooperative map refusal lacks the independent terminal/stack-switch proof')
                terminal_escapes.add((name,label));return []
            raise ValueError("unexpected low callback from worker stack: " + name + " at " + label)
        fixed = {
            "vmu_durable.c": ("persistent_lease",),
            "vmu_emulation.c": ("durable_card_read", "durable_card_commit"),
            "vmu_native_heap.c": ("read_heap",),
            "vmu_native_layout.c": ("read_value",),
            "vmu_native_lifetime.c": ("read_heap",),
            "vmu_native_live.c": ("boundary_read",),
            "vmu_native_runtime.c": ("boundary_read",),
            "vmu_store.c": ("durable_card_sector_read", "durable_card_sector_write", "durable_card_sector_sync"),
            "vmu_readonly_overlay.c": ("persistent_admit", "persistent_handler"),
        }
        owners = {
            "vmu_durable.c": {"durable_card_execute"},
            "vmu_emulation.c": {"kui_vmu_emulation_handle"},
            "vmu_native_heap.c": {"read_is", "kui_vmu_native_heap_lease_valid", "kui_vmu_native_heap_lease_valid_with_release"},
            "vmu_native_layout.c": {"kui_vmu_native_layout_bind_checked"},
            "vmu_native_lifetime.c": {"kui_vmu_native_lifetime_release_safe_ranges"},
            "vmu_native_live.c": {"kui_vmu_native_live_check"},
            "vmu_native_runtime.c": {"kui_vmu_native_runtime_check"},
            "vmu_store.c": {"write_sync", "kui_vmu_store_open", "kui_vmu_store_read", "kui_vmu_store_sync"},
            "vmu_readonly_overlay.c": {"kui_vmu_readonly_overlay_prepare", "kui_vmu_readonly_overlay_complete"},
        }
        if source in fixed:
            require(function in owners[source], "unexpected callback source owner: " + name)
            return high(*fixed[source])
        if (function, source) == ("persistent_lease", "vmu_native_passive.c"):
            return resolve("low", "kui_vmu_observe_sd_lease")
        if (function, source)==('reader_cooperative_service','vmu_native_reader_capture.inc'):
            return resolve('low','kui_vmu_observe_sd_lease')
        if source == "sd_reader.c":
            if background and function=='read_payload':
                # The exact typed descriptor above, copied by the admitted
                # native loan, has no transfer_block callback. The byte PIO
                # fallback remains in the emitted graph through transfer.
                require(descriptor['transfer_block_null'],'Background block callback lacks its immutable NULL proof')
                null_sites.add((name,label));return []
            targets = {"transfer": ("transfer",), "ticks": ("ticks",), "release": ("select_card",),
                       "read_payload": ("transfer_block",), "kui_loader_sd_read": ("select_card",)}
            require(function in targets, "unexpected initialized-card reader callback: " + name)
            return bus(*targets[function])
        if source == "vmu_backing.c":
            require(function in ("byte", "wait_byte", "kui_vmu_sd_identity_read"),
                    "unexpected backing callback: " + name)
            return bus("transfer", "ticks", "select_card")
        if source == "sci_sd_storage.c":
            if function == "kui_sci_sd_write" and label.endswith(":83:17"):
                # Actual native PIO adapters independently prove this callback
                # NULL at the sole writer entry; this optional branch is dead.
                null_sites.add((name, label)); return []
            return bus("transfer", "ticks", "select_card")
        raise ValueError("unadmitted indirect worker call: " + name + " at " + label)
    def depth(name):
        if name in memo:
            return memo[name]
        require(name not in visiting, "recursive private-stack path: " + name)
        if name not in frames:
            if _function_base(name) in ("__udivsi3", "__sdivsi3", "__udivdi3", "__umoddi3", "__moddi3"):
                return 0, [name + "(library margin)"]
            domain, target = name.split(":", 1)
            alternatives = resolve(domain, target)
            return max((depth(node) for node in alternatives), key=lambda item: item[0])
        visiting.add(name); best = (0, [])
        for target, label in calls.get(name, []):
            targets = indirect(name, label) if target == "__indirect_call" else [target]
            if target == "__indirect_call":
                indirect_sites.add((name, label, tuple(targets)))
            for callee in targets:
                found = depth(callee)
                if found[0] > best[0]: best = found
        visiting.remove(name)
        memo[name] = (frames[name] + best[0], [name + "(" + str(frames[name]) + ")"] + best[1])
        return memo[name]
    for root in ("kui_vmu_native_passive_arm", "kui_vmu_native_passive_submit",
                 "kui_vmu_native_passive_complete", "kui_vmu_native_readonly_release_check",
                 "kui_vmu_native_passive_report", "kui_vmu_native_runtime_fault",
                 "kui_vmu_native_trace_game"):
        answer = max((depth(node) for node in resolve("worker", root)), key=lambda item: item[0])
        # Keep a full256B untouched border above the stack-bottom canaries,
        # plus the existing80B assembly/library-call allowance. Runtime
        # stack_valid requires that border; the linked deepest call path
        # must establish it independently of the sampled SP checks.
        total = answer[0] + 80 + 256
        require(total <= 4096, f"private stack path {root} needs{total} bytes")
        paths[root] = {"c_frame_bytes": answer[0], "assembly_and_guard_margin": 336,
                       "conservative_bytes": total, "worst_path": answer[1]}
    require(null_sites, "missing independently guarded optional writer block callback")
    return {"paths": paths, "indirect_sites": len(indirect_sites),
            "null_writer_block_sites": [list(site) for site in sorted(null_sites)],
            "source_qualified_calls": True,
            "verified_zero_stack_assembly_leaves": ["kui_vmu_native_trace_irq",*fault_leaves],
            "verified_cold_owner_stack_gateways":cold_gates,
            "verified_stack_replacing_terminals": [list(site) for site in sorted(terminal_escapes)],
            "background_loan_descriptor":descriptor}


def check_stacks(directory, worker, fault_proof=None, coherence_proof=None, *, background_audit=None,
                 terminal_proof=None,terminal_gateway_cases=0,read_ahead_proof=None):
    resident = inspect_elf((directory / "resident-sci.elf").read_bytes(), LOW,
                           0x8c007ba0 if background_audit else 0x8C007800)
    low_rows = linked_stack_rows(sorted((directory / "resident/lto").glob("*.ltrans*.su")), resident["symbols"])
    high_rows = linked_stack_rows(sorted((directory / "native").rglob("*.su")), worker[4])
    reported = {_function_base(name) for name, _ in high_rows}
    assembly = {"kui_vmu_native_passive_submit_hook", "kui_vmu_native_passive_complete_hook",
                "kui_vmu_native_passive_terminal", "kui_vmu_native_readonly_release_hook",
                "kui_vmu_native_runtime_fault_hook", "kui_vmu_native_allocation_failure_hook",
                "kui_vmu_native_trace_irq"}
    assembly.update(name[1:] for name, *_ in TRACE_GATES)
    if fault_proof is None:fault_proof=fault_capture_admission(worker)
    assembly.update(fault_proof.get("verified_zero_stack_assembly",[]))
    if coherence_proof is None:coherence_proof=sdk_coherence_admission(worker)
    # Cold gates replay the original owner's prologue only after the proved
    # purge; they do not use this private C stack and are not zero-stack leaves.
    assembly.update(coherence_proof.get("verified_cold_owner_stack_gateways",[]))
    required = {_function_base(name) for name, (_, _, kind, _) in worker[4].items()
                if kind == 2 and _function_base(name).startswith(("kui_", "durable_", "readonly_overlay_", "sci_storage_"))
                and _function_base(name) not in assembly}
    require(required <= reported, "missing linked native C stack frame: " + ", ".join(sorted(required - reported)))
    lease = [(name, frame) for name, frame in low_rows if _function_base(name) == "kui_vmu_observe_sd_lease"]
    require(len(lease) == 1, "missing unique worker-stack-only lease frame")
    # Every retained C frame is included, including low bus callbacks called
    # from the worker. Summing them overstates any single actual path.
    high_sum, low_sum = sum(frame for _, frame in high_rows), sum(frame for _, frame in low_rows)
    call_paths = worker_stack_paths(directory, worker, resident, fault_proof, coherence_proof,
        terminal_proof=terminal_proof,terminal_gateway_cases=terminal_gateway_cases,
        read_ahead_proof=read_ahead_proof)
    total = max(path["conservative_bytes"] for path in call_paths["paths"].values())
    available = (resident_layout.stack_top("sci", False, True) -
                 resident_layout.stack_bottom("sci", False, True) -
                 resident_layout.STACK_GUARD_BYTES - resident_layout.STACK_ALIGNMENT_GAP)
    ordinary = low_sum - lease[0][1] + resident_layout.ASSEMBLY_STACK_BYTES
    if background_audit:
        # Async GD/IRQ paths are mutually exclusive and independently walked
        # by the typed Background proof. Keep worker loan+callbacks in the
        # call graph above; do not replace them with a byte-identical worker
        # assertion because the linked low loan implementation differs.
        low_stack=background_audit["private_stack"]
        ordinary,available=low_stack["worst_total_bytes"],low_stack["available_bytes"]
    require(ordinary <= available, f"ordinary native GD stack sum {ordinary} exceeds{available}")
    return {"worker_c_frame_sum": high_sum, "resident_callback_c_frame_sum": low_sum,
            "conservative_worker_bytes": total, "available_worker_bytes": 4096,
            "ordinary_low_stack_bytes": ordinary, "ordinary_low_available_bytes": available,
            "lease_only_frame_bytes": lease[0][1], "required_worker_frames": len(required), "callgraph": call_paths}


def check(directory, *, self_test=False, trace_startup=None):
    require(__debug__, "gateway model requires Python assertions")
    directory = Path(directory)
    base = inspect_base(directory, self_test=self_test,
                        objdump=shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump"))
    worker = read_elf(directory / "native-worker.elf")
    data, header, segments, sections, raw_symbols = worker
    symbols = {name: record[0] for name, record in raw_symbols.items()}
    binary = (directory / "native-worker.bin").read_bytes()
    wire = (directory / "native-worker.reloc").read_bytes()
    fault_capture=fault_capture_admission(worker,self_test)
    coherence=sdk_coherence_admission(worker,self_test)
    policy = worker_load_policy(worker, binary)
    cached, code_base = policy["cached_code"], policy["code_alias"]
    require(base['gd_map_refusal_terminal']['cached_worker_terminal']==cached,
            'low GD refusal terminal uses a different worker code alias')
    require(len(binary) >= 80, "worker header is truncated")
    fields = struct.unpack_from("<20I", binary)
    names = ("_kui_vmu_native_passive_arm", "_kui_vmu_native_passive_submit_hook",
             "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_terminal",
             "__vmu_native_worker_bss_begin", "__vmu_native_worker_bss_end",
             "__vmu_native_worker_stack_bottom", "__vmu_native_worker_stack_top",
             "__vmu_native_worker_end", "_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
             "_kui_vmu_native_readonly_release_hook", "_kui_vmu_native_readonly_release_resume",
             "_kui_vmu_native_persistent_sd_transaction", "_kui_vmu_native_runtime_fault_hook",
             "_kui_vmu_native_allocation_failure_hook")
    require(fields[:3] == (0x4B4E5031, 3, 80) and
            fields[3:19] == tuple(symbols[name] for name in names) and fields[19] == symbols['_kui_vmu_native_trace_exports'],
            "persistent v3 worker exports differ from exact typed symbols")
    require(fields[3] == header[4] and all(not p & 1 and code_base + 80 <= p < code_base + len(binary) for p in fields[3:7]),
            "worker entry/hook points outside initialized code")
    require(P2 + len(binary) == fields[7] <= fields[8] <= fields[9] and
            fields[9:12] == (P2 + 0x1F000, P2 + SPAN, P2 + SPAN), "worker BSS/stack layout differs")
    address, exported, section = symbol_bytes(worker, "_kui_vmu_native_worker_exports", 1)
    require(address == P2 and exported == binary[:80] and section[2] & 7 == 2,
            "exports must be typed immutable header")
    trace_address, trace_exports, trace_section = symbol_bytes(worker, '_kui_vmu_native_trace_exports', 1)
    require(trace_address == fields[19] and trace_address % 4 == 0 and
            trace_section[2] & 7 == 2 and
            ((coherence["available"] and len(trace_exports)==40) or
             (not coherence["available"] and len(trace_exports)==32 and
              struct.unpack('<8I',trace_exports)==
              (0x4B545231,1,32,*(symbols[name] for name,*_ in TRACE_GATES),0))),
            'trace exports differ from exact typed immutable gateways')
    for name, *_ in TRACE_GATES:
        _, _, owner = symbol_bytes(worker, name, 2)
        require(owner[2] & 7 == 6, 'trace gateway is not executable')
    trace_audit = dict(exact_typed_exports=True, exact_image_only=True,
                       arbitrary_stall_pc_capture=False, startup_equivalence_checked=False)
    if trace_startup is not None:
        trace_audit.update(audit_trace_gates(directory, trace_startup, self_test=self_test),
                           startup_equivalence_checked=True)
    for name in names[:4] + (names[11], names[13], names[14], names[15]):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "exported entry is not executable")
    for name in ("_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
                 "_kui_vmu_native_readonly_release_resume"):
        address, cell, section = symbol_bytes(worker, name, 1)
        require(len(cell) == 4 and address % 4 == 0 and not any(cell) and section[2] & 7 == 3,
                "resume/consumer cells must be typed, zero-initialized worker-owned words")
    for section in sections:
        require(not section[2] & 2 or not section[5] or
                physical(P1) <= physical(section[3]) < physical(section[3]) + section[5] <= physical(P1) + SPAN,
                "allocated worker bytes escape physical lease span")
    for name, (value, _, symbol_kind, owner) in raw_symbols.items():
        require(symbol_kind not in (1, 2) or owner < len(sections) and
                physical(P1) <= physical(value) < physical(P1) + SPAN,
                "worker object/function escapes physical lease: " + name)
        if symbol_kind == 2:
            require(code_base <= value < code_base + len(binary), "function uses wrong cached/uncached alias: " + name)
        if symbol_kind == 1 and sections[owner][2] & 1:
            require(value & 0xFF000000 == 0xAC000000, "mutable object has a cached P1 alias: " + name)
        require(not name.startswith(("_kui_retail_storage_", "_kui_sci_sd_acquire", "_kui_sci_sd_release",
                                      "_kui_loader_sd_init", "_kui_loader_sd_adopt", "_kui_loader_sd_shutdown")),
                "persistent worker independently owns/reinitializes resident storage: " + name)
    for name in ("_kui_vmu_durable_init_address", "_kui_vmu_durable_handle", "_kui_vmu_durable_admit",
                 "_kui_vmu_durable_transaction", "_kui_vmu_store_open", "_kui_vmu_store_write",
                 "_kui_vmu_store_read", "_kui_sci_sd_write", "_kui_sci_sd_sync",
                 "_kui_vmu_readonly_overlay_prepare", "_kui_vmu_readonly_overlay_complete",
                 "_kui_vmu_volatile_chain_build_address", "_kui_vmu_volatile_chain_verify",
                 "_kui_vmu_native_readonly_release_check"):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "missing bounded persistent bridge implementation: " + name)
    require(generate(data, binary, P1, SPAN) == wire, "relocation table differs from exact ELF extraction")
    runtime_fault = check_runtime_fault_gateways(worker, binary, wire, self_test)
    stack_guard = check_cached_stack_guard(worker, self_test) if cached else {"cached_runtime":False}
    compiled_reader = check_compiled_heap_reader(worker,self_test)
    stall=worker[4].get('__kui_native_reader_stall_capture')
    low_stall=read_elf(directory/'resident-sci.elf')[4].get('__kui_native_reader_stall_resident')
    require(bool(stall)==bool(low_stall),'reader stall worker/resident diagnostic mismatch')
    if stall:
        require(stall==low_stall==(1,0,0,0xfff1),'invalid paired reader stall markers')
    cooperative=worker[4].get('__kui_native_cooperative_reader')
    low_cooperative=read_elf(directory/'resident-sci.elf')[4].get('__kui_native_cooperative_resident')
    require(bool(cooperative)==bool(low_cooperative),'cooperative reader worker/resident mismatch')
    if cooperative:
        require(cooperative==low_cooperative==(1,0,0,0xfff1),'invalid paired cooperative reader markers')
    timed=worker[4].get('__kui_native_timed_reader')
    low_timed=read_elf(directory/'resident-sci.elf')[4].get('__kui_native_timed_resident')
    require(bool(timed)==bool(low_timed),'timed reader worker/resident mismatch')
    if timed:require(cooperative and timed==low_timed==(1,0,0,0xfff1),'invalid paired timed reader markers')
    paced=worker[4].get('__kui_native_paced_reader')
    low_paced=read_elf(directory/'resident-sci.elf')[4].get('__kui_native_paced_resident')
    require(bool(paced)==bool(low_paced),'paced reader worker/resident mismatch')
    if paced:require(cooperative and not timed and paced==low_paced==(1,0,0,0xfff1),'invalid paired paced reader markers')
    irq_rearm=worker[4].get('__kui_native_irq_rearm_reader')
    low_rearm=read_elf(directory/'resident-sci.elf')[4].get('__kui_native_irq_rearm_resident')
    require(bool(irq_rearm)==bool(low_rearm),'IRQ rearm worker/resident mismatch')
    if irq_rearm:require(cooperative and not timed and not paced and irq_rearm==low_rearm==(1,0,0,0xfff1),
                         'invalid paired IRQ rearm reader markers')
    from check_vmu_native_readahead import check as check_read_ahead
    read_ahead_proof=base.get('background_reader',{}).get('read_ahead_companion')
    if read_ahead_proof is None:read_ahead_proof=check_read_ahead(directory,self_test=self_test)
    sdk_sci_proof=check_sdk_sci_route(read_elf(directory/'resident-sci.elf'),worker=worker,
        stage=read_elf(directory/'stage.elf'),self_test=self_test,read_ahead_proof=read_ahead_proof)
    if read_ahead_proof.get('enabled'):
        require(sdk_sci_proof.get('enabled'),'read-ahead requires the independently proved SDK SCI route')
    if sdk_sci_proof.get('enabled'):
        # This paired architecture owns the original SDK callback slots and
        # never installs raw VBR vectors. Its strict linked-code proof is
        # separate from the unchanged legacy rearm/release proof.
        irq_rearm_proof=dict(enabled=True,reader_sci_level=1,
            sdk_callback_route=True,raw_vbr_rearm=False,console_performance_verified=False)
    else:
        irq_rearm_proof=check_irq_rearm(read_elf(directory/'resident-sci.elf'),worker=worker,self_test=self_test)
    timeline_proof=check_loader_timeline(directory,self_test=self_test,read_ahead_proof=read_ahead_proof)
    priority7=worker[4].get('__kui_native_sci_priority7_worker')
    low_priority7=read_elf(directory/'resident-sci.elf')[4].get('__kui_native_sci_priority7_resident')
    rehook_priority7=read_elf(directory/'resident-sci.elf')[4].get('__kui_native_sci_priority7_rehook')
    require(bool(priority7)==bool(low_priority7)==bool(rehook_priority7),
            'SCI priority7 worker/resident/rehook mismatch')
    if priority7:
        require(priority7==low_priority7==rehook_priority7==(7,0,0,0xfff1) and
                timeline_proof.get('enabled') and irq_rearm_proof.get('reader_sci_level')==7,
                'SCI priority7 requires the paired timeline and exact linked priority proof')
    prepared_lifetime = check_prepared_lifetime(directory, worker, binary, wire, self_test,
        build=base["runtime"]["build"],read_ahead_proof=read_ahead_proof)
    relocations=check_worker_pointer_aliases(wire,policy,fault_capture,coherence)

    stage = read_elf(directory / "stage.elf")
    prepared = inspect_elf(stage[0], 0x8CE00000, 0x8CFE0000)
    stage_binary = (directory / "stage.bin").read_bytes()
    require(prepared["payload"] == stage_binary, "stage binary differs from ELF")
    publication = check_resume_publication(stage, cached, self_test)
    objdump = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    stage_disassembly = subprocess.check_output([objdump, "-d", str(directory / "stage.elf")], text=True)
    publisher = check_worker_publication(stage, cached, stage_disassembly, self_test,
        background_tail=base.get('background_reader',{}).get('stage',{}).get('tail_copy_replay'))
    seed = audit_cache_seed(stage[0], (directory / "entry.elf").read_bytes(),
                            (directory / "retail-maple-observe.kui").read_bytes(), cached_code=cached)
    runtime_mode = audit_runtime_mode(stage[0], (directory / "entry.elf").read_bytes(),
        (directory / "retail-maple-observe.kui").read_bytes(), cached_code=cached,
        stage_image=stage,resident_image=read_elf(directory / "resident-sci.elf"),self_test=self_test)
    cache_gateway = check_cache_gateway(stage, self_test=self_test) if seed["enabled"] else {"enabled": False}
    require(bool(paced or irq_rearm)==bool(cache_gateway.get('pure_extent_cache_scope',{}).get('enabled')),
            'paced profile/pure extent cache scope mismatch')
    installer_stack = check_stage_stack(stage, read_elf(directory / "resident-sci.elf"),
                                        worker=worker, self_test=self_test)
    require(not any(name.startswith(("__native_resolution_cache_seed_", "_kui_vmu_native_resolution_cache_",
                                    "_native_cached_resolution", "_native_resolution_replay"))
                    for name in symbols), "temporary resolution cache is retained in the gameplay worker")
    for begin, end, payload in (("__native_worker_blob_begin", "__native_worker_blob_end", binary),
                                ("__native_worker_reloc_begin", "__native_worker_reloc_end", wire)):
        a, b = stage[4][begin][0], stage[4][end][0]
        require(b - a == len(payload) and region(stage_binary, a - 0x8CE00000, b - a, begin) == payload,
                "stage embeds different worker/relocation bytes")
        require(any(s[1] == 1 and s[2] & 7 == 2 and s[3] <= a < b <= s[3] + s[5] for s in stage[3]),
                "embedded worker must occupy stage immutable data")
    gateway_address, gateway, _ = symbol_bytes(stage, "_kui_vmu_native_worker_call", 2)
    stage_cases = (model_cached_stage if cached else model_gateway)(gateway_address, gateway)
    hook_cases = (model_cached_hooks if cached else model_hooks)(binary, wire, symbols)
    objdump = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    disassembly = subprocess.check_output([objdump, "-d", str(directory / "native-worker.elf")], text=True)
    try:
        instructions = instruction_audit("native-readonly", disassembly)
    except SystemExit as error:
        raise ValueError(str(error)) from error
    hook_ranges = [symbol_bytes(worker, name, 2)[:2] for name in
                   names[1:4] + (names[11], names[14]) + tuple(gate[0] for gate in TRACE_GATES)]
    if fault_capture["available"]:hook_ranges.append(symbol_bytes(worker,FAULT_GATEWAY,2)[:2])
    if coherence["available"]:hook_ranges.extend(symbol_bytes(worker,name,2)[:2] for name in SDK_COHERENCE_GATES)
    for address, _, mnemonic, operands in literal_aware_instructions(disassembly):
        require(mnemonic not in ("trapa", "sleep", "rte", "ldtlb"), "unexpected privileged worker instruction")
        if mnemonic in ("ldc", "ldc.l") and operands.split("!", 1)[0].strip().endswith(",sr"):
            require(any(begin <= address < begin + len(body) for begin, body in hook_ranges),
                    "C worker changes SR outside audited gateways")
    pio = check_pio_writers(directory, worker)
    stacks = check_stacks(directory, worker, fault_capture, coherence,
                          background_audit=base.get("background_reader"),
                          terminal_proof=base.get('gd_map_refusal_terminal'),terminal_gateway_cases=hook_cases,
                          read_ahead_proof=read_ahead_proof)
    source = check_source(directory, base["runtime"])
    report = {"schema": "kui.native.persistent.probe.artifact.v1", "allocator_bootstrap_audit": base,
              "module_sha256": base["module_sha256"], "runtime": base["runtime"],
              "worker_bytes": len(binary), "worker_span": SPAN, "worker_stack_bytes": 4096,
              "worker_load_policy": policy, "cached_code": cached,
              "prepared_lifetime_admission": prepared_lifetime,
              "reader_stall_capture": {"enabled": bool(stall),
                  "paired_resident_worker": bool(stall),"private_worker_state_bytes": 508 if stall else 0,
                  "diagnostic_sd_log_writes": False,"console_root_cause_established": False},
              "cooperative_reader": {"enabled": bool(cooperative),
                  "service_enabled":bool(cooperative and not paced and not irq_rearm),
                  "pump_blocks_per_visit": 2 if cooperative and not paced and not irq_rearm else 0,
                  "foreground_catchup_changed": bool(paced),"service_after_physical_controller_proof": bool(cooperative and not paced and not irq_rearm),
                  "completed_request_timing": bool(cooperative),"console_performance_verified": False},
              "paced_reader": {"enabled":bool(paced),"paired_resident_worker":bool(paced),
                  "tight_check_polls":16 if paced else 0,"near_scanline_delta_max":1 if paced else 0,
                  "ordinary_check_waits":False if paced else None,
                  "exec_catchup_blocks":[20,25] if paced else [],"irq_batch_limit":8 if paced else 0,
                  "pio_blocks_per_entry_max":1 if paced else 0,"new_resident_ram_bytes":0,
                  "controller_reader_service_enabled":False if paced else None,
                  "console_performance_verified":False},
              "irq_rearm_reader":dict(irq_rearm_proof,paired_resident_worker=bool(irq_rearm),
                  full_check_and_exec_catchup=bool(irq_rearm),controller_reader_service_enabled=False,
                  rearm_and_irq_entry_counters=bool(irq_rearm)),
              "loader_timeline":timeline_proof,
              "sdk_sci_route":sdk_sci_proof,
              "read_ahead":read_ahead_proof,
              "sci_priority7":dict(enabled=bool(priority7),paired_resident_worker_rehook=bool(priority7),
                  sci_priority=7 if priority7 else 1,caller_sr_changed=False,
                  check_exec_irq_work_budgets_changed=False,console_performance_verified=False),
              "timed_reader": {"enabled":bool(timed),"window_ticks":586 if timed else 0,
                  "window_max_visits":64 if timed else 0,"blocks_per_window_max":2 if timed else 0,
                  "read_only_clock_admission":bool(timed),"invalid_clock_single_visit":bool(timed),
                  "checks_deadline_between_visits":bool(timed),"hard_elapsed_deadline":False},
              "failure_only_gateways": runtime_fault,
              "private_stack_guard": stack_guard,
              "compiled_owner_reader": compiled_reader,
              "optional_fault_capture": fault_capture,
              "sdk_cold_cache_coherence":coherence,
              "evolution_consolidated_trace": trace_audit,
              "stage_cache_publication": publication,
              "worker_icache_publication": publisher,
              "resolution_cache_seed": seed,
              "runtime_mode": runtime_mode,
              "resolution_cache_gateway": cache_gateway,
              "temporary_installer_stack": installer_stack,
              "relocation_records": relocations, "worker_instructions": instructions,
              "stage_gateway_model_cases": stage_cases, "persistent_gateway_model_cases": hook_cases,
              "original_release_mask_model_cases": hook_cases // 4,
              "stage_embedded_worker_verified": True, "maple_interception_installed": True,
              "virtual_replies_enabled": True, "storage_writes_enabled": True, "scoped_sci_transactions": True,
              "source_provenance": source, "stack_audit": stacks, "pio_writer_audit": pio,
              "console_execution_verified": False, "expected_phase": "native persistent session"}
    if self_test:
        report["gateway_mutation_rejections"] = (model_cached_mutations if cached else model_mutations)(binary, symbols)
        report["artifact_mutation_rejections"] = mutation_checks(directory)
    return report


def mutation_checks(directory):
    """Coherent changed flat images still must fail their typed ELF binding."""
    checks = 0
    with tempfile.TemporaryDirectory() as temp:
        target = Path(temp)
        for path in directory.iterdir():
            if path.is_file() and path.suffix in (".elf", ".bin", ".reloc", ".kui"):
                (target / path.name).symlink_to(path.resolve())
        for name, at in (("native-worker.bin", 0), ("native-worker.bin", 12),
                         ("native-worker.reloc", 72), ("stage.bin", 128)):
            path = target / name
            original = (directory / name).read_bytes()
            changed = bytearray(original); changed[at] ^= 1
            path.unlink(); path.write_bytes(changed)
            try:
                check(target)
            except (ValueError, AssertionError, KeyError, struct.error):
                checks += 1
            else:
                raise ValueError("modified artifact accepted: " + name)
            path.unlink(); path.symlink_to((directory / name).resolve())
    return checks



def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--output", type=Path)
    p.add_argument("--trace-startup", type=Path)
    args = p.parse_args()
    try:
        result = json.dumps(check(args.directory, self_test=args.self_test, trace_startup=args.trace_startup), indent=2) + "\n"
    except (OSError, ValueError, KeyError, AssertionError, struct.error, subprocess.CalledProcessError) as error:
        p.exit(2, f"Native persistent artifact audit refused: {error}\n")
    if args.output: args.output.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main()
