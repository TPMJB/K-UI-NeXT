#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the isolated Armada CE excerpt's linked bytes and masked stack.

The ordinary loader audit retains its FPU prohibition. The additional low
controller gets one narrowly pinned exception: the authored owner bridge
whose complete instruction/literal bytes are compared below. Only its final
relocated C-attempt pointer varies, and that must target the linked attempt.
All other controller instructions pass the ordinary FPU-free audit.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from check_loader_layout import EH, SH, SYM, inspect_elf, region
from check_retail_loader_layout import (FORBIDDEN_PREFIXES, FORBIDDEN_SYMBOLS,
                                       check_bss, check_directory)
from check_retail_instructions import IMAGES, audit
from check_retail_calls import check_directory as check_direct_calls
from check_retail_stack import (CE_INDIRECT, EXCERPT_NATIVE_PATH, STREAM_NATIVE_PATH,
                                STREAM_BUS_CALLBACKS,
                                base, load, worst)

BASE, LIMIT, STAGE = 0x8c004000, 0x8c007c00, 0x8ce10000
STREAM_BASE, STREAM_LIMIT = 0x8cfa7000, 0x8cfbf000
STREAM_CONTRACT_MAGIC, STREAM_CONTRACT_BYTES = 0x31444341, 32
POOL_BEGIN, POOL_END = 0x8cf50000, 0x8cfd0000
PCM_BYTES, SEED_BYTES, SECTORS, START_LBA = 352800, 64, 150, 149992


def stream_layout(*, worms=False):
    """Exact independently reserved code/source bank for each kernel."""
    return ((0x8cec7000, 0x8cedf000, 0x8ce70000, 0x8cef0000) if worms else
            (STREAM_BASE, STREAM_LIMIT, POOL_BEGIN, POOL_END))


CALL = "_kui_armada_ce_excerpt_ce_call"
DEFERRED = "_kui_armada_ce_excerpt_deferred"
OWNER_DEFERRED = "_kui_armada_ce_excerpt_owner_deferred"
OWNER_CALLBACK = "_kui_armada_ce_excerpt_owner_callback"
DISPATCH = "_kui_armada_ce_excerpt_dispatch"
ATTEMPT = "_kui_armada_ce_excerpt_attempt"
ARRAY_READ = "_kui_armada_ce_excerpt_array_read"

# Audited SH little-endian native-call ABI: save r8/r9/PR/GBR; allocate 32
# bytes, keep the 16-byte Microsoft home area and marshal args4..7 above it;
# r4..r7 are args0..3. Only jsr @r8 invokes the validated native target.
CALL_BYTES = bytes.fromhex(
    "862f962f224f134f43685369e07f9450041f9550051f9650061f9750071f"
    "92649155925693570b480900207f174f264ff6690b00f668")

# Complete owner bridge, including its four exact context literals. SR BL/RB/
# interrupt masks are rejected before STS FPSCR. FD is permitted here only
# after entry through NK's native owner callback; its native lazy-FPU trap
# establishes the owner's hardware bank. Scalar PR/SZ is checked
# before the first FPU write. Both banks' 16 FRs, FPUL/FPSCR, all 15 integer
# registers, PR/GBR/MACH/MACL/SR are saved and restored. Each C call has a
# 16-byte home area. The final four bytes alone are an ELF relocation.
OWNER_DEFERRED_BYTES = bytes.fromhex(
    "062f162f262f362f462f562f662f762f862f962fa62fb62fc62fd62fe62f"
    "224f134f024f124f034ff2643ed149213ed220315c8b6a053dd11825598b"
    "624f524f3bd1176159216a410bff1bff2bff3bff4bff5bff6bff7bff"
    "8bff9bffabffbbffcbffdbffebfffbfffdfb0bff1bff2bff3bff4bff"
    "5bff6bff7bff8bff9bffabffbbffcbffdbffebfffbfffdfbf07f29d0"
    "0b400900107f21e10841fc31126124d22b216a41f9fff9fef9fdf9fc"
    "f9fbf9faf9f9f9f8f9f7f9f6f9f5f9f4f9f3f9f2f9f1f9f0fdfb"
    "f9fff9fef9fdf9fcf9fbf9faf9f9f9f8f9f7f9f6f9f5f9f4f9f3"
    "f9f2f9f1f9f0564f664f06a00900ffe5f07f10d00b400900107f"
    "074f164f064f174f264ff66ef66df66cf66bf66af669f668f667f666f"
    "665f664f663f662f6610b00f660f0000070000000400000180000002000"
    "00000000")

# Complete integer-only caller bridge. It saves all twenty integer/control
# words, passes the original caller SP alongside SR, and supplies the native
# Microsoft sixteen-byte home area. Its only call targets the linked owner
# dispatcher, before any FPU instruction.
DEFERRED_BYTES = bytes.fromhex(
    "062f162f262f362f462f562f662f762f862f962fa62fb62fc62fd62fe62f"
    "224f134f024f124f034ff264f3655075f07f0cd00b400900107f074f164f064f174f"
    "264ff66ef66df66cf66bf66af669f668f667f666f665f664f663f662f661"
    "0b00f660090000000000")

BRIDGE_TARGETS = {DEFERRED: DISPATCH, OWNER_DEFERRED: ATTEMPT}
NATIVE_CALLERS = frozenset({"call", "kui_armada_ce_excerpt_attempt",
                            "kui_armada_ce_excerpt_dispatch"})
STREAM_NATIVE_CALLERS = {("call", "armada_ce_stream_player.c"): 1,
                         ("refill", "armada_ce_stream_player.c"): 2,
                         ("kui_armada_ce_excerpt_dispatch", "armada_ce_stream_player.c"): 1,
                         ("native_call", "armada_ce_stream_native.c"): 1}

# A read-only cache-array MOV must execute through P2. This zero-stack leaf
# masks SR interrupts for the MOV and eight explicit following NOPs, then
# restores the caller's entire SR before returning. It gets no FPU waiver.
ARRAY_READ_BYTES = bytes.fromhex(
    "02011360f0cb0e404260" + "0900" * 8 + "0e410b000900")
# Complete reviewed compiler body of read_cache, including range admission,
# P2 OR, JSR/delay slot and every literal. Only the two linked function
# pointers are relocated; their values must be the exact admitted symbols.
CACHE_READ_BYTES = bytes.fromhex(
    "862f5360962f03c8224f5368fc7f028f63696826058b00e0047f264ff6690b00f668"
    "1f9110d25921203104891b910fd259212031078b0ed10ed01b200b4083640229e9af"
    "01e00cd10cd259212031e38f00e00bd0f3650b4083640820db898261efaf12291fc0"
    "03c0000000f4000000f50000002000000000000000e0000000a000000000")
CACHE_READ_RELOCS = ((0x74, ARRAY_READ), (0x80, "_kui_armada_ce_ram_p1"))
# Complete reviewed native-owner value reader and mapping-only walk. All
# local call/data literals are normalized only after exact source-qualified
# target and addend admission. This path samples the admitted owner VA,
# retains metadata/PFN witnesses, masks IRQs for the full bounded read,
# rechecks TTB/SR and restores the entire saved SR. It gets no FPU waiver.
STREAM_OWNER_BODIES = {
    '_kui_armada_ce_ram_p1': bytes.fromhex(
        '436003c80f8f00e008d309d149231033098b08d108d709d249211c372637028d3b21122501e00b0009000900000000c00000'
        '0080ffffff1f0000fff3fcfffe00'
    ),
    '_kui_armada_ce_ram_table_p1': bytes.fromhex(
        '04d105d249212031028b04d02b4009000b0000e0000000e00000008000000000'
    ),
    '_mask_begin': bytes.fromhex(
        '020103901b200e400b001360f000'
    ),
    '_pointer': bytes.fromhex(
        '16d2436016d14c321632228d00e103c8ffe11a61582529022b2118211b8b11d248325232158f43670fd22824048d29270ed2'
        '215270320c8b0dd30ed2092338325232068f7827028b08d111511b20022601e10b001360fcaf00e109000000fffffffffe7f'
        '00000080000000fe00000000ffffff0100000002'
    ),
    '_read_ram': bytes.fromhex(
        '224f6826f87f058f536400e0087f264f0b00090005d0f365622f0b4004750820f38df266f1511261f0af122600000000'
    ),
    '_read_native': bytes.fromhex(
        '862f6826962f6368224f5369068ffc7f00e0047f264ff6690b00f66809d004e5f3660b4093640820f28df2619031ef8b05d2'
        '06d3226232632033e98b1261e8af12280900000000009058148c00000000'
    ),
    '_kui_armada_ce_ram_bind_with_reader.constprop.0': bytes.fromhex(
        '862f00e1962f5369a62f636ab62fc62fd62fe62f224f54dbcc7ff3651216111608750b4b122608200b8fe7e102e0347f264f'
        'f66ef66df66cf66bf66af6690b00f66893641d44f2510844f3651c340b4b0c750820ea8d00e444d8f35ef36610760b48e365'
        '0820028ff45de0af07e0f36514750b4bd3640820028ff365d7af03e06f91936429441924f551084418750b4b1c340820f28d'
        'f65100e4f3661c76122f0b4813650820e08df75c114c038fc36404e0bcaf0900f3650b4b20750820f78df4e193601d40f851'
        '0fc9f36508400c71036424750b4b1c340820e98df95100e4f3662876111f0b4813650820bd89fa5bb36001c89b8d05e03491'
        'f36533942c751bd01929b9241bd19b240b401b240820cf8d06e000e4f366e3650b4830760820a38dfc51103d028df26580af'
        '08e000e4f3660b4830760820978dfc51103cf48ff15500e4f3660b4830760820028ffc518baf0900103be88ffb5109e0b21a'
        '66af122aff01ff0f00f000000000000000000000000000000080'
    ),
    '_kui_armada_ce_stream_owner_word': bytes.fromhex(
        '862f40e1962fa62f636ab62f736bc62fd62fe62f224fcc7f9d92fc31421f1a5e1659175800e1121e111e122e4bd16c312631'
        '6b8d01ec4ad313620942353219e70a0209420142236301737d43b0335e8fc36054e0fe06fde7ff76763656897b9740d37702'
        '9923336c1a0228311b2cc82c068b3dd19921b031028b3cd77829018bada001ec3ad119253ad110350189a8a002ec39d500e4'
        'f3660b480c760820458da3620c72222f00e4f36623650b48107608203b8da3610871111f00e4f36613650b4814760820318d'
        'f351a031078bf451b031048b54e0f551fe077031018982a004ec26ddf366f2541c760b4d936509881e8d05880a8d08ec05e1'
        '1630118d0388048d06ec048807ec008905ecc360347f264ff66ef66df66cf66bf66af6690b00f6680688f28d09ec08881289'
        'eeaf03ec60e0f366fe00187600e40b4093650820538df254f36693650b4d28760988178df952dbaf0aec7f139c0009002019'
        'eb733548833403000080000000fe0000ff01f0000070f00000409058148c00000000fc511032e58ff752fa511032e18f00e4'
        '17d5f3660b480c760820c78df26500e4f3660b4810760820c08df15500e4f3660b4814760820b98df351a031c98ff451b031'
        'c68f54e0f551fe022031c18fe36408d7f3650b472875f65197af111e96afc36094afc36092afc3608faf0bec09009058148c'
        '00000000'
    ),
    '_mapped_word': bytes.fromhex(
        '862f01e1962f5368a62fb62fc62fd62f00ede62f224f21dbe07f21d0f3664b1b1c761a1bd215d115d2250b4004e508201cdc'
        '29891cd00b400900061f02091bda93651bd2a26e231f1ad2e364851f221ff752d41f211f18d218d02252222fc1570b40c266'
        '0202a261e031038f2a2914d11829048900e112180ae0111812280a1bf6510e4108202900207f264ff66ef66df66cf66bf66a'
        'f6690b00f66800000000000000000000000000000000080000ff00000000000000000000000000000000f0000070'
    ),
    "___movmemSI12_i4": bytes.fromhex('526051515252022411140b002214'),
}
STREAM_OWNER_RELOCS = {
    '_kui_armada_ce_ram_table_p1': ((28, '_kui_armada_ce_ram_p1', 0),),
    '_pointer': ((108, '_player', 0),),
    '_read_ram': ((44, '_kui_armada_ce_ram_table_p1', 0),),
    '_read_native': ((68, '_pointer', 0), (76, '_player', 0)),
    '_kui_armada_ce_ram_bind_with_reader.constprop.0': ((360, '_kui_armada_ce_ram_table_p1', 0), (364, '_read_ram', 0), (368, '_kui_armada_ce_ram_p1', 0)),
    '_kui_armada_ce_stream_owner_word': ((380, '_kui_armada_ce_ram_bind_with_reader.constprop.0', 0), (500, '___movmemSI12_i4', 0)),
    '_mapped_word': ((156, '_player', 4028), (160, '_pointer', 0), (164, '_player', 0), (168, '_mask_begin', 0), (176, '_read_native', 0), (180, '_read_ram', 0), (184, '_player', 188), (188, '_kui_armada_ce_stream_owner_word', 0)),
}


def stream_owner_bodies(*, bam4=False, worms=False, wwp=False):
    """Select reviewed kernel globals without relaxing any linked-body pin.

    The title kernels retain the admitted owner structures and page
    walk, with their independently verified global addresses. GCC emits
    PROCESS_BASE negated for the range/index calculation. Only these four
    literal words change; instructions, lengths and targets remain exact.
    """
    if sum((bam4, worms, wwp)) > 1:
        raise ValueError('Select only one WinCE title profile')
    bodies = dict(STREAM_OWNER_BODIES)
    if bam4 or worms or wwp:
        # WWP's NK globals and process/page contracts were reviewed against
        # its complete supplied kernel; matching BAM4 values are intentional.
        changes = {
            '_read_native': ((72, 0x8c145890, 0x8c1d1890),),
            '_kui_armada_ce_stream_owner_word': (
                (348, 0x73eb1920, 0x73e25920),
                (376, 0x8c145890, 0x8c1d1890),
                (496, 0x8c145890, 0x8c1d1890)),
        }
        if worms:
            changes = {
                '_read_native': ((72, 0x8c145890, 0x8c121890),),
                '_kui_armada_ce_stream_owner_word': (
                    (348, 0x73eb1920, 0x73ed8940),
                    (376, 0x8c145890, 0x8c121890),
                    (496, 0x8c145890, 0x8c121890)),
            }
        for name, words in changes.items():
            body = bytearray(bodies[name])
            for offset, original, replacement in words:
                if body[offset:offset + 4] != struct.pack('<I', original):
                    raise ValueError('Title owner pin no longer matches reviewed Armada literal')
                body[offset:offset + 4] = struct.pack('<I', replacement)
            bodies[name] = bytes(body)
    return bodies

