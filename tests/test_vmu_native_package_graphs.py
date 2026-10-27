#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse incomplete compiler metadata even when EOF follows a complete node."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from package_vmu_native_persistent import closed_callgraph


class CallgraphCompletenessTests(unittest.TestCase):
    def test_truncation_after_complete_node_still_refused(self):
        text='graph: { title: "resident.o"\nnode: { title: "entry" label: "0 bytes" }\n}'
        self.assertTrue(closed_callgraph(text))
        self.assertFalse(closed_callgraph(text[:-1]))
        self.assertFalse(closed_callgraph(text[:50]))

    def test_quoted_braces_and_escaped_quotes_do_not_close_graph(self):
        text=r'graph: { node: { title: "function" label: "{ \" }" } }'
        self.assertTrue(closed_callgraph(text))
        self.assertFalse(closed_callgraph(text[:-1]))

    def test_missing_root_or_unexpected_trailing_content_refused(self):
        self.assertFalse(closed_callgraph(''))
        self.assertFalse(closed_callgraph('node: { title: "function" }'))
        self.assertFalse(closed_callgraph('graph: {} graph: {}'))


if __name__=='__main__':unittest.main()
