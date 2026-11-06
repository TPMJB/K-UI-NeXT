#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A partially restored GCC graph cannot silently supply stack evidence."""
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from check_retail_stack import load

GRAPH=('graph: { title: "fixture.c"\n'
       'node: { title: "fixture" label: "fixture\\nfixture.c:1:1\\n12 bytes (static)" }\n'
       '}\n')

class CompleteGraph(unittest.TestCase):
    def test_complete_graph(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'fixture.ci';path.write_text(GRAPH)
            self.assertEqual(load([path])[0],{'fixture':12})

    def test_partial_graph_even_after_complete_node(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'fixture.ci';path.write_text(GRAPH.rsplit('\n}',1)[0]+'\n')
            with self.assertRaisesRegex(ValueError,'Truncated compiler callgraph'):
                load([path])

if __name__=='__main__':unittest.main()
