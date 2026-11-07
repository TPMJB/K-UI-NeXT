#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare shared allocator admission with its pre-gate implementation.

Instrument only the existing candidate-initialization statement. This counts
executed initializations without replacing the production recognizer. The
frozen control must reproduce the unwanted work; unknown opcodes in the current
source must reach no candidate initialization. Counts are not console timings.
Original executable inputs are optional, private and never copied into reports.
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/core/vmu_native_heap.c'
BASELINE = 'f8807cb1656137cc5fad7854802d685ea168736f'
BASELINE_SHA256 = '7d99629da1954d1332c1da653a67b9d7139ee488c0cc33e78946f207f88594e2'
GATED = '''        /* Most halfwords cannot start either allocator. Keep that cheap gate
         * ahead of the candidate's scratch initialization: the cold stage's
         * freestanding memset otherwise clears 72 bytes at every offset. */
        uint16_t op=h16(p+at);
        bool dispatch=op==0x2fe6u && h16(p+at+2u)==0x6e53u;
        if(!dispatch && op!=0x741fu) continue;
        struct kui_vmu_native_heap h={0};
        if(dispatch) {
            if(!candidate(p,n,base,at,&h)) continue;
        } else {
            if(!direct_candidate(p,n,base,at,&h)) continue;
        }
'''
UNGATED = '''        struct kui_vmu_native_heap h={0};
        if(h16(p+at)==0x2fe6u && h16(p+at+2u)==0x6e53u) {
            if(!candidate(p,n,base,at,&h)) continue;
        } else if(h16(p+at)==0x741fu) {
            if(!direct_candidate(p,n,base,at,&h)) continue;
        } else continue;
'''
INITIALIZER = 'struct kui_vmu_native_heap h={0};'
LOAD = 0x8c010000
METADATA_BYTES = 72


def run(command, **kwargs):
    result = subprocess.run(list(map(str, command)), cwd=ROOT, text=True,
                            capture_output=True, timeout=180, **kwargs)
    if result.returncode:
        raise AssertionError('Command failed: ' + shlex.join(list(map(str, command))) +
                             '\n' + result.stdout + result.stderr)
    return result.stdout


def baseline_source(ref):
    if ref is not None:
        return run(['git', 'show', ref + ':src/core/vmu_native_heap.c'])
    # Reconstruct the exact pre-gate translation unit in shallow CI checkouts.
    source = SOURCE.read_bytes()
    gated = GATED.encode()
    if source.count(gated) != 1:
        raise AssertionError('Exact heap gate hunk changed; supply --baseline-ref')
    baseline = source.replace(gated, UNGATED.encode())
    if hashlib.sha256(baseline).hexdigest() != BASELINE_SHA256:
        raise AssertionError(f'Reconstructed heap source differs from {BASELINE}; supply --baseline-ref')
    return baseline.decode()


WRAPPERS = r'''
uint64_t test_scan_initializations(void) {return scan_candidate_initializations;}
int test_scan(const uint8_t *p,size_t n,uint32_t load,struct kui_vmu_native_heap *out) {
    scan_candidate_initializations=0u;
    return kui_vmu_native_heap_discover(p,n,load,out);
}
size_t test_fixture(unsigned kind,unsigned alias,unsigned half,uint8_t *out) {
    uint32_t a=alias==1u?UINT32_C(0x80000000):alias==2u?UINT32_C(0xa0000000):0u;
    size_t at=0x800u+2u*half;
    struct kui_vmu_native_heap h=fixture(at,a,true);
    if(kind==1u)put16(at,0u);
    if(kind==2u)put32(0x104u,h.initializer_target);
    if(kind==3u || kind==4u) {
        put32(0x100u,0u);put32(0x700u,h.initializer_target);
        put32(0x704u,h.initializer_target);known_call(0x400u,0x700u,h.initializer_target);
        if(kind==4u)known_call(0x500u,0x704u,h.initializer_target);
    }
    if(kind==5u) {
        body(0x1400u,a);
        put32(0x2100u,a|(UINT32_C(0x0c010000)+(uint32_t)at+0xe2u));
        put32(0x2104u,a|(UINT32_C(0x0c010000)+(uint32_t)at+0x1b0u));
        put32(0x1400u+0x220u+7u*4u,a|UINT32_C(0x0c012200));
        put32(0x2200u,a|UINT32_C(0x0c0114e2));
        put32(0x2204u,a|UINT32_C(0x0c0115b0));
    }
    memcpy(out,image,sizeof(image));return sizeof(image);
}
'''


def build(folder, label, source, fast, compiler):
    assert source.count(INITIALIZER) == 1, 'Candidate initialization is no longer unique'
    instrumented = ('#include "kui/vmu_native_heap.h"\n'
                    'static uint64_t scan_candidate_initializations;\n' +
                    source.replace(INITIALIZER,
                                   '++scan_candidate_initializations; ' + INITIALIZER))
    source_path = folder / f'{label}-{fast}-source.c'
    source_path.write_text(instrumented)
    fixture = (ROOT / 'tests/test_vmu_native_heap.c').read_text()
    assert fixture.count('#include "../src/core/vmu_native_heap.c"') == 1
    fixture = fixture.replace('#include "../src/core/vmu_native_heap.c"',
                              '#include ' + json.dumps(str(source_path)))
    common = [*compiler, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
              '-fno-builtin', '-fno-omit-frame-pointer', '-Iinclude', '-I.', '-Isrc/core',
              f'-DKUI_NATIVE_STAGE_FAST_SCAN={fast}']
    corpus = folder / f'{label}-{fast}-corpus.c'
    corpus.write_text(fixture)
    binary = folder / f'{label}-{fast}-corpus'
    run([*common, '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
         '-fno-pie', '-no-pie', corpus, '-o', binary])
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    output = run([binary], env=env)
    harness = folder / f'{label}-{fast}-library.c'
    harness.write_text('#define main fixture_main\n' + fixture +
                       '\n#undef main\n' + WRAPPERS)
    library = folder / f'{label}-{fast}.so'
    run([*common, '-shared', '-fPIC', harness, '-o', library])
    lib = ct.CDLL(str(library))
    lib.test_scan.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_uint32, ct.c_void_p]
    lib.test_scan.restype = ct.c_int
    lib.test_scan_initializations.restype = ct.c_uint64
    lib.test_fixture.argtypes = [ct.c_uint, ct.c_uint, ct.c_uint, ct.c_void_p]
    lib.test_fixture.restype = ct.c_size_t
    return lib, output