# Authored P0 thread entry: twelve saved bytes, sixteen-byte native home,
# SetKMode then an actual SR.MD test before the high P1 worker call. Both
# branches return through P0, restoring the old native mode.
VENEER_INSTRUCTIONS = bytes.fromhex(
    "862f962f224ff07f436801e40cd00b4009000369020000400040068b836409d00b40"
    "0900036801a00900ffe8936404d00b4009008360107f264ff6690b00f668")
VENEER_WORD_BYTES = bytes.fromhex(
    "436010880e8d11880c8d636506d14d6508451c35518503654d6008401d0128451d611b25"
    "0b00536000000000")
NATIVE_PUBLISH_BYTES = bytes.fromhex(
    "862f5361962f6b21224f1fe913601fc8f87f1929268f536426916368ff781638208df"
    "36511d0622f0b4004750820198df2660fd1f15268312231138ffbe283611f762d4120"
    "e222360171008901e1f1529c32a3021041fa8f2079087f264ff6690b00f668f9af"
    "00e0ff0f0900000000000000008d")
NATIVE_RAM_P1_BYTES = bytes.fromhex(
    "436003c80f8f00e008d309d149231033098b08d108d709d249211c372637028d3b21"
    "122501e00b0009000900000000c000000080ffffff1f0000fff3fcfffe00")
NATIVE_RAM_TABLE_BYTES = bytes.fromhex(
    "04d105d249212031028b04d02b4009000b0000e0000000e00000008000000000")
STREAM_LOW_CODE_BYTES = bytes.fromhex(
    "436001c8058f00e003d11c340391423129000b000900ff54007dff73")
STREAM_BUS_VALID_BYTES = bytes.fromhex(
    "862f4824962f4361a62fb62fc62fd62fe62f224f038dec7f436003c8018977a000e0"
    "41d37e924c3326337089426001886d8b41503c886a8b425008206789435448246489"
    "145228226189155ee82e5e89165dd82d5b89175cc82c5889185bb82b5589195aa82a"
    "52891a5998294f891b5338234c8d311f1c533823488d321f1d533823448d331f1e51"
    "1821408d141f03c83d8b25d11c3046911630388923d80b48222f082033890b48f264"
    "08202f890b48e36408202b890b48d364082027890b48c364082023890b48b36408201f"
    "890b48a36408201b890b489364082017890b48f154082013890b48f25408200f890b48"
    "f35408200b8df4548362147f264ff66ef66df66cf66bf66af6692b42f66800e0147f"
    "264ff66ef66df66cf66bf66af6690b00f668c454a045007dff7300000000")
STREAM_BUS_RELAY_BYTES = bytes.fromhex("02d0536700e643652b40fee400000000")

# Whole linked bootstrap handoff bodies, reviewed for the stage's original
# immutable-code/BSS/source checks followed by the sole activation call.
# Relocations are admitted against linked symbols before normalization.
STREAM_BOOT_BODIES = {
    ('controller', '_kui_armada_ce_bind_probe'): bytes.fromhex(
        '862f4360962ffe88a62f736ab62f536b224f0a8fd47f67d07365b3642c7f264ff66bf66af6692b40f66863d0421f0b40'
        '611f0820f254698df15660d800e14360ff8883694079698f1e195dd15dda0b41a364a5511518a6511618a25118210589'
        'a7522718a8522818a95229181b18f364a351a2621c18a4512a181d18af511e18aa511f18ab511229ac511119ae511219'
        '4dd110522319175224191852251919522619145215512719181948d10b410c74f351f3641218f4511318f551141844d1'
        '0b411874f651182108891919f7511a19f8511b19f9511c19fa511d193dd13ed40b4109003dd20be13dd3222310410473'
        'fb8f28723bd13cd40b4109003bd208e13bd3222310410473fb8f28722bd02c7f264ff66bf66af6690b00f66801e24361'
        'ff712631098da82a078b32d00b4009000820eb8930d1e9af1e1948242900e58f01ca6330e28903e2b361ec712631028f'
        'b3602188da8b8260ff88018d01700228a36003c8068f01e124d22191ac321632028f01e1caaf1118a350018804898450'
        'ff88018d01700418a25100e41cd0b365122fa1570b40a2660820b7898350ff88018d01700318a3500188af8bc2af0900'
        'f05409003ccafa8c2ccafa8c2c0efb8c2ce4fa8c8410fb8cc410fb8c14e3fa8c3ce3fa8c20f4fa8ca80efb8c8807fb8c'
        'ac0ffb8cb0f2fa8cd80ffb8c4806fb8c6410fb8cece2fa8c84a5fa8c007dff737ce3fa8c'
    ),
    ('controller', '_kui_armada_ce_stream_transport_attached'): bytes.fromhex(
        '02d11b5001880b0029000900eceefa8c'
    ),
    ('controller', '_kui_armada_ce_stream_transport_event'): bytes.fromhex(
        '862f4824962f4360a62f536ab62fc62fd62f224f1f8ff87f60d00b40536408204e8d3ce65ed85fd48b522822068da3655dd0'
        '0b400900082019a029005bd00b40090001e202512b18a2922c311c5258d124110ca001e053d993687c788b5118210e8f0188'
        '29010688290213602b20087f264ff66df66cf66bf66af6690b00f6684cd10b41422ff2600388338b4ad28091ac321632148d'
        '00e1121a131a8c51182104898d52a26110320c8d00e243d11f522822058d40711052a2611032028d01e2d5af00e0669301e7'
        '659037021a019c311e01721a1821078f111a3e225c971a017c319c31131a3702579000e21a019c31afaf260101886b8b8a5c'
        'c36002883c8f01e12fd0a82affe40b404a64011f97500b4009000820d38b25ddd75bb82b258b81500188228f936a9c50407a'
        'f365ac540b4004750820238d036522d09364b5184c740b40b81d0820018d03e111188451a95201711418875101711718a751'
        '1032048b81500188018f02e1111816d10b41090089510171191801e11a1869af0900f154c634f28b11d10b410900eeaf0900'
        '800cf05c0c02b400b800ac0018aefa8c4cf1fa8cd0f0fa8c5694fa8c0e94fa8c8cf5fa8cc4acfa8c007dff734cf3fa8c24ad'
        'fa8c60dcfa8c78adfa8cf4acfa8c02881b8f0fe386512ad20171161825513631018d01711512925147977c31145118212f89'
        '815001882c8f0fe125521632288b21d00b40090036a08a5006880a8b81500188028d01e029af090019d10fe2155109af1232'
        '0488128b8a500288118b9e510b4101e414d00b40090097500b40090008209a8b95510b41090001af09000588018934af0900'
        '815001880b8b0bd00b4009000820cc89925108900c31135206911632c5898a50deae0288bc0cfc0c000609008cf5fa8c24b1'
        'fa8c84acfa8c'
    ),
    ('resident', '_kui_retail_resident_init'): bytes.fromhex(
        '862f4824962f2901a62f5825b62f2902c62f2b21224f1821f07f436a038f53687827028f6360fba001e001c82f8f01e01cd1'
        '1cd26921203104891bd11cd269212031248b1bd11bd269212b211bd22c31259226311b8d01e038e618d919d40b497365a652'
        '3fe12363ff7316330f8d02e0a75338230b892b614071163307890f9611d40b49a36510daa038208b03e0107f264ff66cf66b'
        'f66af6690b00f668ff7efc03000000ff000000ac0000007f0000000cffffff000000008c00ffff732ca1008cf8d2008c00d4'
        '008cb0d2008c82600188db8b8360407004840820d68b48e600e580bfa36483612071826b19840820cc8db22a1a840820c88b'
        '895aa82a038f885cc82cc38d03e02ce648d483650b49047547d546d40b491ce646d88151a631b48d8262a231028fc632b08d'
        '03e08b51b031ac8f03e040d18c50801114e10288008b19e13eda3ed510e6f3640b49161a10e6f365a3640b4974743ad101e3'
        '3ad26097211186527511221138d25b97231138d28d5024117711136236d14072331201c8351202e3301110e3008b4c933e12'
        '885331d9341231d232da211132d132d212291222111231d81212a26231d131d432d52221399c0b48483593640b4810e52fd4'
        '0b4804e52ed204e52ed12fd422210b4809002ed20ce52ed12ed422210b4809002dd122902dd20ce5c121118109e012811381'
        '22112ad40b4809002ad115902ad204e5c121a364118109e01281138122110b48090025d226d1222137af00e035af09000020'
        '0008800001d02b40b4d2008ce0c3008c00d4008c1cd1008c5cd1008c60c0008cdcd1008cfcd4008c0080008c0000008d5cd2'
        '008c00d8008ce0b3008ce000008c4749554b04d8008c3883008c1cc4008c00c4008ce0d7008cbc00008c7cbb008cbc0000ac'
        'c000008c82bb008cc00000ac0010008c001000ac88bb008cf010008cf01000ac8ebb008ca0bc008ce00000ac43605b20862f'
        '03c8'
    ),
    ('stage', '_kui_retail_stage_relay'): bytes.fromhex(
        '862f962fa62f436ab62f03ebc62f536cd62fa92be62f62d1224f62d40b41f87f61d862d40b480900a36003c8088b60d1'
        '60d261d3a9212b21b1921c332633048b5ed4a3655ed10b4109005ed2a35520355dd20b8b5dd3231aa4523b22241aa452'
        '5bd33822078b58d502a009002035f68958d4e7af090058d9136558d40b49547557d40b49a45557d40b49c36556d057d5'
        '57d40b4040e657d000e457d154d50b40126656d112610031038d036c54d4c9af036554dd10e654d154d555d40b4d126a'
        '08203b8fa36253de00e5e2670fe601721042378f536150d150d551d01835122f511f0b4053640820f261398df1555363'
        '00e201731043368b4ad24bd126355831008b01e110413d8f03e348d700e554e25361e47136313f8d5360084144d61c31'
        '1b611d466c6143d65c3660666c661036018b8fa0104240d484af090024e53fd480af0900dc716631088f53603cd17c03'
        '5c3110613c631c613031f08bb8af017538d46faf090038d710662c3770676c667c677036048d0171ff7134d462af2365'
        'b8af017230d25c3220622822028930d458af0900b6af01757c01c4af1c61ac1f6821e18cb87de38cd01fe18cd863e18c'
        'ffffff1f000000800020ff73f863e18c7808e18c0002e18c00f4008c00000010000000401064e18cf021e18c0464e18c'
        '2c64e18c3464e18c8a4be18c5c8de38c0000018c5450e18ca4eae28cb0eae28c4064e18cd24be18c9ceae28c88e2e28c'
        '2483008ca0eae28cec33e28cc8d1e28c800ae18c008001000180010034e2e28c0000f58c201414ace864e18c6064e18c'
        '0083008c7c64e18c0070faaca064e18cc464e18c028d01755baf536134d535d40b4d40e60820028933d4ebae00e55e96'
        '32d533d40b4d09000820f58b31d132d2bc32084222622822038db3652fd4d9ae09001041f38f017b37e1163a038da365'
        '2bd4cfae09002bdde2640b4d347403c80d8f036b28d11630098b28d77c3a023a058b27d40b4d0900b030038d036a25d4'
        'b8aeb365036724d000e600e50b40fee40188038da36521d4acae090020d40b48090020d40b49c3651fd40b4809001fd4'
        '0b4809001ed40b4809001ed40b4809001dd40b4809001dd178e4087f264ff66ef66df66cf66bf66af6692b41f6680010'
        'ccd1e28c0000f5ac1065e18c0cd2e28c00f0fbac8858010010403deb3465e18c5865e18c4008e18cff82008cc482008c'
        '3483008c7465e18c0070fa8c9065e18cac65e18ce45ee18cd465e18ca480e38ce865e18cc85ce18c1066e18c3023e18c'
    ),
}
STREAM_BOOT_RELOCS = {
    # Reviewed terminal-only history copy/text publication follows the unchanged
    # attachment guard. Pre-attachment paths still write no controller state.
    ("controller", "_kui_armada_ce_bind_probe"): (
        (0x1b4, "_kui_armada_ce_stream_transport_event", 0),
        (0x1b8, "_kui_armada_ce_stream_transport_attached", 0),
        (0x1bc, "_snapshot", 0),
        (0x1c0, "_kui_armada_ce_excerpt_snapshot", 0),
        (0x1c4, "_audio", 0),
        (0x1c8, "_audio", 64),
        (0x1cc, "_kui_armada_ce_stream_progress", 0),
        (0x1d0, "_kui_armada_ce_stream_fault", 0),
        (0x1d4, "_kui_armada_ce_stream_trace_snapshot", 0),
        (0x1d8, "_snapshot", 124),
        (0x1dc, "_labels.1", 0),
        (0x1e0, "_snapshot", 384),
        (0x1e4, "_kui_armada_ce_stream_underrun_snapshot", 0),
        (0x1e8, "_snapshot", 428),
        (0x1ec, "_underrun_labels.0", 0),
        (0x1f0, "_snapshot", 568),
        (0x1f4, "_kui_armada_ce_stream_needs_deferred", 0),
        (0x1f8, "_kui_armada_ce_excerpt_deferred", 0),
        (0x200, "_kui_armada_ce_excerpt_queue", 0),
    ),
    ("controller", "_kui_armada_ce_stream_transport_attached"): ((0xc, "_t", 124),),
    ("controller", "_kui_armada_ce_stream_transport_event"): (
        (0x19c, "_bus_valid", 0), (0x1a0, "_t", 124), (0x1a4, "_t", 0),
        (0x1a8, "_memcmp", 0), (0x1ac, "_memcpy", 0),
        (0x1b0, "_t", 1212), (0x1bc, "_t", 636),
        (0x1b4, "_refresh_holds", 0), (0x1c0, "_finish", 0),
        (0x1c4, "_kui_retail_cursor_feed", 0), (0x1c8, "_release_audio", 0),
        (0x1cc, "_audio_failure", 0), (0x284, "_t", 1212),
        (0x288, "_kui_armada_ce_stream_transport_pump", 0), (0x28c, "_data_active", 0)),
    # Terminal reporting relayout changes address literals only. Every
    # initializer instruction, bound and activation ordering is unchanged.
    ("resident", "_kui_retail_resident_init"): (
        (0xbe, "_memcpy", 0),
        (0x232, "_bus", 0),
        (0x242, "_kui_retail_ce_stream_bus", -16),
        (0x25e, "_kui_retail_async_read_part", 0),
        (0x282, "_kui_retail_resident_hook", 0),
        (0x28e, "_kui_retail_gd_c0_hook", 0),
        (0x29e, "_kui_retail_gd_1000_hook", 0),
        (0x2aa, "_kui_retail_gd_10f0_hook", 0),
        (0x2ae, "_kui_retail_menu_hook", 0),
        (0xc2, "_kui_retail_async_region", 3672),
        (0xc6, "_manifest", 0),
        (0xca, "_kui_retail_async_region", 3600),
        (0x22e, "_kui_retail_async_region", 3604),
        (0x236, "_manifest", 0),
        (0x23a, "_kui_retail_async_region", 3196),
        (0x23e, "_kui_retail_async_region", 3260),
        (0x246, "_kui_retail_async_region", 3388),
        (0x24a, "_manifest", 252),
        (0x256, "_kui_retail_async_region", 3516),
        (0x272, "_kui_retail_original_menu", 0),
        (0x276, "_kui_retail_hook_caller", 0),
        (0x27a, "__retail_resident_bss_end", 0),
    ),
    # The stage embeds the complete controller, so its existing subsequent
    # state/seed literals move with the controller's verified byte length.
    ("stage", "_kui_retail_stage_relay"): (
        (0x1a4, "_display", 0), (0x1ec, "_original_entry", 0),
        (0x1f8, "_exec_bytes", 0), (0x1fc, "_boot_crc", 0),
        (0x208, "_resident_bytes", 0), (0x20c, "_ce_kernel", 0),
        (0x214, "_resident_blob", 0), (0x218, "__armada_bind_blob_start", 0),
        (0x21c, "__armada_bind_blob_end", 0),
        (0x22c, "_armada_original_romhdr", 0), (0x330, "_armada_excerpt_seed", 0),
        (0x33c, "_armada_excerpt_seed", 64), (0x37c, "_manifest", 68)),
}
for _key, _targets in STREAM_BOOT_RELOCS.items():
    _body = bytearray(STREAM_BOOT_BODIES[_key])
    for _offset, _, _ in _targets:
        _body[_offset:_offset + 4] = bytes(4)
    STREAM_BOOT_BODIES[_key] = bytes(_body)

