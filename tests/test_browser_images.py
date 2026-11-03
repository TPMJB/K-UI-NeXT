#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real HTTP clients through W5500 and Wi-Fi firmware models, on real FatFs.
The sparse FAT32 fixture needs no system formatter. The gateway may only read:
the entire seeded card must retain its exact SHA-256 after serving/stopping.
"""
import hashlib
import io
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build/browser-image"
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")


def fixture(path):
    total, reserved, fats, fat_sectors = 196608, 32, 2, 1513
    clusters = total - reserved - fats * fat_sectors
    assert (clusters + 2 + 127) // 128 == fat_sectors
    boot = bytearray(512)
    boot[:3], boot[3:11] = b"\xebX\x90", b"KUI TEST"
    struct.pack_into("<HBHBHHBHHHII", boot, 11, 512, 1, reserved, fats, 0, 0, 0xF8, 0, 63, 255, 0, total)
    struct.pack_into("<IHHIHH", boot, 36, fat_sectors, 0, 0, 2, 1, 6)
    boot[64], boot[66] = 0x80, 0x29
    struct.pack_into("<I", boot, 67, 12345)
    boot[71:82], boot[82:90], boot[510:512] = b"KUI BROWSER", b"FAT32   ", b"\x55\xaa"
    info = bytearray(512)
    struct.pack_into("<I", info, 0, 0x41615252)
    struct.pack_into("<I", info, 484, 0x61417272)
    struct.pack_into("<III", info, 488, 0xFFFFFFFF, 0xFFFFFFFF, 0xAA550000)
    fat = bytearray(fat_sectors * 512)
    struct.pack_into("<III", fat, 0, 0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF)
    with path.open("wb") as stream:
        stream.truncate(total * 512)
        stream.write(boot)
        stream.write(info)
        stream.seek(6 * 512)
        stream.write(boot)
        stream.write(info)
        for i in range(fats):
            stream.seek((reserved + i * fat_sectors) * 512)
            stream.write(fat)


def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Server:
    def __init__(self, image, port, mode):
        self.lines = []
        self.ready = threading.Event()
        self.idle = threading.Event()
        self.proc = subprocess.Popen([str(BINARY), str(image), "browser-serve", str(port), mode],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, env=ENV)
        threading.Thread(target=self.read, daemon=True).start()
        assert self.ready.wait(45), "\n".join(self.lines[-40:])
        assert self.proc.poll() is None, "\n".join(self.lines[-40:])

    def read(self):
        for line in self.proc.stdout:
            self.lines.append(line.rstrip())
            if line.startswith("BROWSER_READY "):
                self.ready.set()
            if line.startswith("BROWSER_IDLE listeners=2 files=0 dirs=0"):
                self.idle.set()

    def wait_idle(self):
        self.idle.clear()
        self.proc.stdin.write("b")
        self.proc.stdin.flush()
        assert self.idle.wait(10), "Browser did not close exports and rearm both sockets:\n" + "\n".join(self.lines[-50:])
        assert self.proc.poll() is None, "\n".join(self.lines[-50:])

    def stop(self):
        self.proc.stdin.close()
        code = self.proc.wait(timeout=30)
        assert code == 0, "\n".join(self.lines[-50:])
        assert any("BROWSER_STOPPED state=2" in x for x in self.lines), "\n".join(self.lines[-30:])


def request(port, target="/", method="GET", partial=False):
    wire = f"{method} {target} HTTP/1.1\r\nHost: dreamcast\r\nConnection: close\r\n\r\n".encode()
    with socket.create_connection(("127.0.0.1", port), timeout=10) as s:
        if partial:
            for at in range(0, len(wire), 3):
                s.sendall(wire[at:at + 3])
                time.sleep(.001)
        else:
            s.sendall(wire)
        out = bytearray()
        while True:
            piece = s.recv(4096)
            if not piece:
                break
            out.extend(piece)
    head, body = bytes(out).split(b"\r\n\r\n", 1)
    lines = head.decode().split("\r\n")
    code = int(lines[0].split()[1])
    headers = dict(x.split(": ", 1) for x in lines[1:])
    assert len(body) == (0 if method == "HEAD" else int(headers["Content-Length"]))
    assert headers["Connection"] == "close"
    return code, headers, body


def trial(image, mode):
    original = digest(image)
    port = free_port()
    service = Server(image, port, mode)
    try:
        code, _, body = request(port, partial=True)
        assert code == 200 and b"/screenshots/" in body
        code, _, body = request(port, "/screenshots/")
        assert code == 200 and b"Fish &amp; Chips.bmp" in body and b".part" not in body
        code, _, body = request(port, "/screenshots/Fish%20%26%20Chips.bmp")
        assert code == 200 and body == b"screen capture\n"
        code, headers, body = request(port, "/screenshots/large.bmp")
        expected = bytes((i * 37) % 256 for i in range(4096)) * 32
        assert code == 200 and body == expected
        assert int(headers["Content-Length"]) == len(expected)
        code, headers, body = request(port, "/screenshots/large.bmp", "HEAD")
        assert code == 200 and not body and int(headers["Content-Length"]) == len(expected)
        code, _, body = request(port, "/reports/")
        assert code == 200 and b"REPORT-000001" in body and b".part" not in body
        code, _, body = request(port, "/reports/REPORT-000001/manifest.json")
        assert code == 200 and body == b'{"complete":true}\n'
        code, _, body = request(port, "/saves/Card.vmd")
        assert code == 200 and body == b"explicitly exported card\n"
        for path in ("/KUI/runtime.kui", "/KUI/ftp-password.txt", "/reports/../runtime.kui",
                     "/reports/REPORT-000002.part/log.txt", "/screenshots/KUI-000002.part",
                     "/saves/card.journal"):
            assert request(port, path)[0] == 403, path
        assert request(port, "/screenshots/%2e%2e%2fruntime.kui")[0] == 400
        assert request(port, "/", "POST")[0] == 405
        # Ordinary response FIN cleanup is asynchronous. Establish both finite
        # listener slots before testing that an aborted download leaves a
        # spare available for the immediately following connection.
        service.wait_idle()
        # Drop mid-download, then immediately request another page. No handles
        # or old response bytes may survive the closed TCP connection.
        with socket.create_connection(("127.0.0.1", port), timeout=10) as s:
            s.sendall(b"GET /screenshots/large.bmp HTTP/1.0\r\n\r\n")
            assert s.recv(300)
        assert request(port)[0] == 200
        service.wait_idle()
        # Stop with a partial request still open; gateway cleanup must close it.
        pending = socket.create_connection(("127.0.0.1", port), timeout=10)
        pending.sendall(b"GET /screenshots/")
        service.stop()
        pending.close()
    except Exception as error:
        error.add_note(f"Browser adapter={mode} exit={service.proc.poll()} server log:\n" +
                       "\n".join(service.lines[-50:]))
        raise
    finally:
        if service.proc.poll() is None:
            service.proc.kill()
            service.proc.wait()
    assert digest(image) == original, "read-only browser changed the card"
    print(f"PASS browser real FatFs + {mode}: HTTP bytes, partial requests, traversal, report publication, disconnect, Stop, card unchanged")


def ftp_regression(image, mode):
    # The same binary retains the existing FTP harness entry point. After
    # browser cleanup, FTP must acquire its normal independent gateway, keep
    # its password/login, and transfer in both directions.
    from test_ftp_images import Server as FtpServer, client
    port, passive = free_port(), free_port()
    while passive > 64000:
        passive = free_port()
    service = FtpServer(str(BINARY), image, port, passive, extra=(mode,), env=ENV)
    try:
        ftp = client(port, service.password)
        received = bytearray()
        ftp.retrbinary("RETR /KUI/screenshots/Fish & Chips.bmp", received.extend)
        assert received == b"screen capture\n"
        upload = b"FTP remains independent of browser sharing\n" * 128
        ftp.storbinary("STOR /ftp-browser-regression.txt", io.BytesIO(upload))
        received = bytearray()
        ftp.retrbinary("RETR /ftp-browser-regression.txt", received.extend)
        assert received == upload
        ftp.quit()
        code, output = service.stop()
        assert code == 0, output
    finally:
        if service.proc.poll() is None:
            service.proc.kill()
            service.proc.wait()
    print(f"PASS shared gateway FTP regression + {mode}: password/login, RETR, STOR, RETR, clean stop")


def main():
    with tempfile.TemporaryDirectory(prefix="kui-browser-") as tmp:
        image = Path(tmp) / "card.img"
        fixture(image)
        seed = subprocess.run([str(BINARY), str(image), "browser-seed"], env=ENV,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert seed.returncode == 0, seed.stdout
        trial(image, "w5500")
        trial(image, "wifi")
        ftp_regression(image, "w5500")
        ftp_regression(image, "wifi")


if __name__ == "__main__":
    main()
