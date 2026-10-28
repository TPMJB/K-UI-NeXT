#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Direct payload-only captures on real FAT32/exFAT with raw synthetic oracle."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import zlib
from test_capture_compressed_images import run, export, digest
import verify_dump


def validate(folder, raw_folder=None):
    m = json.loads((folder / 'manifest.json').read_text())
    assert m['schema'] == 3 and m['output_format'] == 'gdi_direct_2048'
    assert m['profile'] == 'gdi-direct2048-rawaudio-v1' and m['saved_data_verified']
    assert not (folder / '.capture.gdi').exists()
    assert not list(folder.glob('*.bin')) or all(p.name.startswith('checkpoint-') for p in folder.glob('*.bin'))
    assert verify_dump.verify(folder)
    for track in m['tracks']:
        saved = (folder / track['file']).read_bytes()
        assert track['sector_bytes'] == (2048 if track['control'] == 4 else 2352)
        assert len(saved) == (track['end_fad'] - track['start_fad']) * track['sector_bytes']
        assert track['crc32'] == f'{zlib.crc32(saved):08x}'
        if raw_folder:
            raw = (raw_folder / f"track{track['number']:02}.{'bin' if track['control'] == 4 else 'raw'}").read_bytes()
            if track['control'] == 4:
                raw = b''.join(verify_dump.data_payload(raw[i:i+2352], track['sector_mode']) for i in range(0, len(raw), 2352))
            assert saved == raw
    return {p.name: digest(p) for p in folder.glob('track*')}


