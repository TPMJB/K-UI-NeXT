#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Metadata-only exact-owner contracts for port-A CE RX slice publication.

Does not export owner bytes/disassembly or establish a runtime allocation lease.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from armada_ce_maple_audit import identity, relocated_text
from armada_ce_maple_fence_owner_audit import source_audit as fence_audit
from armada_ce_maple_hook_owner_audit import hx, require, window
from armada_ce_rom_audit import ROM
from armada_pe_audio_audit import PE

IDENTITIES = (
    ('0WINCEOS.BIN',1253376,'a7628c78090fce2c93012d5e4db6142096e138dceb7ebbf555142e902ca03c1f'),
    ('WINCE/MAPLE.DLL',26191,'b8eb7afc2b13fcec8551af4fa75288fca1881f671181b88ea042aa9d77524e6e'),
    ('WINCE/MAPLEDEV.DLL',55381,'16dfc7b3bb6519af633e09e7886e36cbaf73c74ffc387c5c6d6369e26862ac28'))
WINDOWS = (
    ('maple.dll','initial_parent_receive_allocations',0x1cf2,0x1d44,
     'The uncached TX common buffer is4096 bytes. Four initial parent-discovery RX allocations request116 bytes each and are stored at extension+94+port*4; these have no live function provenance and are excluded by the slice query.'),
    ('maple.dll','generic_raw_frame_request_capacity',0x2e60,0x2f1c,
     'Request byte7 is zero-extended and multiplied by4 into function+10. A token resolves the unit/function. The driver rejects an already pending IRP, stores pending IRP+1c, payload/command fields and rebuild flag. The ordinary branch allocates function+0c from the pool; the explicit-physical branch instead writes+20 and clears+0c and is excluded.'),
    ('maple.dll','generic_completion_receive_lifetime',0x2bd4,0x2c4c,
     'The unchanged completion copies min(received payload+4, caller output capacity), frees the pooled RX using function+10, clears RX/capacity/pending/physical fields, rebuilds and completes the IRP. Publication must finish before this original consumer; an earlier allocation pointer must never be reused across visits.'),
    ('maple.dll','unit_and_function_factory',0x3564,0x36c4,
     'A unit is28 bytes, has byte8 function count and function pointers at12+4*i. Functions are244 bytes and start with zero command/expected/payload/included/RX/capacity/pending/physical fields. Unit creation sets rebuild and notifications.'),
    ('maple.dll','device_info_function_transition',0x3968,0x3ac0,
     'DEVINFO function bitmap is endian-converted and at most three functions are admitted. The first factory function is reused, its discovery RX is freed and cleared, and dispatch value2 follows the ordinary generic completion route. The existing controller dispatch1 remains separate.'),
    ('maple.dll','controller_poll_setup',0x3ec0,0x3f94,
     'Controller initializer chooses command9, one payload word and expected response8, replaces its RX allocation according to its controller data size and requests a list rebuild. Parent publication uses this normal live function path, not the initial discovery buffer.'),
    ('maple.dll','ordinary_tx_builder_and_receive_clear',0x582c,0x5a22,
     'The builder omits NULL units, iterates function byte8 count and marks active functions byte17 included. It writes control, RX VA+pool bias, command/destination/source/word count and outgoing payload, clears exactly function+10 bytes at function+0c, and publishes last/end. PortA unit2 encodes wire destination02. No appended synthetic descriptor is needed or permitted.'),
    ('maple.dll','attached_unit_creation_and_devinfo_capacity',0x5a2c,0x5b0c,
     'A changed parent mask is copied to main-unit byte24. Newly set bit1 creates unit index2, assigns command1/expected5/no payload and exact116-byte RX capacity, allocates and clears that pooled RX. New included byte17 remains zero until the next real list rebuild.'),
    ('maple.dll','ordinary_reply_walker_attachment_and_dispatch',0x5b0c,0x5d28,
     'The consumer visits live units and included functions, dereferences function+0c, treats FFFFFFFF as missing response, and extracts attached bits from reply byte2&1f. For main unit0 a changed mask calls5a2c; response5 for dispatch0 calls3968; other storage dispatch uses2bd4. The body and interrupt completion remain original.'),
    ('maple.dll','uncached_receive_pool_initialization',0x5e60,0x5f48,
     'Pool size is8192 bytes from HalAllocateCommonBuffer cache0, independently allocated from TX. Pool low/high/originalVA/bias and page cursors are driver-owned. Runtime must independently repeat both uncached RX PTE witnesses and reject workspace/TX overlap.'),
    ('maple.dll','receive_bucket_allocator',0x5f78,0x6168,
     'Requested sizes select32,64,256,512,1024-byte buckets using strict greater-than thresholds32,64,256,512. A matching bucket freelist record is popped or a fresh bucket is taken from a page cursor. Requested capacity0 is excluded by the authored query. Generic request byte7 caps requested capacity at1020.'),
    ('maple.dll','receive_bucket_free_and_record_storage',0x6168,0x6274,
     'Free reconstructs missing VA/physical through pool bias and puts a12-byte record onto the selected bucket freelist. Pool+54 stores spare bookkeeping records. It is not an active allocation list: fresh allocations are not represented there. Live included function ownership and exact requested capacity are the usable publication provenance.'),
    ('maple.dll','bounded_receive_pool_pages',0x6274,0x62b4,
     'Page supplier refuses assigned bytes+4096 greater than8192, returns the next physical and virtual page and advances both by4096. Every bucket is at least32 bytes and at most1024, wholly within an admitted pool page.'),
    ('mapledev.dll','memcard_read_write_phase_geometry',0x38a8,0x3934,
     'MAPLEDEV decodes media function geometry and read/write phase counts. This exact owner path is relevant to the read-only emulator phase and block bounds, independently of persistent writes.'),
    ('mapledev.dll','memcard_command13_finalizer',0x42a0,0x4324,
     'Owner finalizer submits MEMCARD command13 with the block location and expects response7. A read-only transport may acknowledge only the independently admitted read-finalization form; this does not admit write phases or durable commits.'),
    ('mapledev.dll','single_phase_read_calls_finalizer',0x7d0c,0x7ea0,
     'Owner read path performs a single read phase and calls the command13 finalizer with phase0. Returning only block-read data is insufficient for the owner API to finish the read successfully.'),
)
MAPLEDEV_WINDOWS = (
    (0x38a8,0x3934,'25fd08566ca81c11058df793fa35db771490008080b1e7a5d69173f6770086b8'),
    (0x42a0,0x4324,'64d8eee74cfa8cdc07fcafbe723efadf4636af4620ad17ba353fb0c81152071e'),
    (0x7d0c,0x7ea0,'c64c142c3d73960ec0fd965fa49f4977a7fe8a969177c628596c088964edfee1'))


