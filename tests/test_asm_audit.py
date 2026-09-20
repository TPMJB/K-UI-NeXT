# SPDX-License-Identifier: GPL-3.0-only
"""Inline asm in this tree must stay simple.

Why this exists: the CI's GCC 15.2 rejected KOS's cache helper arch_dcache_purge_line
("asm operand has impossible constraints"): one inline asm with EIGHT memory operands plus a
register, inlined into a function with many live values. The same source compiled on GCC 13
and 14, so no host build and no available cross-compiler can catch it. The only defence is
not to write such asm, and to keep KOS's heavy inline helpers out of busy functions.

The rules, checked on every `make test`:
  1. every inline asm statement under src/ and include/ has at most MAX_OPERANDS operands;
  2. KOS's <arch/cache.h> is included only under `#ifndef __SH4__` (the host tests' double).
This does not prove a statement compiles on the console toolchain; it stops the known way of
failing there from coming back."""
import pathlib
import re
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
MAX_OPERANDS = 3   # the tree's asm has 0 to 1; KOS's failing helper had 9


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def asm_statements(text):
    """Yield the argument text of every __asm__ / asm statement: balanced parentheses, aware of
    string literals (a ')' or ':' inside a template must not end or split anything)."""
    for m in re.finditer(r"\b(__asm__|__asm|asm)\b\s*(?:__volatile__|volatile)?\s*\(", text):
        i, depth, in_str = m.end(), 1, False
        while i < len(text) and depth:
            c = text[i]
            if in_str:
                if c == "\\": i += 1
                elif c == '"': in_str = False
            elif c == '"': in_str = True
            elif c == "(": depth += 1
            elif c == ")": depth -= 1
            i += 1
        yield text[m.end():i - 1]


def top_level_split(text, sep):
    parts, cur, depth, in_str, i = [], "", 0, False, 0
    while i < len(text):
        c = text[i]
        if in_str:
            cur += c
            if c == "\\" and i + 1 < len(text): i += 1; cur += text[i]
            elif c == '"': in_str = False
        elif c == '"': in_str = True; cur += c
        elif c in "([": depth += 1; cur += c
        elif c in ")]": depth -= 1; cur += c
        elif c == sep and depth == 0: parts.append(cur); cur = ""
        else: cur += c
        i += 1
    parts.append(cur)
    return parts


def operand_count(asm_args):
    sections = top_level_split(asm_args, ":")          # template : outputs : inputs : clobbers
    return sum(len([o for o in top_level_split(s, ",") if o.strip()]) for s in sections[1:3])


def audit(root):
    """Return (file, operands) for every over-complex asm statement under root."""
    bad = []
    for path in sorted(list(root.glob("src/**/*.[ch]")) + list(root.glob("include/**/*.h"))):
        for args in asm_statements(strip_comments(path.read_text())):
            n = operand_count(args)
            if n > MAX_OPERANDS: bad.append((str(path.relative_to(root)), n))
    return bad


# The statement that failed on the CI compiler, verbatim from KOS's arch/cache.h.
KOS_PURGE_LINE = '''__asm__ __volatile__("ocbp @%8\\n\\t"
             : "+m"(ptr[0]), "+m"(ptr[1]), "+m"(ptr[2]), "+m"(ptr[3]),
               "+m"(ptr[4]), "+m"(ptr[5]), "+m"(ptr[6]), "+m"(ptr[7])
             : "r" (ptr)
    );'''


class AsmAudit(unittest.TestCase):
    def test_the_audit_would_have_caught_the_asm_that_failed(self):
        args = list(asm_statements(KOS_PURGE_LINE))
        self.assertEqual(len(args), 1)
        self.assertEqual(operand_count(args[0]), 9)
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            (root / "src").mkdir()
            (root / "src" / "busy.c").write_text("void f(void *ptr) {\n" + KOS_PURGE_LINE + "\n}\n")
            self.assertEqual(audit(root), [("src/busy.c", 9)])

    def test_parser_handles_the_shapes_in_this_tree(self):
        self.assertEqual(operand_count(' "ocbp @%0" : : "r"(start) : "memory" '), 1)
        self.assertEqual(operand_count(' "" ::: "memory" '), 0)
        self.assertEqual(operand_count(' "mov r15,%0" : "=r"(stack) '), 1)
        self.assertEqual(operand_count(' "a:b,c)" : "=r"(x), "=r"(y) : "r"(z) '), 3)   # ':' ',' ')' inside a template

    def test_tree_has_only_simple_inline_asm(self):
        self.assertEqual(audit(ROOT), [], "inline asm with too many operands (see this file's docstring)")

    def test_kos_cache_header_is_host_only(self):
        for path in sorted((ROOT / "src").glob("**/*.[ch]")):
            lines = path.read_text().splitlines()
            for n, line in enumerate(lines):
                if re.match(r"\s*#\s*include\s*<arch/cache\.h>", line):
                    self.assertTrue(any("#ifndef __SH4__" in l for l in lines[max(0, n - 2):n]),
                                    f"{path.name}:{n + 1} includes KOS's cache header on the console build")


if __name__ == "__main__":
    unittest.main()
