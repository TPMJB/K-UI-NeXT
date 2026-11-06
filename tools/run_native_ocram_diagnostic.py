#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check diagnostic snapshots and render the real compact framebuffer font.

All values are authored fixtures. Fixed Linux mappings substitute PVR/VRAM;
this does not execute SH code, console cache behavior or display hardware.
"""
import argparse,array,hashlib,json,os,shlex,shutil,subprocess,sys
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
COMMON=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fno-sanitize-recover=all','-fno-pie','-no-pie',
        '-DKUI_RETAIL_GD_ASYNC=1','-Iinclude']
RENDER=['-DKUI_OC_DIAG_RENDER=1','-DKUI_NATIVE_OCRAM_BOOTSTRAP=1',
        '-DKUI_NATIVE_COOP_PUMP=1','-DKUI_RETAIL_ASYNC=1',
        '-DKUI_NATIVE_LOADER_TIMELINE=1','-DKUI_NATIVE_READER_DIAGNOSTIC=1',
        '-DKUI_BUILD_ID="FIXTURE"']
SOURCE='tests/test_native_ocram_diagnostic.c'
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(command,env=None):
    return subprocess.run(command,cwd=ROOT,env=env,text=True,
                          stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cc',default='cc')
    parser.add_argument('--pio-stream',type=int,choices=(0,1),default=0)
    parser.add_argument('--vram-dma',type=int,choices=(0,1),default=0)
    args=parser.parse_args()
    if args.vram_dma and not args.pio_stream:
        parser.error('--vram-dma 1 requires the complete --pio-stream 1 profile')
    pio_flags=['-DKUI_NATIVE_GD_PIO_STREAM=1','-DKUI_NATIVE_GD_DMA_STREAM=1',
               '-DKUI_NATIVE_GD_MMU_BUFFERS=1','-DKUI_NATIVE_GD_LEGACY_STREAM=1',
               '-DKUI_NATIVE_OCRAM_BOOTSTRAP=1'] if args.pio_stream else []
    if args.vram_dma:pio_flags+=['-DKUI_NATIVE_GD_VRAM_DMA=1']
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    compiler=Path(shutil.which(args.cc) or args.cc).resolve()
    paths={Path(__file__).resolve(),ROOT/SOURCE}
    for profile in [[],pio_flags,RENDER+pio_flags]:
        dependencies=run([str(compiler),*COMMON,*profile,'-MM',SOURCE])
        assert dependencies.returncode==0,dependencies.stdout
        for line in dependencies.stdout.replace('\\\n',' ').splitlines():
            paths.update((ROOT/name).resolve() for name in shlex.split(line.partition(':')[2]))
    before={str(p):sha(p)for p in sorted(paths)};compiler_hash=sha(compiler)
    report=dict(status='FAIL',source_sha256=before,compiler=str(compiler),
        compiler_sha256=compiler_hash,authored_fixtures=True,
        hardware_executed=False,source_renderer_executed=True,pio_stream_enabled=bool(args.pio_stream),
        vram_dma_enabled=bool(args.vram_dma),cases=[],
        limits=['Formatter tests use authored cached/uncached state and mutate it during restore to check capture ordering.',
                'The real renderer/font runs on the host against guarded mappings at the PVR/VRAM addresses.',
                'ASan/UBSan cover the formatter; fixed-address render mappings use UBSan because they overlap ASan shadow space.',
                'No target assembly, MMU, cache coherency, physical display or game execution is proved.'])
    logs=[]
    try:
        capture=out/'test-formatter'
        command=[str(compiler),*COMMON,*pio_flags,'-fsanitize=address,undefined',SOURCE,'-o',str(capture)]
        result=run(command);logs.append(result.stdout);assert result.returncode==0,result.stdout
        result=run([str(capture)],dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        logs.append(result.stdout);assert result.returncode==0,result.stdout
        report['formatter']=json.loads(result.stdout)
        assert report['formatter']['snapshot_survives_render_mutation']
        baseline=out/'render-formatter-original'
        command=[str(compiler),*COMMON,*RENDER,'-DKUI_NATIVE_GD_DMA_STREAM=0',
                 '-fsanitize=undefined',SOURCE,'-o',str(baseline)]
        result=run(command);logs.append(result.stdout);assert result.returncode==0,result.stdout
        baseline_raw=[]
        for number in range(3):
            raw=out/('original-'+str(number)+'.rgb565')
            result=run([str(baseline),str(number),str(raw)])
            logs.append(result.stdout);assert result.returncode==0,result.stdout
            baseline_raw.append(sha(raw))
        renderer=out/'render-formatter'
        command=[str(compiler),*COMMON,*RENDER,*pio_flags,'-DKUI_NATIVE_GD_DMA_STREAM=1',
                 '-DKUI_NATIVE_GD_MMU_BUFFERS=1','-DKUI_NATIVE_GD_LEGACY_STREAM=1',
                 '-fsanitize=undefined',SOURCE,'-o',str(renderer)]
        result=run(command);logs.append(result.stdout);assert result.returncode==0,result.stdout
        for number,name in enumerate(['policy-refusal','stale-token','early-busy','dma-transfer-refusal']):
            raw=out/(name+'.rgb565');png=out/(name+'.png')
            result=run([str(renderer),str(number),str(raw)])
            logs.append(result.stdout);assert result.returncode==0,result.stdout
            case=json.loads(result.stdout);assert case['status']=='PASS'
            if number<3:
                assert sha(raw)==baseline_raw[number], 'Compact terminal pixels differ from original renderer'
            pixels=array.array('H',raw.read_bytes())
            if sys.byteorder!='little':pixels.byteswap()
            assert len(pixels)==640*480
            rgb=bytearray(len(pixels)*3)
            for i,p in enumerate(pixels):
                rgb[3*i:3*i+3]=bytes((((p>>11)&31)*255//31,((p>>5)&63)*255//63,(p&31)*255//31))
            Image.frombytes('RGB',(640,480),bytes(rgb)).save(png)
            case.update(name=name,png=str(png),png_sha256=sha(png),
                raw_sha256=sha(raw),width=640,height=480,
                original_raw_sha256=baseline_raw[number] if number<3 else None,
                original_pixels_identical=number<3,
                glyph_gap_px=4,bottom_clearance_px=480-case['glyph_bottom']-1,
                semantic_fixture=['Authored policy refusal','Authored cached/uncached token mismatch',
                    'Authored early assembly busy result4 with stale C result2',
                    'Authored FN6 destination refusal with packet and stream accounting'][number])
            report['cases'].append(case)
        assert before=={path:sha(path)for path in before} and sha(compiler)==compiler_hash
        report['source_hashes_unchanged_after_tests']=True
        report['status']='PASS'
    except Exception as error:report['error']=str(error);raise
    finally:
        (out/'diagnostic-render.json').write_text(json.dumps(report,indent=2)+'\n')
        (out/'diagnostic-render.log').write_text('\n'.join(logs))
    print('PASS diagnostic snapshots and production renders:',
          [(x['name'],x['glyph_bottom'],x['bottom_clearance_px'])for x in report['cases']])
if __name__=='__main__':main()
