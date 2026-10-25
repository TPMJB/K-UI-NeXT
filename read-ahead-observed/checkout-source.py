#!/usr/bin/env python3
import json,pathlib,subprocess
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parent
item=json.loads((HERE/'SOURCE.json').read_text())
dest=ROOT/'restored-source'
def git(*args):return subprocess.check_output(['git','-C',str(dest),*args],text=True).strip()
if not dest.exists():raise SystemExit('First run read-ahead/checkout-source.py')
if git('status','--porcelain'):raise SystemExit('Refusing a dirty source checkout')
subprocess.run(['git','-C',str(dest),'fetch',str(HERE/'native-source-update.bundle'),'HEAD'],check=True)
if git('rev-parse',item['source_commit']+'^{tree}')!=item['source_tree']:raise SystemExit('Source tree mismatch')
subprocess.run(['git','-C',str(dest),'checkout','--detach',item['source_commit']],check=True)
print('Verified source',item['source_commit'],item['source_tree'])
