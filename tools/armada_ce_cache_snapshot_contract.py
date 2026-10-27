#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Complete native-byte contract for K-UI's authored P2 snapshot leaf.

Only the shared job's absolute address and the explicitly selected private
reservation lower bound are relocated. The caller must also
prove that symbol is a typed60-byte object inside its private P2 workspace.
The instruction model tests the complete body, all255 masks, cache aliases,
changed tags/data, failure write bounds and callee-register preservation.
"""
import hashlib
import struct

LEAF_SYMBOL='_kui_armada_ce_cache_snapshot'
JOB_SYMBOL='_kui_armada_ce_cache_snapshot_job'
STACK_BYTES=40
JOB_BYTES=60
JOB_LITERAL_OFFSET=416
PRIVATE_LITERAL_OFFSET=428
INSTRUCTION_BYTES=JOB_LITERAL_OFFSET
BODY=bytes.fromhex(
    '67d00034028903e00b000900862f962fa62fb62fc62fd62ff07f4368805481558256835d43605b201fc82f8b'
    '11442d8b5cd002352a8b5cd0063527895bd00235028b5bd00235218b43605a2059d118201c8b636001c81989'
    '57d06361092157d0536209221032118bd36008200e89194008200b8b020052d119201030068b51d11260a1c9'
    '01880a8b11a0090080500c1881500d1882500e1803e070a0090049d11c1812600d1801e00e1868a009005369'
    '45d0092945d00b29536a45d0092a436b44d0092b9b2b536c43d00b2c636508e0092542d3fce2f364936604e7'
    '626039200224047401c8108903612921a0310c8b02c8038958253789b036358b58250489b036028b636c36d11c3c'
    '36d11c361047e48b83641074c365d36608e70146018b52600224047404751047f78bf364936504e7526039204661'
    '10301c8b29d22c351047f68b83641074c365d36608e70146038b5260426110300d8b047404751047f58b00e00ba0'
    '09006c180d1882500e1801e004a009005c180d181e1802e009000900090009000900090009000900107ff66df66c'
    'f66bf66af669f6680b00090000f0fcac0000018ce0ffff8c00a0fc8c0000fd8cff0f000000f0ffff00f0ff1ff000'
    '00001c0000ffe00f0000000000f400fcff1f003000000000002003fcff1f0000000100100000')
assert len(BODY)==488


def expected_body(job_address,write_layout=False):
    if type(write_layout) is not bool:
        raise ValueError('Snapshot layout must select an explicit supported profile')
    private_begin=0x8cfc4000 if write_layout else 0x8cfca000
    if job_address&3 or not private_begin+0x20000000<=job_address<=0xacfcf800-JOB_BYTES:
        raise ValueError('P2 snapshot job leaves the private workspace/stack boundary')
    body=bytearray(BODY)
    struct.pack_into('<I',body,JOB_LITERAL_OFFSET,job_address)
    struct.pack_into('<I',body,PRIVATE_LITERAL_OFFSET,private_begin)
    return bytes(body)


def check(body,job_address,write_layout=False):
    if body!=expected_body(job_address,write_layout):
        raise ValueError('Complete P2 snapshot leaf differs from its authored byte contract')
    return {'leaf_bytes':len(body),'leaf_sha256':hashlib.sha256(body).hexdigest(),
        'job_bytes':JOB_BYTES,'job_p2':hex(job_address),'job_literal_offset':JOB_LITERAL_OFFSET,
        'private_stack_bytes':STACK_BYTES,'complete_bytes_pinned':True,'callbacks':False,
        'callee_saved_registers':['r8','r9','r10','r11','r12','r13','sp'],
        'sr_mode_and_mask_preserved':True,'condition_flag_unspecified':True,
        'array_to_cached_return_nops':8,'selected_data_reads_per_success':2,
        'tag_scans_per_success':2,'tag_colors_per_scan':4,'native_hardware_validation_pending':True}