# Adding WWP's scalar leaf moves the original first-text purge helper by
# twelve bytes. Its complete body is unchanged from the booting b579 build;
# admit only that exact named helper, never an arbitrary low-image address.
WWP_INIT_PURGE_BYTES = bytes.fromhex('e0e14c3519245234018b0b000900a304f9af2074')


def stream_boot_relocs(*, wwp=False):
    relocations = dict(STREAM_BOOT_RELOCS)
    if wwp:
        key = ('resident', '_kui_retail_resident_init')
        relocations[key] += ((0x26e, '_purge.lto_priv.0', 0),)
    return relocations


STREAM_BOOT_REQUIRED = frozenset({
    ("controller", "_kui_armada_ce_bind_probe"),
    ("controller", "_kui_armada_ce_stream_transport_attached"),
    ("controller", "_kui_armada_ce_stream_transport_event"),
    ("resident", "_kui_retail_resident_init"),
    ("stage", "_kui_retail_stage_relay"),
})

# The terminal report keeps the old 428-byte ABI prefix, then appends a 140-byte
# frozen underrun and eight 32-bit label pointers. These labels are emitted only
# after the worker is stopped; no guest or device memory is read for the page.
STREAM_REPORT_LABELS = (
    'GD BIND SOURCE GAPS WORKER', 'AUDIO WHY NODE VALUE EXPECT DETAIL',
    'SR FPSCR CURRENT OWNER DEVICE', 'STREAM ATTEMPT PLAY REFUSED HR',
    'BUFFER CAPS BYTES VOICES CRC', 'CMD FIRST LAST PLAYCUR WRITECUR',
    'EVENTS SAMPLES VISIT-MS JOB-MS JOBS', 'LAST SIX AUDIO EVENTS',
    'LAST SIX ONE-SECOND SAMPLES', 'CODE MS SOURCE READY AUX',
    '5S PER PAGE - POWER OFF TO EXIT')
STREAM_UNDERRUN_LABELS = (
    'LAST UNDERRUN - FROZEN CONTEXT', 'MS READY DELTA STAGED READ-MS',
    'ENTRY-GAP SLEEP OBS-MS SRCAGE RINGAGE',
    'STATUS OWNER READ-BYTES TOTAL RETRIES',
    'DATA-D AUDIO-D PRE-D YIELD-D COPY-D',
    'MS SOURCE READY STAGED FLAGS', 'MS SOURCE READY STAGED FLAGS',
    'MS SOURCE READY STAGED FLAGS')

# The title admission code changes the stage's link positions, while the
# 912-byte relay's instructions remain identical. Pin every moved literal
# to its original named function or its complete original terminal message;
# never admit an arbitrary in-image address. Armada retains its historical
# complete-body pins after each linked target has passed the same exact
# symbol/message proof. These offsets were reviewed against all four relays.
BAM4_STAGE_LINKED_TARGETS = (
    (0x1a0, 0x8ce12168, '_retail_display_restore', None),
    (0x1a8, 0x8ce11fd0, '_retail_display_line', None),
    (0x1ac, 0x8ce163d8, None, 'BOOTSTRAP 2 REACHED GAME ENTRY'),
    (0x1bc, 0x8ce163f8, None, 'UNSUPPORTED BOOT STACK'),
    (0x1c0, 0x8ce10878, '_stopped', None),
    (0x1d4, 0x8ce16410, None, 'UNSUPPORTED BOOT CPU STATE'),
    (0x1d8, 0x8ce121f0, '_retail_display_hex', None),
    (0x1dc, 0x8ce16404, None, 'BOOT STACK'),
    (0x1e0, 0x8ce1642c, None, 'BOOT SR'),
    (0x1e4, 0x8ce16434, None, 'BOOT CACHE'),
    (0x1e8, 0x8ce14b8a, '_memcpy', None),
    (0x1f4, 0x8ce15054, '_kui_retail_crc32', None),
    (0x200, 0x8ce16440, None, 'BOOTSTRAP ALTERED EXECUTABLE'),
    (0x204, 0x8ce14bd2, '_memcmp', None),
    (0x220, 0x8ce10a80, '_armada_stream_controller_valid', None),
    (0x238, 0x8ce164e8, None, 'BOOTSTRAP ALTERED CE RAM RESERVATION'),
    (0x23c, 0x8ce16460, None, 'BOOTSTRAP ALTERED RESIDENT'),
    (0x244, 0x8ce1647c, None, 'BOOTSTRAP ALTERED STREAM CONTRACT'),
    (0x24c, 0x8ce164a0, None, 'BOOTSTRAP ALTERED ARMADA BIND CODE'),
    (0x250, 0x8ce164c4, None, 'BOOTSTRAP ALTERED ARMADA BIND RAM'),
    (0x338, 0x8ce16510, None, 'BOOTSTRAP ALTERED STREAM SOURCE MAP'),
    (0x34c, 0x8ce16534, None, 'BOOTSTRAP ALTERED AUDIO SOURCE BANK'),
    (0x350, 0x8ce16558, None, 'STREAM BUS ENTRY MISSING'),
    (0x364, 0x8ce16574, None, 'STREAM BUS ENTRY INVALID'),
    (0x36c, 0x8ce16590, None, 'STREAM BUS ATTACH FAILED'),
    (0x370, 0x8ce165ac, None, 'READER INTACT - ORIGINAL ENTRY RESTORED'),
    (0x374, 0x8ce15ee4, None, 'BODY CRC32'),
    (0x378, 0x8ce165d4, None, 'ENTERING WINDOWS CE'),
    (0x380, 0x8ce165e8, None, 'IF IT STOPS PHOTOGRAPH THE LAST SCREEN'),
    (0x384, 0x8ce15cc8, None, 'POWER OFF AND ON TO RETURN'),
    (0x388, 0x8ce16610, None, 'A RESET NOW MEANS WINDOWS CE ITSELF FAILED'),
    (0x38c, 0x8ce12330, '_retail_display_pause', None),
)


def stream_boot_bodies(*, bam4=False, worms=False, wwp=False):
    """Keep the complete startup proof, selecting the admitted ROM header."""
    if sum((bam4, worms, wwp)) > 1:
        raise ValueError('Select only one WinCE title profile')
    bodies = dict(STREAM_BOOT_BODIES)
    if wwp:
        key = ('resident', '_kui_retail_resident_init')
        body = bytearray(bodies[key])
        if body[0x26e:0x272] != struct.pack('<I', 0x8c008338):
            raise ValueError('WWP initializer purge pin differs from the booting reference')
        body[0x26e:0x272] = bytes(4)
        bodies[key] = bytes(body)
        bodies[('resident', '_purge.lto_priv.0')] = WWP_INIT_PURGE_BYTES
    if bam4 or worms or wwp:
        key = ('stage', '_kui_retail_stage_relay')
        body = bytearray(bodies[key])
        if body[0x234:0x238] != struct.pack('<I', 0xac141420):
            raise ValueError('Title startup pin no longer matches reviewed Armada ROMHDR literal')
        body[0x234:0x238] = struct.pack('<I', 0xac11de70 if worms else
                                     0xac1cd614 if wwp else 0xac1ce19c)
        if worms:
            for offset, original, replacement in (
                    (0x230, 0x8cf50000, 0x8ce70000),
                    (0x248, 0xacfa7000, 0xacec7000),
                    (0x334, 0xacf50000, 0xace70000),
                    (0x340, 0xacfbf000, 0xacedf000),
                    # Source zero-check forms (base/4 + i) << 2. The
                    # compiler retains the signed high bits of base/4.
                    (0x348, 0xeb3d4010, 0xeb39c010),
                    (0x368, 0x8cfa7000, 0x8cec7000)):
                if body[offset:offset + 4] != struct.pack('<I', original):
                    raise ValueError('Worms startup pin differs from reviewed bank literal')
                body[offset:offset + 4] = struct.pack('<I', replacement)
        for offset, original, _, _ in BAM4_STAGE_LINKED_TARGETS:
            if body[offset:offset + 4] != struct.pack('<I', original):
                raise ValueError('Title startup pin no longer matches reviewed linked literal')
            body[offset:offset + 4] = bytes(4)
        bodies[key] = bytes(body)
    return bodies


def audit_bam4_stage_literals(code, image, *, original_literals=False):
    """Prove exact linked targets before normalizing address-only movement.

    The title port bodies use zero placeholders. Armada's original whole-body
    fixture retains the historical addresses, so normalize back to those only
    after the same linked symbol or complete terminal-message proof succeeds.
    No instruction, pool constant or arbitrary in-image target is exempted.
    """
    for offset, original, symbol, message in BAM4_STAGE_LINKED_TARGETS:
        target = struct.unpack_from('<I', code, offset)[0]
        if not STAGE <= target < STAGE + len(image['payload']):
            raise ValueError('Title startup literal leaves linked stage')
        if symbol:
            if target != image['symbols'].get(symbol):
                raise ValueError('Title startup literal targets unexpected function: ' + symbol)
        else:
            expected = message.encode('ascii') + b'\0'
            if region(image['payload'], target - STAGE, len(expected), message) != expected:
                raise ValueError('Title startup literal targets unexpected terminal message')
        code[offset:offset + 4] = struct.pack('<I', original) if original_literals else bytes(4)

# Pinned compiler's direct-RAM destination admission. Its physical alias
# range test rejects pool overlap (physical cf50000..cfd0000) before any
# cache purge/copy. The body includes all branch instructions and literals;
# checking two constants in otherwise unaudited code would be insufficient.
MAP_BYTES = 220
MAP_SHA256 = "d0c40ff2abc2a371a56ab0e801f3b41ba83c6ef3e475b710e11adf2d651f5349"
SEED_HEADER = struct.pack("<16I", 0x31584543, 1, PCM_BYTES, 0,
                          SECTORS, START_LBA, *([0] * 10))
STREAM_SEED_HEADER = struct.pack("<16I", 0x32584543, 2, 0x8cf50040,
                                PCM_BYTES, 0x8cfbf000, 4096, 0, 352768, *([0] * 8))


def function_sizes(data, *, kinds=(2,)):
    """Read function sizes after inspect_elf has admitted the ELF structure."""
    header = EH.unpack_from(data)
    so, ss, sn = header[6], header[11], header[12]
    sections = [SH.unpack_from(data, so + i * ss) for i in range(sn)]
    result = {}
    for section in sections:
        _, kind, _, _, offset, size, link, _, _, stride = section
        if kind != 2:
            continue
        strings = region(data, sections[link][4], sections[link][5], "symbol strings")
        for at in range(offset, offset + size, stride):
            name, value, count, info, _, _ = SYM.unpack_from(data, at)
            if info & 15 not in kinds:
                continue
            end = strings.find(b"\0", name)
            label = strings[name:end].decode("ascii")
            if label in result and result[label] != (value, count):
                # Several independently compiled stage objects legitimately
                # use the same static helper name. Keep an ambiguity marker:
                # a required bridge/guard can never use it as a valid span.
                result[label] = None
            elif label not in result:
                result[label] = value, count
    return result


def local_spans(data):
    """Keep ELF local names attached to their STT_FILE translation unit.

    Several controller units have independent static RAM/cache helpers.
    A flat symbol dictionary must never select one unit's helper for a
    different unit's exact relocated call-target proof.
    """
    header = EH.unpack_from(data)
    sections = [SH.unpack_from(data, header[6] + i * header[11]) for i in range(header[12])]
    result = {}
    for section in sections:
        _, kind, _, _, offset, size, link, _, _, stride = section
        if kind != 2:
            continue
        strings = region(data, sections[link][4], sections[link][5], "symbol strings")
        owner = None
        for at in range(offset, offset + size, stride):
            name, address, count, info, _, _ = SYM.unpack_from(data, at)
            end = strings.find(b"\0", name)
            label = strings[name:end].decode("ascii")
            if info & 15 == 4:
                owner = label
            elif owner and info >> 4 == 0 and info & 15 in (1, 2):
                key = (owner, label)
                if key in result:
                    raise ValueError("Duplicate source-qualified local symbol: " + repr(key))
                result[key] = address, count
    return result


WWP_DRIVE_SYMBOL = '_kui_wwp_cdda_drive_status'
WWP_DRIVE_READER = '_wwp_ce_stream_drive_status'
# Complete zero-stack leaf: load one named aligned scalar, then return.
# The final literal alone relocates to the checked controller BSS object.
WWP_DRIVE_READER_BYTES = bytes.fromhex('01d112600b00090000000000')


def wwp_drive_status_symbol(data):
    """Resolve one public scalar object from the linked zero-initialized bank."""
    image = inspect_elf(data, STREAM_BASE, STREAM_LIMIT,
                        entry_symbol='_kui_armada_ce_bind_probe')
    check_bss(image, STREAM_BASE, '__armada_bind')
    audit_stream_contract(image)
    header = EH.unpack_from(data)
    sections = [SH.unpack_from(data, header[6] + i * header[11]) for i in range(header[12])]
    definitions = []
    for section in sections:
        _, kind, _, _, offset, size, link, _, _, stride = section
        if kind != 2:
            continue
        strings = region(data, sections[link][4], sections[link][5], 'symbol strings')
        for at in range(offset, offset + size, stride):
            name, address, count, info, other, index = SYM.unpack_from(data, at)
            end = strings.find(b'\0', name)
            if strings[name:end].decode('ascii') == WWP_DRIVE_SYMBOL:
                definitions.append((address, count, info, other, index))
    if len(definitions) != 1:
        raise ValueError('Missing unique public WWP drive-status object')
    address, count, info, other, index = definitions[0]
    begin = image['symbols']['__armada_bind_bss_begin']
    end = image['symbols']['__armada_bind_bss_end']
    if (info != 0x11 or other or count != 4 or address % 4 or
            index >= len(sections) or sections[index][1] != 8 or sections[index][2] & 7 != 3 or
            not begin <= address <= end - 4 or
            not sections[index][3] <= address <= sections[index][3] + sections[index][5] - 4):
        raise ValueError('WWP drive status must be one aligned public four-byte BSS object')
    return address


