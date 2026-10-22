# SPDX-License-Identifier: GPL-3.0-only
"""The actual portable C client and capture engine against the PC receiver."""
import importlib.util
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("network_rip_integration_server", ROOT / "tools/network_rip_server.py")
server = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(server)
from test_network_rip_server import fixture_image


def run(*args, expected=0):
    result = subprocess.run([str(arg) for arg in args], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"}, timeout=120)
    if result.returncode != expected:
        raise AssertionError(f"{args}: expected {expected}, got {result.returncode}\n{result.stdout}")
    return result.stdout


class Receiver:
    def __init__(self, image, identity, key):
        self.volume = server.Volume(image, identity)
        self.key = key
        self.errors = []
        self.stop = threading.Event()
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.port = self.listener.getsockname()[1]
        self.listener.listen(2)
        self.listener.settimeout(0.1)
        self.thread = threading.Thread(target=self.loop, daemon=True)

    def loop(self):
        try:
            while not self.stop.is_set():
                try:
                    sock, _ = self.listener.accept()
                except socket.timeout:
                    continue
                with sock:
                    server.serve_one(sock, self.volume, self.key)
        except BaseException as error:
            self.errors.append(error)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join(5)
        self.listener.close()
        self.volume.close()
        if self.thread.is_alive():
            raise AssertionError("PC receiver did not stop after client closed")
        if self.errors:
            raise self.errors[0]


class NetworkRipIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = ROOT / "build"
        cls.build.mkdir(exist_ok=True)
        cc = os.environ.get("CC", "cc")
        cls.flags = [cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                     "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-Iinclude"]
        common = ["src/core/network_rip.c", "src/core/hash.c", "src/core/data.c"]
        run(*cls.flags, *common, "tests/test_network_rip.c", "-o", cls.build / "test-network-rip")
        run(*cls.flags, *common, "tests/network_rip_loopback.c", "-o", cls.build / "network-rip-loopback")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kui-network-rip-integration-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)

    def initialize(self, image):
        identity, key_path, config = (self.base / name for name in (image.stem + ".json", image.stem + ".key", image.stem + ".cfg"))
        metadata = server.initialize(image, identity, key_path, "10.0.0.2", 7412, config)
        return identity, server.load_key(key_path), metadata["volume"]

    def test_portable_unit_guards(self):
        self.assertIn("PASS independent authenticated vectors", run(self.build / "test-network-rip"))

    def test_native_adapter_session_guards(self):
        fatfs = ROOT / ".deps/fatfs/source"
        if not (fatfs / "ff.h").is_file():
            self.skipTest("native adapter session checks require make deps (FatFs headers)")
        binary = self.build / "test-capture-network"
        run(*self.flags, "-I" + str(fatfs), "-Ifirmware/kui-wifi/components/kwlink/include", "-Itests/apps_stubs",
            "src/core/network_rip.c", "src/core/hash.c", "src/core/data.c", "src/apps/capture_network.c",
            "tests/test_capture_network.c", "-o", binary)
        self.assertIn("PASS native PC session", run(binary))

    def test_real_tcp_fragmentation_random_io_and_refusal(self):
        image = self.base / "wire.img"
        fixture_image(image)
        identity, key, volume = self.initialize(image)
        with Receiver(image, identity, key) as receiver:
            binary = self.build / "network-rip-loopback"
            self.assertIn("PASS fragmented", run(binary, receiver.port, key.hex(), volume, "io"))
            wrong_key = bytes([key[0] ^ 1]) + key[1:]
            self.assertIn("PASS refused", run(binary, receiver.port, wrong_key.hex(), volume, "refused"))
            wrong_volume = bytes([bytes.fromhex(volume)[0] ^ 1]) + bytes.fromhex(volume)[1:]
            self.assertIn("PASS refused", run(binary, receiver.port, key.hex(), wrong_volume.hex(), "refused"))

    def test_actual_capture_resume_verify_on_fat32_and_exfat(self):
        tools = {name: shutil.which(name) for name in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat")}
        if not all(tools.values()):
            self.skipTest("real filesystem capture requires mkfs.fat, mkfs.exfat and their fsck tools")
        fatfs = ROOT / ".deps/fatfs/source"
        if not (fatfs / "ff.h").is_file():
            self.skipTest("real filesystem capture requires make deps (FatFs)")
        codec = ["-Ithird_party/miniz", "-Ithird_party/minilzo", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_STDIO",
                 "-DMINIZ_NO_TIME", "-DMINIZ_NO_ZLIB_APIS", "-DMINIZ_NO_MALLOC", "-DLZO_CFG_NO_UNALIGNED=1"]
        sources = ["src/core/network_rip.c", "src/core/command.c", "src/core/data.c", "src/core/diskio.c", "src/core/clock.c",
                   "src/core/destination.c", "src/core/destination_file.c", "src/core/hash.c", "src/core/capture_plan.c",
                   "src/core/capture.c", "src/core/recovery_sector.c", "src/core/known_dumps.c", "src/core/timing.c", "src/core/capture_export.c",
                   "src/core/capture_export_file.c", "src/core/capture_cooked.c", "src/core/capture_chd.c", "third_party/miniz/miniz.c",
                   "third_party/minilzo/minilzo.c", "src/core/storage_probe.c", str(fatfs / "ff.c"), str(fatfs / "ffunicode.c"),
                   "tests/network_rip_image.c"]
        binary = self.build / "network-rip-image"
        run(*self.flags, "-I" + str(fatfs), *codec, *sources, "-o", binary)
        for kind in ("fat32", "exfat"):
            with self.subTest(filesystem=kind):
                image = self.base / (kind + ".img")
                with image.open("wb") as stream:
                    # FatFs clips block I/O at cluster boundaries. A 96 MiB
                    # FAT32 fixture defaults to one-sector clusters, making
                    # this full GD capture spend its deadline on tens of
                    # thousands of tiny round trips. Keep the full payload
                    # and deadline, using 4 KiB clusters as on larger media.
                    # 512 MiB leaves more than FAT32's 65,525 minimum clusters.
                    stream.truncate((512 if kind == "fat32" else 96) * 1024 * 1024)
                if kind == "fat32":
                    # Explicit track geometry prevents mkfs.fat rounding the
                    # BPB capacity below the dedicated image's exact length.
                    run(tools["mkfs.fat"], "-F", "32", "-s", "8", "-g", "1/32", image)
                    with image.open("rb") as stream:
                        self.assertEqual(stream.read(512)[13], 8)
                else:
                    run(tools["mkfs.exfat"], image)
                identity, key, volume = self.initialize(image)
                with Receiver(image, identity, key) as receiver:
                    args = [binary, receiver.port, key.hex(), volume, image]
                    run(*args, "seed")
                    stopped = run(*args, "new", "stop-early", expected=3)
                    self.assertIn("RESULT 1", stopped)
                    resumed = run(*args, "resume")
                    self.assertIn("RESULT 2", resumed)
                    self.assertIn("verified=1", resumed)
                    verified = run(*args, "verify")
                    self.assertIn("RESULT 2", verified)
                    self.assertIn("verified=1", verified)
                    output = self.base / (kind + "-export")
                    output.mkdir()
                    run(*args, "export", output)
                run(tools["fsck.fat"] if kind == "fat32" else tools["fsck.exfat"], "-n", image)
                jobs = list(output.iterdir())
                self.assertEqual(len(jobs), 1)
                # Independent PC dump verifier checks saved tracks, descriptor,
                # manifest hashes, EDC and track layout after network readback.
                verified = run("python3", ROOT / "tools/verify_dump.py", jobs[0])
                self.assertIn("VERIFIED", verified.upper())


if __name__ == "__main__":
    unittest.main()
