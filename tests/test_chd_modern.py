#!/usr/bin/env python3
"""MAME-generated v5 CD/GD decoding, independent byte oracles and fault tests.

Set KUI_CHDMAN=/path/to/chdman (MAME mame-tools). Images are generated from
synthetic tracks in a temporary directory; no game or generated image is kept
in the repository. KUI_REQUIRE_CODEC_ORACLES=1 makes a missing oracle fatal.
"""
import hashlib
import os
from pathlib import Path
import random
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

from test_capture_chd_oracle import FIXTURE as CAPTURE_FIXTURE, COUNTS, STARTS, AUDIO

ROOT = Path(__file__).resolve().parents[1]
CHDMAN = shutil.which(os.environ.get("KUI_CHDMAN", "chdman"))
FRAME = 2448
SANITIZERS = shlex.split(os.environ.get(
    "KUI_CHD_SANITIZERS", "-fsanitize=address,undefined -fno-omit-frame-pointer"))


def run(command, *, cwd=ROOT, timeout=180, expected_code=0):
    environment = dict(os.environ)
    # LeakSanitizer cannot use ptrace in the managed test environment. Address
    # and undefined-behaviour checks remain enabled for every reader object.
    environment.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    result = subprocess.run(list(map(str, command)), cwd=cwd, env=environment,
                            capture_output=True, timeout=timeout)
    if result.returncode != expected_code:
        raise RuntimeError(f"Command failed ({result.returncode}): {command}\n"
                           f"{result.stdout.decode(errors='replace')}\n"
                           f"{result.stderr.decode(errors='replace')}")
    return result


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def synthetic_track(number, sectors, stride, audio=False, raw_mode=0):
    """Mix repeated hunks, random data and stereo samples with unequal bytes."""
    rng = random.Random(0x4B5549434844 + number)
    result = bytearray()
    for sector in range(sectors):
        if sector < 8:
            frame = bytearray(bytes([number * 19]) * stride)
        elif sector < 16:
            frame = bytearray(rng.randbytes(stride))
        else:
            frame = bytearray((number * 29 + sector * 17 + n * 7) & 255 for n in range(stride))
        if audio:
            # A second independent pattern catches accidental sample swapping
            # even when the codec takes a raw or a self-reference path.
            for sample in range(0, stride, 2):
                frame[sample] ^= 0x35
                frame[sample + 1] ^= 0xA7
        elif raw_mode:
            # Deliberately include both raw non-ECC sectors and CD sync/mode
            # headers. The importer must preserve all raw bytes exactly.
            frame[0:12] = b"\0" + b"\xff" * 10 + b"\0"
            frame[12:16] = bytes((0, sector // 75 + 2, sector % 75, raw_mode))
        result.extend(frame)
    return bytes(result)


def metadata(data):
    version = struct.unpack_from(">I", data, 12)[0]
    offset = struct.unpack_from(">Q", data, 48 if version == 5 else 36)[0]
    entries = []
    while offset:
        tag, length, following = struct.unpack_from(">4sIQ", data, offset)
        length &= 0xFFFFFF
        text = data[offset + 16:offset + 16 + length].rstrip(b"\0").decode()
        entries.append((offset, tag, dict(part.split(":", 1) for part in text.split())))
        offset = following
    return entries


def legacy_audio_image(*, chain=0):
    """Construct v4 optical layouts independently of both C decoders.

    MAME only writes v5 now. Its verify/extract reader remains the external
    oracle for this explicit big-endian v4 map and CRC32 implementation.
    """
    hunk_bytes = 8 * FRAME
    if chain:
        decoded = [bytes(hunk_bytes)] * chain
        entries = [(3, 0)] + [(4, n - 1) for n in range(1, chain)]
    else:
        original = synthetic_track(7, 16, 2352, audio=True)
        canonical = bytearray()
        for n in range(0, len(original), 2352):
            sector = original[n:n + 2352]
            canonical.extend(b"".join(sector[i:i + 2][::-1] for i in range(0, 2352, 2)))
            canonical.extend(bytes(96))
        first, second = bytes(canonical[:hunk_bytes]), bytes(canonical[hunk_bytes:])
        decoded = [first, second, bytes(hunk_bytes), first]
        entries = [(1, first), (2, second), (3, 0), (4, 0)]
    count = len(entries)
    payload = bytearray()
    rawmap = bytearray()
    offset = 108 + 16 * (count + 1)
    rawsha = hashlib.sha1()
    for (kind, value), raw in zip(entries, decoded):
        rawsha.update(raw)
        if kind in (1, 2):
            packed = zlib.compress(value, 9)[2:-4] if kind == 1 else value
            entry_offset, length = offset + len(payload), len(packed)
            payload.extend(packed)
        else:
            entry_offset, length = value, 0
        rawmap.extend(struct.pack(">QIHBB", entry_offset, zlib.crc32(raw),
                                  length & 0xFFFF, length >> 16, kind))
    text = (f"TRACK:1 TYPE:AUDIO SUBTYPE:NONE FRAMES:{count * 8} "
            "PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0\0").encode()
    tag = b"CHT2"
    overall = hashlib.sha1(rawsha.digest() + tag + hashlib.sha1(text).digest()).digest()
    header = bytearray(108)
    header[:8] = b"MComprHD"
    struct.pack_into(">5I2QI", header, 8, 108, 4, 0, 1, count,
                     count * hunk_bytes, offset + len(payload), hunk_bytes)
    header[48:68], header[88:108] = overall, rawsha.digest()
    meta = struct.pack(">4sIQ", tag, 0x01000000 | len(text), 0) + text
    image = bytes(header + rawmap + b"EndOfListCookie\0" + payload + meta)
    return image, decoded


class ModernCHD(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CHDMAN:
            if os.environ.get("KUI_REQUIRE_CODEC_ORACLES") == "1":
                raise RuntimeError("KUI_REQUIRE_CODEC_ORACLES=1 requires MAME chdman or KUI_CHDMAN")
            raise unittest.SkipTest("MAME chdman is not installed")
        cls.temporary = tempfile.TemporaryDirectory(prefix="kui-modern-chd-")
        cls.folder = Path(cls.temporary.name)
        if os.environ.get("KUI_CHD_KEEP_FIXTURES") == "1":
            cls.temporary._finalizer.detach()
            print(f"CHD oracle fixtures: {cls.folder}", flush=True)
        cls.reader = cls.folder / "reader"
        cls.exporter = cls.folder / "exporter"
        cls.memory = cls.folder / "memory"
        cls.build_reader()
        cls.fixtures = {}
        # 45000 density boundary, explicit PAD and alignment are intentionally
        # independent of the stored frame addresses. Mixed 2048/2352 tracks
        # also prove that a cooked CHD track is never silently exported raw.
        cls.create_geometry("gd", [
            (0, 4, 2352, 12, 1), (162, 0, 2352, 12, 0),
            (45000, 4, 2048, 37, 0), (45187, 0, 2352, 17, 0),
            (45354, 4, 2352, 19, 2)], gdi=True)
        cls.create_geometry("cd", [
            (0, 4, 2352, 17, 1), (17, 0, 2352, 23, 0),
            (40, 4, 2352, 19, 2)], gdi=False)
        for geometry in ("gd", "cd"):
            folder = cls.folder / geometry
            descriptor = folder / ("source.gdi" if geometry == "gd" else "source.cue")
            for codec in ("none", "cdzl", "cdlz", "cdfl", "cdzs", "default"):
                image = folder / (codec + ".chd")
                command = [CHDMAN, "createcd", "-i", descriptor, "-o", image, "-np", "2"]
                if codec != "default":
                    command.extend(("-c", codec))
                run(command)
                run([CHDMAN, "verify", "-i", image])
                cls.fixtures[(geometry, codec)] = image
            raw = folder / "mame-canonical.bin"
            run([CHDMAN, "extractraw", "-i", cls.fixtures[(geometry, "cdzl")], "-o", raw])
            cls.expected[geometry]["canonical"] = sha256_file(raw)
            raw.unlink()
        # Real optical CHDs may use the general codecs too. Keep CD metadata
        # and canonical data identical while forcing each independent decoder.
        for codec in ("zlib", "lzma", "huff", "flac", "zstd"):
            image = cls.folder / "cd" / ("generic-" + codec + ".chd")
            run([CHDMAN, "copy", "-i", cls.fixtures[("cd", "default")],
                 "-o", image, "-c", codec, "-np", "2"])
            run([CHDMAN, "verify", "-i", image])
            cls.fixtures[("cd", "generic-" + codec)] = image

    @classmethod
    def tearDownClass(cls):
        if os.environ.get("KUI_CHD_KEEP_FIXTURES") != "1":
            cls.temporary.cleanup()

    @classmethod
    def build_reader(cls):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        flags = ["-std=c11", "-O1", "-g", *SANITIZERS]
        vendor = ROOT / "third_party/libchdr"
        sources = sorted((vendor / "src").glob("*.c")) + [
            vendor / "deps/lzma-26.02/src/LzmaDec.c",
            vendor / "deps/miniz-3.1.2/miniz.c",
            vendor / "deps/zstd-1.5.7/zstddeclib.c"]
        objects = []
        for index, source in enumerate(sources):
            obj = cls.folder / f"codec-{index}.o"
            run([*compiler, *flags, "-Ithird_party/libchdr/include", "-include",
                 "third_party/libchdr/kui_config.h", "-c", source, "-o", obj])
            objects.append(obj)
        for index, source in enumerate(("tests/test_chd_modern.c", "src/core/chd_lib.c",
                                        "src/core/hash.c", "src/core/data.c")):
            obj = cls.folder / f"reader-{index}.o"
            run([*compiler, *flags, "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                 "-Iinclude", "-Ithird_party/libchdr/include", "-c", source, "-o", obj])
            objects.append(obj)
        run([*compiler, *flags, *objects, "-lm", "-o", cls.reader])
        export_objects = []
        for index, source in enumerate(("tests/test_chd_modern_export.c", "src/core/chd_cache.c")):
            obj = cls.folder / f"export-{index}.o"
            run([*compiler, *flags, "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                 "-Iinclude", "-c", source, "-o", obj])
            export_objects.append(obj)
        # Reuse the exact decoder and hashing objects from the read tests.
        run([*compiler, *flags, *objects[:-4], *objects[-3:], *export_objects,
             "-lm", "-o", cls.exporter])
        memory = cls.folder / "memory.o"
        run([*compiler, *flags, "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Iinclude",
             "-c", "tests/test_chd_memory.c", "-o", memory])
        run([*compiler, *flags, *objects[:-4], *objects[-3:], memory,
             "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free", "-lm", "-o", cls.memory])

    expected = {}

    @classmethod
    def create_geometry(cls, name, tracks, *, gdi):
        folder = cls.folder / name
        folder.mkdir()
        source_hashes = []
        descriptor = []
        if gdi:
            descriptor.append(str(len(tracks)))
        for index, (lba, control, stride, sectors, mode) in enumerate(tracks, 1):
            filename = f"track{index:02}.bin"
            data = synthetic_track(index, sectors, stride, not control, mode)
            (folder / filename).write_bytes(data)
            source_hashes.append(hashlib.sha256(data).hexdigest())
            if gdi:
                descriptor.append(f"{index} {lba} {control} {stride} {filename} 0")
            else:
                track_type = "AUDIO" if not control else f"MODE{mode}/2352"
                descriptor.extend((f'FILE "{filename}" BINARY',
                                   f"  TRACK {index:02} {track_type}", "    INDEX 01 00:00:00"))
        suffix = "gdi" if gdi else "cue"
        (folder / ("source." + suffix)).write_text("\n".join(descriptor) + "\n")
        cls.expected[name] = dict(tracks=tracks, hashes=source_hashes)

    def check_image(self, geometry, codec):
        output = run([self.reader, self.fixtures[(geometry, codec)]]).stdout.decode()
        lines = [line.split() for line in output.splitlines()]
        digests = {(line[0], int(line[1])): line[2] for line in lines
                   if line[0] in ("canonical", "track")}
        self.assertEqual(digests[("canonical", 0)], self.expected[geometry]["canonical"])
        info = next(line for line in lines if line[0] == "info")
        self.assertEqual((int(info[1]), int(info[4]), int(info[5]), int(info[6])),
                         (5, len(self.expected[geometry]["tracks"]), int(geometry == "gd"), 0))
        actual_tracks = [list(map(int, line[1:])) for line in lines if line[0] == "trackinfo"]
        storage = 0
        for index, ((lba, control, stride, sectors, mode), actual) in enumerate(
                zip(self.expected[geometry]["tracks"], actual_tracks)):
            self.assertEqual(digests[("track", index)], self.expected[geometry]["hashes"][index])
            frames = (self.expected[geometry]["tracks"][index + 1][0] - lba
                      if index + 1 < len(actual_tracks) else sectors)
            stored = (frames + 3) & ~3
            if control and geometry == "gd":
                # GDI declares only control/stride, so MAME labels both raw
                # and cooked data MODE1. Raw header bytes still stay exact.
                mode = 1
            self.assertEqual(actual, [index, lba, storage, frames, frames - sectors,
                                      stored, stride, control, mode])
            storage += stored
        checks = int(next(line[1] for line in lines if line[0] == "checks"))
        self.assertGreater(checks, 200)

    def test_cd_all_common_codecs_and_byte_order(self):
        for codec in ("none", "cdzl", "cdlz", "cdfl", "cdzs", "default"):
            with self.subTest(codec=codec):
                self.check_image("cd", codec)

    def test_gd_density_pad_alignment_and_cooked_tracks(self):
        for codec in ("none", "cdzl", "cdlz", "cdfl", "cdzs", "default"):
            with self.subTest(codec=codec):
                self.check_image("gd", codec)

    def test_general_codecs_on_optical_chd(self):
        for codec in ("zlib", "lzma", "huff", "flac", "zstd"):
            with self.subTest(codec=codec):
                self.check_image("cd", "generic-" + codec)

    def test_fixture_really_uses_raw_self_and_requested_codecs(self):
        for codec, description in (("cdzl", "CD Deflate"), ("cdlz", "CD LZMA"),
                                   ("cdfl", "CD FLAC"), ("cdzs", "CD Zstandard")):
            with self.subTest(codec=codec):
                image = self.fixtures[("gd", codec)]
                data = image.read_bytes()
                self.assertEqual(data[16:20], codec.encode())
                output = run([CHDMAN, "info", "-i", image, "-v"]).stdout.decode()
                self.assertRegex(output, r"[1-9][0-9]*\s+[0-9.]+%\s+Copy from self")
                self.assertRegex(output, rf"[1-9][0-9]*\s+[0-9.]+%\s+{description}")
        output = run([CHDMAN, "info", "-i", self.fixtures[("gd", "cdzl")], "-v"]).stdout.decode()
        self.assertRegex(output, r"[1-9][0-9]*\s+[0-9.]+%\s+Uncompressed")

    def reject(self, name, data):
        image = self.folder / (name + ".chd")
        image.write_bytes(data)
        self.assertRegex(run([self.reader, image, "reject"]).stdout.decode(), r"^rejected ")

    def test_header_rejections(self):
        original = self.fixtures[("cd", "cdzl")].read_bytes()
        for name, offset, replacement in (
                ("magic", 0, b"BADMAGIC"), ("version3", 12, struct.pack(">I", 3)),
                ("header-size", 8, struct.pack(">I", 108)), ("parent", 104, bytes([1]) * 20),
                ("unknown-codec", 16, b"xxxx"), ("zero-hunk", 56, bytes(4)),
                ("wrong-unit", 60, struct.pack(">I", 2048)),
                ("overlarge-hunk", 56, struct.pack(">I", 1024 * 1024)),
                ("logical-overflow", 32, bytes([255]) * 8)):
            with self.subTest(case=name):
                data = bytearray(original)
                data[offset:offset + len(replacement)] = replacement
                self.reject(name, data)
        for cut in (0, 8, 107, 123, len(original) - 1):
            with self.subTest(truncated=cut):
                self.reject("truncated-" + str(cut), original[:cut])

    def test_map_and_payload_corruption(self):
        original = self.fixtures[("cd", "cdzl")].read_bytes()
        offset = struct.unpack_from(">Q", original, 40)[0]
        self.assertGreater(offset, 124)
        for name, at in (("map-crc", offset + 10), ("map-bits", offset + 16)):
            with self.subTest(case=name):
                data = bytearray(original)
                data[at] ^= 0x80
                self.reject(name, data)
        first_payload = int.from_bytes(original[offset + 4:offset + 10], "big")
        self.assertGreaterEqual(first_payload, 124)
        data = bytearray(original)
        data[first_payload + 7] ^= 0x45
        self.reject("payload-crc", data)

    def test_metadata_rejections(self):
        original = self.fixtures[("gd", "cdzl")].read_bytes()
        entries = metadata(original)
        offset = entries[0][0]
        for name, before, after in (("pregap", b"PREGAP:0", b"PREGAP:1"),
                                   ("postgap", b"POSTGAP:0", b"POSTGAP:1"),
                                   ("subchannel", b"SUBTYPE:NONE", b"SUBTYPE:RW  "),
                                   ("track-number", b"TRACK:1", b"TRACK:9")):
            with self.subTest(case=name):
                data = bytearray(original)
                at = data.index(before, offset + 16)
                data[at:at + len(before)] = after
                self.reject(name, data)
        data = bytearray(original)
        struct.pack_into(">Q", data, offset + 8, offset)
        self.reject("metadata-cycle", data)

    def test_parent_delta_is_explicitly_rejected(self):
        parent = self.fixtures[("cd", "cdzl")]
        child = self.folder / "child.chd"
        run([CHDMAN, "copy", "-i", parent, "-op", parent, "-o", child, "-np", "2"])
        self.assertNotEqual(child.read_bytes()[104:124], bytes(20))
        self.assertRegex(run([self.reader, child, "reject"]).stdout.decode(), "unsupported")

    def test_v4_raw_deflate_mini_self_and_crc32(self):
        data, hunks = legacy_audio_image()
        image = self.folder / "legacy-audio.chd"
        image.write_bytes(data)
        run([CHDMAN, "verify", "-i", image])
        canonical = self.folder / "legacy-canonical.bin"
        run([CHDMAN, "extractraw", "-i", image, "-o", canonical])
        self.assertEqual(canonical.read_bytes(), b"".join(hunks))
        output = run([self.reader, image]).stdout.decode()
        hashes = {tuple(row.split()[:2]): row.split()[2] for row in output.splitlines()
                  if row.startswith(("canonical ", "track "))}
        self.assertEqual(hashes[("canonical", "0")], hashlib.sha256(b"".join(hunks)).hexdigest())
        audio = bytearray()
        for hunk in hunks:
            for at in range(0, len(hunk), FRAME):
                frame = hunk[at:at + 2352]
                audio.extend(b"".join(frame[n:n + 2][::-1] for n in range(0, 2352, 2)))
        self.assertEqual(hashes[("track", "0")], hashlib.sha256(audio).hexdigest())
        for name, hunk, within, replacement in (
                ("v4-kind-zero", 0, 15, b"\0"), ("v4-kind-six", 0, 15, b"\x06"),
                ("v4-kind-fifteen", 0, 15, b"\x0f"), ("v4-unknown-flags", 0, 15, b"\x81"),
                ("v4-no-crc-direct", 0, 15, b"\x11"), ("v4-no-crc-mini", 2, 15, b"\x13"),
                ("v4-raw-short", 1, 12, b"\x00\x01"),
                ("v4-self-forward", 0, 0, struct.pack(">Q", 3)),
                ("v4-self-cycle", 3, 0, struct.pack(">Q", 3)),
                ("v4-self-range", 3, 0, struct.pack(">Q", 0xFFFFFFFF)),
                ("v4-direct-crc", 0, 8, b"\0\0\0\0")):
            with self.subTest(case=name):
                corrupt = bytearray(data)
                if name == "v4-self-forward":
                    corrupt[108 + 15] = 4
                    corrupt[108 + 12:108 + 15] = bytes(3)
                at = 108 + hunk * 16 + within
                corrupt[at:at + len(replacement)] = replacement
                self.reject(name, corrupt)

    def test_v4_cold_deep_self_chain_is_bounded(self):
        image = self.folder / "v4-chain.chd"
        valid, _ = legacy_audio_image(chain=1024)
        image.write_bytes(valid)
        result = run([self.reader, image, "last"]).stdout.decode()
        self.assertIn(hashlib.sha256(bytes(FRAME)).hexdigest(), result)
        deep, _ = legacy_audio_image(chain=4098)
        image.write_bytes(deep)
        self.assertRegex(run([self.reader, image, "reject-last"]).stdout.decode(), "rejected")

    def test_existing_capture_v4_is_readable_and_exportable(self):
        folder = self.folder / "capture-v4"
        folder.mkdir()
        fixture = folder / "writer.c"
        fixture.write_text(CAPTURE_FIXTURE)
        writer = folder / "writer"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        flags = ["-std=c11", "-O1", "-g", "-Iinclude", "-I.deps/fatfs/source", "-Isrc/core",
                 "-Ithird_party/miniz", "-Ithird_party/minilzo", "-DMINIZ_NO_ARCHIVE_APIS",
                 "-DMINIZ_NO_STDIO", "-DMINIZ_NO_TIME", "-DMINIZ_NO_ZLIB_APIS", "-DMINIZ_NO_MALLOC"]
        run([*compiler, *flags, fixture, "src/core/capture_export.c", "src/core/capture_chd.c",
             "src/core/data.c", "src/core/hash.c", "third_party/miniz/miniz.c",
             "third_party/minilzo/minilzo.c", "-Wl,--wrap=kui_export_deflate", "-o", writer])
        image = folder / "capture.chd"
        run([writer, image], cwd=folder)
        run([CHDMAN, "verify", "-i", image])
        raw = folder / "canonical.bin"
        run([CHDMAN, "extractraw", "-i", image, "-o", raw])
        lines = run([self.reader, image]).stdout.decode().splitlines()
        hashes = {tuple(row.split()[:2]): row.split()[2] for row in lines
                  if row.startswith(("canonical ", "track "))}
        self.assertEqual(hashes[("canonical", "0")], sha256_file(raw))
        destination = folder / "export"
        destination.mkdir()
        run([self.exporter, image, destination])
        descriptor = (destination / "result/disc.gdi").read_text().splitlines()
        self.assertEqual(descriptor[0], "5")
        for track in range(5):
            original = folder / f"track{track + 1:02}.bin"
            expected = sha256_file(original)
            self.assertEqual(hashes[("track", str(track))], expected)
            exported = destination / f"result/track{track + 1:02}.bin"
            self.assertEqual(exported.stat().st_size, COUNTS[track] * 2352)
            self.assertEqual(sha256_file(exported), expected)
            self.assertEqual(descriptor[track + 1],
                f"{track + 1} {STARTS[track]} {0 if AUDIO[track] else 4} 2352 track{track + 1:02}.bin 0")

    def test_every_decoder_allocation_failure_cleans_up_and_retries(self):
        for fixture in (("gd", "default"), ("cd", "generic-huff")):
            image = self.fixtures[fixture]
            baseline = run([self.memory, image]).stdout.decode()
            fields = dict(part.split("=", 1) for part in baseline.split()[1:])
            self.assertEqual(fields["retained"], "0")
            self.assertEqual(fields["fired"], "0")
            attempts = int(fields["attempts"])
            self.assertGreater(attempts, 10)
            print("CHD decoder heap " + "/".join(fixture) + " " + " ".join(
                  f"{key}={fields[key]}" for key in ("opened", "peak", "largest", "attempts")), flush=True)
            for attempt in range(1, attempts + 1):
                with self.subTest(fixture=fixture, allocation=attempt):
                    result = run([self.memory, image, attempt]).stdout.decode()
                    fields = dict(part.split("=", 1) for part in result.split()[1:])
                    self.assertEqual(fields["fired"], "1")
                    self.assertEqual(fields["retained"], "0")

    def test_real_reader_to_verified_gdi_publication(self):
        for geometry in ("cd", "gd"):
            for codec in ("none", "cdzl", "cdlz", "cdfl", "cdzs", "default"):
                with self.subTest(geometry=geometry, codec=codec):
                    destination = self.folder / f"export-{geometry}-{codec}"
                    destination.mkdir()
                    run([self.exporter, self.fixtures[(geometry, codec)], destination])
                    self.assertFalse((destination / "staging").exists())
                    published = destination / "result"
                    descriptor = published / "disc.gdi"
                    lines = descriptor.read_text().splitlines()
                    tracks = self.expected[geometry]["tracks"]
                    self.assertEqual(lines[0], str(len(tracks)))
                    for index, ((lba, control, stride, sectors, mode), line) in enumerate(zip(tracks, lines[1:])):
                        filename = f"track{index + 1:02}.bin"
                        self.assertEqual(line, f"{index + 1} {lba} {control} {stride} {filename} 0")
                        output = published / filename
                        self.assertEqual(output.stat().st_size, sectors * stride)
                        self.assertEqual(sha256_file(output), self.expected[geometry]["hashes"][index])

    def test_decode_failure_never_publishes_partial_gdi(self):
        data = bytearray(self.fixtures[("cd", "cdzl")].read_bytes())
        map_offset = struct.unpack_from(">Q", data, 40)[0]
        first_payload = int.from_bytes(data[map_offset + 4:map_offset + 10], "big")
        data[first_payload + 7] ^= 0x45
        source = self.folder / "export-corrupt.chd"
        source.write_bytes(data)
        original_hash = sha256_file(source)
        destination = self.folder / "export-corrupt"
        destination.mkdir()
        result = run([self.exporter, source, destination], timeout=120, expected_code=4)
        self.assertEqual(result.returncode, 4, result.stderr.decode())
        self.assertFalse((destination / "result").exists())
        self.assertFalse((destination / "staging").exists())
        self.assertEqual(sha256_file(source), original_hash)

    def test_publication_preserves_existing_cache(self):
        destination = self.folder / "export-existing"
        final = destination / "result"
        final.mkdir(parents=True)
        sentinel = final / "existing-owner.bin"
        sentinel.write_bytes(b"pre-existing verified cache")
        result = run([self.exporter, self.fixtures[("cd", "cdzl")], destination],
                     timeout=120, expected_code=4)
        self.assertEqual(result.returncode, 4, result.stderr.decode())
        self.assertEqual(sentinel.read_bytes(), b"pre-existing verified cache")
        self.assertEqual(list(final.iterdir()), [sentinel])
        self.assertFalse((destination / "staging").exists())


if __name__ == "__main__":
    unittest.main()
