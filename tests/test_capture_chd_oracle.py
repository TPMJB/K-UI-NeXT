#!/usr/bin/env python3
"""Independent CHDv4 checks; real MAME/libchdr are optional external oracles.

Run with KUI_CHDMAN=/path/to/chdman and optionally
KUI_LIBCHDR_DUMP=/path/to/libchdr/tests/chd_dump_order. CI should install
mame-tools so verify/extract are exercised rather than skipped. libchdr
607694ca0812edfc9cc2030c64634fc2393668de is the tested independent reader.
"""
import hashlib
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
CHDMAN = shutil.which(os.environ.get("KUI_CHDMAN", "chdman"))
LIBCHDR = shutil.which(os.environ.get("KUI_LIBCHDR_DUMP", ""))
COUNTS = (10, 7, 13, 5, 9)
STARTS = (0, 160, 45000, 45163, 45318)
AUDIO = (False, True, False, True, False)
FRAME = 2448

FIXTURE = r'''
#define _POSIX_C_SOURCE 200809L
#include "capture_export_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *container;
static unsigned char *sources[5];
static const unsigned counts[]={10,7,13,5,9},starts[]={150,310,45150,45313,45468};
static const unsigned controls[]={4,0,4,0,4};
static const unsigned modes[]={1,0,1,0,2};
size_t __real_kui_export_deflate(const uint8_t *,size_t,uint8_t *,size_t);
size_t __wrap_kui_export_deflate(const uint8_t *src,size_t size,uint8_t *out,size_t capacity) {
 /* Exercise the legitimate uncompressed fallback once. NONE subchannel
  * filler compresses even random mainchannel frames, so entropy alone
  * cannot reliably exercise both v4 map entry types. */
 static unsigned calls;if(!calls++)return 0;
 return __real_kui_export_deflate(src,size,out,capacity);
}
static bool track(void *c,unsigned n,uint64_t at,void *out,size_t size) {
 (void)c;if(n>=5 || at>(uint64_t)counts[n]*2352 || size>(uint64_t)counts[n]*2352-at)return false;
 memcpy(out,sources[n]+at,size);return true;
}
static bool read_container(void *c,uint64_t at,void *out,size_t size) {
 (void)c;return !fseeko(container,(off_t)at,SEEK_SET) && fread(out,1,size,container)==size;
}
static bool write_container(void *c,uint64_t at,const void *data,size_t size) {
 (void)c;return !fseeko(container,(off_t)at,SEEK_SET) && fwrite(data,1,size,container)==size;
}
static bool sync_container(void *c) {(void)c;return fflush(container)==0;}
int main(int argc,char **argv) {
 if(argc!=2 || !(container=fopen(argv[1],"w+b")))return 1;
 struct kui_capture_plan plan={0};struct kui_checkpoint state={0};plan.count=state.count=5;
 for(unsigned n=0;n<5;n++) {
  unsigned end=starts[n]+counts[n];
  unsigned toc_end=n==0 || n==2 || n==3?end+150:end;
  plan.tracks[n]=(struct kui_capture_track){n+1,controls[n],n<2?0:1,starts[n],end,toc_end};
  state.track[n].sectors=counts[n];state.track[n].sector_mode=modes[n];
  size_t bytes=(size_t)counts[n]*2352;sources[n]=malloc(bytes);if(!sources[n])return 2;
  uint32_t rng=0x12345678u+n*97u;
  for(size_t i=0;i<bytes;i++) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;sources[n][i]=(unsigned char)rng;}
  if(controls[n]) for(unsigned s=0;s<counts[n];s++) sources[n][s*2352+15]=(unsigned char)modes[n];
  state.track[n].crc32=kui_crc32(0,sources[n],bytes);
  struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,sources[n],bytes);
  kui_sha256_digest(&sha,state.track[n].sha256);
  char filename[32];snprintf(filename,sizeof(filename),"track%02u.bin",n+1);
  FILE *fp=fopen(filename,"wb");if(!fp || fwrite(sources[n],1,bytes,fp)!=bytes || fclose(fp))return 3;
 }
 struct kui_capture_export_io io={NULL,track,read_container,write_container,sync_container,NULL,NULL};
 struct kui_capture_export_report report={0};
 enum kui_capture_export_result result=kui_capture_chd_write(&plan,&state,&io,&report);
 if(fclose(container) || result!=KUI_EXPORT_COMPLETE)return 4;
 for(unsigned n=0;n<5;n++)free(sources[n]);return 0;
}
'''


