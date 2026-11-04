#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutation checks for the private bank's identity and sparse file allowlist."""
import sys
from pathlib import Path
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from package_toy_report_bank import package
from check_native_cdda_compat import expected_descriptor
from runtime_package import envelope
from update_package import CAPABILITY,HANDOFF_HEADER

def refused(fn):
    try:fn()
    except ValueError:return
    raise AssertionError('Malformed or mixed test bank accepted')

with tempfile.TemporaryDirectory(prefix='toy-bank-check-') as temporary:
    root=Path(temporary);build='d20261104032'
    gui=root/'runtime.kui';raw=root/'raw.kui';cooked=root/'cooked.kui'
    payload=CAPABILITY+HANDOFF_HEADER+bytes(176)+b'TOY TXT TEST\0'+bytes(3)
    payload+=bytes((-len(payload))%4)
    gui.write_bytes(envelope(payload,len(payload),build))
    raw.write_bytes(envelope(expected_descriptor('toy-raw'),128,build))
    cooked.write_bytes(envelope(expected_descriptor('toy-cooked'),128,build))
    good=package(gui,raw,cooked,build,root/'good.kup')
    assert sum(row['policy']==2 for row in good['members'])==23
    refused(lambda:package(gui,cooked,raw,build,root/'swapped.kup'))
    refused(lambda:package(gui,raw,cooked,'d20261104031',root/'wrong-bank.kup'))
    raw.write_bytes(envelope(expected_descriptor('toy-raw'),128,'d20261104031'))
    refused(lambda:package(gui,raw,cooked,build,root/'mixed.kup'))
    refused(lambda:package(gui,raw,cooked,build,root/'good.kup'))
print('PASS Toy bank: exact three-path replacement; all23 native/VMU/CE/other paths inherited; mixed IDs and wrong formats refused')