def scan(lib, data, alias=LOAD, offset=0):
    storage = ct.create_string_buffer(bytes([0xa5]) * offset + bytes(data))
    before = storage.raw
    out = (ct.c_ubyte * METADATA_BYTES)(*([0xa5] * METADATA_BYTES))
    result = lib.test_scan(ct.addressof(storage) + offset, len(data), alias, out)
    assert storage.raw == before, 'The immutable input was changed'
    assert result in (-2, -1, 0, 1)
    return (result, bytes(out)), lib.test_scan_initializations()


def same(before, after, data, **kwargs):
    old, old_work = scan(before, data, **kwargs)
    new, new_work = scan(after, data, **kwargs)
    assert old == new, 'Admission/refusal or complete metadata changed'
    return new, old_work, new_work


def check(before, after):
    cases = 0
    # One scan position and an exact allocation tail: every possible first
    # halfword keeps the complete old result, including the two allocator
    # sentinels. A matching first word alone grants no allocator admission.
    short = bytearray(0x1d0)
    for op in range(65536):
        struct.pack_into('<H', short, 0, op)
        result, old_work, new_work = same(before, after, short)
        assert result == (0, bytes(METADATA_BYTES)) and old_work == 1
        assert new_work == int(op == 0x741f), 'Unknown opcode initialized a candidate'
        cases += 1
    struct.pack_into('<HH', short, 0, 0x2fe6, 0x6e53)
    _, old_work, new_work = same(before, after, short)
    assert old_work == new_work == 1
    cases += 1
    # Sparse real-sized negative input reproduces the original scratch work
    # with no source identity, cached mapping or authored allocator body.
    sparse = bytes(1024 * 1024)
    _, old_work, new_work = same(before, after, sparse)
    assert old_work == (len(sparse) - 0x1d0) // 2 + 1 and new_work == 0
    sparse_result = dict(scan_bytes=len(sparse), baseline_initializations=old_work,
                         candidate_initializations=new_work,
                         avoided_scratch_bytes=old_work * METADATA_BYTES)
    cases += 1
    for kind in range(6):
        for alias in range(3):
            for half in range(2):
                data = ct.create_string_buffer(0x3001)
                assert after.test_fixture(kind, alias, half, data) == len(data)
                for offset in range(8):
                    result, old_work, new_work = same(before, after, data.raw, offset=offset)
                    assert result[0] == (1 if kind in (0, 3) else 0 if kind == 1 else -2), \
                        (kind, alias, half, offset, result[0])
                    assert new_work < old_work
                    cases += 1
    return cases, sparse_result


def check_originals(before, after, paths):
    cases = 0
    for path in paths:
        original = bytearray(Path(path).read_bytes())
        found, old_work, new_work = same(before, after, original)
        assert found[0] == 1 and new_work < old_work
        # Fresh original recognition selects this body; mutate each bit of
        # its first opcode. Nothing from an original is emitted or retained.
        owner = struct.unpack('<18I', found[1])
        at = (owner[6] & 0x1fffffff) - (LOAD & 0x1fffffff)
        assert 0 <= at <= len(original) - 2
        for bit in range(16):
            original[at + bit // 8] ^= 1 << (bit & 7)
            changed, _, _ = same(before, after, original)
            assert changed[0] != 1, 'Damaged original allocator remained admitted'
            original[at + bit // 8] ^= 1 << (bit & 7)
            cases += 1
        cases += 1
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--baseline-ref', help='Optional Git ref for the historical control')
    parser.add_argument('--original', type=Path, action='append', default=[])
    args = parser.parse_args()
    baseline = baseline_source(args.baseline_ref)
    candidate = args.source.read_text()
    compiler = shlex.split(os.environ.get('HOST_CC', 'cc'))
    results = []
    with tempfile.TemporaryDirectory(prefix='kui-heap-scan-work-') as temp:
        for fast in (0, 1):
            before, baseline_corpus = build(Path(temp), 'baseline', baseline, fast, compiler)
            after, candidate_corpus = build(Path(temp), 'candidate', candidate, fast, compiler)
            assert baseline_corpus == candidate_corpus, 'Existing heap regression corpus changed'
            cases, sparse = check(before, after)
            originals = check_originals(before, after, args.original)
            results.append(dict(fast_scan=fast, authored_comparison_cases=cases,
                                original_comparison_cases=originals, **sparse))
    print(json.dumps(dict(status='PASS', existing_sanitized_corpus_identical=True,
                          original_inputs=len(args.original),
                          private_metadata_emitted=False, console_timing=False,
                          modes=results), indent=2))


if __name__ == '__main__':
    main()