def source_audit(startup_dir):
    root=Path(startup_dir)
    identities=[identity(root/name,name) for name,_,_ in IDENTITIES]
    for result,(name,size,digest) in zip(identities,IDENTITIES):
        require((result['bytes'],result['sha256']),(size,digest),'Exact owner identity '+name)
    baseline=fence_audit(root)
    rom=ROM(root/'0WINCEOS.BIN')
    maple=rom.modules['maple.dll']
    def word(rva):
        return struct.unpack('<H',rom.rva_read(maple,rva,2))[0]
    checks=((0x2ec2,0x19a4),(0x2ed4,0x5594),(0x2ee4,0x1948),(0x2ee6,0x1913),
            (0x5926,0xc017),(0x593e,0x5583),(0x5962,0x5684),(0x5966,0x5483),
            (0x5a8e,0xe174),(0x5aa4,0x1914),(0x5aaa,0xe574),
            (0x5bd6,0xe017),(0x5be0,0x5b83),(0x5c2e,0x84b2),(0x5c32,0xc91f),
            (0x5f96,0x9183),(0x5fb6,0x9174),(0x5fd6,0xe140),(0x5ff6,0xe120),
            (0x6276,0x921b),(0x627a,0x9318))
    for at,expected in checks:
        require(word(at),expected,'Reviewed capacity/attachment instruction '+hx(at))
    pe=PE(root/'WINCE/MAPLEDEV.DLL')
    pe_windows=[]
    for start,end,digest in MAPLEDEV_WINDOWS:
        data=pe.read(pe.rva_offset(start,end-start),end-start)
        actual=hashlib.sha256(data).hexdigest()
        require(actual,digest,'MAPLEDEV external source window '+hx(start))
        pe_windows.append(dict(rva=hx(start),end_rva_exclusive=hx(end),bytes=end-start,sha256=actual))
    reused=hashlib.sha256(json.dumps(baseline,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    return dict(format='armada-ce-maple-rx-slice-owner-contracts-v1',license='GPL-3.0-only',
        scope='Metadata-only exact-source evidence for live included-function pooled RX provenance and parent/A2 read-only response publication. No owner instructions, binaries or disassembly are exported.',
        source_identities=identities,reused_fence_contract=dict(tool='armada_ce_maple_fence_owner_audit.py',canonical_audit_json_sha256=reused),
        external_text_equivalence=dict(maple=baseline['external_text_equivalence'],mapledev=relocated_text(rom,'mapledev.dll',pe)),
        source_contracts=[window(rom,*spec) for spec in WINDOWS],external_mapledev_read_finalizer_windows=pe_windows,
        allocation_contract=dict(extension_bytes=0x198,unit_bytes=28,function_bytes=0xf4,
            unit_pointer_offset='4 + port*32 + unit_index*4',function_count_offset=8,function_pointer_offset='12 + function_index*4',
            function_fields=dict(dispatch=0,rx_virtual=12,requested_capacity=16,command=20,expected_response=21,payload_words=22,included=23,pending_irp=28,explicit_rx_physical=32),
            generic_requested_capacity='zero_extend(request.byte7) * 4',generic_max_requested_capacity=1020,
            discovery_requested_capacity=116,buckets=[32,64,256,512,1024],receive_pool_bytes=8192,receive_page_bytes=4096,
            pool_spare_record_head_offset=0x54,pool_has_active_allocation_list=False,
            source_lifetime='Match a current unit/function and current built descriptor while DMA is disabled and the uniprocessor helper has IMASK15. The original consumer owns/free-clears the allocation after publication; retain no RX pointer for the next visit.'),
        publication_contract=dict(ports=[0],unit_indices=[0,2],a2_wire_address=2,parent_advertise_mask=2,
            initial_parent_discovery_excluded=True,append_descriptors=False,source_reply_consumer_unchanged=True,
            required_checks=['Accepted exact-owner traffic capture and acquired DMA-off fence.','Repeat context, extension geometry, four admitted mappings and target descriptor/RX header after acquisition.',
                'Coherently follow live unit and at most three functions; require exactly one RX match, included1 and exact command/word-count.',
                'Require nonzero pooled RX VA, zero explicit physical RX field, valid requested capacity at least the staged response length.',
                'Match RX VA+source pool bias to descriptor bus; require exact uncached pool VA offset and whole allocated bucket within pool.',
                'Retain/recheck all metadata PTE witnesses; reject dirty aliases, mutations, read failures and other current RX headers inside target bucket.',
                'Stage complete response privately, requery and compare proof, then store payload before header; advertise validated parent last.',
                'Validate real A controller response and absence of physical A2; preserve A1 and all non-target words.','Allow only read-only protocol shapes with independently admitted response lengths; no write or SD persistence.']),
        limitations=['Runtime still must hold the tested fence and IMASK15 until publication; repeated reads alone grant no lease.',
            'Source allocator disjointness is inherited from the exact driver; a source live function pointer is not an arbitrary game-owned frame.',
            'The pure slice query exports metadata and performs no guest writes; publisher and protocol tests remain independently required.',
            'Root-controller/driver unload, asynchronous worker termination and concurrent lifecycle initialization remain outside this test.',
            'No source or design is borrowed from DreamShell or SWAT.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-dir',type=Path,required=True)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--check',type=Path)
    args=parser.parse_args()
    try:
        if args.output and args.output.resolve() in [(args.startup_dir/n).resolve() for n,_,_ in IDENTITIES]:
            raise ValueError('Metadata output must not overwrite an owner input')
        result=source_audit(args.startup_dir)
        if args.check and json.loads(args.check.read_text())!=result:
            raise ValueError('Checked-in RX slice contracts differ from exact owner audit')
        if args.output: args.output.write_text(json.dumps(result,indent=2)+'\n')
        elif args.check: print(json.dumps(dict(matched=True,source_contracts=len(result['source_contracts']),owner_bytes_exported=False)))
        else: print(json.dumps(result,indent=2))
    except (OSError,ValueError,UnicodeError,KeyError,struct.error) as error:
        parser.exit(1,'RX slice owner audit failed: '+str(error)+'\n')


if __name__=='__main__':
    main()
