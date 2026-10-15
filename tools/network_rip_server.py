#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authenticated, exclusive block-volume receiver for K-UI PC capture.

This exports an existing dedicated FAT32/exFAT volume image, not the host's
filesystem. It never formats, mounts, resizes, or discovers a destination.
"""

from __future__ import annotations

import argparse
import errno
import fcntl
import hashlib
import hmac
import ipaddress
import json
import os
from pathlib import Path
import secrets
import socket
import stat
import struct
import sys
import time
import zlib

SECTOR = 512
MAX_BLOCKS = 32
MAX_PAYLOAD = SECTOR * MAX_BLOCKS
TIMEOUT = 15.0
HEADER = struct.Struct("<4sBBHQQIIIIII")
CHALLENGE, AUTH, READ, WRITE, SYNC, CLOSE = range(1, 7)
OK, IO_ERROR, BOUNDS_ERROR = range(3)
IDENTITY_FORMAT = "kui-network-rip-volume"


class ProtocolError(Exception):
    """A peer supplied an invalid or unauthenticated frame."""


def _read_at(fd: int, size: int, offset: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        try:
            part = os.pread(fd, size - len(result), offset + len(result))
        except InterruptedError:
            continue
        if not part:
            raise OSError(errno.EIO, "short image read")
        result.extend(part)
    return bytes(result)


def _write_at(fd: int, data: bytes, offset: int) -> None:
    done = 0
    while done < len(data):
        try:
            size = os.pwrite(fd, data[done:], offset + done)
        except InterruptedError:
            continue
        if size <= 0:
            raise OSError(errno.EIO, "short image write")
        done += size


def _image_info(fd: int) -> tuple[int, str]:
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or not info.st_size or info.st_size % SECTOR:
        raise ValueError("image must be a nonempty regular file with 512-byte sectors")
    blocks = info.st_size // SECTOR
    if blocks > 0xFFFFFFFF:
        raise ValueError("image exceeds the protocol's 32-bit sector limit")
    boot = bytearray(_read_at(fd, SECTOR, 0))
    if boot[510:512] != b"\x55\xaa":
        raise ValueError("image has no FAT32/exFAT volume boot signature")
    if boot[3:11] == b"EXFAT   ":
        if boot[108] != 9 or struct.unpack_from("<Q", boot, 72)[0] != blocks:
            raise ValueError("exFAT volume sector size or capacity does not match image")
        # FatFs updates VolumeFlags and PercentInUse during ordinary use.
        boot[106:108] = b"\0\0"
        boot[112] = 0
    else:
        sector_bytes = struct.unpack_from("<H", boot, 11)[0]
        sectors_per_cluster = boot[13]
        reserved = struct.unpack_from("<H", boot, 14)[0]
        root_entries = struct.unpack_from("<H", boot, 17)[0]
        short_total = struct.unpack_from("<H", boot, 19)[0]
        short_fat = struct.unpack_from("<H", boot, 22)[0]
        total = struct.unpack_from("<I", boot, 32)[0]
        fat_sectors = struct.unpack_from("<I", boot, 36)[0]
        root_cluster = struct.unpack_from("<I", boot, 44)[0]
        if (sector_bytes != SECTOR or not sectors_per_cluster or
                sectors_per_cluster & (sectors_per_cluster - 1) or
                sectors_per_cluster > 128 or not reserved or boot[16] not in (1, 2) or
                root_entries or short_total or short_fat or not fat_sectors or
                total != blocks or root_cluster < 2 or
                reserved + boot[16] * fat_sectors >= blocks):
            raise ValueError("image must contain a direct FAT32/exFAT volume, without an MBR")
    return blocks, hashlib.sha256(boot).hexdigest()


def _open_image(path: Path, writable: bool) -> int:
    if not hasattr(os, "O_NOFOLLOW"):
        raise RuntimeError("this helper needs POSIX O_NOFOLLOW and advisory file locking")
    fd = os.open(path, (os.O_RDWR if writable else os.O_RDONLY) | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise ValueError("image must be a regular file")
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return fd
    except BaseException:
        os.close(fd)
        raise


def _identity(path: Path) -> dict:
    # Refuse unexpectedly large or nonregular metadata, too.
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size > 4096:
            raise ValueError("invalid volume identity file")
        raw = os.read(fd, 4097)
    finally:
        os.close(fd)
    value = json.loads(raw)
    keys = {"format", "version", "volume", "blocks", "boot_sha256"}
    if (not isinstance(value, dict) or set(value) != keys or
            value["format"] != IDENTITY_FORMAT or type(value["version"]) is not int or
            value["version"] != 1 or type(value["blocks"]) is not int or
            not 0 < value["blocks"] <= 0xFFFFFFFF):
        raise ValueError("invalid volume identity file")
    for name, length in (("volume", 16), ("boot_sha256", 32)):
        text = value[name]
        if not isinstance(text, str) or len(text) != length * 2:
            raise ValueError("invalid volume identity file")
        try:
            decoded = bytes.fromhex(text)
        except ValueError as error:
            raise ValueError("invalid volume identity file") from error
        if len(decoded) != length or (name == "volume" and not any(decoded)):
            raise ValueError("invalid volume identity file")
    return value


class Volume:
    """One exclusively locked image; use as a context manager or call close()."""

    def __init__(self, image: str | Path, identity: str | Path):
        self.path = Path(image)
        self.fd = _open_image(self.path, True)
        try:
            self.info = os.fstat(self.fd)
            metadata = _identity(Path(identity))
            self.blocks, self.boot_sha256 = _image_info(self.fd)
            self.volume = bytes.fromhex(metadata["volume"])
            if (metadata["blocks"] != self.blocks or
                    metadata["boot_sha256"].lower() != self.boot_sha256):
                raise ValueError("image capacity or boot identity changed; refuse stale identity")
            self.check()
        except BaseException:
            os.close(self.fd)
            self.fd = -1
            raise

    def check(self) -> None:
        if self.fd < 0:
            raise OSError(errno.EBADF, "volume is closed")
        current = os.fstat(self.fd)
        named = os.stat(self.path, follow_symlinks=False)
        if (not stat.S_ISREG(named.st_mode) or
                (current.st_dev, current.st_ino, current.st_size) !=
                (self.info.st_dev, self.info.st_ino, self.info.st_size) or
                (named.st_dev, named.st_ino, named.st_size) !=
                (self.info.st_dev, self.info.st_ino, self.info.st_size)):
            raise OSError(errno.ESTALE, "image was replaced or resized")
        blocks, boot = _image_info(self.fd)
        if blocks != self.blocks or boot != self.boot_sha256:
            raise OSError(errno.ESTALE, "image boot identity changed")

    def read(self, block: int, count: int) -> bytes:
        if not 0 < count <= MAX_BLOCKS or not 0 <= block <= self.blocks - count:
            raise ValueError("image read out of bounds")
        self.check()
        return _read_at(self.fd, count * SECTOR, block * SECTOR)

    def write(self, block: int, data: bytes) -> None:
        if (not data or len(data) % SECTOR or len(data) > MAX_PAYLOAD or
                not 0 <= block <= self.blocks - len(data) // SECTOR):
            raise ValueError("image write out of bounds")
        self.check()
        _write_at(self.fd, data, block * SECTOR)

    def sync(self) -> None:
        self.check()
        os.fsync(self.fd)

    def close(self) -> None:
        if self.fd >= 0:
            fd, self.fd = self.fd, -1
            try:
                os.fsync(fd)
            finally:
                os.close(fd)

    def __enter__(self) -> Volume:
        return self

    def __exit__(self, *_exc) -> None:
        self.close()


def make_frame(op: int, session: int, sequence: int, *, block: int = 0,
               count: int = 0, payload: bytes = b"", response: bool = False,
               status: int = OK, key: bytes | None = None) -> bytes:
    """Encode one bounded frame (also useful to independent protocol tests)."""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    header = HEADER.pack(b"KNR1", 1, op, int(response), session, sequence,
                         block, count, len(payload), status, 0, 0)
    crc = zlib.crc32(payload, zlib.crc32(header))
    header = header[:40] + struct.pack("<I", crc) + header[44:]
    tag = hmac.digest(key, header + payload, "sha256") if key is not None else bytes(32)
    return header + payload + tag


def _recv_exact(sock: socket.socket, length: int, deadline: float) -> bytes:
    data = bytearray()
    while len(data) < length:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("frame timed out")
        sock.settimeout(remaining)
        part = sock.recv(length - len(data))
        if not part:
            raise EOFError("peer closed during frame")
        data.extend(part)
    return bytes(data)


def _recv_frame(sock: socket.socket, key: bytes, session: int, sequence: int,
                authenticating: bool) -> tuple[int, int, int, bytes]:
    deadline = time.monotonic() + TIMEOUT
    header = _recv_exact(sock, HEADER.size, deadline)
    magic, version, op, flags, sid, seq, block, count, length, status, crc, reserved = HEADER.unpack(header)
    if (magic != b"KNR1" or version != 1 or flags or sid != session or
            seq != sequence or status or reserved or length > MAX_PAYLOAD or
            (authenticating and op != AUTH) or (not authenticating and op not in (READ, WRITE, SYNC, CLOSE))):
        raise ProtocolError("invalid request header")
    if op == AUTH:
        valid = block == count == 0 and length == 32
    elif op in (READ, WRITE):
        valid = 1 <= count <= MAX_BLOCKS and length == (count * SECTOR if op == WRITE else 0)
    else:
        valid = block == count == length == 0
    if not valid:
        raise ProtocolError("invalid request shape")
    payload = _recv_exact(sock, length, deadline)
    tag = _recv_exact(sock, 32, deadline)
    if not hmac.compare_digest(tag, hmac.digest(key, header + payload, "sha256")):
        raise ProtocolError("authentication failed")
    zero_crc = header[:40] + bytes(4) + header[44:]
    if zlib.crc32(payload, zlib.crc32(zero_crc)) != crc:
        raise ProtocolError("checksum failed")
    return op, block, count, payload


def _send(sock: socket.socket, frame: bytes) -> None:
    sock.settimeout(TIMEOUT)
    sock.sendall(frame)


def serve_one(sock: socket.socket, volume: Volume, key: bytes) -> None:
    """Serve one authenticated session; fail closed on malformed or lost peers.

    The caller owns the socket and Volume, and must serialize all calls using a
    Volume. No request is retried. An unacknowledged write may have reached disk;
    K-UI's explicit Resume/Verify operation resolves that ambiguity.
    """
    if len(key) != 32 or not any(key):
        raise ValueError("key must be a nonzero 32-byte secret")
    original_timeout = sock.gettimeout()
    session = secrets.randbits(64) or 1
    nonce = secrets.token_bytes(32)
    identity = volume.volume + struct.pack("<Q", volume.blocks)
    dirty = False
    try:
        volume.check()
        _send(sock, make_frame(CHALLENGE, session, 0, payload=identity + nonce, response=True))
        op, block, count, payload = _recv_frame(sock, key, session, 1, True)
        if not hmac.compare_digest(payload, nonce):
            raise ProtocolError("stale authentication challenge")
        volume.check()
        _send(sock, make_frame(AUTH, session, 1, payload=identity, response=True, key=key))
        sequence = 2
        while True:
            op, block, count, payload = _recv_frame(sock, key, session, sequence, False)
            status = OK
            output = b""
            try:
                if op in (READ, WRITE):
                    if block + count > volume.blocks:
                        status = BOUNDS_ERROR
                    elif op == READ:
                        output = volume.read(block, count)
                    else:
                        # A failed pwrite may already have changed a prefix.
                        dirty = True
                        volume.write(block, payload)
                else:
                    volume.sync()
                    dirty = False
            except (OSError, ValueError):
                status = IO_ERROR
            _send(sock, make_frame(op, session, sequence, block=block, count=count,
                                  payload=output, response=True, status=status, key=key))
            if status or op == CLOSE or sequence == 0xFFFFFFFFFFFFFFFF:
                break
            sequence += 1
    except (ProtocolError, EOFError, OSError, ValueError):
        # Do not reveal which authentication check failed and never resynchronize
        # a byte stream after malformed input or an ambiguous write.
        pass
    finally:
        if dirty:
            try:
                # Flush already accepted writes even if a later identity check
                # failed. Do not perform new reads or writes after bad input.
                os.fsync(volume.fd)
            except OSError:
                pass
        sock.settimeout(original_timeout)


def _write_new(path: Path, data: bytes) -> None:
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        done = 0
        while done < len(data):
            size = os.write(fd, data[done:])
            if size <= 0:
                raise OSError(errno.EIO, "short configuration write")
            done += size
        os.fsync(fd)
    except BaseException:
        os.close(fd)
        path.unlink()
        raise
    else:
        os.close(fd)


def initialize(image: str | Path, identity: str | Path, key_path: str | Path,
               host: str, port: int, config: str | Path) -> dict:
    """Create fresh identity, secret and console config; never alter the image."""
    address = ipaddress.IPv4Address(host)
    if (not address.packed[0] or address.is_multicast or address.is_loopback or
            address.is_link_local or address.packed[0] >= 224 or not 0 < port <= 65535):
        raise ValueError("host must be a usable PC IPv4 address and port must be 1..65535")
    fd = _open_image(Path(image), False)
    created: list[Path] = []
    try:
        blocks, boot = _image_info(fd)
        metadata = {"format": IDENTITY_FORMAT, "version": 1,
                    "volume": secrets.token_bytes(16).hex(), "blocks": blocks,
                    "boot_sha256": boot}
        key = secrets.token_bytes(32)
        content = (f"host={address}\nport={port}\nkey={key.hex()}\n"
                   f"volume={metadata['volume']}\n").encode("ascii")
        outputs = ((Path(identity), (json.dumps(metadata, indent=2) + "\n").encode("ascii")),
                   (Path(key_path), (key.hex() + "\n").encode("ascii")),
                   (Path(config), content))
        if len({str(path.resolve()) for path, _ in outputs}) != len(outputs):
            raise ValueError("identity, key and config paths must differ")
        for path, data in outputs:
            _write_new(path, data)
            created.append(path)
        return metadata
    except BaseException:
        for path in created:
            path.unlink()
        raise
    finally:
        os.close(fd)


def load_key(path: str | Path) -> bytes:
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size > 128:
            raise ValueError("key file must contain exactly 64 hexadecimal digits")
        text = os.read(fd, 129).decode("ascii").strip()
    finally:
        os.close(fd)
    if len(text) != 64:
        raise ValueError("key file must contain exactly 64 hexadecimal digits")
    key = bytes.fromhex(text)
    if len(key) != 32 or not any(key):
        raise ValueError("key must be a nonzero 32-byte secret")
    return key


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    init = commands.add_parser("init", help="bind an existing image to a new identity and secret")
    serve = commands.add_parser("serve", help="serve one exclusive image to K-UI clients")
    for command in (init, serve):
        command.add_argument("--image", required=True, type=Path)
        command.add_argument("--identity", required=True, type=Path)
        command.add_argument("--key", required=True, type=Path, help="secret key file")
    init.add_argument("--host", required=True, help="PC IPv4 address reachable from the Dreamcast")
    init.add_argument("--port", default=7412, type=int)
    init.add_argument("--config", required=True, type=Path, help="new K-UI configuration file")
    serve.add_argument("--bind", required=True, help="explicit local IPv4 interface address")
    serve.add_argument("--port", default=7412, type=int)
    args = parser.parse_args(argv)
    try:
        if args.command == "init":
            metadata = initialize(args.image, args.identity, args.key, args.host, args.port, args.config)
            print(f"Created volume {metadata['volume']} ({metadata['blocks']} sectors); image unchanged.")
        else:
            address = str(ipaddress.IPv4Address(args.bind))
            if not 0 <= args.port <= 65535:
                raise ValueError("port must be 0..65535")
            key = load_key(args.key)
            with Volume(args.image, args.identity) as volume, socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind((address, args.port))
                listener.listen(1)
                print(f"READY {listener.getsockname()[1]}", flush=True)
                while True:
                    connection, _peer = listener.accept()
                    with connection:
                        serve_one(connection, volume, key)
        return 0
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f"network-rip: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
