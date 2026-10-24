#!/usr/bin/env python3
import pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
DEST=ROOT/'restored-source'
COMMIT="1920659698aeec78c1e812bbb76b2b9274f7558d"
TREE="569869f7b50ec1096c1cfb4a19664ad9e5f37973"
def git(*args):
    return subprocess.check_output(['git','-C',str(DEST),*args],text=True).strip()
if not DEST.exists():
    subprocess.run([sys.executable,str(ROOT/'restore.py')],cwd=ROOT,check=True)
if git('status','--porcelain'):
    raise SystemExit('Refusing to replace a dirty source checkout')
if git('rev-parse','HEAD')!=COMMIT:
    subprocess.run(['git','-C',str(DEST),'fetch',str(ROOT/'hybrid/native-hybrid-update.bundle'),'HEAD'],check=True)
    if git('rev-parse','FETCH_HEAD')!=COMMIT:
        raise SystemExit('Incremental checkpoint commit mismatch')
    subprocess.run(['git','-C',str(DEST),'checkout','--detach',COMMIT],check=True)
if git('rev-parse','HEAD^{tree}')!=TREE:
    raise SystemExit('Incremental checkpoint tree mismatch')
subprocess.run(['git','-C',str(DEST),'fsck','--full','--no-reflogs'],check=True)
print('Verified native hybrid source:',COMMIT,DEST)