def run(args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, timeout=90, **kwargs)


def metadata_entries(data):
    offset = struct.unpack_from(">Q", data, 36)[0]
    entries = []
    while offset:
        _, length, following = struct.unpack_from(">4sIQ", data, offset)
        text = data[offset + 16:offset + 16 + (length & 0xFFFFFF)]
        entries.append(dict(item.split(":", 1) for item in text.rstrip(b"\0").decode().split()))
        offset = following
    return entries


class CaptureCHDOracle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.environ.get("KUI_REQUIRE_CODEC_ORACLES") == "1" and not CHDMAN:
            raise RuntimeError("KUI_REQUIRE_CODEC_ORACLES=1 requires installed MAME chdman or KUI_CHDMAN")
        cls.tmp = tempfile.TemporaryDirectory(prefix="kui-chd-oracle-")
        cls.folder = Path(cls.tmp.name)
        fixture = cls.folder / "fixture.c"
        fixture.write_text(FIXTURE)
        executable = cls.folder / "fixture"
        sources = ["src/core/capture_export.c", "src/core/capture_chd.c", "src/core/data.c",
                   "src/core/hash.c", "third_party/miniz/miniz.c", "third_party/minilzo/minilzo.c"]
        flags = ["-std=c11", "-O2", "-Iinclude", "-I.deps/fatfs/source", "-Isrc/core",
                 "-Ithird_party/miniz", "-Ithird_party/minilzo", "-DMINIZ_NO_ARCHIVE_APIS",
                 "-DMINIZ_NO_STDIO", "-DMINIZ_NO_TIME", "-DMINIZ_NO_ZLIB_APIS", "-DMINIZ_NO_MALLOC"]
        run([*shlex.split(os.environ.get("CC", "cc")), *flags, str(fixture), *sources,
             "-Wl,--wrap=kui_export_deflate", "-o", str(executable)], cwd=ROOT)
        cls.image = cls.folder / "fixture.chd"
        run([str(executable), str(cls.image)], cwd=cls.folder)
        cls.originals = [(cls.folder / f"track{n + 1:02}.bin").read_bytes() for n in range(5)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_header_map_checksums_and_independent_inflate(self):
        data = self.image.read_bytes()
        self.assertEqual(data[:8], b"MComprHD")
        length, version, flags, codec, hunks = struct.unpack_from(">5I", data, 8)
        logical, meta = struct.unpack_from(">2Q", data, 28)
        hunk = struct.unpack_from(">I", data, 44)[0]
        self.assertEqual((length, version, flags, codec, hunk), (108, 4, 0, 1, 8 * FRAME))
        self.assertNotEqual(logical % hunk, 0, "Exercise final partially logical hunk")
        self.assertEqual(data[108 + 16 * hunks:108 + 16 * (hunks + 1)], b"EndOfListCookie\0")
        rawsha = hashlib.sha1()
        seen_types = set()
        decoded = bytearray()
        for i in range(hunks):
            offset, crc, low, high, kind = struct.unpack_from(">QIHBB", data, 108 + 16 * i)
            size = low | (high << 16)
            self.assertIn(kind, (1, 2))
            seen_types.add(kind)
            stored = data[offset:offset + size]
            payload = zlib.decompress(stored, -15) if kind == 1 else stored
            self.assertEqual(len(payload), hunk)
            self.assertEqual(zlib.crc32(payload), crc)
            used = min(hunk, logical - len(decoded))
            rawsha.update(payload[:used])
            decoded.extend(payload[:used])
        self.assertEqual(seen_types, {1, 2}, "Fixture must exercise compression and uncompressed fallback")
        self.assertEqual(rawsha.digest(), data[88:108])
        metadata_hashes = []
        entries = []
        while meta:
            tag, flen, following = struct.unpack_from(">4sIQ", data, meta)
            size = flen & 0xFFFFFF
            text = data[meta + 16:meta + 16 + size]
            self.assertEqual((tag, flen >> 24), (b"CHGD", 1))
            self.assertIn(b"SUBTYPE:NONE", text)
            self.assertNotIn(b"RW", text)
            metadata_hashes.append(tag + hashlib.sha1(text).digest())
            entries.append(dict(item.split(":", 1) for item in text.rstrip(b"\0").decode().split()))
            meta = following
        self.assertEqual(len(entries), 5)
        self.assertEqual(hashlib.sha1(rawsha.digest() + b"".join(sorted(metadata_hashes))).digest(), data[48:68])
        frame = 0
        for n, entry in enumerate(entries):
            self.assertEqual(entry["TYPE"], "AUDIO" if AUDIO[n] else "MODE2_RAW" if n == 4 else "MODE1_RAW")
            self.assertEqual(int(entry["FRAMES"]) - int(entry["PAD"]), COUNTS[n])
            original = self.originals[n]
            for sector in range(COUNTS[n]):
                expected = original[sector * 2352:(sector + 1) * 2352]
                if AUDIO[n]:
                    expected = b"".join(expected[i:i + 2][::-1] for i in range(0, 2352, 2))
                at = (frame + sector) * FRAME
                self.assertEqual(decoded[at:at + 2352], expected)
                self.assertEqual(decoded[at + 2352:at + FRAME], bytes(96))
            stored_frames = (int(entry["FRAMES"]) + 3) & ~3
            zero_start = (frame + COUNTS[n]) * FRAME
            self.assertFalse(any(decoded[zero_start:(frame + stored_frames) * FRAME]))
            frame += stored_frames
        self.assertEqual(frame * FRAME, logical)

    def test_reader_metadata_preserves_disc_addresses(self):
        # Independent consumer contract: Flycast's CHD reader and MAME's CD
        # reader accumulate virtual PREGAP before a track, and FRAMES after
        # it. Padding at the end is excluded from track end, not from the
        # next track's address. This catches cumulative gaps that survive
        # container checksums but move the 45000 boot session.
        # https://github.com/flyinghead/flycast/blob/master/core/imgread/chd.cpp
        # https://github.com/mamedev/mame/blob/mame0264/src/lib/util/cdrom.cpp
        fad = 150
        for n, entry in enumerate(metadata_entries(self.image.read_bytes())):
            frames, pregap, pad = (int(entry[key]) for key in ("FRAMES", "PREGAP", "PAD"))
            self.assertEqual(pregap, 0, "GD compatibility profile represents omitted ranges as PAD")
            expected_pad = STARTS[n + 1] - STARTS[n] - COUNTS[n] if n + 1 < len(STARTS) else 0
            self.assertEqual((frames, pad), (COUNTS[n] + expected_pad, expected_pad),
                             f"Track {n + 1} captured frames and explicitly missing tail")
            stored_pregap = pregap if entry["PGTYPE"].startswith("V") else 0
            start = fad + pregap
            fad = start + frames - stored_pregap
            self.assertEqual(start, STARTS[n] + 150, f"Track {n + 1} INDEX 01 address")
            self.assertEqual(fad - pad, STARTS[n] + 150 + COUNTS[n], f"Track {n + 1} captured end")

    @unittest.skipUnless(CHDMAN, "MAME chdman is not installed")
    def test_mame_verify_extract_track_geometry_and_original_audio(self):
        run([CHDMAN, "verify", "-i", str(self.image)])
        cue = self.folder / "roundtrip.cue"
        run([CHDMAN, "extractcd", "-i", str(self.image), "-o", str(cue)])
        cue_bytes = cue.with_suffix(".bin").read_bytes()
        original_bytes = b"".join(self.originals)
        # Legacy MAME 0.264 CUE extraction expands ordinary missing PAD
        # ranges as zero frames, but omits the density hole. Current MAME
        # excludes declared PAD. Both must recover all original LE audio and
        # raw data exactly; no arbitrary bytes or captured gap claim allowed.
        # The old tool explicitly warns that its GD-ROM CUE descriptor is
        # unusable. Its payload is an audio oracle; GDI below proves geometry.
        legacy_cue = bytearray()
        for n, original in enumerate(self.originals):
            legacy_cue.extend(original)
            if n + 1 < len(STARTS) and STARTS[n + 1] != 45000:
                omitted = STARTS[n + 1] - STARTS[n] - COUNTS[n]
                legacy_cue.extend(bytes(omitted * 2352))
        self.assertTrue(cue_bytes == original_bytes or cue_bytes == legacy_cue,
                        "CUE payload differs from captured raw/LE audio plus declared legacy PAD")
        gdi = self.folder / "roundtrip-gdi.gdi"
        run([CHDMAN, "extractcd", "-i", str(self.image), "-o", str(gdi)])
        lines = gdi.read_text().splitlines()
        self.assertEqual(int(lines[0]), 5)
        for n, line in enumerate(lines[1:]):
            fields = shlex.split(line)
            self.assertEqual(tuple(map(int, fields[:4])), (n + 1, STARTS[n], 0 if AUDIO[n] else 4, 2352))
            actual = (self.folder / fields[4]).read_bytes()
            expected = self.originals[n]
            # chdman v4 GD extraction retains canonical CHGD audio byte order;
            # CUE above independently proves reversal to the captured bytes.
            if AUDIO[n]:
                expected = b"".join(expected[i:i + 2][::-1] for i in range(0, len(expected), 2))
            self.assertEqual(actual, expected)

    @unittest.skipUnless(CHDMAN or LIBCHDR, "No independent CHD reader is configured")
    def test_independent_reader_rejects_corrupt_hunk(self):
        data = bytearray(self.image.read_bytes())
        offset, _, low, high, kind = struct.unpack_from(">QIHBB", data, 108)
        self.assertEqual(kind, 2, "Fixture forces a raw first hunk for precise corruption")
        self.assertEqual(low | high << 16, 8 * FRAME)
        data[offset + 33] ^= 1
        image = self.folder / "corrupt.chd"
        image.write_bytes(data)
        for command in ([CHDMAN, "verify", "-i", str(image)] if CHDMAN else None,
                        [LIBCHDR, str(image), "sequential"] if LIBCHDR else None):
            if command:
                result = subprocess.run(command, capture_output=True, timeout=90)
                self.assertNotEqual(result.returncode, 0, f"Corruption accepted by {command[0]}")

    @unittest.skipUnless(CHDMAN, "MAME chdman is not installed")
    def test_actual_export_imports_to_exact_gdi_and_little_endian_audio(self):
        spec = importlib.util.spec_from_file_location("chd_oracle_importer", ROOT / "tools/game_image_import.py")
        importer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(importer)
        source_hash = hashlib.sha256(self.image.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(prefix="kui-chd-import-oracle-") as tmp:
            output = Path(tmp) / "imported"
            report = importer.import_image(self.image, output, chdman=CHDMAN)
            self.assertEqual(report["launch_file"], "disc.gdi")
            lines = (output / report["launch_file"]).read_text().splitlines()
            self.assertEqual(int(lines[0]), len(COUNTS))
            for n, line in enumerate(lines[1:]):
                fields = shlex.split(line)
                self.assertEqual(tuple(map(int, fields[:4])), (n + 1, STARTS[n], 0 if AUDIO[n] else 4, 2352))
                self.assertEqual(int(fields[5]), 0)
                payload = (output / fields[4]).read_bytes()
                self.assertEqual(payload, self.originals[n], f"Imported track {n + 1}: raw data and LE CDDA")
                self.assertEqual(STARTS[n] + len(payload) // 2352, STARTS[n] + COUNTS[n])
            for record in report["outputs"]:
                payload = (output / record["file"]).read_bytes()
                self.assertEqual(record["sha256"], hashlib.sha256(payload).hexdigest())
                self.assertEqual(record["bytes"], len(payload))
            self.assertEqual(report["source"]["sha256"], source_hash)
        self.assertEqual(hashlib.sha256(self.image.read_bytes()).hexdigest(), source_hash)

    @unittest.skipUnless(LIBCHDR, "Independent libchdr dump helper is not configured")
    def test_libchdr_decodes_every_hunk(self):
        result = run([LIBCHDR, str(self.image), "sequential"])
        data = self.image.read_bytes()
        hunks = struct.unpack_from(">I", data, 24)[0]
        hunk = struct.unpack_from(">I", data, 44)[0]
        self.assertEqual(len(result.stdout), hunks * (hunk + 4))
        for n in range(hunks):
            at = n * (hunk + 4)
            self.assertEqual(struct.unpack_from(">I", result.stdout, at)[0], n)
            offset, crc, low, high, kind = struct.unpack_from(">QIHBB", data, 108 + 16 * n)
            stored = data[offset:offset + (low | high << 16)]
            expected = zlib.decompress(stored, -15) if kind == 1 else stored
            self.assertEqual(result.stdout[at + 4:at + 4 + hunk], expected)


if __name__ == "__main__":
    unittest.main()