def audit_wwp_drive_projection(controller_elf, resident, sizes, calls):
    """Pin one atomic scalar load and preserve the booting bridge call graph."""
    address = wwp_drive_status_symbol(controller_elf)
    if resident['symbols'].get(WWP_DRIVE_SYMBOL) != address:
        raise ValueError('Low WWP status symbol differs from the linked controller object')
    start, count = sizes.get(WWP_DRIVE_READER) or (0, 0)
    if (start % 4 or count != len(WWP_DRIVE_READER_BYTES) or
            not 0x8c008300 <= start <= 0x8c008300 + len(resident['payload']) - count):
        raise ValueError('Missing exact WWP scalar-only low reader')
    code = bytearray(region(resident['payload'], start - 0x8c008300, count, WWP_DRIVE_READER))
    if code[8:12] != struct.pack('<I', address):
        raise ValueError('WWP low reader does not target the exact controller scalar')
    code[8:12] = bytes(4)
    if code != WWP_DRIVE_READER_BYTES:
        raise ValueError('WWP low scalar reader instructions/literals changed')
    readers = [(base(source), label.split(':', 1)[0])
               for source, edges in calls.items() for target, label in edges
               if base(target) == WWP_DRIVE_READER[1:]]
    if readers != [('kui_retail_resident_dispatch', 'retail_resident.c')]:
        raise ValueError('WWP scalar reader has an unexpected low caller')
    bridge_calls = Counter((base(source), label.split(':', 1)[0])
                           for source, edges in calls.items() for target, label in edges
                           if base(target) == 'kui_armada_ce_bind_probe')
    expected = Counter({('stream_event', 'retail_async.c'): 1,
                        ('kui_retail_menu_return', 'armada_ce_audio_observe.inc'): 2,
                        ('kui_retail_resident_dispatch', 'armada_ce_audio_observe.inc'): 2})
    if bridge_calls != expected:
        raise ValueError('WWP adds or changes a cross-controller bootstrap/audio call')
    return {'word_address': hex(address), 'word_bytes': 4, 'word_alignment': 4,
            'public_zero_initialized_controller_bss_object': True,
            'exact_linked_object_not_fixed_offset': True,
            'reader_complete_body_pinned': True, 'reader_bytes': count,
            'reader_stack_bytes': 0, 'scalar_loads': 1,
            'reader_calls_and_sr_changes': 0, 'existing_controller_call_sites': 5,
            'additional_cross_controller_query_calls': 0}


def audit_bridges(image, sizes, *, base_address=BASE, limit=LIMIT):
    symbols, payload = image["symbols"], image["payload"]
    for name, expected in ((CALL, CALL_BYTES), (DEFERRED, DEFERRED_BYTES),
                           (OWNER_DEFERRED, OWNER_DEFERRED_BYTES)):
        start = symbols.get(name, 0)
        if (not expected or start % 4 or sizes.get(name) != (start, len(expected)) or
                not base_address <= start <= min(limit, base_address + len(payload)) - len(expected)):
            raise ValueError("Authored bridge size/alignment changed: " + name)
        code = region(payload, start - base_address, len(expected), name)
        if name in BRIDGE_TARGETS:
            target_name = BRIDGE_TARGETS[name]
            target = symbols.get(target_name, 0)
            if target % 2 or not base_address <= target < min(limit, base_address + len(payload)):
                raise ValueError("Missing linked excerpt target: " + target_name)
            if code[-4:] != struct.pack("<I", target):
                raise ValueError("Authored bridge targets an unexpected C function: " + name)
            code = code[:-4] + bytes(4)
        if code != expected:
            raise ValueError("Authored bridge instruction/literal bytes changed: " + name)
    # Only the inner owner bridge gets an FPU waiver. The integer dispatcher
    # and outer bridge remain subject to the ordinary instruction audit.
    return symbols[OWNER_DEFERRED], symbols[OWNER_DEFERRED] + len(OWNER_DEFERRED_BYTES)


def audit_native_callers(controller_calls, *, stream=False):
    if stream:
        observed = {}
        for source, edges in controller_calls.items():
            for target, label in edges:
                if base(target) == CALL[1:]:
                    key = (base(source), label.split(":", 1)[0])
                    observed[key] = observed.get(key, 0) + 1
        if observed != STREAM_NATIVE_CALLERS:
            raise ValueError("Native API bridge has an unexpected stream caller/site count")
        return
    native_sources = {base(source) for source, edges in controller_calls.items()
                      if any(base(target) == "kui_armada_ce_excerpt_ce_call" for target, _ in edges)}
    if native_sources != NATIVE_CALLERS:
        raise ValueError("Native API bridge has an unexpected controller caller")


def audit_owner_callers(controller_calls, *, stream=False):
    owner_edges = [(base(source), label) for source, edges in controller_calls.items()
                   for target, label in edges if base(target) == OWNER_DEFERRED[1:]]
    expected = ({(OWNER_CALLBACK[1:], "armada_ce_stream_player.c"),
                 ("kui_armada_ce_stream_worker", "armada_ce_stream_player.c")}
                if stream else {(OWNER_CALLBACK[1:], "armada_ce_excerpt.c")})
    observed = {(source, label.split(":", 1)[0]) for source, label in owner_edges}
    if len(owner_edges) != len(expected) or observed != expected:
        raise ValueError("Owner FPU bridge has an unexpected controller caller")


def audit_stream_contract(image, *, worms=False):
    """Admit one final initialized footer and exact reserved P1 BSS bounds."""
    STREAM_BASE, STREAM_LIMIT, POOL_BEGIN, POOL_END = stream_layout(worms=worms)
    symbols, payload = image["symbols"], image["payload"]
    binary_end = STREAM_BASE + len(payload)
    begin = symbols.get("__armada_bind_bss_begin", 0)
    end = symbols.get("__armada_bind_bss_end", 0)
    expected = struct.pack("<8I", STREAM_CONTRACT_MAGIC, 1, STREAM_BASE,
                           binary_end, begin, end, STREAM_LIMIT, STREAM_BASE)
    if (len(payload) < STREAM_CONTRACT_BYTES or len(payload) % 4 or
            symbols.get("_kui_armada_ce_bind_probe") != STREAM_BASE or
            symbols.get("__armada_bind_binary_end") != binary_end or
            symbols.get("__armada_stream_contract") != binary_end - STREAM_CONTRACT_BYTES or
            begin != binary_end or end % 4 or not begin <= end <= STREAM_LIMIT or
            image["memory_end"] != end or payload[-STREAM_CONTRACT_BYTES:] != expected or
            STREAM_BASE < ((POOL_BEGIN + SEED_BYTES + PCM_BYTES + 4095) & ~4095) or
            not STREAM_BASE < STREAM_LIMIT <= POOL_END):
        raise ValueError("High stream controller footer/BSS reservation changed")
    return {"base": hex(STREAM_BASE), "limit": hex(STREAM_LIMIT),
            "footer_magic": hex(STREAM_CONTRACT_MAGIC), "footer_bytes": STREAM_CONTRACT_BYTES,
            "binary_end": hex(binary_end), "bss_begin": hex(begin), "bss_end": hex(end),
            "reserved_budget_bytes": STREAM_LIMIT - STREAM_BASE,
            "pcm_code_ranges_disjoint": True}


def audit_map_guard(image, sizes, *, worms=False):
    start = image["symbols"].get("_map_guest", 0)
    span = sizes.get("_map_guest") or (0, 0)
    if span[0] != start or span[1] < MAP_BYTES:
        raise ValueError("Missing direct-RAM destination admission")
    code = region(image["payload"], start - 0x8c008300, MAP_BYTES, "pool destination guard")
    if worms:
        # Keep the complete original body pinned after canonicalizing only
        # its reviewed physical pool endpoint/begin literals.
        code = bytearray(code)
        for offset, expected, original in ((200, 0x0ceeffff, 0x0cfcffff),
                                           (216, 0x0ce70000, 0x0cf50000)):
            if code[offset:offset + 4] != struct.pack('<I', expected):
                raise ValueError('Worms direct-RAM pool guard literal changed')
            code[offset:offset + 4] = struct.pack('<I', original)
    if hashlib.sha256(code).hexdigest() != MAP_SHA256:
        raise ValueError("Linked direct-RAM pool exclusion has changed; review required")


def audit_cache_reader(image, sizes, *, base_address=BASE, limit=LIMIT,
                       locals=None, stream=False):
    """Admit exactly the array leaf and its bounded computed-P2 caller."""
    symbols, payload = image["symbols"], image["payload"]
    readers = [(None, ARRAY_READ, ARRAY_READ_BYTES),
               ("armada_ce_excerpt_entry.c", "_read_cache", CACHE_READ_BYTES)]
    if stream:
        if locals is None:
            raise ValueError("Stream cache reader requires source-qualified ELF locals")
    for unit, name, expected in readers:
        span = locals.get((unit, name)) if unit and locals is not None else sizes.get(name)
        start = span[0] if span else 0
        if (start % 4 or span != (start, len(expected)) or
                not base_address <= start <= min(limit, base_address + len(payload)) - len(expected)):
            raise ValueError("Cache reader size/alignment/address changed: " + name)
        code = bytearray(region(payload, start - base_address, len(expected), name))
        if name == "_read_cache":
            for offset, target_name in CACHE_READ_RELOCS:
                target = (locals.get((unit, target_name), (0, 0))[0]
                          if locals is not None and target_name != ARRAY_READ
                          else symbols.get(target_name, 0))
                if (target % 2 or not base_address <= target < min(limit, base_address + len(payload)) or
                        code[offset:offset + 4] != struct.pack("<I", target)):
                    raise ValueError("Cache reader targets an unexpected function: " + target_name)
                code[offset:offset + 4] = bytes(4)
        if code != expected:
            raise ValueError("Cache reader instruction/literal bytes changed: " + name)
    return {"array_read_p1": hex(symbols[ARRAY_READ]),
            "array_read_p2": hex(symbols[ARRAY_READ] | 0x20000000),
            "array_read_bytes": len(ARRAY_READ_BYTES), "array_read_stack_bytes": 0,
            "array_read_only_mov_p2_pinned": True, "array_read_post_mov_nops": 8,
            "cache_reader_complete_body_pinned": True,
            "cache_reader_entry_binder_only": stream}


def audit_stream_owner_code(image, locals, sizes, *, bam4=False, worms=False, wwp=False):
    """Pin the bounded worker reader in its native owner's virtual domain."""
    STREAM_BASE, STREAM_LIMIT, _, _ = stream_layout(worms=worms)
    unit = "armada_ce_stream_player.c"
    symbols, payload = image["symbols"], image["payload"]
    player, player_bytes = locals.get((unit, "_player"), (0, 0))
    if (player % 4 or player_bytes < 4100 or
            not symbols.get("__armada_bind_bss_begin", 0) <= player < player + player_bytes <=
                symbols.get("__armada_bind_bss_end", 0) <= STREAM_LIMIT):
        raise ValueError("Missing bounded native-owner player state")
    bodies = stream_owner_bodies(bam4=bam4, worms=worms, wwp=wwp)
    for name, expected in bodies.items():
        span = (sizes.get(name) if name.startswith("___") else locals.get((unit, name)))
        start, count = span or (0, 0)
        if (start % 2 or count != len(expected) or
                not STREAM_BASE <= start <= min(STREAM_LIMIT, STREAM_BASE + len(payload)) - count):
            raise ValueError("Missing exact native-owner reader span: " + name)
        if name.startswith("___") and symbols.get(name) != start:
            raise ValueError("Native-owner copy symbol differs from its span")
        code = bytearray(region(payload, start - STREAM_BASE, count, name))
        for offset, target_name, addend in STREAM_OWNER_RELOCS.get(name, ()):
            target_span = (sizes.get(target_name) if target_name.startswith("___") else
                           locals.get((unit, target_name)))
            target, target_bytes = target_span or (0, 0)
            if target_name == "_player":
                valid = target == player and 0 <= addend <= player_bytes - 4
            else:
                valid = (target % 2 == 0 and target_bytes >= 2 and addend == 0 and
                         STREAM_BASE <= target <= STREAM_BASE + len(payload) - target_bytes)
                if target_name.startswith("___"):
                    valid = valid and symbols.get(target_name) == target
            if not valid or code[offset:offset + 4] != struct.pack("<I", target + addend):
                raise ValueError("Native-owner reader targets an unexpected local/data span: " + name)
            code[offset:offset + 4] = bytes(4)
        if code != expected:
            raise ValueError("Native-owner reader instruction/literal bytes changed: " + name)
    if any((unit, name) in locals for name in ("_read_cache", "_read_mapped")):
        raise ValueError("Native worker retains the entry binder's cache-array observer")
    return {"complete_owner_reader_bodies_pinned": len(bodies),
            "kernel_profile": "worms-us" if worms else "wwp-us" if wwp else
                              "bam4-us" if bam4 else "armada-us",
            "current_process_p1": hex(0x8c121890 if worms else
                                      0x8c1d1890 if bam4 or wwp else 0x8c145890),
            "process_base_p1": hex(0x8c1276c0 if worms else
                                   0x8c1da6e0 if bam4 or wwp else 0x8c14e6e0),
            "irq_mask_covers_both_walks_and_native_sample": True,
            "entire_saved_sr_restored": True, "ttb_and_sr_rechecked": True,
            "sr_stability_mask": "0x700000f0", "arithmetic_status_flags_do_not_refuse": True,
            "owner_process_slot_handle_rechecked": True, "mapping_pfn_rechecked": True,
            "native_owner_va_sample_only": True, "metadata_p1_reads_at_most": 18,
            "native_va_reads_at_most": 1, "worker_cache_array_observer_absent": True,
            "worker_tlb_and_cache_maintenance_absent": True}


