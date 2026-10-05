#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The FTP server end to end: build/ftp-image runs K-UI's server on the
W5500 model (tests/w5500_model.c) with real FatFs on FAT32 and exFAT images,
and Python's ftplib is the client. Both filesystems run again on the Wi-Fi board
model (tests/wifi_model.c: the firmware's own bridge core). fsck checks
every image afterwards; on FAT32, mtools reads the uploads back without
K-UI's code."""
import ftplib
import hashlib
import os
import random
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/ftp-image")
HOST = "127.0.0.1"
# build/ftp-image dates new files by a fixed clock: 2026-09-26 12:34:56.
CLOCK = "20260926123456"


def run(*args, expected=0):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != expected:
        raise RuntimeError(f"{args}: expected exit {expected}, got {result.returncode}\n{result.stdout}")
    return result.stdout


class Server:
    """build/ftp-image serving an image until its stdin closes."""

    def __init__(self, binary, image, port, passive, extra=(), env=None, ready=True):
        self.proc = subprocess.Popen([binary, str(image), "serve", str(port), str(passive), *extra],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     text=True, env=env)
        self.lines = []
        self.password = None
        self.adapter = None
        self.seen = threading.Event()
        threading.Thread(target=self._read, daemon=True).start()
        if ready:
            assert self.seen.wait(60), "server never became ready:\n" + "\n".join(self.lines[-40:])
            assert self.password, "\n".join(self.lines[-40:])

    def _read(self):
        for line in self.proc.stdout:
            line = line.rstrip("\n")
            self.lines.append(line)
            if line.startswith("READY "):
                fields = dict(part.split("=", 1) for part in line.split()[1:4])
                self.password = fields["password"]
                self.adapter = line.split("adapter=", 1)[1]
                self.seen.set()
            if line.startswith("STOPPED "):
                self.seen.set()

    def send(self, key):
        """A key on the harness's stdin: r restarts the Wi-Fi board, d drops
        its Wi-Fi, u brings it back on 127.0.0.2; n makes the next upload's
        rename to its name fail, N also the rename that puts the old file
        back."""
        self.proc.stdin.write(key)
        self.proc.stdin.flush()

    def wait_for(self, text, seconds=20):
        deadline = time.monotonic() + seconds
        while not any(text in line for line in self.lines):
            assert time.monotonic() < deadline, f"never saw {text!r}:\n" + "\n".join(self.lines[-30:])
            time.sleep(0.05)

    def stop(self):
        self.proc.stdin.close()
        code = self.proc.wait(60)
        time.sleep(0.1)
        return code, "\n".join(self.lines)

    def output(self):
        return "\n".join(self.lines)

    def count(self, text):
        return sum(text in line for line in self.lines)


def client(port, password, passive=True):
    f = ftplib.FTP()
    f.connect(HOST, port, timeout=30)
    f.login("kui", password)
    f.set_pasv(passive)
    return f


def expect(code, action):
    """The command must fail with this reply code."""
    try:
        action()
    except ftplib.all_errors as error:
        text = str(error)
        assert text.startswith(code), f"wanted {code}, got {text}"
        return text
    raise AssertionError(f"wanted {code}, the command succeeded")


def upload(f, name, data):
    import io
    f.storbinary(f"STOR {name}", io.BytesIO(data), 8192)


def download(f, name, rest=None):
    parts = []
    f.retrbinary(f"RETR {name}", parts.append, 8192, rest)
    return b"".join(parts)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def basics(port, password):
    f = ftplib.FTP()
    f.connect(HOST, port, timeout=30)
    assert f.getwelcome().startswith("220 ")
    expect("530", lambda: f.pwd())
    assert f.sendcmd("SYST") == "215 UNIX Type: L8"
    feats = f.sendcmd("FEAT")
    for feature in ("EPSV", "MDTM", "MLST type*;size*;modify*;", "PASV", "REST STREAM", "SIZE", "UTF8"):
        assert f"\n {feature}\n" in feats + "\n", feats
    expect("502", lambda: f.sendcmd("AUTH TLS"))
    f.login("anyone", password)
    assert f.sendcmd("NOOP").startswith("200")
    assert f.sendcmd("TYPE I").startswith("200") and f.sendcmd("TYPE A").startswith("200")
    expect("504", lambda: f.sendcmd("TYPE E"))
    assert f.sendcmd("MODE S").startswith("200") and f.sendcmd("STRU F").startswith("200")
    assert f.sendcmd("HELP").startswith("214") and f.sendcmd("STAT").startswith("211")
    assert f.sendcmd("OPTS UTF8 ON").startswith("200")
    expect("502", lambda: f.sendcmd("SITE CHMOD 777 x"))
    expect("500", lambda: f.sendcmd("X1Y2"))
    expect("500", lambda: f.sendcmd("NOOP " + "x" * 1100))
    assert f.sendcmd("NOOP").startswith("200")
    expect("503", lambda: f.sendcmd("USER again"))
    assert f.quit().startswith("221")

    wrong = ftplib.FTP()
    wrong.connect(HOST, port, timeout=30)
    started = time.monotonic()
    expect("530", lambda: wrong.login("kui", "not-the-password"))
    assert time.monotonic() - started > 1.5, "a wrong password is answered only after a pause"
    expect("530", lambda: wrong.login("kui", "still-wrong"))
    expect("421", lambda: wrong.login("kui", "wrong-again"))
    wrong.close()
    print("PASS FTP basics: banner, FEAT, login, wrong passwords, long lines", flush=True)


def folders(f):
    assert f.pwd() == "/"
    assert f.mkd("Games") == "/Games"
    expect("550", lambda: f.mkd("Games"))
    assert f.cwd("Games").startswith("250") and f.pwd() == "/Games"
    assert f.mkd("Crazy Taxi") == "/Games/Crazy Taxi"
    assert f.sendcmd("CDUP").startswith("250") and f.pwd() == "/"
    assert f.sendcmd("XCWD Games").startswith("250") and f.pwd() == "/Games"
    assert f.sendcmd("XCUP").startswith("250") and f.pwd() == "/"
    assert f.sendcmd("CWD /Games/Crazy Taxi/../../Games/./Crazy Taxi").startswith("250")
    assert f.sendcmd("XPWD") == '257 "/Games/Crazy Taxi" is the current folder'
    f.cwd("/")
    expect("550", lambda: f.cwd("/nowhere"))
    expect("553", lambda: f.cwd("bad:name"))
    expect("553", lambda: f.mkd("con"))
    expect("550", lambda: f.mkd("/missing/child"))
    print("PASS FTP folders: MKD, CWD, CDUP, XCWD, PWD, bad names", flush=True)


def transfers(f, port, password, image_kind):
    random.seed(image_kind)
    small = b"hello, Dreamcast\n"
    one = random.randbytes(1024 * 1024 + 123)
    big = random.randbytes(20 * 1024 * 1024 + 4567)
    upload(f, "hello.txt", small)
    upload(f, "empty.bin", b"")
    upload(f, "Games/one.bin", one)
    started = time.monotonic()
    upload(f, "Games/big.bin", big)
    seconds = time.monotonic() - started
    print(f"  20 MB upload through the model: {seconds:.1f} s", flush=True)
    upload(f, "café.txt", small)
    assert f.size("Games/big.bin") == len(big) and f.size("empty.bin") == 0
    assert download(f, "hello.txt") == small and download(f, "empty.bin") == b""
    assert digest(download(f, "Games/one.bin")) == digest(one)
    assert digest(download(f, "Games/big.bin")) == digest(big)
    assert download(f, "café.txt") == small
    # REST: the rest of a file from an offset; past the end is refused.
    assert download(f, "Games/one.bin", rest=123456) == one[123456:]
    assert download(f, "Games/one.bin", rest=len(one)) == b""
    expect("554", lambda: download(f, "Games/one.bin", rest=len(one) + 1))
    expect("554", lambda: f.sendcmd("REST 5") and upload(f, "resume.bin", b"x"))
    # Replacing a file: the new content, and no part file left behind.
    upload(f, "hello.txt", b"replaced\n")
    assert download(f, "hello.txt") == b"replaced\n"
    names = f.nlst()
    assert "hello.txt" in names and "Games" in names and "café.txt" in names, names
    # Wildcards, as mget sends them: the folder's names that match.
    assert sorted(f.nlst("Games/*.BIN")) == ["big.bin", "one.bin"], f.nlst("Games/*.BIN")
    assert f.nlst("*.txt") and all(n.endswith(".txt") for n in f.nlst("*.txt"))
    assert f.nlst("Games/nothing*") == []
    wild = []
    f.dir("-l Games/o?e.*", wild.append)
    assert len(wild) == 1 and wild[0].endswith(" one.bin"), wild
    assert not any(n.endswith(".kui-part") for n in names + f.nlst("Games"))
    listing = []
    f.dir("Games", listing.append)
    assert any(line.startswith("-rw-r--r--") and line.endswith(" big.bin") and f" {len(big)} " in line
               for line in listing), listing
    assert any(line.startswith("drwxr-xr-x") and line.endswith(" Crazy Taxi") for line in listing), listing
    facts = dict(f.mlsd("Games"))
    assert facts["big.bin"]["type"] == "file" and int(facts["big.bin"]["size"]) == len(big)
    assert facts["Crazy Taxi"]["type"] == "dir" and len(facts["big.bin"]["modify"]) == 14
    one_line = []
    f.dir("Games/one.bin", one_line.append)
    assert len(one_line) == 1 and one_line[0].endswith(" one.bin"), one_line
    stamp = f.sendcmd("MDTM Games/one.bin")
    assert stamp == "213 " + CLOCK if CLOCK else len(stamp) == 18, stamp
    if CLOCK:
        assert facts["one.bin"]["modify"] == CLOCK, facts["one.bin"]
        assert any(line.endswith(" Sep 26 12:34 big.bin") for line in listing), listing
    mlst = f.sendcmd("MLST Games/one.bin")
    assert "type=file;size=%d;" % len(one) in mlst and "/Games/one.bin" in mlst, mlst
    expect("550", lambda: f.size("Games"))
    expect("550", lambda: f.size("nothing.bin"))
    expect("550", lambda: download(f, "nothing.bin"))
    expect("550", lambda: download(f, "Games"))
    expect("553", lambda: upload(f, "Games", b"x"))
    expect("553", lambda: upload(f, "bad:name.txt", b"x"))
    expect("553", lambda: upload(f, "no/such/folder.bin", b"x"))
    expect("501", lambda: f.sendcmd("REST abc"))

    # Active mode: the server connects to the client.
    f.set_pasv(False)
    active = random.randbytes(300 * 1024)
    upload(f, "Games/active.bin", active)
    assert digest(download(f, "Games/active.bin")) == digest(active)
    assert "active.bin" in f.nlst("Games")
    f.set_pasv(True)
    expect("500", lambda: f.sendcmd("PORT 10,0,0,99,200,10"))
    expect("500", lambda: f.sendcmd("PORT 127,0,0,1,0,21"))
    expect("501", lambda: f.sendcmd("PORT 1,2,3"))
    expect("522", lambda: f.sendcmd("EPRT |2|::1|5000|"))
    # EPSV and EPRT by hand (ftplib uses them only for IPv6).
    reply = f.sendcmd("EPSV")
    data_port = int(reply.split("|||")[1].split("|")[0])
    with socket.create_connection((HOST, data_port), 30) as conn:
        assert f.sendcmd("NLST Games").startswith("150")
        received = b""
        while chunk := conn.recv(65536):
            received += chunk
    assert f.voidresp().startswith("226") and b"one.bin\r\n" in received
    listener = socket.socket()
    listener.bind((HOST, 0))
    listener.listen(1)
    assert f.sendcmd("EPRT |1|%s|%d|" % (HOST, listener.getsockname()[1])).startswith("200")
    assert f.sendcmd("RETR hello.txt").startswith("150")
    conn, _ = listener.accept()
    received = b""
    while chunk := conn.recv(65536):
        received += chunk
    conn.close()
    listener.close()
    assert f.voidresp().startswith("226") and received == b"replaced\n"
    expect("522", lambda: f.sendcmd("EPSV 2"))
    expect("425", lambda: f.sendcmd("RETR hello.txt"))
    print("PASS FTP transfers: STOR, RETR, REST, replace, LIST, NLST, MLSD, MLST, SIZE, MDTM, active, EPSV, EPRT",
          flush=True)
    return {"one": one, "big": big, "active": active}


def changes(f, case_insensitive):
    upload(f, "Games/move-me.txt", b"moving\n")
    f.rename("Games/move-me.txt", "moved.txt")
    assert download(f, "moved.txt") == b"moving\n" and "move-me.txt" not in f.nlst("Games")
    expect("550", lambda: f.rename("nothing.txt", "x.txt"))
    expect("503", lambda: f.sendcmd("RNTO x.txt"))
    expect("553", lambda: f.rename("moved.txt", "hello.txt"))
    expect("553", lambda: f.rename("Games", "Games/Crazy Taxi/Games"))
    expect("553", lambda: f.rename("moved.txt", "bad|name.txt"))
    f.rename("Games/Crazy Taxi", "Games/Crazy Taxi 2")
    assert "Crazy Taxi 2" in f.nlst("Games")
    if case_insensitive:
        f.rename("moved.txt", "MOVED.txt")
        assert "MOVED.txt" in f.nlst() and "moved.txt" not in f.nlst()
        f.rename("MOVED.txt", "moved.txt")
    f.delete("moved.txt")
    assert "moved.txt" not in f.nlst()
    expect("550", lambda: f.delete("moved.txt"))
    expect("550", lambda: f.rmd("Games"))
    expect("550", lambda: f.delete("Games"))
    expect("550", lambda: f.rmd("hello.txt"))
    f.rmd("Games/Crazy Taxi 2")
    assert "Crazy Taxi 2" not in f.nlst("Games")
    f.delete("Read only/locked.txt")
    assert f.nlst("Read only") == []
    expect("550", lambda: f.rmd("/"))
    print("PASS FTP changes: rename, move, delete, RMD, read-only", flush=True)


def replacing(f, server):
    """A file an upload replaces stays until the upload has its name: if that
    rename fails, the old file is back under its name; if putting it back
    fails too, it is kept under the spare name the reply gives."""
    upload(f, "Games/keep.txt", b"the original\n")
    server.send("n")
    server.wait_for("ARMED 1")
    text = expect("451", lambda: upload(f, "Games/keep.txt", b"its replacement\n"))
    assert "old file kept" in text, text
    assert download(f, "Games/keep.txt") == b"the original\n"
    names = f.nlst("Games")
    assert not any(n.startswith("KUI-ftp-") for n in names), names
    server.send("N")
    server.wait_for("ARMED 2")
    text = expect("451", lambda: upload(f, "Games/keep.txt", b"its replacement\n"))
    spare = text.split()[-1]
    names = f.nlst("Games")
    assert spare.startswith("KUI-ftp-") and spare.endswith(".kui-old") and spare in names, (text, names)
    assert "keep.txt" not in names and not any(n.endswith(".kui-part") for n in names), names
    assert download(f, "Games/" + spare) == b"the original\n"
    f.rename("Games/" + spare, "Games/keep.txt")
    upload(f, "Games/keep.txt", b"its replacement\n")
    assert download(f, "Games/keep.txt") == b"its replacement\n"
    names = f.nlst("Games")
    assert not any(n.startswith("KUI-ftp-") for n in names), names
    f.delete("Games/keep.txt")
    print("PASS FTP replacing: the old file stays until the upload has its name", flush=True)


def keeps_moving(f, server):
    """While a transfer moves, the server yields between passes instead of
    sleeping. On the console a sleep lasts a scheduler tick (about 8 ms), and
    sleeping each time it waited for the client's acknowledgements, or for
    its next segments, held transfers near 300 KiB/s. With the model's 1 ms
    network latency, a server that sleeps like that shows a sleep for about
    every 4 KB here: over 500 for 2 MiB."""
    data = random.randbytes(2 * 1024 * 1024)
    upload(f, "Games/moving.bin", data)
    reports = [0]

    def marker(key, name, count):
        server.send(key)
        text = f"{name} {count} "
        server.wait_for(text)
        return int(next(line for line in server.lines if line.startswith(text)).split()[2])

    def sleeps():
        reports[0] += 1
        return marker("p", "PAUSES", reports[0])

    assert marker("l", "LATENCY", 1) == 1000
    sleeps()
    assert download(f, "Games/moving.bin") == data
    down = sleeps()
    upload(f, "Games/moving.bin", data)
    up = sleeps()
    assert marker("l", "LATENCY", 2) == 0
    f.delete("Games/moving.bin")
    # About 50 sleeps come from the commands around the transfers.
    assert down < 200 and up < 200, (down, up)
    print(f"PASS FTP transfers keep moving: {down} and {up} sleeps for 2 MiB each way", flush=True)


def protected(f):
    for action in (lambda: f.delete("KUI/runtime.kui"), lambda: f.rename("KUI", "KUI-old"),
                   lambda: f.rename("KUI/apps", "apps"), lambda: f.rmd("KUI/apps/games"),
                   lambda: upload(f, "KUI/runtime.kui", b"replacement"),
                   lambda: f.rename("hello.txt", "KUI/runtime.kui"),
                   lambda: f.delete("KUI/apps/games/retail-boot.kui")):
        assert "K-UI needs" in expect("550", action)
    assert download(f, "KUI/runtime.kui") == b"K-UI runtime stand-in\n"
    listing = []
    f.dir("KUI", listing.append)
    assert any(line.startswith("-r--r--r--") and line.endswith(" runtime.kui") for line in listing), listing
    upload(f, "KUI/notes.txt", b"allowed\n")
    assert f.mkd("KUI/covers") == "/KUI/covers"
    f.delete("KUI/notes.txt")
    print("PASS FTP protected files: K-UI's start-up files cannot be changed", flush=True)


def aborts(f, port, password, files):
    big = files["big"]
    # The client closes the data connection part way: 426, and the session goes on.
    conn = f.transfercmd("RETR Games/big.bin")
    got = conn.recv(100000)
    conn.close()
    assert got
    expect("426", lambda: f.voidresp())
    assert f.sendcmd("NOOP").startswith("200")
    # ABOR during a download: 426 for the transfer, then 226 for ABOR.
    conn = f.transfercmd("RETR Games/big.bin")
    conn.recv(100000)
    f.sock.sendall(b"ABOR\r\n")
    replies = [f.getline(), f.getline()]
    conn.close()
    assert replies[0].startswith("426") and replies[1].startswith("226"), replies
    assert f.sendcmd("ABOR").startswith("225")
    # An upload cut off by a reset: nothing is kept.
    conn = f.transfercmd("STOR Games/partial.bin")
    conn.sendall(random.randbytes(500 * 1024))
    conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    conn.close()
    expect("426", lambda: f.voidresp())
    time.sleep(0.2)
    names = f.nlst("Games")
    assert "partial.bin" not in names and not any(n.endswith(".kui-part") for n in names), names
    # A file being sent to one client cannot be changed by another.
    reader = client(port, password)
    conn = reader.transfercmd("RETR Games/big.bin")
    time.sleep(0.5)
    other = client(port, password)
    assert "another client" in expect("450", lambda: other.delete("Games/big.bin"))
    expect("450", lambda: upload(other, "Games/big.bin", b"x"))
    other.sendcmd("RNFR Games/big.bin")
    expect("450", lambda: other.sendcmd("RNTO Games/bigger.bin"))
    expect("450", lambda: other.rmd("Games"))
    received = bytearray()
    while chunk := conn.recv(1 << 20):
        received += chunk
    conn.close()
    assert reader.voidresp().startswith("226") and digest(bytes(received)) == digest(big)
    other.quit()
    reader.quit()
    print("PASS FTP aborts: closed data connection, ABOR, reset upload discarded, files in use", flush=True)


def limits(port, password, connected):
    """`connected` clients are already logged in."""
    clients = [client(port, password) for _ in range(3 - connected)]
    extra = ftplib.FTP()
    expect("421", lambda: extra.connect(HOST, port, timeout=30))
    extra.close()
    clients.pop().quit()
    deadline = time.monotonic() + 10
    while True:
        try:
            late = client(port, password)
            break
        except ftplib.error_temp:
            assert time.monotonic() < deadline
            time.sleep(0.2)
    assert late.pwd() == "/"
    for c in clients + [late]:
        c.quit()
    print("PASS FTP limits: three clients at once, the fourth told why", flush=True)


def full_card(f):
    class Endless:
        def __init__(self):
            self.left = 200 * 1024 * 1024

        def read(self, size):
            size = min(size, self.left)
            self.left -= size
            return b"\xa5" * size

    try:
        f.storbinary("STOR Games/huge.bin", Endless(), 65536)
        raise AssertionError("the card never filled")
    except (ConnectionError, OSError, ftplib.error_temp) as error:
        if not isinstance(error, ftplib.error_temp):
            # The server closed the data connection while data was still
            # going out; its reply follows on the control connection.
            try:
                f.voidresp()
                raise AssertionError("an upload larger than the card succeeded")
            except ftplib.error_temp as late:
                error = late
        assert str(error).startswith("452"), str(error)
    names = f.nlst("Games")
    assert "huge.bin" not in names and not any(n.endswith(".kui-part") for n in names), names
    upload(f, "Games/after-full.bin", b"there is room again\n")
    assert download(f, "Games/after-full.bin") == b"there is room again\n"
    print("PASS FTP full card: 452, nothing kept, room again afterwards", flush=True)


def wifi_events(server, port, password):
    """The Wi-Fi board's news while serving: its Wi-Fi drops and comes back
    on another address, then the board restarts, closing every connection;
    the server listens again by itself."""
    f = client(port, password)
    server.send("d")
    server.wait_for("FTP: Wi-Fi connection lost; the board is reconnecting")
    server.send("u")
    server.wait_for("FTP: Wi-Fi connection back")
    server.wait_for("FTP: New address: 127.0.0.2")
    assert f.sendcmd("NOOP").startswith("200")
    server.send("r")
    server.wait_for("FTP: The Wi-Fi board restarted; its connections were closed")
    try:
        f.sendcmd("NOOP")
        f.sendcmd("NOOP")
        raise AssertionError("the connection survived the board's restart")
    except (EOFError,) + ftplib.all_errors:
        pass
    f.close()
    deadline = time.monotonic() + 20
    while True:
        try:
            again = client(port, password)
            break
        except ftplib.all_errors:
            assert time.monotonic() < deadline, "no listening again after the board's restart"
            time.sleep(0.2)
    assert again.pwd() == "/" and again.sendcmd("PASV").startswith("227 Entering Passive Mode (127,0,0,2,")
    again.quit()
    print("PASS FTP over Wi-Fi: connection lost and back, new address, board restart", flush=True)


def streams(f, server):
    """Uploads and downloads move by the model's async frames while the
    card works (the adapter line says "overlapped"), and each leaves the
    screen's line for the last upload or download. A frame that fails
    partway is tried again (the line gives the share of pieces tried again,
    rounded up) and the transfer goes on streaming; 64 failures
    in a row leave that transfer to go on without them, and the next one
    streams again; one that never ends is given up after 50 ms, and later
    transfers go without them."""
    assert "overlapped" in server.adapter, server.adapter
    data = random.randbytes(3 * 1024 * 1024 + 777)
    faults = [0]

    def fault(key):
        faults[0] += 1
        server.send(key)
        server.wait_for(f"FAULT {faults[0]} {key}")

    def last(way, action):
        """Runs the transfer, then returns its line (way "up" or "down")."""
        prefix = f"LOG FTP: Last {way} "
        before = server.count(prefix)
        result = action()
        deadline = time.monotonic() + 20
        while server.count(prefix) <= before:
            assert time.monotonic() < deadline, server.output()[-3000:]
            time.sleep(0.05)
        return [line for line in server.lines if prefix in line][-1], result

    # Every event is logged; EVENT shows only the latest at each update.
    kept = "LOG FTP: DMA transfers kept failing; going on without"
    line, _ = last("up", lambda: upload(f, "Games/stream.bin", data))
    assert "KiB/s: card " in line and ", overlap " in line and "retr" not in line, line
    fault("f")
    line, _ = last("up", lambda: upload(f, "Games/stream.bin", data))
    assert line.endswith(", 0.1% retried"), line
    fault("f")
    line, got = last("down", lambda: download(f, "Games/stream.bin"))
    assert digest(got) == digest(data) and line.endswith(", 0.1% retried"), line
    assert server.count(kept) == 0, server.output()[-3000:]
    fault("F")
    line, _ = last("up", lambda: upload(f, "Games/stream.bin", data))
    assert line.endswith(", then no DMA") and server.count(kept) == 1, line
    line, got = last("down", lambda: download(f, "Games/stream.bin"))
    assert digest(got) == digest(data) and ", overlap " in line and "no DMA" not in line, line
    fault("x")
    upload(f, "Games/stream.bin", data)
    server.wait_for("LOG FTP: DMA transfers stopped; going on without them")
    line, got = last("down", lambda: download(f, "Games/stream.bin"))
    assert digest(got) == digest(data) and line.endswith(", DMA off"), line
    f.delete("Games/stream.bin")
    print("PASS FTP streams: overlapped transfers, their lines, retried, failing and stalled DMA frames", flush=True)


def card_fails(f, server):
    """A card write that fails during an upload: 451, nothing kept, and the
    next upload on the same connection works."""
    server.send("w")
    server.wait_for("WRITE 1")
    try:
        upload(f, "Games/failing.bin", random.randbytes(300 * 1024))
        raise AssertionError("an upload whose card write failed succeeded")
    except (ConnectionError, OSError, ftplib.error_temp) as error:
        if not isinstance(error, ftplib.error_temp):
            try:
                f.voidresp()
                raise AssertionError("an upload whose card write failed succeeded")
            except ftplib.error_temp as late:
                error = late
        assert str(error).startswith("451"), str(error)
    names = f.nlst("Games")
    assert "failing.bin" not in names and not any(n.endswith(".kui-part") for n in names), names
    upload(f, "Games/after-fail.bin", b"the card works again\n")
    assert download(f, "Games/after-fail.bin") == b"the card works again\n"
    f.delete("Games/after-fail.bin")
    print("PASS FTP card write failure: 451, nothing kept, the next upload works", flush=True)


def serve_image(binary, image, kind, port, passive, case_insensitive=True, env=None, adapter=()):
    server = Server(binary, image, port, passive, env=env, extra=adapter)
    password = server.password
    assert len(password) == 8 and password.isdigit(), password
    basics(port, password)
    f = client(port, password)
    folders(f)
    files = transfers(f, port, password, kind)
    changes(f, case_insensitive)
    replacing(f, server)
    keeps_moving(f, server)
    protected(f)
    aborts(f, port, password, files)
    limits(port, password, 1)
    full_card(f)
    if adapter == ("wifi",):
        wifi_events(server, port, password)
        # The board's restart closed every connection, this one too.
        f.close()
        f = client(port, password)
    card_fails(f, server)
    if not adapter:
        streams(f, server)
    # Stopping with a client connected: it is told.
    f.sock.settimeout(30)
    code, output = server.stop()
    try:
        line = f.getline()
    except (EOFError, OSError):
        line = ""
    assert line.startswith("421"), (line, output[-3000:])
    f.close()
    assert code == 0 and "STOPPED state=2" in output, output[-3000:]
    # Every event is logged; the screen shows only the latest.
    assert "LOG FTP: Received /Games/big.bin (20.0 MB)" in output, output[-3000:]
    return password, files


def main():
    for binary in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat", "mcopy", "mdir"):
        if not shutil.which(binary):
            raise SystemExit(f"Missing test prerequisite: {binary}")
    # Below Linux's ephemeral ports (32768 and up), so no binding collides
    # with an outgoing connection.
    base_port = 22000 + os.getpid() % 400 * 10
    with tempfile.TemporaryDirectory(prefix="kui-ftp-") as temp:
        base = Path(temp)
        for number, kind in enumerate(("fat32", "exfat")):
            port = base_port + number * 5
            passive = 26000 + (os.getpid() % 40) * 150 + number * 50
            image = base / f"{kind}.img"
            with image.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(image)) if kind == "fat32" else run("mkfs.exfat", str(image))
            fsck = ("fsck.fat",) if kind == "fat32" else ("fsck.exfat",)
            run(BINARY, str(image), "seed")

            # No W5500: the server stops before touching the card.
            quiet = Server(BINARY, image, port + 1, passive, extra=("absent",), ready=False)
            assert quiet.proc.wait(60) == 1
            time.sleep(0.1)
            assert "STOPPED state=3" in quiet.output() and "No W5500 answered" in quiet.output(), quiet.output()
            # Neither a W5500 nor the Wi-Fi board.
            quiet = Server(BINARY, image, port + 1, passive, extra=("none",), ready=False)
            assert quiet.proc.wait(60) == 1
            time.sleep(0.1)
            assert "No W5500 or Wi-Fi board answered on the SCI port" in quiet.output(), quiet.output()
            assert "Opening the SD card" not in quiet.output(), quiet.output()
            # The Wi-Fi board with no network set up.
            quiet = Server(BINARY, image, port + 1, passive, extra=("offline",), ready=False)
            assert quiet.proc.wait(60) == 1
            time.sleep(0.1)
            assert "STOPPED state=3" in quiet.output() and "No Wi-Fi network is set up" in quiet.output(), quiet.output()

            password, files = serve_image(BINARY, image, kind, port, passive)
            run(*fsck, "-n", str(image))
            if kind == "fat32":
                out = base / "out"
                out.mkdir(exist_ok=True)
                run("mcopy", "-n", "-i", str(image), "::/Games/big.bin", str(out / "big.bin"))
                assert digest((out / "big.bin").read_bytes()) == digest(files["big"])
                run("mcopy", "-n", "-i", str(image), "::/KUI/ftp-password.txt", str(out / "password.txt"))
                assert (out / "password.txt").read_text().strip() == password
                listing = run("mdir", "-i", str(image), "-/", "-a", "::/")
                assert ".kui-part" not in listing.lower(), listing
            # The password is kept for the next start. This time the W5500
            # has no async frames: transfers go the plain way.
            again = Server(BINARY, image, port + 2, passive, extra=("sync",))
            assert again.password == password and "overlapped" not in again.adapter, again.adapter
            f = client(port + 2, password)
            data = random.randbytes(5 * 1024 * 1024 + 99)
            upload(f, "Games/plain.bin", data)
            assert digest(download(f, "Games/plain.bin")) == digest(data)
            assert download(f, "Games/plain.bin", rest=12345) == data[12345:]
            f.delete("Games/plain.bin")
            f.quit()
            code, output = again.stop()
            assert code == 0, output
            run(*fsck, "-n", str(image))
            print(f"PASS {kind} FTP server", flush=True)
            # Everything again over the Wi-Fi board, on a fresh card of
            # the same filesystem. The owner's hardware uses exFAT.
            wifi = base / f"wifi-{kind}.img"
            with wifi.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(wifi)) if kind == "fat32" else run("mkfs.exfat", str(wifi))
            run(BINARY, str(wifi), "seed")
            _, wifi_files = serve_image(BINARY, wifi, kind, port + 4, passive + 100, adapter=("wifi",))
            run(*fsck, "-n", str(wifi))
            if kind == "fat32":
                out = base / "out"
                out.mkdir(exist_ok=True)
                run("mcopy", "-n", "-i", str(wifi), "::/Games/big.bin", str(out / "wifi-big.bin"))
                assert digest((out / "wifi-big.bin").read_bytes()) == digest(wifi_files["big"])
            wifi.unlink()
            print(f"PASS {kind} FTP server over the Wi-Fi board", flush=True)

            # An unusable password file stops the server with the reason.
            broken = base / f"{kind}-broken.img"
            with broken.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(broken)) if kind == "fat32" else run("mkfs.exfat", str(broken))
            run(BINARY, str(broken), "bad-password")
            bad = Server(BINARY, broken, port + 3, passive, ready=False)
            assert bad.proc.wait(60) == 1
            time.sleep(0.1)
            assert "must hold 1 to 32" in bad.output(), bad.output()
            run(*fsck, "-n", str(broken))
            broken.unlink()
            image.unlink()


if __name__ == "__main__":
    main()
