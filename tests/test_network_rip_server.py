# SPDX-License-Identifier: GPL-3.0-only
"""PC receiver checks with independent wire encoding and real local sockets."""

import hashlib
import hmac
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock
import zlib


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/network_rip_server.py"
SPEC = importlib.util.spec_from_file_location("network_rip_server", SCRIPT)
server = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(server)
WIRE = struct.Struct("<4sBBHQQIIIIII")


def fixture_image(path, exfat=False):
    blocks = 131072  # Sparse 64 MiB volume; only the BPB is needed by these tests.
    boot = bytearray(512)
    boot[510:] = b"\x55\xaa"
    if exfat:
        boot[3:11] = b"EXFAT   "
        struct.pack_into("<Q", boot, 72, blocks)
        boot[108] = 9
    else:
        boot[0:3] = b"\xeb\x58\x90"
        boot[3:11] = b"TESTFAT "
        struct.pack_into("<H", boot, 11, 512)
        boot[13] = 1
        struct.pack_into("<H", boot, 14, 32)
        boot[16] = 2
        struct.pack_into("<I", boot, 32, blocks)
        struct.pack_into("<I", boot, 36, 1009)
        struct.pack_into("<I", boot, 44, 2)
        boot[82:90] = b"FAT32   "
    with path.open("wb") as stream:
        stream.write(boot)
        stream.truncate(blocks * 512)
    return blocks


def encode(op, session, seq, key, block=0, count=0, payload=b"", **changes):
    values = [b"KNR1", 1, op, 0, session, seq, block, count, len(payload), 0, 0, 0]
    positions = {"magic": 0, "version": 1, "flags": 3, "status": 9, "reserved": 11,
                 "length": 8, "crc": 10}
    for name, value in changes.items():
        values[positions[name]] = value
    header = WIRE.pack(*values)
    if "crc" not in changes:
        values[10] = zlib.crc32(header + payload)
        header = WIRE.pack(*values)
    return header + payload + hmac.new(key, header + payload, hashlib.sha256).digest()


def exact(sock, count):
    result = bytearray()
    while len(result) < count:
        part = sock.recv(count - len(result))
        if not part:
            raise EOFError("closed")
        result.extend(part)
    return bytes(result)


def decode(sock, key=None):
    header = exact(sock, 48)
    fields = WIRE.unpack(header)
    payload = exact(sock, fields[8])
    tag = exact(sock, 32)
    zero_crc = header[:40] + bytes(4) + header[44:]
    if fields[0:2] != (b"KNR1", 1) or fields[3] != 1 or fields[11]:
        raise AssertionError("invalid response header")
    if zlib.crc32(zero_crc + payload) != fields[10]:
        raise AssertionError("invalid response CRC")
    expected = bytes(32) if key is None else hmac.new(key, header + payload, hashlib.sha256).digest()
    if not hmac.compare_digest(expected, tag):
        raise AssertionError("invalid response HMAC")
    return fields, payload


class Session:
    def __init__(self, volume, key, tcp=False):
        self.errors = []
        if tcp:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                self.client = socket.create_connection(listener.getsockname())
                self.peer, _ = listener.accept()
        else:
            self.client, self.peer = socket.socketpair()
        self.client.settimeout(2)

        def run():
            try:
                with self.peer:
                    server.serve_one(self.peer, volume, key)
            except BaseException as error:
                self.errors.append(error)

        self.thread = threading.Thread(target=run)
        self.thread.start()
        fields, self.challenge = decode(self.client)
        if fields[2] != server.CHALLENGE or fields[5] != 0 or len(self.challenge) != 56:
            raise AssertionError("invalid challenge")
        self.session = fields[4]
        self.key = key
        self.sequence = 1

    def auth(self, nonce=None, key=None):
        self.client.sendall(encode(server.AUTH, self.session, 1, key or self.key,
                                   payload=self.challenge[24:] if nonce is None else nonce))
        fields, payload = decode(self.client, self.key)
        if fields[2] != server.AUTH or fields[5] != 1 or payload != self.challenge[:24]:
            raise AssertionError("invalid authenticated identity")
        return self

    def request(self, op, block=0, count=0, payload=b"", fragmented=False):
        self.sequence += 1
        frame = encode(op, self.session, self.sequence, self.key, block, count, payload)
        if fragmented:
            for index in range(0, len(frame), 7):
                self.client.sendall(frame[index:index + 7])
        else:
            self.client.sendall(frame)
        return decode(self.client, self.key)

    def close(self):
        self.client.close()
        self.thread.join(3)
        if self.thread.is_alive():
            raise AssertionError("server did not stop after disconnect")
        if self.errors:
            raise self.errors[0]


class NetworkRipServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.image = self.root / "disc.img"
        self.identity = self.root / "disc.json"
        self.keyfile = self.root / "disc.key"
        self.config = self.root / "network-rip.cfg"
        self.blocks = fixture_image(self.image)
        self.meta = server.initialize(self.image, self.identity, self.keyfile,
                                      "192.0.2.3", 7412, self.config)
        self.key = server.load_key(self.keyfile)
        self.volume = None
        self.sessions = []

    def tearDown(self):
        for session in self.sessions:
            session.close()
        if self.volume:
            self.volume.close()
        self.temp.cleanup()

    def open_volume(self):
        self.volume = server.Volume(self.image, self.identity)
        return self.volume

    def start(self, tcp=False):
        if self.volume is None:
            self.open_volume()
        session = Session(self.volume, self.key, tcp)
        self.sessions.append(session)
        return session

    def rejected(self, session, frame):
        with mock.patch.object(server.os, "pwrite", side_effect=AssertionError("unauthenticated write")), \
                mock.patch.object(server.os, "pread", side_effect=AssertionError("invalid-frame read")), \
                mock.patch.object(server.os, "fsync", side_effect=AssertionError("invalid-frame sync")):
            session.client.sendall(frame)
            try:
                self.assertEqual(session.client.recv(1), b"")
            except ConnectionResetError:
                pass  # Header rejection may leave unread input in the socket.
            session.thread.join(2)
        self.assertFalse(session.thread.is_alive())

    def test_init_does_not_modify_image_and_creates_private_explicit_config(self):
        self.assertEqual(self.meta["blocks"], self.blocks)
        self.assertEqual(len(self.meta["volume"]), 32)
        self.assertEqual(len(self.key), 32)
        self.assertEqual(self.config.read_text(),
                         f"host=192.0.2.3\nport=7412\nkey={self.key.hex()}\nvolume={self.meta['volume']}\n")
        for path in (self.identity, self.keyfile, self.config):
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.image.stat().st_size, self.blocks * 512)
        with self.image.open("rb") as stream:
            self.assertEqual(stream.read(11)[3:11], b"TESTFAT ")
        with self.assertRaises(FileExistsError):
            server.initialize(self.image, self.identity, self.keyfile, "192.0.2.3", 7412, self.config)
        self.assertEqual(server.load_key(self.keyfile), self.key)

    def test_init_rolls_back_only_new_files_on_output_collision(self):
        new_identity = self.root / "new.json"
        new_key = self.root / "new.key"
        with self.assertRaises(FileExistsError):
            server.initialize(self.image, new_identity, new_key, "192.0.2.3", 7412, self.config)
        self.assertFalse(new_identity.exists())
        self.assertFalse(new_key.exists())
        self.assertTrue(self.config.exists())

    def test_config_host_validation_matches_the_console(self):
        for host in ("0.1.2.3", "127.0.0.1", "169.254.1.2", "224.0.0.1", "255.255.255.255"):
            with self.subTest(host=host), self.assertRaises(ValueError):
                server.initialize(self.image, self.identity, self.keyfile, host, 7412, self.config)

    def test_real_tcp_random_io_sync_close_and_second_session(self):
        session = self.start(tcp=True).auth()
        payload = bytes(range(256)) * 64
        fields, result = session.request(server.WRITE, 100, 32, payload, fragmented=True)
        self.assertEqual((fields[2], fields[6:10], result), (server.WRITE, (100, 32, 0, 0), b""))
        fields, result = session.request(server.READ, 107, 3)
        self.assertEqual(result, payload[7 * 512:10 * 512])
        self.assertEqual(fields[9], server.OK)
        with mock.patch.object(server.os, "fsync", wraps=os.fsync) as sync:
            self.assertEqual(session.request(server.SYNC)[0][9], server.OK)
            self.assertGreater(sync.call_count, 0)
            self.assertEqual(session.request(server.CLOSE)[0][9], server.OK)
        session.thread.join(2)
        second = self.start(tcp=True).auth()
        self.assertNotEqual(session.session, second.session)
        self.assertEqual(second.request(server.READ, 100, 1)[1], payload[:512])
        second.request(server.CLOSE)

    def test_wrong_key_and_wrong_nonce_cannot_authenticate(self):
        session = self.start()
        self.rejected(session, encode(server.AUTH, session.session, 1, bytes([7]) * 32,
                                      payload=session.challenge[24:]))
        session = self.start()
        self.rejected(session, encode(server.AUTH, session.session, 1, self.key, payload=bytes(32)))

    def test_bad_hmac_crc_session_sequence_and_shapes_fail_closed(self):
        def bad_tag(session):
            frame = encode(server.WRITE, session.session, 2, self.key, 100, 1, b"A" * 512)
            return frame[:-1] + bytes([frame[-1] ^ 1])

        factories = [
            bad_tag,
            lambda s: encode(server.WRITE, s.session, 2, self.key, 100, 1, b"A" * 512, crc=123),
            lambda s: encode(server.WRITE, s.session ^ 1, 2, self.key, 100, 1, b"A" * 512),
            lambda s: encode(server.WRITE, s.session, 3, self.key, 100, 1, b"A" * 512),
            lambda s: encode(server.WRITE, s.session, 2, self.key, 100, 0, b""),
            lambda s: encode(server.WRITE, s.session, 2, self.key, 100, 1, b"A" * 512, reserved=1),
            lambda s: encode(server.SYNC, s.session, 2, self.key, flags=1),
            lambda s: encode(server.AUTH, s.session, 2, self.key, payload=s.challenge[24:]),
        ]
        for factory in factories:
            with self.subTest(factory=factory):
                session = self.start().auth()
                self.rejected(session, factory(session))
        self.assertEqual(self.volume.read(100, 1), bytes(512))

    def test_oversized_header_rejected_before_payload(self):
        session = self.start().auth()
        header = encode(server.WRITE, session.session, 2, self.key, 100, 33,
                        length=server.MAX_PAYLOAD + 1)[:48]
        self.rejected(session, header)

    def test_authenticated_bounds_error_and_io_error_end_session(self):
        session = self.start().auth()
        fields, _ = session.request(server.WRITE, self.blocks - 1, 2, b"X" * 1024)
        self.assertEqual(fields[9], server.BOUNDS_ERROR)
        self.assertEqual(session.client.recv(1), b"")
        session = self.start().auth()
        with mock.patch.object(self.volume, "read", side_effect=OSError("disk failed")):
            fields, _ = session.request(server.READ, 100, 1)
        self.assertEqual(fields[9], server.IO_ERROR)
        self.assertEqual(session.client.recv(1), b"")

    def test_partial_frame_disconnect_does_not_write(self):
        session = self.start().auth()
        frame = encode(server.WRITE, session.session, 2, self.key, 100, 1, b"X" * 512)
        with mock.patch.object(server.os, "pwrite", side_effect=AssertionError("partial write")):
            session.client.sendall(frame[:-20])
            session.client.shutdown(socket.SHUT_WR)
            self.assertEqual(session.client.recv(1), b"")
            session.thread.join(2)
        self.assertEqual(self.volume.read(100, 1), bytes(512))

    def test_acknowledged_write_survives_disconnect_and_is_fsynced(self):
        session = self.start().auth()
        with mock.patch.object(server.os, "fsync", wraps=os.fsync) as sync:
            session.request(server.WRITE, 100, 1, b"P" * 512)
            session.client.shutdown(socket.SHUT_WR)
            self.assertEqual(session.client.recv(1), b"")
            session.thread.join(2)
            self.assertGreater(sync.call_count, 0)
        self.assertEqual(self.volume.read(100, 1), b"P" * 512)

    def test_short_disk_io_and_interrupted_syscalls_are_completed(self):
        volume = self.open_volume()
        real_read, real_write = os.pread, os.pwrite
        first_read, first_write = True, True

        def short_read(fd, size, offset):
            nonlocal first_read
            if first_read:
                first_read = False
                raise InterruptedError()
            return real_read(fd, max(1, size // 3), offset)

        def short_write(fd, data, offset):
            nonlocal first_write
            if first_write:
                first_write = False
                raise InterruptedError()
            return real_write(fd, data[:max(1, len(data) // 3)], offset)

        with mock.patch.object(server.os, "pread", side_effect=short_read), \
                mock.patch.object(server.os, "pwrite", side_effect=short_write):
            volume.write(100, b"Q" * 1024)
            self.assertEqual(volume.read(100, 2), b"Q" * 1024)

    def test_zero_length_disk_io_is_an_error(self):
        volume = self.open_volume()
        with mock.patch.object(server.os, "pread", return_value=b""):
            with self.assertRaises(OSError):
                volume.read(100, 1)
        with mock.patch.object(server.os, "pwrite", return_value=0):
            with self.assertRaises(OSError):
                volume.write(100, b"Z" * 512)

    def test_stale_capacity_boot_identity_and_replacement(self):
        metadata = dict(self.meta)
        metadata["blocks"] += 1
        self.identity.write_text(json.dumps(metadata))
        with self.assertRaisesRegex(ValueError, "stale"):
            server.Volume(self.image, self.identity)
        self.identity.write_text(json.dumps(self.meta))
        volume = self.open_volume()
        with self.image.open("r+b") as stream:
            stream.seek(3)
            stream.write(b"CHANGED!")
        with self.assertRaises((OSError, ValueError)):
            volume.read(100, 1)
        volume.close()
        self.volume = None
        with self.assertRaisesRegex(ValueError, "stale"):
            server.Volume(self.image, self.identity)
        fixture_image(self.image)
        volume = self.open_volume()
        self.image.rename(self.root / "old.img")
        fixture_image(self.image)
        with self.assertRaisesRegex(OSError, "replaced"):
            volume.read(100, 1)

    def test_exfat_dirty_and_usage_fields_preserve_identity(self):
        other = self.root / "exfat.img"
        fixture_image(other, exfat=True)
        identity, key, config = (self.root / name for name in ("exfat.json", "exfat.key", "exfat.cfg"))
        server.initialize(other, identity, key, "192.0.2.3", 7412, config)
        with other.open("r+b") as stream:
            stream.seek(106)
            stream.write(b"\x03\x80")
            stream.seek(112)
            stream.write(b"\x40")
        with server.Volume(other, identity) as volume:
            volume.sync()

    def test_symlink_nonvolume_mbr_and_exclusive_lock_are_rejected(self):
        link = self.root / "link.img"
        link.symlink_to(self.image)
        with self.assertRaises(OSError):
            server.Volume(link, self.identity)
        fifo = self.root / "fifo.img"
        os.mkfifo(fifo)
        with self.assertRaises(ValueError):
            server._open_image(fifo, False)
        with self.assertRaises(ValueError):
            server.load_key(fifo)
        self.open_volume()
        with self.assertRaises(BlockingIOError):
            server.Volume(self.image, self.identity)
        self.volume.close()
        self.volume = None
        with self.image.open("r+b") as stream:
            stream.write(bytes(512))
        with self.assertRaisesRegex(ValueError, "boot signature"):
            server.Volume(self.image, self.identity)

    def test_cli_loopback_ready_and_exclusive_receiver(self):
        process = subprocess.Popen([sys.executable, str(SCRIPT), "serve", "--image", str(self.image),
                                    "--identity", str(self.identity), "--key", str(self.keyfile),
                                    "--bind", "127.0.0.1", "--port", "0"],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            ready = process.stdout.readline().strip()
            self.assertRegex(ready, r"^READY [1-9][0-9]*$")
            port = int(ready.split()[1])
            with socket.create_connection(("127.0.0.1", port), timeout=2) as client:
                fields, payload = decode(client)
                self.assertEqual(payload[:16], bytes.fromhex(self.meta["volume"]))
                client.sendall(encode(server.AUTH, fields[4], 1, self.key, payload=payload[24:]))
                self.assertEqual(decode(client, self.key)[0][2], server.AUTH)
                client.sendall(encode(server.CLOSE, fields[4], 2, self.key))
                self.assertEqual(decode(client, self.key)[0][9], server.OK)
            with self.assertRaises(BlockingIOError):
                server.Volume(self.image, self.identity)
        finally:
            process.terminate()
            process.communicate(timeout=3)


if __name__ == "__main__":
    unittest.main()