def audit_stream_owner_callers(controller_calls):
    owner = "kui_armada_ce_stream_owner_word"
    direct = [(base(source), label.split(":", 1)[0])
              for source, edges in controller_calls.items()
              for target, label in edges if base(target) == owner]
    if direct != [("mapped_word", "armada_ce_stream_player.c")]:
        raise ValueError("Native-owner word reader has an unexpected caller")
    indirect = [label.split(":", 1)[0]
                for source, edges in controller_calls.items() if base(source) == owner
                for target, label in edges if target == "__indirect_call"]
    if len(indirect) != 7 or set(indirect) != {"armada_ce_stream_owner_word.h"}:
        raise ValueError("Native-owner word reader callback sites changed")
    for source, edges in controller_calls.items():
        if "armada_ce_stream_player.c:" not in source:
            continue
        if base(source) in ("read_cache", "read_mapped") or any(
                base(target) in ("read_native", "read_cache", "read_mapped", "kui_armada_ce_cache_word")
                for target, _ in edges):
            raise ValueError("Native-owner sample bypasses its bounded reader")


def audit_stream_native_code(image, locals, *, worms=False):
    """Pin the authored entry emitter and fresh-P1-only cache publication."""
    STREAM_BASE, STREAM_LIMIT, _, _ = stream_layout(worms=worms)
    unit = "armada_ce_stream_native.c"
    payload = image["payload"]
    for name, expected, relocation in (
            ("_instruction.0", VENEER_INSTRUCTIONS, None),
            ("_kui_armada_ce_ram_p1", NATIVE_RAM_P1_BYTES, None),
            ("_kui_armada_ce_ram_table_p1", NATIVE_RAM_TABLE_BYTES, (0x1c, "_kui_armada_ce_ram_p1")),
            ("_veneer_word", VENEER_WORD_BYTES, (0x28, "_instruction.0")),
            ("_native_publish", NATIVE_PUBLISH_BYTES, (0x6c, "_kui_armada_ce_ram_table_p1"))):
        start, count = locals.get((unit, name), (0, 0))
        if (start % 4 or count != len(expected) or
                not STREAM_BASE <= start <= min(STREAM_LIMIT, STREAM_BASE + len(payload)) - count):
            raise ValueError("Missing exact native worker code span: " + name)
        code = bytearray(region(payload, start - STREAM_BASE, count, name))
        if relocation:
            offset, target_name = relocation
            target, target_bytes = locals.get((unit, target_name), (0, 0))
            if (target % 2 or target_bytes < 2 or
                    not STREAM_BASE <= target <= STREAM_BASE + len(payload) - target_bytes or
                    code[offset:offset + 4] != struct.pack("<I", target)):
                raise ValueError("Native worker code targets an unexpected local: " + name)
            code[offset:offset + 4] = bytes(4)
        if code != expected:
            raise ValueError("Native worker code instruction/literal bytes changed: " + name)
    return {"veneer_authored_bytes": 72, "veneer_instruction_bytes": len(VENEER_INSTRUCTIONS),
            "veneer_native_home_bytes": 16, "veneer_saved_bytes": 12,
            "veneer_mode_test_before_p1": True, "veneer_emitter_complete_body_pinned": True,
            "native_owned_code_ocbp_body_pinned": True, "native_code_publish_line_bytes": 96,
            "native_code_main_ram_admission_pinned": True}


def audit_stream_bus(controller, controller_locals, resident, resident_spans, *, worms=False):
    """Admit the exact low descriptor, relay and high pre-dereference guard."""
    STREAM_BASE, _, _, _ = stream_layout(worms=worms)
    unit = "armada_ce_stream_transport.c"
    high_payload, low_payload = controller["payload"], resident["payload"]
    for name, expected in (("_low_code", STREAM_LOW_CODE_BYTES),
                           ("_bus_valid", STREAM_BUS_VALID_BYTES)):
        start, count = controller_locals.get((unit, name), (0, 0))
        if (start % 4 or count != len(expected) or
                not STREAM_BASE <= start <= STREAM_BASE + len(high_payload) - count):
            raise ValueError("Missing exact stream bus admission span: " + name)
        code = bytearray(region(high_payload, start - STREAM_BASE, count, name))
        if name == "_bus_valid":
            target, target_bytes = controller_locals.get((unit, "_low_code"), (0, 0))
            if target_bytes != len(STREAM_LOW_CODE_BYTES) or code[-4:] != struct.pack("<I", target):
                raise ValueError("Stream bus admission targets an unexpected low-code guard")
            code[-4:] = bytes(4)
        if code != expected:
            raise ValueError("Stream bus admission instruction/literal bytes changed: " + name)
    symbols = resident["symbols"]
    descriptor_name = "_kui_retail_ce_stream_bus"
    descriptor, descriptor_bytes = resident_spans.get(descriptor_name) or (0, 0)
    region_start, region_bytes = resident_spans.get("_kui_retail_async_region") or (0, 0)
    if (descriptor_bytes != 60 or descriptor % 4 or symbols.get(descriptor_name) != descriptor or
            not 0x8c008300 <= descriptor <= 0x8c008300 + len(low_payload) - 60 or
            region_start % 32 or region_bytes != 0xf60 or
            symbols.get("_kui_retail_async_region") != region_start or
            not 0x8c008300 <= region_start <= 0x8c00d800 - region_bytes):
        raise ValueError("Missing exact immutable low bus descriptor/region")
    targets = []
    for callback in STREAM_BUS_CALLBACKS:
        name = "_" + callback
        address, count = resident_spans.get(name) or (0, 0)
        if (symbols.get(name) != address or address % 2 or count < 2 or
                not 0x8c008300 <= address <= 0x8c008300 + len(low_payload) - count):
            raise ValueError("Missing bounded low stream callback: " + name)
        targets.append(address)
    expected = struct.pack("<15I", 1, 60, region_start, *targets)
    if region(low_payload, descriptor - 0x8c008300, 60, "immutable stream bus") != expected:
        raise ValueError("Low stream bus descriptor has an unexpected callback")
    relay, relay_bytes = resident_spans.get("_stream_event") or (0, 0)
    if (relay_bytes != len(STREAM_BUS_RELAY_BYTES) or relay % 4 or
            not 0x8c008300 <= relay <= 0x8c008300 + len(low_payload) - relay_bytes):
        raise ValueError("Missing exact low stream event relay")
    code = region(low_payload, relay - 0x8c008300, relay_bytes, "stream event relay")
    if code[:-4] != STREAM_BUS_RELAY_BYTES[:-4] or code[-4:] != struct.pack("<I", STREAM_BASE):
        raise ValueError("Low stream event relay target/instructions changed")
    return {"descriptor_address": hex(descriptor), "descriptor_bytes": 60,
            "region_address": hex(region_start), "region_bytes": region_bytes,
            "callback_targets": dict(zip(STREAM_BUS_CALLBACKS, map(hex, targets))),
            "descriptor_immutable_exact_targets": True,
            "descriptor_guard_before_dereference_pinned": True,
            "low_bus_relay_complete_body_pinned": True}


def audit_seed(stage, *, stream=False, worms=False):
    start = stage["symbols"].get("_armada_excerpt_seed", 0)
    header=STREAM_SEED_HEADER if stream else SEED_HEADER
    if worms:
        if not stream:
            raise ValueError('Worms requires a stream source seed')
        header = struct.pack('<16I', 0x32584543, 2, 0x8ce70040,
                             PCM_BYTES, 0x8cedf000, 4096, 0, 352768, *([0] * 8))
    payload_bytes=4096 if stream else PCM_BYTES
    seed = region(stage["payload"], start - STAGE, SEED_BYTES + payload_bytes, "private source seed")
    if start % 4 or seed[:SEED_BYTES] != header or any(seed[SEED_BYTES:]):
        raise ValueError("Distributed audio package must have an exact blank source seed")
    found = [i for i in range(0, len(stage["payload"]) - SEED_BYTES + 1, 4)
             if stage["payload"][i:i + SEED_BYTES] == header]
    if found != [start - STAGE]:
        raise ValueError("The typed excerpt seed is missing or duplicated")
    return start


def audit_stream_terminal_snapshot(image, locals_, *, worms=False):
    """Keep the terminal ABI and its immutable labels tied to the entry unit."""
    base_address, _, _, _ = stream_layout(worms=worms)
    unit = 'armada_ce_excerpt_entry.c'
    address, count = locals_.get((unit, '_snapshot')) or (0, 0)
    begin = image['symbols'].get('__armada_bind_bss_begin', 0)
    end = image['symbols'].get('__armada_bind_bss_end', 0)
    if (count != 600 or address % 4 or
            image['symbols'].get('_snapshot') != address or
            not base_address <= begin <= address <= end - count or
            end != image['memory_end']):
        raise ValueError('Missing exact 600-byte terminal snapshot ABI')
    for name, labels in (('_labels.1', STREAM_REPORT_LABELS),
                         ('_underrun_labels.0', STREAM_UNDERRUN_LABELS)):
        expected = b''.join(label.encode('ascii').ljust(40, b'\0') for label in labels)
        start, length = locals_.get((unit, name)) or (0, 0)
        if (start % 4 or length != len(expected) or
                image['symbols'].get(name) != start or
                not base_address <= start <= base_address + len(image['payload']) - length or
                region(image['payload'], start - base_address, length, name) != expected):
            raise ValueError('Missing exact terminal label table: ' + name)
    return {'snapshot_bytes': 600, 'preserved_prefix_bytes': 428,
            'frozen_underrun_offset': 428, 'frozen_underrun_bytes': 140,
            'frozen_labels_offset': 568, 'frozen_label_pointers': 8}


def audit_stream_boot_arm(controller, controller_spans, resident, resident_spans, stage, stage_spans,
                          *, bam4=False, worms=False, wwp=False):
    """Pin startup quiescence and the exact post-verification arming path.

    This intentionally checks complete compiled bodies, not a source-text
    search or an allowlist that exempts bootstrap-written high BSS. The low
    high entry returns before any state write until the transport's checked
    attachment exists. Only after every relay guard does the stage attach
    the descriptor named by the immutable low entry word at offset 52.
    """
    STREAM_BASE, _, _, _ = stream_layout(worms=worms)
    low, symbols = resident["payload"], resident["symbols"]
    descriptor_name = "_kui_retail_ce_stream_bus"
    descriptor, descriptor_bytes = resident_spans.get(descriptor_name) or (0, 0)
    slot = 52
    if (symbols.get("_kui_retail_ce_stream_bus_entry") != 0x8c008300 + slot or
            descriptor % 4 or descriptor_bytes != 60 or symbols.get(descriptor_name) != descriptor or
            not 0x8c008300 + 56 <= descriptor <= 0x8c008300 + len(low) - descriptor_bytes or
            region(low, slot, 4, "stream bus entry") != struct.pack("<I", descriptor)):
        raise ValueError("Missing exact immutable stream descriptor entry at slot 52")
    bodies = stream_boot_bodies(bam4=bam4, worms=worms, wwp=wwp)
    relocations = stream_boot_relocs(wwp=wwp)
    required = STREAM_BOOT_REQUIRED | ({('resident', '_purge.lto_priv.0')} if wwp else set())
    if set(bodies) != required:
        raise ValueError("Missing stream bootstrap complete-body proof")
    pinned = []
    for (image_name, name), expected in bodies.items():
        image, spans, image_base = {"resident": (resident, resident_spans, 0x8c008300),
                                    "stage": (stage, stage_spans, STAGE),
                                    "controller": (controller, controller_spans, STREAM_BASE)}[image_name]
        address, count = spans.get(name) or (0, 0)
        if (address % 2 or count != len(expected) or
                image["symbols"].get(name) != address or
                not image_base <= address <= image_base + len(image["payload"]) - count):
            raise ValueError("Missing exact stream bootstrap body: " + name)
        code = bytearray(region(image["payload"], address - image_base, count, name))
        for offset, target_name, addend in relocations.get((image_name, name), ()):
            target = image["symbols"].get(target_name, 0) + addend
            # The initializer's exclusive BSS boundary is a checked bound,
            # not a dereferenced object or call target. Keep its literal tied
            # exactly to this image's audited end, within the service stack.
            exclusive_end = (image_name == "resident" and
                name == "_kui_retail_resident_init" and offset == 0x27a and
                target_name == "__retail_resident_bss_end" and addend == 0 and
                target == image["memory_end"] <= 0x8c00d800)
            if not (image_base <= target < image["memory_end"] or exclusive_end):
                raise ValueError("Missing stream bootstrap relocation: " + target_name)
            if code[offset:offset + 4] != struct.pack("<I", target):
                raise ValueError("Stream bootstrap body targets an unexpected address: " + name)
            code[offset:offset + 4] = bytes(4)
        if image_name == 'stage':
            audit_bam4_stage_literals(code, image, original_literals=not (bam4 or worms or wwp))
        if code != expected:
            raise ValueError("Stream bootstrap instructions/order/literals changed: " + name)
        pinned.append(image_name + ":" + name)
    if not pinned:
        raise ValueError("Missing stream bootstrap reviewed body pins")
    return {"entry_slot": slot, "descriptor_address": hex(descriptor),
            "romhdr_p2": hex(0xac11de70 if worms else 0xac1cd614 if wwp else
                              0xac1ce19c if bam4 else 0xac141420),
            "preserves_controller_zero_check": True, "attach_only_after_relay": True,
            "bootstrap_audio_and_bus_gated": True, "complete_linked_body_pins": pinned}