def main():
    with tempfile.TemporaryDirectory(prefix='kui-direct2048-') as tmp:
        base = Path(tmp)
        for fs in ('fat32', 'exfat'):
            seed = base / f'{fs}.img'
            with seed.open('wb') as f:
                f.truncate(96 * 1024 * 1024)
            subprocess.run(['mkfs.fat', '-F', '32', str(seed)] if fs == 'fat32' else
                           ['mkfs.exfat', str(seed)], check=True, stdout=subprocess.DEVNULL)
            run(seed, 'seed', layout='six-track')
            for mode, opts in ((1, 'direct2048,dma'), (2, 'direct2048,crc32,noend,dma')):
                raw_image = base / f'{fs}-raw-{mode}.img'; shutil.copyfile(seed, raw_image)
                run(raw_image, 'new', layout='six-track', mode=mode)
                raw_folder = export(raw_image, base / f'{fs}-raw-{mode}', layout='six-track', mode=mode)
                image = base / f'{fs}-direct-{mode}.img'; shutil.copyfile(seed, image)
                out = run(image, 'new', opts=opts, layout='six-track', mode=mode)
                assert 'REFERENCE checked=0' in out
                folder = export(image, base / f'{fs}-direct-{mode}', layout='six-track', mode=mode)
                expected = validate(folder, raw_folder)
                before = digest(image)
                assert 'WRITES 0' in run(image, 'verify', layout='six-track', mode=mode)
                assert digest(image) == before
                run(image, 'resume', layout='six-track', mode=mode)
                before = digest(image)
                quick = run(image, 'resume', opts='crc32,size,noend', layout='six-track', mode=mode)
                assert 'STATS verified=1' in quick and 'WRITES 0' in quick and digest(image) == before
                if mode == 2:
                    assert 'Direct 2048 output: rereading every saved payload/audio byte' in quick
                    assert 'crc_only=1' in quick
                    before = digest(image)
                    failed = run(image, 'resume', 'read-fail', opts='crc32,size,noend', layout='six-track', mode=mode, expected=1)
                    assert 'WRITES 0' in failed and 'STATS verified=0' in failed and digest(image) == before
                print(f'PASS {fs} direct2048 mode{mode}: completed Quick Resume requires saved-byte verification', flush=True)
                assert validate(export(image, base / f'{fs}-again-{mode}', layout='six-track', mode=mode)) == expected
                run(image, 'mutate', 'cooked-track', layout='six-track', mode=mode)
                before = digest(image)
                run(image, 'verify', layout='six-track', mode=mode, expected=1)
                assert digest(image) == before
                run(image, 'resume', layout='six-track', mode=mode, expected=1)
                if mode == 2:
                    before = digest(image)
                    failed = run(image, 'resume', opts='crc32,size,noend', layout='six-track', mode=mode, expected=1)
                    assert 'WRITES 0' in failed and 'STATS verified=0' in failed and digest(image) == before
                    print(f'PASS {fs} direct2048: completed Quick Resume refuses changed saved bytes', flush=True)
                image.unlink(); raw_image.unlink()
                print(f'PASS {fs} direct2048 mode{mode}: exact payload/audio, saved hashes, verify, corruption refusal', flush=True)
            for fault, opts, status in (('stop-early', 'direct2048,dma', 3), ('write-fail', 'direct2048', 1),
                                         ('sync-fail', 'direct2048', 1), ('buffer-mutate', 'direct2048,crc32,noend', 1),
                                         ('edc-fail', 'direct2048', 1), ('wrong-address', 'direct2048', 1),
                                         ('mixed-mode', 'direct2048', 1), ('disc-fail', 'direct2048', 1)):
                image = base / f'{fs}-{fault}.img'; shutil.copyfile(seed, image)
                run(image, 'new', fault, opts=opts, layout='six-track', expected=status)
                run(image, 'resume', layout='six-track')
                validate(export(image, base / f'{fs}-resumed-{fault}', layout='six-track'))
                image.unlink()
                print(f'PASS {fs} direct2048: {fault} and resume', flush=True)
            # A real acquisition failure leaves committed cooked prefixes and
            # missing tails. Both resume modes must finish then verify all bytes.
            partial = base / f'{fs}-quick-partial.img'; shutil.copyfile(seed, partial)
            run(partial, 'new', 'disc-fail', opts='direct2048,crc32,noend', layout='six-track', expected=1)
            outputs = []
            for resume_opts in ('crc32,noend', 'crc32,size,noend'):
                image = base / f'{fs}-quick-partial-{len(outputs)}.img'; shutil.copyfile(partial, image)
                out = run(image, 'resume', opts=resume_opts, layout='six-track')
                assert 'STATS verified=1 crc_only=1' in out
                folder = export(image, base / f'{fs}-quick-partial-{len(outputs)}', layout='six-track')
                outputs.append(validate(folder)); image.unlink()
            assert outputs[0] == outputs[1]
            print(f'PASS {fs} direct2048: partial normal/Quick Resume finish with identical fully verified bytes', flush=True)
            image = base / f'{fs}-quick-readback-fail.img'; shutil.copyfile(partial, image)
            failed = run(image, 'resume', 'read-fail', opts='crc32,size,noend', layout='six-track', expected=1)
            assert 'STATS verified=0' in failed
            folder = export(image, base / f'{fs}-quick-readback-failed', layout='six-track')
            assert not (folder / 'manifest.json').exists() and not (folder / 'disc.gdi').exists()
            # Acquisition is now checkpoint-complete, but it must still pass
            # the same full saved-byte gate when Quick Resume is repeated.
            finished = run(image, 'resume', opts='crc32,size,noend', layout='six-track')
            assert 'STATS verified=1 crc_only=1' in finished
            assert validate(export(image, base / f'{fs}-quick-readback-recovered', layout='six-track')) == outputs[0]
            image.unlink()
            print(f'PASS {fs} direct2048: interrupted end readback stays incomplete; Quick Resume verifies before publication', flush=True)
            # Admission remains read-only when a partial direct job contains a
            # forged or incomplete in-place receipt, before tail acquisition.
            image = base / f'{fs}-quick-malformed-receipt.img'; shutil.copyfile(partial, image)
            folder = export(image, base / f'{fs}-quick-malformed-receipt', layout='six-track')
            payload = base / f'{fs}-malformed-receipt'; payload.write_bytes(b'broken repair receipt')
            run(image, 'put', str(payload), f'0:/KUI/dumps/{folder.name}/.kui-inplace-complete', layout='six-track')
            before = digest(image)
            failed = run(image, 'resume', opts='crc32,size,noend', layout='six-track', expected=1)
            assert 'WRITES 0' in failed and digest(image) == before
            image.unlink(); partial.unlink()
            print(f'PASS {fs} direct2048: partial Quick Resume refuses malformed receipt before writes', flush=True)
            run(seed, 'new', opts='direct2048', layout='six-track')
            # A retained in-place-repair marker blocks normal Verify/Resume before writes.
            for marker in ('.kui-inplace-active', '.kui-inplace-active.part'):
                image = base / f'{fs}-marker.img'; shutil.copyfile(seed, image)
                payload = base / 'marker'; payload.write_bytes(b'active')
                folder = export(image, base / f'{fs}-marker-{marker}', layout='six-track')
                run(image, 'put', str(payload), f'0:/KUI/dumps/{folder.name}/{marker}', layout='six-track')
                before = digest(image)
                assert 'WRITES 0' in run(image, 'verify', layout='six-track', expected=1)
                assert 'WRITES 0' in run(image, 'resume', layout='six-track', expected=1)
                assert digest(image) == before
                image.unlink()
                print(f'PASS {fs} normal capture: active repair marker {marker}', flush=True)


if __name__ == '__main__':
    main()
