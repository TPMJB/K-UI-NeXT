#!/usr/bin/env python3
import json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
item=json.loads((ROOT/'sdk-sci-bound/CANDIDATE.json').read_text())
dest=ROOT/'restored-source'
if not dest.exists():subprocess.run([sys.executable,str(ROOT/'restore.py')],cwd=ROOT,check=True)
def git(*a):return subprocess.check_output(['git','-C',str(dest),*a],text=True).strip()
if git('status','--porcelain'):raise SystemExit('Refusing a dirty source checkout')
subprocess.run(['git','-C',str(dest),'fetch',str(ROOT/'sdk-sci-bound/native-source-update.bundle'),'HEAD'],check=True)
if git('rev-parse',item['source_commit']+'^{tree}')!=item['source_tree']:raise SystemExit('Source tree mismatch')
subprocess.run(['git','-C',str(dest),'checkout','--detach',item['source_commit']],check=True)
print('Verified native SDK SCI route source',item['source_commit'],dest)
