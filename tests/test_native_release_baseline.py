# SPDX-License-Identifier: GPL-3.0-only
"""Keep accepted native drive/SCI fixes in release builds and out of CE."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeReleaseBaseline(unittest.TestCase):
    def dry_run(self, target, *settings):
        with tempfile.TemporaryDirectory() as temporary:
            kos = Path(temporary)
            (kos / "Makefile.rules").write_text("")
            result = subprocess.run(
                ["make", "--no-print-directory", "-Bn", "-f", "Makefile.dc", target,
                 "KOS_BASE=" + str(kos), "BUILD_ID=0123456789ab", *settings],
                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_release_native_readers_keep_accepted_flags_without_return_diagnostics(self):
        text = self.dry_run("build/retail/entry.elf")
        self.assertIn("SCI_RESUME_FIX=1", text)
        self.assertNotIn("SKIES_READ_DIAGNOSTIC=1", text)
        lines = [line for line in text.splitlines()
                 if line.startswith("sh-elf-gcc ") and " -c " in line]
        residents = [line for line in lines if "/src/core/retail_gd.o" in line]
        self.assertEqual(len(residents), 4)  # SCIF, SCI, IDE and SCI background.
        for line in residents:
            self.assertIn("-DKUI_RETAIL_NATIVE_DISC_CHECK=1", line)
            self.assertIn("-DKUI_RETAIL_NATIVE_DRIVE_STATE=1", line)
            self.assertNotIn("-DKUI_RETAIL_RETURN_TRACE=3", line)
        background = [line for line in lines if "build/retail/scia/" in line]
        self.assertTrue(background)
        self.assertTrue(all("-DKUI_GENERAL_SCI_RESUME_FIX=1" in line
                            for line in background))
        stage = next(line for line in lines if " -o build/retail/src/loader/retail_stage.o" in line)
        self.assertIn("-DKUI_GAME_RAM_PATCHES_STAGE=1", stage)
        diagnostic = self.dry_run("build/retail/src/loader/retail_stage.o", "KUI_RETAIL_RETURN_TRACE=3")
        self.assertIn("-DKUI_GAME_RAM_PATCHES_STAGE=0", diagnostic)

    def test_native_defaults_do_not_leak_into_ce_or_its_music_modules(self):
        for target in ("build/retail-ce/entry.elf", "wince-cdda"):
            with self.subTest(target=target):
                text = self.dry_run(target)
                self.assertNotIn("-DKUI_GENERAL_SCI_RESUME_FIX=1", text)
                self.assertNotIn("-DKUI_RETAIL_NATIVE_DISC_CHECK=1", text)
                self.assertNotIn("-DKUI_RETAIL_NATIVE_DRIVE_STATE=1", text)
                self.assertIn("-DKUI_CE_SCI_RESUME_FIX=1", text)

    def test_pending_command_does_not_change_native_logical_drive_condition(self):
        # Reuse the real GD service's host mapping/track fixtures, including
        # raw and converted-2048 metadata. CHECK retains pending progress;
        # DRIVE must report mounted-image condition independently of it.
        harness = r'''
#define main existing_gd_suite
#include "tests/test_retail_gd.c"
#undef main
int main(void) {
    for(use_cooked_tracks=0;use_cooked_tracks<2;use_cooked_tracks++) {
        reset();
        read_params(45000,20,OUTPUT);
        int32_t token=call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM);
        CHECK(token>0 && service.pending);
        CHECK(call(KUI_GD_DRIVE,STATUS,0)==0);
        CHECK(get(STATUS)==EXPECTED_DRIVE && get(STATUS+4)==0x80);
        CHECK(service.pending && ctx.reads==0);
        CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
#ifdef KUI_RETAIL_GD_ASYNC
        kui_retail_gd_progress(&service,20,0);
#else
        while(service.pending) CHECK(call(KUI_GD_EXEC,0,0)==0);
#endif
        CHECK(!service.pending);
        CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
        CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==1);
    }
    return 0;
}
'''
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler)
        variants = (("native", 1, []),
                    ("native-background", 1, ["-DKUI_RETAIL_GD_ASYNC=1"]),
                    ("ce-control", 0, ["-DKUI_RETAIL_CE=1", "-DKUI_RETAIL_GD_ASYNC=1"]))
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "drive.c"
            source.write_text(harness)
            for name, enabled, flags in variants:
                with self.subTest(variant=name):
                    executable = Path(temporary) / name
                    result = subprocess.run(
                        [compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                         "-I.", "-Iinclude", f"-DKUI_RETAIL_NATIVE_DRIVE_STATE={enabled}",
                         f"-DEXPECTED_DRIVE={enabled}", *flags,
                         "src/core/retail_gd.c", str(source), "-o", str(executable)],
                        cwd=ROOT, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = subprocess.run([str(executable)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_image_disc_check_uses_native_answer_and_retains_ce_and_toy_routes(self):
        # Preprocess the actual assembly entry: native function 2 answers
        # unchanged-image; CE still enters its GD driver and Toy forwards.
        variants = (("native", ["-DKUI_RETAIL_NATIVE_DISC_CHECK=1"], "rts"),
                    ("ce", ["-DKUI_RETAIL_CE=1"], "bra .Lgd_common"),
                    ("toy", ["-DKUI_RETAIL_TOY_PILOT=1"], None))
        for name, flags, answer in variants:
            with self.subTest(variant=name):
                result = subprocess.run(
                    ["cc", "-E", "-P", "-x", "assembler-with-cpp", "-Iinclude",
                     *flags, "src/loader/retail_resident.S"],
                    cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                menu = result.stdout.split("kui_retail_menu_hook:", 1)[1].split(".Lmenu_forward:", 1)[0]
                if answer is None:
                    self.assertNotIn("cmp/eq #2", menu)
                else:
                    self.assertIn("cmp/eq #2", menu)
                    self.assertIn(answer, menu)


if __name__ == "__main__":
    unittest.main()
