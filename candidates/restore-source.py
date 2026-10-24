#!/usr/bin/env python3
import argparse,json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--candidate',choices=['A','B'],default='B');args=p.parse_args()
item=json.loads((ROOT/'candidates/CANDIDATES.json').read_text())[args.candidate]
dest=ROOT/'restored-source'
if not dest.exists():subprocess.run([sys.executable,str(ROOT/'restore.py')],cwd=ROOT,check=True)
def git(*a):return subprocess.check_output(['git','-C',str(dest),*a],text=True).strip()
if git('status','--porcelain'):raise SystemExit('Refusing a dirty source checkout')
subprocess.run(['git','-C',str(dest),'fetch',str(ROOT/'candidates/native-source-update.bundle'),'HEAD'],check=True)
if git('rev-parse',item['source_commit']+'^{tree}')!=item['source_tree']:raise SystemExit('Source tree mismatch')
subprocess.run(['git','-C',str(dest),'checkout','--detach',item['source_commit']],check=True)
print('Verified candidate',args.candidate,item['source_commit'],dest)