def check(directory, *, stream=False, bam4=False, worms=False, wwp=False):
    if sum((bam4, worms, wwp)) > 1:
        raise ValueError('Select only one WinCE title profile')
    if (bam4 or worms or wwp) and not stream:
        raise ValueError('Title profiles require the explicit stream audit')
    STREAM_BASE, STREAM_LIMIT, POOL_BEGIN, POOL_END = stream_layout(worms=worms)
    directory = Path(directory)
    controller_base, controller_limit = (STREAM_BASE, STREAM_LIMIT) if stream else (BASE, LIMIT)
    ordinary = check_directory(directory, ce=True)
    direct_calls = check_direct_calls(directory) if wwp else None
    ordinary_instructions = 0
    for name in IMAGES:
        decoded = subprocess.check_output(
            ["sh-elf-objdump", "-d", str(directory / (name + ".elf"))], text=True)
        (directory / (name + ".dis")).write_text(decoded)
        ordinary_instructions += audit(name, decoded)
    elf = (directory / "bind-probe.elf").read_bytes()
    controller = inspect_elf(elf, controller_base, controller_limit, entry_symbol="_kui_armada_ce_bind_probe")
    check_bss(controller, controller_base, "__armada_bind")
    stream_contract = audit_stream_contract(controller, worms=worms) if stream else None
    if not stream and "__armada_stream_contract" in controller["symbols"]:
        raise ValueError("Stream controller requires the explicit high-layout audit")
    if any(s.startswith(FORBIDDEN_PREFIXES) or s in FORBIDDEN_SYMBOLS
           for s in controller["symbols"]):
        raise ValueError("Forbidden allocator, hardware, SDK or runtime dependency")
    if (directory / "bind-probe.bin").read_bytes() != controller["payload"]:
        raise ValueError("Controller binary differs from linked ELF")
    controller_sizes = function_sizes(elf)
    controller_locals = local_spans(elf)
    terminal_snapshot = audit_stream_terminal_snapshot(controller, controller_locals,
                         worms=worms) if stream else None
    bridge_start, bridge_end = audit_bridges(controller, controller_sizes,
                                             base_address=controller_base, limit=controller_limit)
    cache_reader = audit_cache_reader(controller, controller_sizes,
                                     base_address=controller_base, limit=controller_limit,
                                     locals=controller_locals, stream=stream)
    native_code = audit_stream_native_code(controller, controller_locals, worms=worms) if stream else None
    owner_reader = audit_stream_owner_code(controller, controller_locals, controller_sizes,
                                           bam4=bam4, worms=worms, wwp=wwp) if stream else None
    disassembly = subprocess.check_output(
        ["sh-elf-objdump", "-d", str(directory / "bind-probe.elf")], text=True)
    (directory / "bind-probe.dis").write_text(disassembly)
    outside = []
    for line in disassembly.splitlines():
        address = re.match(r"^\s*([0-9a-f]+):", line)
        if address and bridge_start <= int(address.group(1), 16) < bridge_end:
            continue
        outside.append(line)
    instructions = audit("bind-probe", "\n".join(outside))

    stage_elf = (directory / "stage.elf").read_bytes()
    stage = inspect_elf(stage_elf, STAGE, POOL_BEGIN)
    if stage["memory_end"] >= POOL_BEGIN:
        raise ValueError("Stage/BSS overlaps the pre-boot excerpt reservation")
    ss = stage["symbols"]
    for name, marker in (("_kui_armada_ce_bind_stage_contract", 0x43454231),
                         ("_kui_armada_ce_excerpt_stage_contract", 0x43455832 if stream else 0x43455831)):
        if region(stage["payload"], ss.get(name, 0) - STAGE, 4, name) != struct.pack("<I", marker):
            raise ValueError("Missing exact kernel/installer/pre-boot reservation contract")
    start, end = ss.get("__armada_bind_blob_start", 0), ss.get("__armada_bind_blob_end", 0)
    if start % 4 or end - start != len(controller["payload"]) or region(
            stage["payload"], start - STAGE, end - start, "embedded excerpt controller") != controller["payload"]:
        raise ValueError("Embedded controller is missing, misaligned or different")
    seed_address = audit_seed(stage,stream=stream, worms=worms)
    if not any(base(n) == "_ce_load" for n in function_sizes(stage_elf)):
        raise ValueError("Missing pre-boot admitted CE loader")

    resident_elf = (directory / "resident-scia.elf").read_bytes()
    resident = inspect_elf(resident_elf, 0x8c008300, 0x8c00d800)
    stream_bus = audit_stream_bus(controller, controller_locals, resident,
                                   function_sizes(resident_elf, kinds=(1, 2)), worms=worms) if stream else None
    stream_boot_arm = audit_stream_boot_arm(controller, controller_sizes, resident,
                          function_sizes(resident_elf, kinds=(1, 2)), stage,
                          function_sizes(stage_elf, kinds=(1, 2)), bam4=bam4, worms=worms,
                          wwp=wwp) if stream else None
    if resident["symbols"].get("_kui_armada_ce_bind_probe") != controller_base:
        raise ValueError("Resident controller call target moved")
    audit_map_guard(resident, function_sizes(resident_elf), worms=worms)
    _, calls = load(sorted((directory / "scia/lto").glob("*.ci")), qualified=wwp)
    sources = {base(source) for source, edges in calls.items()
               if any(base(target) == "kui_armada_ce_bind_probe" for target, _ in edges)}
    expected_sources = {"kui_retail_resident_dispatch", "kui_retail_menu_return"}
    if stream:
        expected_sources.add("stream_event")
    if sources != expected_sources:
        raise ValueError("Controller entry has an unexpected resident caller")
    drive_projection = audit_wwp_drive_projection(elf, resident,
                        function_sizes(resident_elf), calls) if wwp else None
    controller_reports = [directory / "bind-probe.ci", directory / "excerpt.ci"]
    if stream:
        controller_reports += sorted(directory.glob("stream-*.ci"))
    _, controller_calls = load(controller_reports, qualified=True)
    audit_native_callers(controller_calls, stream=stream)
    audit_owner_callers(controller_calls, stream=stream)
    if stream:
        audit_stream_owner_callers(controller_calls)
    for name in (DISPATCH, OWNER_CALLBACK, ATTEMPT):
        start, size = controller_sizes.get(name) or (0, 0)
        if (start != controller["symbols"].get(name) or start % 2 or size < 2 or
                not controller_base <= start <= min(controller_limit, controller_base + len(controller["payload"])) - size):
            raise ValueError("Missing bounded native-owner callback function: " + name)
    if any(base(target) == "kui_armada_ce_excerpt_array_read"
           for edges in controller_calls.values() for target, _ in edges):
        raise ValueError("Array leaf has an unexpected direct caller")
    cache_calls = [label for source, edges in controller_calls.items() if base(source) == "read_cache"
                   for target, label in edges if target == "__indirect_call"]
    if (len(cache_calls) != 1 or
            {label.split(":", 1)[0] for label in cache_calls} !=
            {"armada_ce_excerpt_entry.c"}):
        raise ValueError("Missing sole computed-P2 cache-array call")
    stack = ordinary["resident_stacks"]["scia"]
    if not stack["call_graph"] or stack["worst_bytes"] + stack["margin"] > stack["available_bytes"]:
        raise ValueError("Excerpt entry is not covered by bounded masked-stack audit")
    return {"controller_entry": hex(controller_base), "controller_memory_end": hex(controller["memory_end"]),
            "title_profile": "worms-us" if worms else "wwp-us" if wwp else
                             "bam4-us" if bam4 else "armada-us",
            "stream_profile": stream, "stream_controller_contract": stream_contract,
            "controller_payload_bytes": len(controller["payload"]), "controller_unresolved_symbols": 0,
            "controller_integer_instructions": instructions, "exact_authored_deferred_bridge": True,
            "outer_integer_bridge_bytes": len(DEFERRED_BYTES),
            "native_owner_fpu_bridge_bytes": len(OWNER_DEFERRED_BYTES),
            "fpu_waiver_owner_bridge_only": True,
            "native_bridge_callers": sorted({caller for caller, _ in STREAM_NATIVE_CALLERS}
                                              if stream else NATIVE_CALLERS),
            "owner_fpu_bridge_sole_caller": None if stream else OWNER_CALLBACK[1:],
            "owner_fpu_bridge_callers": ([OWNER_CALLBACK[1:], "kui_armada_ce_stream_worker"]
                                          if stream else [OWNER_CALLBACK[1:]]),
            "native_owner_functions_within_controller": True,
            "ordinary_fpu_free_instructions": ordinary_instructions,
            "both_fpu_banks_restored": True, "refuse_context_before_fpu_write": True,
            "native_abi_home_bytes": 16, "native_abi_stack_arguments": 4,
            "controller_bss_bytes": controller["memory_end"] - controller["symbols"]["__armada_bind_bss_begin"],
            "embedded_controller_matches": True, "resident_target_matches": True,
            "gd_direct_ram_pool_exclusion_pinned": True,
            "seed_address": hex(seed_address), "distributed_pcm_bytes": 0 if stream else PCM_BYTES,
            "distributed_pcm_is_zero": True,
            "distributed_audio_map_bytes": 4096 if stream else 0,
            "preboot_pool_begin": hex(POOL_BEGIN), "preboot_pool_end": hex(POOL_END),
            "stage_bss_below_pool": True,
            "masked_stack_includes_entry_binder_queue_snapshot": True,
            "native_deferred_path_excluded_from_masked_stack": True,
            "resident_stacks": ordinary["resident_stacks"],
            "cache_reader": cache_reader, "native_worker_code": native_code,
            "native_owner_reader": owner_reader,
            "stream_bus": stream_bus, "stream_boot_arm": stream_boot_arm,
            "terminal_snapshot": terminal_snapshot, "drive_status_projection": drive_projection,
            "linked_direct_calls": direct_calls}


def stream_boot_self_test(*, bam4=False, worms=False, wwp=False):
    """Reject entry-slot, gate, every compiled guard and ordering bit."""
    STREAM_BASE, _, _, _ = stream_layout(worms=worms)
    bodies = stream_boot_bodies(bam4=bam4, worms=worms, wwp=wwp)
    relocations = stream_boot_relocs(wwp=wwp)
    fixtures = {}
    for image_name, image_base in (("controller", STREAM_BASE),
                                   ("resident", 0x8c008300), ("stage", STAGE)):
        payload = bytearray(64)
        symbols, spans = {}, {}
        for (owner, name), code in bodies.items():
            if owner != image_name:
                continue
            payload.extend(bytes(-len(payload) % 4))
            address = image_base + len(payload)
            symbols[name] = address; spans[name] = (address, len(code))
            payload.extend(code)
        payload.extend(bytes(-len(payload) % 4))
        if image_name == "resident":
            descriptor = image_base + len(payload)
            symbols["_kui_retail_ce_stream_bus"] = descriptor
            spans["_kui_retail_ce_stream_bus"] = (descriptor, 60)
            symbols["_kui_retail_ce_stream_bus_entry"] = image_base + 52
            payload[52:56] = struct.pack("<I", descriptor)
            payload.extend(bytes(60))
        elif image_name == "controller":
            symbols["_kui_armada_ce_stream_fault"] = image_base + len(payload)
            payload.extend(bytes.fromhex("0b000900"))
            symbols["_t"] = image_base + len(payload)
            payload.extend(bytes(1248))
        # Relocation pins also name unpinned helpers and data, including the
        # appended terminal snapshot. Give each declared target a distinct
        # synthetic span; only the complete bodies above are code fixtures.
        external_targets = {}
        for (owner, _), targets in relocations.items():
            if owner == image_name:
                for _, target, addend in targets:
                    external_targets[target] = max(external_targets.get(target, 4), addend + 4)
        for target, target_bytes in sorted(external_targets.items()):
            if target not in symbols:
                symbols[target] = image_base + len(payload)
                payload.extend(bytes(target_bytes))
        for (owner, name), targets in relocations.items():
            if owner != image_name:
                continue
            body_start = symbols[name] - image_base
            for offset, target_name, addend in targets:
                payload[body_start + offset:body_start + offset + 4] = struct.pack(
                    "<I", symbols[target_name] + addend)
        if image_name == 'stage':
            # Exercise the real address-aware proof with distinct synthetic
            # functions/messages. Historical absolute addresses must not be
            # accepted merely because they appear in the expected body.
            relay_start = symbols['_kui_retail_stage_relay'] - image_base
            for offset, _, symbol, message in BAM4_STAGE_LINKED_TARGETS:
                payload.extend(bytes(-len(payload) % 4))
                target = image_base + len(payload)
                if symbol:
                    symbols[symbol] = target
                    payload.extend(bytes.fromhex('0b000900'))
                else:
                    payload.extend(message.encode('ascii') + b'\0')
                struct.pack_into('<I', payload, relay_start + offset, target)
        fixtures[image_name] = ({"payload": payload, "symbols": symbols,
                                  "memory_end": image_base + len(payload)}, spans)
    def check_boot(changed_name=None, changed_image=None, changed_spans=None):
        args = []
        for owner in ("controller", "resident", "stage"):
            fixture, spans = fixtures[owner]
            args.extend((changed_image if changed_name == owner and changed_image is not None else fixture,
                         changed_spans if changed_name == owner and changed_spans is not None else spans))
        return audit_stream_boot_arm(*args, bam4=bam4, worms=worms, wwp=wwp)
    proof = check_boot()
    assert proof["entry_slot"] == 52 and proof["preserves_controller_zero_check"]
    assert proof["attach_only_after_relay"] and proof["bootstrap_audio_and_bus_gated"]
    checks = 1
    for owner, (fixture, spans) in fixtures.items():
        positions = [n for (im, name), code in bodies.items() if im == owner
                     for n in range(spans[name][0] - (STREAM_BASE if owner == "controller" else
                                                      0x8c008300 if owner == "resident" else STAGE),
                                    spans[name][0] - (STREAM_BASE if owner == "controller" else
                                                      0x8c008300 if owner == "resident" else STAGE) + len(code))]
        if owner == "resident":
            positions += list(range(52, 56))
        for at in positions:
            for bit in range(8):
                broken = bytearray(fixture["payload"]); broken[at] ^= 1 << bit
                try:
                    check_boot(owner, dict(fixture, payload=broken))
                except ValueError:
                    checks += 1
                else:
                    raise AssertionError("Compiled stream bootstrap guard/order corruption admitted")
        for name in spans:
            broken = dict(spans); broken[name] = (spans[name][0] + 2, spans[name][1])
            try:
                check_boot(owner, changed_spans=broken)
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Stream bootstrap function/descriptor relocation admitted")
        for name in fixture["symbols"]:
            broken = dict(fixture["symbols"]); broken[name] += 4
            try:
                check_boot(owner, dict(fixture, symbols=broken))
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Stream bootstrap linked symbol corruption admitted")
    return checks


