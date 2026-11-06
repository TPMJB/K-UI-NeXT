#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source-bound Sega Swirl CE owner metadata; emits no retail code bytes.

This binds the complete original and its stored components. It grants no
runtime memory, hook or I/O authority; the live gates remain mandatory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zlib
from armada_ce_rom_audit import ROMError, ROMInventory

KERNEL_BYTES = 1828864
KERNEL_SHA256 = 'a19080f2e243c04e01a3a473c7a3caa36fb85a5a8fbb2163c8f7a1b2c8ed5ea8'
MODULES = {'nk.exe': {'nominal_virtual_base': 2348875776,
            'entry_rva': 8384,
            'virtual_bytes': 196608,
            'e32_address': 2349199248,
            'o32_address': 2349223776,
            'load_address': 2348875776,
            'sections': [{'index': 0,
                          'virtual_bytes': 121576,
                          'rva': 4096,
                          'stored_bytes': 121856,
                          'data_address': 2348879872,
                          'real_address_field': 69632,
                          'flags': 1610612768,
                          'compressed': False,
                          'stored_sha256': '6f90e780f6e407ea89152332eef4137d64a12eed0d527934131de4ed56640e2a'},
                         {'index': 1,
                          'virtual_bytes': 7168,
                          'rva': 126976,
                          'stored_bytes': 0,
                          'data_address': 2349001448,
                          'real_address_field': 192512,
                          'flags': 3221225600,
                          'compressed': False,
                          'stored_sha256': 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'},
                         {'index': 2,
                          'virtual_bytes': 41096,
                          'rva': 135168,
                          'stored_bytes': 1261,
                          'data_address': 2349001448,
                          'real_address_field': 200704,
                          'flags': 3221225536,
                          'compressed': False,
                          'stored_sha256': 'b9a9d91a118489912bc9f5a19daba8d6645354605ebf13c4af4e89c3b3b11ae2'},
                         {'index': 3,
                          'virtual_bytes': 4472,
                          'rva': 126976,
                          'stored_bytes': 4608,
                          'data_address': 2349002752,
                          'real_address_field': 192512,
                          'flags': 1073741888,
                          'compressed': False,
                          'stored_sha256': 'c31241765d5c9fc6b2760f912c69f1837d4ec6879ac04ca9231acc3230ff8146'}]},
 'coredll.dll': {'nominal_virtual_base': 30801920,
                 'entry_rva': 11820,
                 'virtual_bytes': 212992,
                 'e32_address': 2349227880,
                 'o32_address': 2349472832,
                 'load_address': 2349465600,
                 'sections': [{'index': 0,
                               'virtual_bytes': 183492,
                               'rva': 4096,
                               'stored_bytes': 183808,
                               'data_address': 2349010944,
                               'real_address_field': 64360448,
                               'flags': 1610612768,
                               'compressed': False,
                               'stored_sha256': 'a3ecf61e8d7beedacb2c4b7cc51ff77962c224acff46b354793158c65fcc5790'},
                              {'index': 1,
                               'virtual_bytes': 356,
                               'rva': 188416,
                               'stored_bytes': 8,
                               'data_address': 2349002709,
                               'real_address_field': 64544768,
                               'flags': 3224371264,
                               'compressed': False,
                               'stored_sha256': '9402bb655bc3b7331a00f1219ef647565bbe517c74aa0e41026885785867d1cd'},
                              {'index': 2,
                               'virtual_bytes': 6216,
                               'rva': 192512,
                               'stored_bytes': 4680,
                               'data_address': 2350460928,
                               'real_address_field': 64548864,
                               'flags': 1073750080,
                               'compressed': True,
                               'stored_sha256': 'a6e500eb85855f4fc4bb06fc40bf08600132c499bb6f875c1d9aedacdbe73fa5'},
                              {'index': 3,
                               'virtual_bytes': 536,
                               'rva': 200704,
                               'stored_bytes': 1024,
                               'data_address': 2349195264,
                               'real_address_field': 64557056,
                               'flags': 1073741888,
                               'compressed': False,
                               'stored_sha256': 'f5f7184beaaa6cfb0695fb11efc37b21b94543a7460ef25459ea7a2f1cd3a130'}]},
 'wsegacd.dll': {'nominal_virtual_base': 30343168,
                 'entry_rva': 23012,
                 'virtual_bytes': 36864,
                 'e32_address': 2349494024,
                 'o32_address': 2349494124,
                 'load_address': 2349899776,
                 'sections': [{'index': 0,
                               'virtual_bytes': 19827,
                               'rva': 4096,
                               'stored_bytes': 19968,
                               'data_address': 2349514752,
                               'real_address_field': 63901696,
                               'flags': 1610612768,
                               'compressed': False,
                               'stored_sha256': '6c60b3c34f8d578621f3c72c99ece98e514411cd3e634856916b08f5d1eef8bf'},
                              {'index': 1,
                               'virtual_bytes': 352,
                               'rva': 24576,
                               'stored_bytes': 261,
                               'data_address': 2349198317,
                               'real_address_field': 63922176,
                               'flags': 3221233728,
                               'compressed': True,
                               'stored_sha256': 'd98fd5d476cc080b4dc8e4888b5dbf33084796e5c4a3cb9a013fb052d413407b'},
                              {'index': 2,
                               'virtual_bytes': 896,
                               'rva': 28672,
                               'stored_bytes': 667,
                               'data_address': 2349198578,
                               'real_address_field': 63926272,
                               'flags': 1073750080,
                               'compressed': True,
                               'stored_sha256': 'cde80e31f96a18b8aee836861f1106c8e1ee4536b8d68ec49b4513af150e56bd'}]},
 'maple.dll': {'nominal_virtual_base': 30277632,
               'entry_rva': 30912,
               'virtual_bytes': 45056,
               'e32_address': 2349498196,
               'o32_address': 2349494196,
               'load_address': 2349940736,
               'sections': [{'index': 0,
                             'virtual_bytes': 27746,
                             'rva': 4096,
                             'stored_bytes': 28160,
                             'data_address': 2349535232,
                             'real_address_field': 63836160,
                             'flags': 1610612768,
                             'compressed': False,
                             'stored_sha256': '9894cf5092cd1accad7f7f6b420d4baaec94ec3c58efbdc80ab176c9818ef108'},
                            {'index': 1,
                             'virtual_bytes': 704,
                             'rva': 32768,
                             'stored_bytes': 173,
                             'data_address': 2349223332,
                             'real_address_field': 63864832,
                             'flags': 3221233728,
                             'compressed': True,
                             'stored_sha256': '4f41a66998bb7cd6cd810f1637e61b03b630e84b62bdba36e7fe1304520585d7'},
                            {'index': 2,
                             'virtual_bytes': 672,
                             'rva': 36864,
                             'stored_bytes': 549,
                             'data_address': 2349224108,
                             'real_address_field': 63868928,
                             'flags': 1073750080,
                             'compressed': True,
                             'stored_sha256': 'a5a390c2b78832bbd10e66a218be820bd74e451278235cae2efb9c932afd8940'}]},
 'mapledev.dll': {'nominal_virtual_base': 29425664,
                  'entry_rva': 55256,
                  'virtual_bytes': 69632,
                  'e32_address': 2349534580,
                  'o32_address': 2349534680,
                  'load_address': 2350526464,
                  'sections': [{'index': 0,
                                'virtual_bytes': 51931,
                                'rva': 4096,
                                'stored_bytes': 52224,
                                'data_address': 2349912064,
                                'real_address_field': 62984192,
                                'flags': 1610612768,
                                'compressed': False,
                                'stored_sha256': 'd95609f9aea62794ec67baa9b318c5242f8fa46278de16cb9beea1116a39a2e9'},
                               {'index': 1,
                                'virtual_bytes': 1380,
                                'rva': 57344,
                                'stored_bytes': 385,
                                'data_address': 2349431320,
                                'real_address_field': 63037440,
                                'flags': 3221233728,
                                'compressed': True,
                                'stored_sha256': '14483959ae28aa0dc0da3e924b7eb3bfd24fdd20fd13a71376c0bf6a667f1612'},
                               {'index': 2,
                                'virtual_bytes': 2032,
                                'rva': 61440,
                                'stored_bytes': 1564,
                                'data_address': 2349490244,
                                'real_address_field': 63041536,
                                'flags': 1073750080,
                                'compressed': True,
                                'stored_sha256': '978dfa158b63d7897a62a47862fbc06699684e60c1c0a5fb6e1bf74baa7cb44c'}]}}
CONTRACTS = (('completion_entry',
  28568,
  32,
  '341964a5ca5e3599f9efd9726f035e78bfcf8203e68aaf56ffd2e40032987677',
  3746979950),
 ('completion_walker',
  28568,
  616,
  '9b1b6321bf52dcbee740ab3fa0674b12c79c07fd1f83084401fdbd38397d9009',
  2673235994),
 ('completion_notification',
  28568,
  72,
  '1e83627db4fdc3cfcf51d2f53c8f9ae7d33ed93d95b1e0391c3d1e8d7184395f',
  2444839464),
 ('worker_loop', 29184, 160, '385398ec000deaf1b2cfa456bc4e4ecba2658be330d577cf0424a0aed53a9ed6', 3348937119),
 ('worker_completion_call',
  29278,
  48,
  '68578ca5edbb1092e8dc1aedeec97c07831db1548a03ad950f834d69a40d11a1',
  3040557707),
 ('worker_literals', 29344, 48, '0a8e4e0d9a6be4d93ec4d74926e84dd42595e6296cf6c0e1453ea5e390e65b27', 81713175),
 ('list_builder', 27832, 512, '655f7103f0b242f73992424487c56aa707240c12154ca2aa52114fd7b6ad4174', 2723980745),
 ('root_load', 6154, 58, 'e89780d1598bec8ce702dc9deeb3ca4c4d26678b779e2ccb3fd52525d4ff0bbc', 2011437577),
 ('SetKMode_thunk',
  30780,
  12,
  '19bb0f7377a6c71f53ee97642f5184cbef8e810363b0ca74d18926c58ed3e62c',
  2812049199),
 ('ordinary_worker_create',
  7400,
  232,
  'f4367f8e917361807f18e15bb9f217a53a0493154f0f951a3ef7a7f4fe3670d7',
  1408945755),
 ('ordinary_worker_shutdown',
  7006,
  68,
  '9df70aee00a88b7b16dc200fc20155c3211c9fae8f85b312fb3c44cdfbb06561',
  2539517533),
 ('WaitForSingleObject_thunk',
  30684,
  12,
  '42d0bf80006c6f2a238dba221a2cd5faf51df50d19920dd86853897e0e6c0349',
  2629468204),
 ('main_worker_wrapper',
  6664,
  24,
  'a7fd0c1406b63a4053bb249f99ec7f8bc4c974f8ef47e7777ef4005ffe34a12c',
  4207132456))

WRITE_REPLY_CONTRACTS = (('maple.dll',
  'RX_minimum_length_copy',
  11252,
  24,
  'b091152e36dcd8cc16958e22a55a856952ccd06c4a8d54b99f91990fea9bc2b7',
  15747694),
 ('mapledev.dll',
  'WRITE_FILEERR_header_only_check',
  32618,
  64,
  '480c688987f462bbc04c2a47fb6a77e0bede7dcea597cf2599a5c2d510126248',
  1800586238),
 ('mapledev.dll',
  'WRITE_FILEERR_status_branch',
  32654,
  14,
  '259cf136213431f0887f07f5cf0845848f7b565a7147be4bba505ecbf43c55a8',
  388856488))

def sha(data): return hashlib.sha256(data).hexdigest()
def require(actual, expected, label):
    if actual != expected: raise ROMError(label + ' differs from exact Swirl source')
class SwirlSourceROM(ROMInventory):
    def validate_identity(self):
        require(len(self.data), KERNEL_BYTES, 'kernel size')
        require(sha(self.data), KERNEL_SHA256, 'kernel SHA256')

def audit(kernel):
    rom=SwirlSourceROM(kernel)
    require(rom.header_address,0x8c1cd61c,'ROMHDR')
    for k,v in (('ram_start',0x8c1ce000),('ram_free',0x8c1dc000),('ram_end',0x8cfd0000),
                ('driver_globals_address',0x8cfd0000),('driver_globals_bytes',0x30000)):
        require(rom.header[k],v,'ROMHDR '+k)
    for name,expected in MODULES.items():
        actual=rom.modules[name]
        for k,v in expected.items():
            if k=='sections':
                require(len(actual[k]),len(v),name+' section count')
                for a,e in zip(actual[k],v):
                    for key,value in e.items(): require(a[key],value,name+' section '+key)
            else: require(actual[k],v,name+' '+k)
    maple=rom.modules['maple.dll'];windows=[]
    for name,rva,n,digest,crc in CONTRACTS:
        raw=rom.rva_read(maple,rva,n)
        require(sha(raw),digest,name+' SHA256');require(zlib.crc32(raw),crc,name+' CRC32')
        windows.append(dict(id=name,rva=hex(rva),bytes=n,sha256=digest,crc32=f'{crc:08x}'))
    core=rom.modules['coredll.dll'];exports={e['ordinal']:e for e in rom.exports(core)}
    imports={s['iat_rva']:s for d in rom.imports(maple) if d['dll'].lower()=='coredll.dll' for s in d['symbols']}
    for ordinal,name,iat in ((630,'SetKMode',0x8044),(497,'WaitForSingleObject',0x8024)):
        require(exports[ordinal]['name'],name,name+' export')
        require(imports[iat]['ordinal'],ordinal,name+' import')
    for module,name,rva,n,digest,crc in WRITE_REPLY_CONTRACTS:
        raw=rom.rva_read(rom.modules[module],rva,n)
        require(sha(raw),digest,name+' SHA256');require(zlib.crc32(raw),crc,name+' CRC32')
        windows.append(dict(id=name,module=module,rva=hex(rva),bytes=n,sha256=digest,crc32=f'{crc:08x}'))
    padding=rom.rva_read(maple,0x7c64,412)
    require(any(padding),True,'original stored tail contains packed module metadata')
    # Source's original completion/SetKMode/Wait opcodes retain the same
    # reviewed roles at shifted RVAs. The geometry audit decodes their live
    # object shapes independently from these fingerprints.
    return dict(schema='kui.swirl-ce-vmu.owner-source.v1',status='PASS',runtime_admitted=False,
                source=dict(bytes=len(rom.data),sha256=sha(rom.data)),load_header=rom.load_header,
                rom_header_address=hex(rom.header_address),rom_header=rom.header,
                modules=MODULES,contracts=windows,
                padding=dict(rva='0x7c64',bytes=412,all_zero=False,sha256=sha(padding),
                    writable=False,relocation_required=True,private_source='0x8cfb8000',
                    copied_meaningful_bytes=0x6c62,private_source_bytes=0x8000),
                limits=['Stored source metadata only; no hardware/runtime execution or card compatibility claim.',
                        'Compressed sections are hashed as stored; no broad relocation or reference-exclusion claim.',
                        'Stored tail contains other ROM metadata and is never writable padding.'])
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('kernel',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();out=audit(a.kernel);wire=json.dumps(out,indent=2)+'\n'
    if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(wire)
    print(wire,end='')
if __name__=='__main__':main()
