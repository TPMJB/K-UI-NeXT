#!/usr/bin/env python3
import json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
item=json.loads((root/'native-universal/CANDIDATE.json').read_text())
dest=root/'restored-source'
if not dest.exists():subprocess.run([sys.executable,str(root/'restore.py')],cwd=root,check=True)
def git(*a):return subprocess.check_output(['git','-C',str(dest),*a],text=True).strip()
if git('status','--porcelain'):raise SystemExit('Refusing a dirty source checkout')
subprocess.run(['git','-C',str(dest),'fetch',str(root/'native-universal/native-source-update.bundle'),'HEAD'],check=True)
if git('rev-parse',item['source_commit']+'^{tree}')!=item['source_tree']:raise SystemExit('Source tree mismatch')
subprocess.run(['git','-C',str(dest),'checkout','--detach',item['source_commit']],check=True)
print('Verified native X/Y source',item['source_commit'],dest)