def stream_code_self_test():
    """Reject every changed veneer, publisher and bus-admission bit."""
    checks = 0
    unit = "armada_ce_stream_native.c"
    cases = (("_instruction.0", VENEER_INSTRUCTIONS, None),
             ("_kui_armada_ce_ram_p1", NATIVE_RAM_P1_BYTES, None),
             ("_kui_armada_ce_ram_table_p1", NATIVE_RAM_TABLE_BYTES, (0x1c, "_kui_armada_ce_ram_p1")),
             ("_veneer_word", VENEER_WORD_BYTES, (0x28, "_instruction.0")),
             ("_native_publish", NATIVE_PUBLISH_BYTES, (0x6c, "_kui_armada_ce_ram_table_p1")))
    payload, locals = bytearray(), {}
    for name, expected, _ in cases:
        locals[(unit, name)] = STREAM_BASE + len(payload), len(expected)
        payload.extend(expected)
    for name, _, relocation in cases:
        if relocation:
            at, target = relocation
            offset = locals[(unit, name)][0] - STREAM_BASE + at
            payload[offset:offset + 4] = struct.pack("<I", locals[(unit, target)][0])
    image = {"payload": payload}
    audit_stream_native_code(image, locals); checks += 1
    for at in range(len(payload)):
        for bit in range(8):
            broken = bytearray(payload); broken[at] ^= 1 << bit
            try:
                audit_stream_native_code(dict(image, payload=broken), locals)
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Native veneer/publication corruption admitted")
    for key in locals:
        for change in ((locals[key][0] + 2, locals[key][1]),
                       (locals[key][0], locals[key][1] + 2)):
            broken = dict(locals); broken[key] = change
            try:
                audit_stream_native_code(image, broken)
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Native code local span corruption admitted")
    changed_unit = {( "unreviewed.c", name): value for (_, name), value in locals.items()}
    try:
        audit_stream_native_code(image, changed_unit)
    except ValueError:
        checks += 1
    else:
        raise AssertionError("Wrong native code translation unit admitted")

    unit = "armada_ce_stream_transport.c"
    high = bytearray(STREAM_LOW_CODE_BYTES + STREAM_BUS_VALID_BYTES)
    high_locals = {(unit, "_low_code"): (STREAM_BASE, len(STREAM_LOW_CODE_BYTES)),
                   (unit, "_bus_valid"): (STREAM_BASE + len(STREAM_LOW_CODE_BYTES), len(STREAM_BUS_VALID_BYTES))}
    high[-4:] = struct.pack("<I", STREAM_BASE)
    controller = {"payload": high}
    low_base = 0x8c008300
    descriptor, relay, target_begin, region_start = low_base, low_base + 64, low_base + 80, 0x8c00c400
    addresses = [target_begin + n * 4 for n in range(len(STREAM_BUS_CALLBACKS))]
    low = bytearray(struct.pack("<15I", 1, 60, region_start, *addresses) + bytes(4) +
                    STREAM_BUS_RELAY_BYTES[:-4] + struct.pack("<I", STREAM_BASE) +
                    bytes(len(STREAM_BUS_CALLBACKS) * 4))
    symbols = {"_kui_retail_ce_stream_bus": descriptor, "_stream_event": relay,
               "_kui_retail_async_region": region_start}
    spans = {"_kui_retail_ce_stream_bus": (descriptor, 60), "_stream_event": (relay, 16),
             "_kui_retail_async_region": (region_start, 0xf60)}
    for name, address in zip(STREAM_BUS_CALLBACKS, addresses):
        symbols["_" + name] = address; spans["_" + name] = (address, 2)
    resident = {"payload": low, "symbols": symbols}
    audit_stream_bus(controller, high_locals, resident, spans); checks += 1
    for image_key, fixture, positions in (("high", high, range(len(high))),
                                          ("low", low, list(range(60)) + list(range(64, 80)))):
        for at in positions:
            for bit in range(8):
                broken = bytearray(fixture); broken[at] ^= 1 << bit
                try:
                    audit_stream_bus(dict(controller, payload=broken) if image_key == "high" else controller,
                                     high_locals,
                                     dict(resident, payload=broken) if image_key == "low" else resident, spans)
                except ValueError:
                    checks += 1
                else:
                    raise AssertionError("Stream descriptor/guard/relay corruption admitted")
    for key in spans:
        broken = dict(spans); broken[key] = (spans[key][0] + 2, spans[key][1])
        try:
            audit_stream_bus(controller, high_locals, resident, broken)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Stream descriptor callback/region span corruption admitted")
    native_calls = {}
    for (caller, source), count in STREAM_NATIVE_CALLERS.items():
        native_calls[caller] = [(CALL[1:], source + ":1")] * count
    audit_native_callers(native_calls, stream=True); checks += 1
    for caller in native_calls:
        broken = dict(native_calls); broken[caller] = broken[caller][:-1]
        try:
            audit_native_callers(broken, stream=True)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Missing stream native callsite admitted")
    for broken in (dict(native_calls, rogue=[(CALL[1:], "armada_ce_stream_player.c:1")]),
                   dict(native_calls, call=[(CALL[1:], "unreviewed.c:1")]),
                   dict(native_calls, refill=native_calls["refill"] + native_calls["refill"][:1])):
        try:
            audit_native_callers(broken, stream=True)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Unexpected stream native callsite admitted")
    return checks


def stream_owner_self_test(*, bam4=False, worms=False, wwp=False):
    """Reject every owner-reader byte, local target, state span and route."""
    STREAM_BASE, STREAM_LIMIT, _, _ = stream_layout(worms=worms)
    bodies = stream_owner_bodies(bam4=bam4, worms=worms, wwp=wwp)
    def check_owner(image, locals, sizes):
        return audit_stream_owner_code(image, locals, sizes, bam4=bam4,
                                       worms=worms, wwp=wwp)
    unit = "armada_ce_stream_player.c"
    payload, locals, sizes, checked = bytearray(), {}, {}, []
    for name, expected in bodies.items():
        payload.extend(bytes(-len(payload) % 4))
        start = STREAM_BASE + len(payload)
        payload.extend(expected)
        span = start, len(expected)
        if name.startswith("___"):
            sizes[name] = span
        else:
            locals[(unit, name)] = span
        checked.extend(range(start - STREAM_BASE, start - STREAM_BASE + len(expected)))
    player, player_bytes = STREAM_LIMIT - 0x2000, 4100
    locals[(unit, "_player")] = player, player_bytes
    symbols = {name: address for name, (address, _) in sizes.items()}
    symbols.update({"__armada_bind_bss_begin": player,
                    "__armada_bind_bss_end": player + player_bytes})
    for name, relocations in STREAM_OWNER_RELOCS.items():
        start, _ = locals[(unit, name)]
        for offset, target_name, addend in relocations:
            target_span = (sizes[target_name] if target_name.startswith("___") else
                           locals[(unit, target_name)])
            struct.pack_into("<I", payload, start - STREAM_BASE + offset, target_span[0] + addend)
    image = {"payload": payload, "symbols": symbols}
    proof = check_owner(image, locals, sizes)
    assert proof['kernel_profile'] == ('worms-us' if worms else 'wwp-us' if wwp else
                                       'bam4-us' if bam4 else 'armada-us')
    checks = 1
    for at in checked:
        for bit in range(8):
            broken = bytearray(payload); broken[at] ^= 1 << bit
            try:
                check_owner(dict(image, payload=broken), locals, sizes)
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Native-owner reader byte corruption admitted")
    for key, (start, count) in locals.items():
        for span in ((start + 2, count), (start, count - 2), (STREAM_LIMIT, count)):
            broken = dict(locals); broken[key] = span
            try:
                check_owner(image, broken, sizes)
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Native-owner reader local/data span corruption admitted")
    for bounds in ({"__armada_bind_bss_begin": player + 4},
                   {"__armada_bind_bss_end": player + player_bytes - 4},
                   {"__armada_bind_bss_end": STREAM_LIMIT + 4}):
        try:
            check_owner(dict(image, symbols=dict(symbols, **bounds)), locals, sizes)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Native-owner state outside admitted BSS allowed")
    try:
        check_owner(image, {("unreviewed.c", n): span for (_, n), span in locals.items()}, sizes)
    except ValueError:
        checks += 1
    else:
        raise AssertionError("Wrong owner-reader translation unit admitted")
    owner = "kui_armada_ce_stream_owner_word"
    calls = {"src/loader/armada_ce_stream_player.c:mapped_word": [(owner, unit + ":1")],
             "src/loader/armada_ce_stream_player.c:" + owner:
                 [("__indirect_call", "armada_ce_stream_owner_word.h:1")] * 7}
    audit_stream_owner_callers(calls); checks += 1
    for broken in ({}, dict(calls, rogue=[(owner, "unreviewed.c:1")]),
                   {key: edges for key, edges in calls.items() if base(key) != "mapped_word"},
                   dict(calls, **{"src/loader/armada_ce_stream_player.c:" + owner:
                       [("__indirect_call", "armada_ce_stream_owner_word.h:1")] * 6}),
                   dict(calls, **{"src/loader/armada_ce_stream_player.c:" + owner:
                       [("__indirect_call", "unreviewed.c:1")] * 7}),
                   dict(calls, **{"src/loader/armada_ce_stream_player.c:rogue":
                       [("read_native", unit + ":1")]}),
                   dict(calls, **{"src/loader/armada_ce_stream_player.c:read_cache": []})):
        try:
            audit_stream_owner_callers(broken)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Unexpected native-owner reader/callback route admitted")
    return checks


