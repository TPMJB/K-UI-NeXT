# SPDX-License-Identifier: GPL-3.0-only
"""Guards for the two ways low-level code has already broken the console build.

1. Inline asm must stay simple. The CI's GCC 15.2 (sh-elf, -m4-single) rejected KOS's cache helper
   arch_dcache_purge_line ("cannot find a register in class GENERAL_REGS while reloading asm"): one
   asm with EIGHT memory operands plus a register, inlined into a busy function. It compiled on every
   compiler available on a PC, so it can only be avoided, not detected: every asm statement under
   src/ and include/ has at most MAX_OPERANDS operands.

2. The console is not `__SH4__`. GCC defines a different macro for each SH-4 mode (__SH4__ only for
   plain -m4; __SH4_SINGLE__ for -m4-single, which is how KOS builds). A test for __SH4__ alone is
   false on the real build, so the "console" branch was silently skipped, the host-test branch ran,
   and KOS's cache header came back in (the second CI failure, which my first fix and my first guard
   test both got backwards). The raw macros may now appear only in platform.h, which turns them into
   KUI_ON_CONSOLE, and the preprocessor is run over src/dreamcast/disc.c under EACH SH-4 macro set the
   toolchain can produce: the console branch must be taken for every one of them.
This does not prove anything compiles on the console toolchain; it stops these two known ways of
failing there from coming back."""
import pathlib
import re
import shutil
import subprocess
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

# Every way the toolchain says "this is an SH-4 / the Dreamcast": one macro per mode, plus KOS's own.
CONSOLE_MACRO_SETS = {
    "-m4 (plain)":               ["-D__SH4__"],
    "-m4-single (how KOS builds)": ["-D__SH4_SINGLE__"],
    "-m4-single-only":           ["-D__SH4_SINGLE_ONLY__"],
    "-m4-nofpu":                 ["-D__SH4_NOFPU__"],
    "KOS's -D_arch_dreamcast":   ["-D_arch_dreamcast=1"],
    "-D__DREAMCAST__":           ["-D__DREAMCAST__"],
    "all of them":               ["-D__SH4__", "-D__SH4_SINGLE__", "-D_arch_dreamcast=1", "-D__DREAMCAST__"],
}


def preprocess_disc(*macros):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc or not (ROOT / ".deps/fatfs/source/ff.h").exists():
        raise unittest.SkipTest("needs a C compiler and FatFs (make test fetches it)")
    cmd = [cc, "-E", "-P", "-std=c11", "-Iinclude", "-Isrc/dreamcast", "-Itests/stubs", "-I.deps/fatfs/source",
           "-DKUI_EXPERIMENTAL_DMA=1", *macros, "src/dreamcast/disc.c"]
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True).stdout


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


class ConsoleMacro(unittest.TestCase):
    def test_raw_sh4_macros_appear_only_in_platform_h(self):
        for path in sorted(list((ROOT / "src").glob("**/*.[ch]")) + list((ROOT / "include").glob("**/*.h"))):
            if path.name == "platform.h": continue
            for n, line in enumerate(strip_comments(path.read_text()).splitlines(), 1):
                self.assertNotRegex(line, r"__SH4|__sh__|_arch_dreamcast|__DREAMCAST__",
                                    f"{path.name}:{n}: use KUI_ON_CONSOLE, not a raw toolchain macro (see docstring)")

    def test_every_sh4_mode_takes_the_console_branch(self):
        for name, macros in CONSOLE_MACRO_SETS.items():
            out = preprocess_disc(*macros)
            self.assertIn("kui_disc_read_probe_dma", out, name)               # the block really is in the output
            self.assertIn('"ocbp @%0"', out, f"{name}: the console cache asm is missing")
            self.assertIn('"ocbi @%0"', out, name)
            self.assertNotIn("arch_dcache", out, f"{name}: KOS's cache helper (or the host double) got in")
            self.assertIn("0x1fffffffu", out, f"{name}: the DMA buffer's physical-address mask is missing")

    def test_a_host_build_takes_the_host_branch(self):
        out = preprocess_disc()
        self.assertIn("kui_disc_read_probe_dma", out)
        self.assertIn("arch_dcache_purge_range", out)     # the recording double the disc tests rely on
        self.assertNotIn("ocbp", out)
        self.assertNotIn("0x1fffffffu", out)


if __name__ == "__main__":
    unittest.main()