def self_test():
    """Corrupt outer/owner opcodes, literals, targets, routing and seed bytes."""
    payload = bytearray()
    symbols, sizes, checked_bytes = {}, {}, []
    for name, expected in ((CALL, CALL_BYTES), (DEFERRED, DEFERRED_BYTES),
                           (OWNER_DEFERRED, OWNER_DEFERRED_BYTES)):
        payload.extend(bytes((-len(payload)) % 4))
        start = BASE + len(payload)
        symbols[name] = start
        sizes[name] = (start, len(expected))
        checked_bytes.extend(range(len(payload), len(payload) + len(expected)))
        payload.extend(expected)
    for target in (DISPATCH, ATTEMPT):
        symbols[target] = BASE + len(payload)
        payload.extend(bytes(4))
    for name, target in BRIDGE_TARGETS.items():
        offset = symbols[name] - BASE + sizes[name][1] - 4
        payload[offset:offset + 4] = struct.pack("<I", symbols[target])
    image = {"payload": payload, "symbols": symbols}
    waived = audit_bridges(image, sizes)
    if waived != (symbols[OWNER_DEFERRED], symbols[OWNER_DEFERRED] + len(OWNER_DEFERRED_BYTES)):
        raise AssertionError("FPU waiver extends beyond exact owner bridge")
    checks = 1
    high_symbols = {name: value + STREAM_BASE - BASE for name, value in symbols.items()}
    high_sizes = {name: (address + STREAM_BASE - BASE, size) for name, (address, size) in sizes.items()}
    high_payload = bytearray(payload)
    for name, target in BRIDGE_TARGETS.items():
        offset = symbols[name] - BASE + sizes[name][1] - 4
        high_payload[offset:offset + 4] = struct.pack("<I", high_symbols[target])
    high_image = {"payload": high_payload, "symbols": high_symbols}
    high_waived = audit_bridges(high_image, high_sizes, base_address=STREAM_BASE, limit=STREAM_LIMIT)
    if high_waived != (high_symbols[OWNER_DEFERRED], high_symbols[OWNER_DEFERRED] + len(OWNER_DEFERRED_BYTES)):
        raise AssertionError("High FPU waiver extends beyond exact owner bridge")
    checks += 1
    try:
        audit_bridges(high_image, high_sizes)
    except ValueError:
        checks += 1
    else:
        raise AssertionError("High controller admitted through low-profile audit")
    contract_end, bss_end = STREAM_BASE + 64, STREAM_BASE + 128
    footer = struct.pack("<8I", STREAM_CONTRACT_MAGIC, 1, STREAM_BASE,
                         contract_end, contract_end, bss_end, STREAM_LIMIT, STREAM_BASE)
    contract_image = {"payload": bytes(32) + footer, "memory_end": bss_end,
                      "symbols": {"_kui_armada_ce_bind_probe": STREAM_BASE,
                                  "__armada_bind_binary_end": contract_end,
                                  "__armada_bind_bss_begin": contract_end,
                                  "__armada_bind_bss_end": bss_end,
                                  "__armada_stream_contract": contract_end - STREAM_CONTRACT_BYTES}}
    audit_stream_contract(contract_image); checks += 1
    for at in range(32, 64):
        for bit in range(8):
            broken = bytearray(contract_image["payload"]); broken[at] ^= 1 << bit
            try:
                audit_stream_contract(dict(contract_image, payload=broken))
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Corrupt stream controller footer admitted")
    for name in contract_image["symbols"]:
        broken_symbols = dict(contract_image["symbols"]); broken_symbols[name] += 4
        try:
            audit_stream_contract(dict(contract_image, symbols=broken_symbols))
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Corrupt stream controller symbol admitted")
    for broken in (dict(contract_image, memory_end=bss_end + 4),
                   dict(contract_image, payload=contract_image["payload"][:-1]),
                   dict(contract_image, payload=contract_image["payload"] + bytes(4))):
        try:
            audit_stream_contract(broken)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Corrupt stream controller payload/bounds admitted")
    for at in checked_bytes:
        for bit in range(8):
            broken = bytearray(payload); broken[at] ^= 1 << bit
            try:
                audit_bridges(dict(image, payload=broken), sizes)
            except ValueError:
                checks += 1
            else:
                raise AssertionError(f"Bridge mutation admitted: byte {at}, bit {bit}")
    for name in (CALL, DEFERRED, OWNER_DEFERRED):
        for address, size in ((sizes[name][0], sizes[name][1] + 2),
                              (sizes[name][0] + 2, sizes[name][1]),
                              (LIMIT, sizes[name][1])):
            broken = dict(sizes); broken[name] = (address, size)
            broken_symbols = dict(symbols); broken_symbols[name] = address
            try:
                audit_bridges(dict(image, symbols=broken_symbols), broken)
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Changed bridge span admitted")
    for target in (DISPATCH, ATTEMPT):
        for address in (0, symbols[target] + 2, LIMIT):
            broken_symbols = dict(symbols); broken_symbols[target] = address
            try:
                audit_bridges(dict(image, symbols=broken_symbols), sizes)
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Changed bridge target admitted")
    native_calls = {name: [(CALL[1:], "armada_ce_excerpt.c:1")] for name in NATIVE_CALLERS}
    audit_native_callers(native_calls); checks += 1
    for missing in NATIVE_CALLERS:
        try:
            audit_native_callers({source: edges for source, edges in native_calls.items() if source != missing})
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Missing admitted native caller passed")
    for forbidden in EXCERPT_NATIVE_PATH - NATIVE_CALLERS:
        try:
            audit_native_callers(dict(native_calls, **{forbidden: [(CALL[1:], "armada_ce_excerpt.c:1")]}))
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Extra native caller admitted")
    owner_calls = {OWNER_CALLBACK[1:]: [(OWNER_DEFERRED[1:], "armada_ce_excerpt.c:1")]}
    audit_owner_callers(owner_calls); checks += 1
    for broken in ({}, {DISPATCH[1:]: [(OWNER_DEFERRED[1:], "armada_ce_excerpt.c:1")]},
                   {OWNER_CALLBACK[1:]: [(OWNER_DEFERRED[1:], "unreviewed.c:1")]},
                   {OWNER_CALLBACK[1:]: owner_calls[OWNER_CALLBACK[1:]] * 2},
                   dict(owner_calls, **{ATTEMPT[1:]: [(OWNER_DEFERRED[1:], "armada_ce_excerpt.c:1")]})):
        try:
            audit_owner_callers(broken)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Unexpected owner bridge route admitted")
    stream_owner_calls = {source: [(OWNER_DEFERRED[1:], "armada_ce_stream_player.c:1")]
                          for source in (OWNER_CALLBACK[1:], "kui_armada_ce_stream_worker")}
    audit_owner_callers(stream_owner_calls, stream=True); checks += 1
    for broken in ({}, owner_calls,
                   {OWNER_CALLBACK[1:]: stream_owner_calls[OWNER_CALLBACK[1:]]},
                   {"kui_armada_ce_stream_worker": stream_owner_calls["kui_armada_ce_stream_worker"]},
                   dict(stream_owner_calls, **{ATTEMPT[1:]: [(OWNER_DEFERRED[1:], "armada_ce_stream_player.c:1")]})):
        try:
            audit_owner_callers(broken, stream=True)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Unexpected stream owner bridge route admitted")
    # The cache reader is pinned without expanding the bridge's FPU waiver.
    # Mutate its whole caller (including P2 mask and both literal targets),
    # every leaf bit, symbol bounds and sizes.
    cache_start = BASE + len(ARRAY_READ_BYTES)
    ram_target = cache_start + len(CACHE_READ_BYTES)
    cache_code = bytearray(CACHE_READ_BYTES)
    for offset, target_name in CACHE_READ_RELOCS:
        cache_code[offset:offset + 4] = struct.pack("<I", BASE if target_name == ARRAY_READ else ram_target)
    cache_image = {"payload": ARRAY_READ_BYTES + cache_code + bytes(2),
                   "symbols": {ARRAY_READ: BASE, "_read_cache": cache_start,
                               "_kui_armada_ce_ram_p1": ram_target}}
    cache_sizes = {ARRAY_READ: (BASE, len(ARRAY_READ_BYTES)),
                   "_read_cache": (cache_start, len(CACHE_READ_BYTES))}
    audit_cache_reader(cache_image, cache_sizes); checks += 1
    for at in range(len(ARRAY_READ_BYTES) + len(CACHE_READ_BYTES)):
        for bit in range(8):
            broken = bytearray(cache_image["payload"]); broken[at] ^= 1 << bit
            try:
                audit_cache_reader(dict(cache_image, payload=broken), cache_sizes)
            except ValueError:
                checks += 1
            else:
                raise AssertionError(f"Cache reader mutation admitted: byte {at}, bit {bit}")
    for name in (ARRAY_READ, "_read_cache"):
        for address, size in ((cache_sizes[name][0], cache_sizes[name][1] + 2),
                              (cache_sizes[name][0] + 2, cache_sizes[name][1]),
                              (LIMIT, cache_sizes[name][1])):
            broken_sizes = dict(cache_sizes); broken_sizes[name] = (address, size)
            broken_symbols = dict(cache_image["symbols"]); broken_symbols[name] = address
            try:
                audit_cache_reader(dict(cache_image, symbols=broken_symbols), broken_sizes)
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Cache reader span mutation admitted")
    stage = {"payload": SEED_HEADER + bytes(PCM_BYTES),
             "symbols": {"_armada_excerpt_seed": STAGE}}
    audit_seed(stage); checks += 1
    for at in list(range(SEED_BYTES)) + [SEED_BYTES, SEED_BYTES + PCM_BYTES // 2, len(stage["payload"]) - 1]:
        for bit in range(8):
            broken = bytearray(stage["payload"]); broken[at] ^= 1 << bit
            try:
                audit_seed(dict(stage, payload=broken))
            except ValueError:
                checks += 1
            else:
                raise AssertionError("Nonblank or malformed seed admitted")
    try:
        audit_seed(dict(stage, payload=stage["payload"] + SEED_HEADER))
    except ValueError:
        checks += 1
    else:
        raise AssertionError("Duplicate typed seed admitted")
    stream_stage = {"payload": STREAM_SEED_HEADER + bytes(4096),
                    "symbols": {"_armada_excerpt_seed": STAGE}}
    audit_seed(stream_stage, stream=True); checks += 1
    # The full profile ships metadata and a blank wire map. No preloaded
    # proprietary PCM or already-filled card map may pass this admission.
    for at in range(len(stream_stage["payload"])):
        for bit in range(8):
            broken = bytearray(stream_stage["payload"]); broken[at] ^= 1 << bit
            try:
                audit_seed(dict(stream_stage, payload=broken), stream=True)
            except ValueError:
                checks += 1
            else:
                raise AssertionError(f"Nonblank or malformed CEX2 seed admitted: byte {at}, bit {bit}")
    for broken in (dict(stream_stage, payload=stream_stage["payload"][:-1]),
                   dict(stream_stage, payload=stream_stage["payload"] + STREAM_SEED_HEADER),
                   dict(stream_stage, symbols={"_armada_excerpt_seed": STAGE + 2})):
        try:
            audit_seed(broken, stream=True)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Changed CEX2 seed bounds/uniqueness admitted")
    for fixture, stream_profile in ((stream_stage, False), (stage, True)):
        try:
            audit_seed(fixture, stream=stream_profile)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("CEX1/CEX2 profile mismatch admitted")
    # A huge native-attempt frame is irrelevant on the masked GD stack,
    # but even a zero-byte accidental edge into it must fail, not silently
    # turn native API calls into assumed leaf frames.
    frames = {name: 0 for values in CE_INDIRECT.values() for name in values}
    frames.update({"kui_retail_resident_dispatch": 60, "kui_retail_ce_irq": 12,
                   "kui_retail_menu_return": 16, "excerpt:kui_armada_ce_bind_probe": 24,
                   "excerpt:entry.c:bind": 20, "excerpt:entry.c:kui_armada_ce_live_word": 8,
                   "excerpt:entry.c:kui_armada_ce_live_physical": 20,
                   "excerpt:entry.c:kui_armada_ce_live_mapped": 16,
                   "excerpt:entry.c:read_ram": 12, "excerpt:entry.c:read_mapped": 76,
                   "excerpt:entry.c:read_cache": 16, "excerpt:kui_armada_ce_excerpt_array_read": 0,
                   "excerpt:kui_armada_ce_excerpt_queue": 4,
                   "excerpt:kui_armada_ce_excerpt_snapshot": 0,
                   "excerpt:kui_armada_ce_excerpt_attempt": 100000})
    calls = {"kui_retail_resident_dispatch": [("excerpt:kui_armada_ce_bind_probe", "x.c:1")],
             "kui_retail_menu_return": [("excerpt:kui_armada_ce_bind_probe", "x.c:1")],
             "excerpt:kui_armada_ce_bind_probe": [("excerpt:entry.c:bind", "x.c:1"),
                 ("excerpt:kui_armada_ce_excerpt_queue", "x.c:1"),
                 ("excerpt:kui_armada_ce_excerpt_snapshot", "x.c:1")],
             "excerpt:entry.c:bind": [("excerpt:entry.c:kui_armada_ce_live_word", "x.c:1")],
             "excerpt:entry.c:kui_armada_ce_live_word": [("__indirect_call", "armada_ce_ram_bind.h:1")],
             "excerpt:entry.c:kui_armada_ce_live_physical": [("__indirect_call", "armada_ce_live_sound.h:1")],
             "excerpt:entry.c:kui_armada_ce_live_mapped": [("__indirect_call", "armada_ce_live_sound.h:1")],
             "excerpt:entry.c:read_mapped": [("excerpt:entry.c:read_cache", "armada_ce_cache_word.h:1")],
             "excerpt:entry.c:read_cache": [("__indirect_call", "armada_ce_excerpt_entry.c:1")]}
    result = worst(frames, calls, ce=True, armada_bind=True, armada_excerpt=True)
    if result["kui_retail_resident_dispatch"]["bytes"] != 220:
        raise AssertionError("Mapped/cache read callbacks omitted from masked stack")
    checks += 1
    for missing in ("read_ram", "read_mapped", "read_cache", "kui_armada_ce_live_mapped",
                    "kui_armada_ce_live_physical", "kui_armada_ce_excerpt_array_read"):
        broken = {f: n for f, n in frames.items() if base(f) != missing}
        try:
            worst(broken, calls, ce=True, armada_bind=True, armada_excerpt=True)
        except ValueError as error:
            if "Missing callback frame" not in str(error):
                raise
            checks += 1
        else:
            raise AssertionError("Missing cache/mapped callback admitted")
    for caller, label in (("read_cache", "armada_ce_live_sound.h:1"),
                          ("kui_armada_ce_live_word", "armada_ce_excerpt_entry.c:1"),
                          ("kui_armada_ce_live_physical", "unreviewed.h:1")):
        broken = dict(calls)
        broken["excerpt:entry.c:" + caller] = [("__indirect_call", label)]
        try:
            worst(frames, broken, ce=True, armada_bind=True, armada_excerpt=True)
        except ValueError as error:
            if "Unexpected" not in str(error):
                raise
            checks += 1
        else:
            raise AssertionError("Unexpected cache/mapped callback site admitted")
    for forbidden in EXCERPT_NATIVE_PATH:
        broken = dict(calls)
        broken["excerpt:kui_armada_ce_bind_probe"] = calls["excerpt:kui_armada_ce_bind_probe"] + [
            ("excerpt:" + forbidden, "x.c:1")]
        try:
            worst(frames, broken, ce=True, armada_bind=True, armada_excerpt=True)
        except ValueError as error:
            if "Native excerpt path reached private GD stack" not in str(error):
                raise
            checks += 1
        else:
            raise AssertionError("Native path admitted on masked stack")
    checks += stream_code_self_test()
    for profile in ({}, {"bam4": True}, {"worms": True}, {"wwp": True}):
        checks += stream_owner_self_test(**profile)
        checks += stream_boot_self_test(**profile)
    for profile in ({"bam4": True}, {"worms": True}, {"wwp": True}):
        try:
            check(Path('/nonexistent-cdda-profile-audit'), **profile)
        except ValueError as error:
            if 'explicit stream audit' not in str(error):
                raise
            checks += 1
        else:
            raise AssertionError('Title profile admitted through excerpt audit')
    for profile in ({"bam4": True, "worms": True}, {"bam4": True, "wwp": True},
                    {"worms": True, "wwp": True}, {"bam4": True, "worms": True, "wwp": True}):
        for selector in (stream_owner_bodies, stream_boot_bodies,
                         lambda **kwargs: check(Path('/nonexistent-cdda-profile-audit'),
                                                stream=True, **kwargs)):
            try:
                selector(**profile)
            except ValueError as error:
                if 'Select only one' not in str(error):
                    raise
                checks += 1
            else:
                raise AssertionError('Multiple title profiles admitted')
    frames.update({callback: 0 for callback in STREAM_BUS_CALLBACKS})
    frames["excerpt:transport.c:write_audio"] = 0
    # The generic and urgent-worker bridges share one integer-only helper.
    # Count both real wrappers and the helper before crossing to the finite
    # resident SCI callback set; an unrelated callback site must still fail.
    pump = "excerpt:armada_ce_stream_transport.c:pump"
    ordinary_pump = "excerpt:kui_armada_ce_stream_transport_pump"
    worker_pump = "excerpt:kui_armada_ce_stream_transport_pump_worker"
    transport_frames = dict(frames, **{pump: 20, ordinary_pump: 4, worker_pump: 8,
                                      "kui_sci_stream_fetch": 36,
                                      "kui_sci_stream_wait": 9000})
    transport_calls = dict(calls, **{
        ordinary_pump: [(pump, "armada_ce_stream_transport.c:224:12")],
        worker_pump: [(pump, "armada_ce_stream_transport.c:233:12")],
        pump: [("__indirect_call", "armada_ce_stream_transport.c:187:5")]})
    pump_roots = {"kui_armada_ce_stream_transport_pump": 0,
                  "kui_armada_ce_stream_transport_pump_worker": 0}
    result = worst(transport_frames, transport_calls, ce=True, armada_bind=True,
                   armada_excerpt=True, armada_stream=True, roots_override=pump_roots)
    if (result["kui_armada_ce_stream_transport_pump"]["bytes"] != 60 or
            result["kui_armada_ce_stream_transport_pump_worker"]["bytes"] != 64):
        raise AssertionError("Transport pump wrapper/helper/SCI frames omitted or widened")
    checks += 1
    for label in ("unreviewed.c:1", "armada_ce_stream_transport.c:1"):
        broken = dict(transport_calls)
        if label.startswith("armada_ce_stream_transport.c"):
            # A callback moved into a wrapper is not an authorized helper
            # site, even though its source file and target set are familiar.
            broken[worker_pump] = [("__indirect_call", label)]
        else:
            broken[pump] = [("__indirect_call", label)]
        try:
            worst(transport_frames, broken, ce=True, armada_bind=True,
                  armada_excerpt=True, armada_stream=True, roots_override=pump_roots)
        except ValueError as error:
            if "Unexpected" not in str(error):
                raise
            checks += 1
        else:
            raise AssertionError("Unexpected transport pump callback site admitted")
    try:
        worst({f: size for f, size in transport_frames.items() if f != pump},
              transport_calls, ce=True, armada_bind=True, armada_excerpt=True,
              armada_stream=True, roots_override=pump_roots)
    except ValueError as error:
        if "without a frame" not in str(error):
            raise
        checks += 1
    else:
        raise AssertionError("Transport pump helper frame omitted")
    broken = dict(transport_calls)
    broken[pump] = transport_calls[pump] + [
        ("excerpt:kui_armada_ce_stream_native_sleep", "armada_ce_stream_transport.c:1")]
    try:
        worst(transport_frames, broken, ce=True, armada_bind=True,
              armada_excerpt=True, armada_stream=True, roots_override=pump_roots)
    except ValueError as error:
        if "Native excerpt path reached private GD stack" not in str(error):
            raise
        checks += 1
    else:
        raise AssertionError("Native call admitted through masked worker pump")
    for forbidden in STREAM_NATIVE_PATH:
        broken = dict(calls)
        broken["excerpt:kui_armada_ce_bind_probe"] = calls["excerpt:kui_armada_ce_bind_probe"] + [
            ("excerpt:" + forbidden, "x.c:1")]
        try:
            worst(frames, broken, ce=True, armada_bind=True, armada_excerpt=True, armada_stream=True)
        except ValueError as error:
            if "Native excerpt path reached private GD stack" not in str(error):
                raise
            checks += 1
        else:
            raise AssertionError("Stream native worker path admitted on masked GD stack")
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--stream", action="store_true", help="Admit only the reserved high-P1 stream controller")
    parser.add_argument("--bam4", action="store_true", help="Use the exact Bust-A-Move 4 US kernel profile")
    parser.add_argument("--worms", action="store_true", help="Use the exact Worms Armageddon US kernel profile")
    parser.add_argument("--wwp", action="store_true", help="Use the exact Worms World Party US kernel profile")
    parser.add_argument('--wwp-drive-link', type=Path, metavar='ELF',
                        help='Emit one checked WWP scalar linker assignment from the controller ELF')
    args = parser.parse_args()
    if args.wwp_drive_link:
        if args.directory or args.self_test or args.stream or args.bam4 or args.worms or args.wwp:
            parser.error('--wwp-drive-link is a separate controller-symbol operation')
        print(WWP_DRIVE_SYMBOL + ' = ' + hex(wwp_drive_status_symbol(args.wwp_drive_link.read_bytes())) + ';')
        return
    if sum((args.bam4, args.worms, args.wwp)) > 1:
        parser.error('Select only one WinCE title profile')
    if (args.bam4 or args.worms or args.wwp) and not args.stream:
        parser.error('Title profiles require --stream')
    if args.self_test:
        print(f"PASS Armada excerpt linked-auditor self-test: {self_test()} checks")
    if args.directory:
        print(json.dumps(check(args.directory, stream=args.stream, bam4=args.bam4,
                               worms=args.worms, wwp=args.wwp), indent=2))
    elif not args.self_test:
        parser.error("Provide a build directory or --self-test")


if __name__ == "__main__":
    main()
