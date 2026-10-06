#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Expand compressed game images into a new, atomically published SD folder.

CSO v1/v2 and ZSO preserve the exact uncompressed ISO bytes. Standard ZSO uses
LZ4; DreamShell's LZO dialect requires --zso-codec lzo and installed liblzo2.
CHD CD/GD images require an installed MAME chdman: verify, then extractcd.
GDI/CUE export preserves tracks and sessions but omits CHD subchannel data.
The source is never edited. Conversion does not establish game compatibility.

Independently implemented from the format descriptions:
https://github.com/unknownbrackets/maxcso/blob/master/README_CSO.md
https://github.com/unknownbrackets/maxcso/blob/master/README_ZSO.md
https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md
https://github.com/mamedev/mame/blob/master/src/lib/util/chd.h
https://docs.mamedev.org/tools/chdman.html
DreamShell LZO dialect confirmed by its historical GPL-2.0 isofs reader:
https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/modules/isofs/ciso.c
"""
import argparse
from array import array
import ctypes
import ctypes.util
import errno
import hashlib
from functools import lru_cache
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zlib

MAX_IMAGE_BYTES = 2 * 1024**3
MAX_BLOCK_BYTES = 1024**2
MAX_INDEX_BYTES = 4 * 1024**2 + 4
MAX_METADATA_BYTES = 1024**2
DESCRIPTOR_LIMIT = 32768
LBA_LIMIT = 719850
COPY_BYTES = 1024**2
PROFILE = "kui-game-image-import-v1"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def signature(path):
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode), f"Not a regular file: {path}")
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def read_exact(stream, size, label):
    data = stream.read(size)
    require(len(data) == size, f"Truncated {label}")
    return data


def hash_file(path):
    before = signature(path)
    require(before[2] <= MAX_IMAGE_BYTES + MAX_INDEX_BYTES + MAX_BLOCK_BYTES,
            "File exceeds hashing size limit")
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while size < before[2]:
            block = read_exact(stream, min(COPY_BYTES, before[2] - size), "file while hashing")
            digest.update(block)
            size += len(block)
        require(not stream.read(1), "File grew while hashing")
    require(signature(path) == before, "File changed while hashing")
    return {"bytes": size, "sha256": digest.hexdigest()}


def safe_name(name):
    return (0 < len(name.encode("utf-8")) < 128
            and not name.startswith((".", " ")) and not name.endswith((".", " "))
            and all(ord(c) >= 32 and not 127 <= ord(c) <= 159
                    and c not in '\\/:*?"<>|' for c in name))


def publish_directory(source, destination):
    """Exclusive atomic rename, including when another process creates the target."""
    if os.name == "nt":
        os.rename(source, destination)
        return
    libc = ctypes.CDLL(None, use_errno=True)
    if sys.platform.startswith("linux") and hasattr(libc, "renameat2"):
        rename = libc.renameat2
        rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        rename.restype = ctypes.c_int
        result = rename(-100, os.fsencode(source), -100, os.fsencode(destination), 1)
    elif sys.platform == "darwin" and hasattr(libc, "renamex_np"):
        rename = libc.renamex_np
        rename.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        rename.restype = ctypes.c_int
        result = rename(os.fsencode(source), os.fsencode(destination), 4)
    else:
        raise OSError(errno.ENOTSUP, "Platform cannot publish atomically without overwriting")
    if result:
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), str(destination))


def decode_lz4(data, expected):
    """Decode one independent LZ4 block; return bytes and consumed input length.

    Output and every literal/match are bounded by the header's block size.
    Input can include alignment padding, which the container validates later.
    """
    result, offset = bytearray(), 0
    last_match_start = None

    def length(base):
        nonlocal offset
        size = base
        if base == 15:
            while True:
                require(offset < len(data), "Truncated LZ4 length")
                extra = data[offset]
                offset += 1
                size += extra
                require(size <= expected, "LZ4 length exceeds output block")
                if extra != 255:
                    break
        return size

    while offset < len(data):
        token = data[offset]
        offset += 1
        literals = length(token >> 4)
        require(literals <= expected - len(result), "LZ4 literals exceed output block")
        require(literals <= len(data) - offset, "Truncated LZ4 literals")
        result.extend(data[offset:offset + literals])
        offset += literals
        if len(result) == expected:
            require(literals >= min(5, expected), "LZ4 block lacks final literals")
            require(last_match_start is None or last_match_start <= expected - 12,
                    "LZ4 final match is too close to block end")
            return bytes(result), offset
        require(offset + 2 <= len(data), "Truncated LZ4 match offset")
        distance = data[offset] | data[offset + 1] << 8
        offset += 2
        require(0 < distance <= len(result), "Invalid LZ4 match offset")
        count = length(token & 15) + 4
        require(count <= expected - len(result), "LZ4 match exceeds output block")
        last_match_start = len(result)
        # Repeating the preceding distance bytes implements overlapping matches
        # without a byte-at-a-time loop over a potentially megabyte-long run.
        pattern = bytes(result[-distance:])
        result.extend((pattern * ((count + distance - 1) // distance))[:count])
    raise ValueError("LZ4 block has incomplete output")


def decode_deflate(data, expected):
    inflater = zlib.decompressobj(-15)
    try:
        result = inflater.decompress(data, expected + 1)
    except zlib.error as error:
        raise ValueError(f"Invalid raw DEFLATE block: {error}") from error
    require(len(result) == expected, "DEFLATE block has incorrect output size")
    require(inflater.eof and not inflater.unconsumed_tail, "Incomplete or oversized DEFLATE block")
    return result, len(data) - len(inflater.unused_data)


@lru_cache(maxsize=4)
def load_lzo_decoder(library=None):
    name = str(library) if library else ctypes.util.find_library("lzo2")
    require(name is not None, "LZO ZSO requires installed liblzo2; install it or supply --lzo-library PATH")
    try:
        codec = ctypes.CDLL(name)
        decode = codec.lzo1x_decompress_safe
    except (OSError, AttributeError) as error:
        raise ValueError(f"Cannot load safe LZO decoder from {name}: {error}") from error
    # LZO's public ABI requires lzo_uint to match size_t (including Win64).
    decode.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                       ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p)
    decode.restype = ctypes.c_int
    return decode


def decode_lzo(data, expected, padding=0, library=None):
    require(0 < expected <= MAX_BLOCK_BYTES and len(data) <= MAX_BLOCK_BYTES + 65536 + 8192,
            "LZO block exceeds memory limit")
    decode = load_lzo_decoder(library)
    source = ctypes.create_string_buffer(data, len(data))
    destination = ctypes.create_string_buffer(expected)

    def trial(size):
        written = ctypes.c_size_t(expected)
        status = decode(source, size, destination, ctypes.byref(written), None)
        return status, written.value

    status, written = trial(len(data))
    require(status in (0, -8) and written == expected,
            f"Invalid or oversized LZO1X block (decoder status {status}, output {written})")
    if status == 0:
        return destination.raw, len(data)
    # Safe LZO reports INPUT_NOT_CONSUMED (-8) after its end marker when CSO
    # alignment padding follows. Find the exact end in logarithmically many
    # bounded decodes, rather than guessing marker bytes inside literal data.
    low, high = max(1, len(data) - padding), len(data)
    while low < high:
        middle = (low + high) // 2
        status, written = trial(middle)
        if status in (0, -8) and written == expected:
            high = middle
        else:
            low = middle + 1
    status, written = trial(low)
    require(status == 0 and written == expected, "LZO block has excess padding or an invalid end marker")
    return destination.raw, low


def expand_cso(source, destination, max_bytes, progress=None, zso_codec="lz4", lzo_library=None):
    with source.open("rb") as stream:
        header = read_exact(stream, 24, "CSO/ZSO header")
        magic, header_size, total, block_size, version, shift, reserved = struct.unpack("<4sIQIBB2s", header)
        require(magic in (b"CISO", b"ZISO"), "Unsupported compressed image signature")
        if magic == b"CISO" and version in (0, 1):
            # Several original v1 writers put zero in these unreliable fields.
            kind = "CSO v1"
        elif magic == b"CISO" and version == 2:
            require(header_size == 24 and reserved == b"\0\0", "Invalid CSO v2 header")
            kind = "CSO v2"
        elif magic == b"ZISO" and zso_codec == "lzo":
            require(version in (0, 1), "Unsupported LZO ZSO version")
            kind = "ZSO (LZO1X)"
        else:
            require(magic == b"ZISO" and version == 1 and header_size == 24
                    and reserved == b"\0\0", "Unsupported ZSO version/header (standard LZ4 ZSO required)")
            kind = "ZSO (LZ4)"
        require(0 < total <= max_bytes and total % 2048 == 0,
                "Uncompressed image must be positive whole 2048-byte sectors within the size limit")
        require(2048 <= block_size <= MAX_BLOCK_BYTES and block_size % 2048 == 0,
                "Block size must be whole 2048-byte sectors from 2048 to 1048576")
        require(shift <= 16, "CSO/ZSO alignment shift exceeds 16")
        block_count = (total + block_size - 1) // block_size
        index_bytes = (block_count + 1) * 4
        require(index_bytes <= MAX_INDEX_BYTES, "Compressed image index exceeds memory limit")
        indices = array("I")
        require(indices.itemsize == 4, "Platform lacks 32-bit array entries")
        indices.frombytes(read_exact(stream, index_bytes, "CSO/ZSO index"))
        if sys.byteorder != "little":
            indices.byteswap()
        alignment = 1 << shift
        file_bytes = signature(source)[2]
        previous = (indices[0] & 0x7fffffff) << shift
        require(previous >= 24 + index_bytes, "Compressed index points into header/index")
        require(kind != "CSO v2" or not indices[-1] & 0x80000000,
                "Terminal CSO v2 index has a flag")
        for entry in indices[1:]:
            current = (entry & 0x7fffffff) << shift
            require(previous < current <= file_bytes, "Compressed indices must increase within the file")
            previous = current
        require(file_bytes - previous < alignment, "Unexpected bytes after final compressed block")
        digest = hashlib.sha256()
        with destination.open("xb") as output:
            for number in range(block_count):
                start = (indices[number] & 0x7fffffff) << shift
                end = (indices[number + 1] & 0x7fffffff) << shift
                span = end - start
                expected = min(block_size, total - number * block_size)
                require(span <= block_size + block_size // 255 + 16 + alignment - 1,
                        f"Block {number}: stored span exceeds limit")
                stream.seek(start)
                stored = read_exact(stream, span, f"compressed block {number}")
                flagged = bool(indices[number] & 0x80000000)
                raw = span >= block_size if kind == "CSO v2" else flagged
                if raw:
                    require(span >= expected, f"Block {number}: short uncompressed block")
                    require(span <= block_size + alignment - 1,
                            f"Block {number}: excess uncompressed-block padding")
                    result = stored[:expected]
                else:
                    try:
                        if magic == b"ZISO" and zso_codec == "lzo":
                            result, consumed = decode_lzo(stored, expected, alignment - 1, lzo_library)
                        elif magic == b"ZISO" or (kind == "CSO v2" and flagged):
                            result, consumed = decode_lz4(stored, expected)
                        else:
                            result, consumed = decode_deflate(stored, expected)
                        require(span - consumed < alignment, "Excess compressed-block padding")
                    except ValueError as error:
                        raise ValueError(f"Block {number}: {error}") from error
                output.write(result)
                digest.update(result)
                if progress and (number % 4096 == 0 or number + 1 == block_count):
                    progress(f"Expanding {kind}: {number + 1}/{block_count} blocks")
            output.flush()
            os.fsync(output.fileno())
        return {"format": kind, "block_bytes": block_size, "blocks": block_count,
                "alignment_bytes": alignment, "uncompressed_bytes": total,
                "zso_codec": zso_codec if magic == b"ZISO" else None}, {
                    "file": destination.name, "bytes": total, "sha256": digest.hexdigest()}


def inspect_chd(path, max_bytes):
    """Inspect bounded headers/metadata; chdman performs actual CHD validation."""
    size = signature(path)[2]
    with path.open("rb") as stream:
        prefix = read_exact(stream, 16, "CHD header")
        magic, length, version = struct.unpack(">8sII", prefix)
        require(magic == b"MComprHD" and version in (3, 4, 5), "CHD CD/GD import requires CHD v3, v4 or v5")
        require(length == {3: 120, 4: 108, 5: 124}[version], "Invalid CHD header length")
        header = prefix + read_exact(stream, length - 16, "CHD header")
        logical = struct.unpack_from(">Q", header, 32 if version == 5 else 28)[0]
        metadata = struct.unpack_from(">Q", header, 48 if version == 5 else 36)[0]
        hunk = struct.unpack_from(">I", header, {3: 76, 4: 44, 5: 56}[version])[0]
        require(0 < logical <= max_bytes, "CHD logical size exceeds image size limit")
        require(16 <= hunk <= MAX_BLOCK_BYTES, "CHD hunk size exceeds memory limit")
        tags, visited, metadata_bytes, subchannel = [], set(), 0, False
        while metadata:
            require(len(visited) < 256 and metadata not in visited, "CHD metadata chain is cyclic or too long")
            require(length <= metadata <= size - 16, "CHD metadata offset is outside the file")
            visited.add(metadata)
            stream.seek(metadata)
            tag, flags_length, next_offset = struct.unpack(">4sIQ", read_exact(stream, 16, "CHD metadata"))
            entry_bytes = flags_length & 0xffffff
            metadata_bytes += entry_bytes
            require(metadata_bytes <= MAX_METADATA_BYTES and entry_bytes <= size - metadata - 16,
                    "CHD metadata is truncated or exceeds memory limit")
            payload = read_exact(stream, entry_bytes, "CHD metadata payload")
            tags.append(tag.decode("ascii", errors="replace"))
            if tag in (b"CHTR", b"CHT2", b"CHGT", b"CHGD"):
                subtype = re.search(rb"(?:^|\s)SUBTYPE:([^\s\0]+)", payload)
                require(subtype is not None, "CHD track metadata lacks SUBTYPE")
                pregap_subtype = re.search(rb"(?:^|\s)PGSUB:([^\s\0]+)", payload)
                if subtype[1] != b"NONE" or (pregap_subtype and pregap_subtype[1] != b"NONE"):
                    subchannel = True
            elif tag == b"CHCD":
                if subchannel is not True:
                    subchannel = None  # Legacy binary metadata; no lossless-export promise.
            metadata = next_offset
        return {"version": version, "logical_bytes": logical, "metadata_tags": tags,
                "gdrom": any(tag in ("CHGT", "CHGD") for tag in tags),
                "cdrom": any(tag in ("CHCD", "CHTR", "CHT2") for tag in tags),
                "subchannel_omitted": subchannel}


def run_chdman(command, stage, timeout):
    # Keep potentially verbose progress on disk, not in a growing captured string.
    log = stage / ".chdman.log"
    with log.open("wb") as output:
        try:
            result = subprocess.run(command, cwd=stage, stdout=output, stderr=subprocess.STDOUT,
                                    timeout=timeout, check=False)
        except subprocess.TimeoutExpired as error:
            raise ValueError(f"chdman {command[1]} exceeded {timeout}s time limit") from error
    with log.open("rb") as stream:
        stream.seek(max(0, log.stat().st_size - 16384))
        tail = stream.read(16384).decode("utf-8", errors="replace")
    log.unlink()
    require(result.returncode == 0, f"chdman {command[1]} failed ({result.returncode}): {tail.strip()}")
    return tail


def validate_extracted(stage, descriptor, max_bytes):
    """Check output confinement, track references, and positive whole sectors."""
    files, total = {}, 0
    for path in stage.iterdir():
        require(safe_name(path.name) and path.name.isascii(), f"Unsafe chdman output name: {path.name}")
        byte_count = signature(path)[2]
        require(byte_count > 0, f"Empty chdman output: {path.name}")
        total += byte_count
        require(total <= max_bytes + DESCRIPTOR_LIMIT, "Extracted image exceeds size limit")
        require(path.name.casefold() not in files, "Ambiguous extracted filenames")
        files[path.name.casefold()] = (path.name, byte_count)
    require(descriptor.name.casefold() in files, "chdman did not produce a descriptor")
    require(descriptor.stat().st_size <= DESCRIPTOR_LIMIT, "Extracted descriptor exceeds 32768 bytes")
    text = descriptor.read_text(encoding="utf-8-sig")
    references, tracks = set(), 0
    if descriptor.suffix == ".gdi":
        previous_end = 0
        lines = [line.strip() for line in text.splitlines() if line.strip()]
        require(lines and lines[0].isdigit() and 1 <= int(lines[0]) <= 99
                and len(lines) == int(lines[0]) + 1, "Invalid extracted GDI track count")
        for number, line in enumerate(lines[1:], 1):
            match = re.fullmatch(r'(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(?:"([^"\r\n]+)"|(\S+))\s+(\d+)', line)
            require(match is not None and int(match[1]) == number, "Invalid extracted GDI track row")
            lba, control, sector, name, offset = int(match[2]), int(match[3]), int(match[4]), match[5] or match[6], int(match[7])
            require(control in (0, 4) and sector in (2048, 2352)
                    and (control == 4 or sector == 2352), "Unsupported extracted GDI sector layout")
            validate_reference(files, name, offset, sector)
            require(offset == 0 and name.casefold() not in references,
                    "Extracted GDI must use separate track files with zero offsets")
            sectors = files[name.casefold()][1] // sector
            require(previous_end <= lba < LBA_LIMIT and lba + sectors <= LBA_LIMIT,
                    "Extracted GDI tracks overlap or exceed LBA limits")
            previous_end = lba + sectors
            references.add(name.casefold())
            tracks += 1
    else:
        tracks, references = validate_cue(text, files)
    require(set(files) == references | {descriptor.name.casefold()}, "Unreferenced chdman output files")
    return tracks, [{"file": name, **hash_file(stage / name)} for name, _ in sorted(files.values())]


def validate_reference(files, name, offset, sector):
    require(safe_name(name) and name.isascii() and name.casefold() in files, "Unsafe or missing extracted track file")
    size = files[name.casefold()][1]
    require(0 <= offset < size and (size - offset) % sector == 0,
            "Extracted track is not a positive whole number of sectors")


def cue_frames(value):
    match = re.fullmatch(r"([0-9]{1,3}):([0-9]{1,2}):([0-9]{1,2})", value)
    require(match is not None, "Invalid extracted CUE MSF timestamp")
    minute, second, frame = map(int, match.groups())
    total = minute * 4500 + second * 75 + frame
    require(second < 60 and frame < 75 and total < LBA_LIMIT,
            "Extracted CUE MSF timestamp exceeds limits")
    return total


def validate_cue(text, files):
    """Validate the separate-track CUE contract requested by extractcd -sb.

    This deliberately accepts one track per FILE. Shared-file CUE geometry is
    handled by the console's general parser, not guessed during CHD export.
    """
    require(all(ch in "\t\r\n" or ord(ch) >= 32 for ch in text),
            "Extracted CUE contains control bytes")
    records, references = [], set()
    current_name, track, session, density, pending_leadin = None, None, 1, 0, None
    scramble_marker = False
    modes = {"AUDIO": 2352, "CDG": 2448, "MODE1/2048": 2048, "MODE1/2352": 2352,
             "MODE1/2448": 2448, "MODE2/2048": 2048, "MODE2/2336": 2336,
             "MODE2/2352": 2352, "MODE2/2448": 2448}
    for line in text.splitlines():
        line = line.strip(" \t")
        if not line:
            continue
        command = line.split(None, 1)[0].upper()
        if command == "FILE":
            require(current_name is None or track is not None and 1 in track["indices"],
                    "Extracted CUE track lacks INDEX 01")
            match = re.fullmatch(r'FILE\s+(?:"([^"\r\n]+)"|(\S+))\s+BINARY', line, re.IGNORECASE)
            require(match is not None, "Extracted CUE requires BINARY files")
            current_name = match[1] or match[2]
            require(safe_name(current_name) and current_name.isascii(), "Unsafe extracted CUE filename")
            require(current_name.casefold() in files, "Missing extracted CUE track file")
            require(current_name.casefold() not in references, "Extracted CUE repeats a FILE")
            references.add(current_name.casefold())
            track = None
        elif command == "TRACK":
            match = re.fullmatch(r'TRACK\s+([0-9]{1,2})\s+(\S+)', line, re.IGNORECASE)
            require(match is not None and current_name is not None and track is None
                    and match[2].upper() in modes, "Unsupported extracted CUE split-track layout")
            require(int(match[1]) == len(records) + 1 and len(records) < 99,
                    "Nonconsecutive extracted CUE tracks")
            sector = modes[match[2].upper()]
            validate_reference(files, current_name, 0, sector)
            track = {"file": current_name, "frames": files[current_name.casefold()][1] // sector,
                     "session": session, "density": density, "indices": {}, "pregap": None,
                     "postgap": None, "leadin": pending_leadin, "leadout": None}
            records.append(track)
            pending_leadin = None
        elif command == "INDEX":
            match = re.fullmatch(r'INDEX\s+([0-9]{1,2})\s+(\S+)', line, re.IGNORECASE)
            require(match is not None and track is not None, "Invalid extracted CUE INDEX")
            number, frame = int(match[1]), cue_frames(match[2])
            indices = track["indices"]
            require(number not in indices and (not indices or number > max(indices)),
                    "Duplicate or out-of-order extracted CUE INDEX")
            require(number in (0, 1) or 1 in indices, "Extracted CUE track lacks INDEX 01")
            require(not indices or frame >= indices[max(indices)], "Extracted CUE INDEX timestamps decrease")
            require(frame < track["frames"],
                    "Extracted CUE INDEX exceeds track file bounds")
            indices[number] = frame
        elif command in ("PREGAP", "POSTGAP"):
            match = re.fullmatch(r'(?:PREGAP|POSTGAP)\s+(\S+)', line, re.IGNORECASE)
            field = command.lower()
            require(match is not None and track is not None and track[field] is None,
                    "Invalid or duplicate extracted CUE gap")
            track[field] = cue_frames(match[1])
        elif command == "REM":
            parts = line.split()
            if len(parts) < 2:
                continue
            kind = parts[1].upper()
            if kind == "SESSION":
                require(len(parts) == 3 and re.fullmatch(r"[0-9]{1,2}", parts[2])
                        and session <= int(parts[2]) <= 2, "Unsupported extracted CUE session")
                session = int(parts[2])
            elif kind in ("LEAD-IN", "PREGAP", "LEAD-OUT"):
                require(len(parts) == 3, "Invalid extracted CUE session gap")
                frames = cue_frames(parts[2])
                if kind == "LEAD-OUT":
                    require(track is not None and track["leadout"] is None,
                            "Invalid extracted CUE LEAD-OUT")
                    track["leadout"] = frames
                else:
                    pending_leadin = (pending_leadin or 0) + frames
                    require(pending_leadin < LBA_LIMIT, "Extracted CUE lead-in exceeds limits")
            elif kind in ("SINGLE-DENSITY", "HIGH-DENSITY"):
                next_density = 1 if kind == "SINGLE-DENSITY" else 2
                require(len(parts) == 3 and parts[2].upper() == "AREA"
                        and not (density == 2 and next_density == 1), "Invalid extracted CUE density marker")
                density = next_density
            elif kind == "KUI":
                require(len(parts) == 4 and parts[2].upper() == "SCRAMBLED"
                        and parts[3] in ("0", "1") and not scramble_marker,
                        "Invalid extracted CUE SCRAMBLED marker")
                scramble_marker = True
        else:
            require(command in ("TITLE", "PERFORMER", "SONGWRITER", "CATALOG", "ISRC", "FLAGS", "CDTEXTFILE"),
                    "Unsupported extracted CUE command")
    require(records and track is not None and 1 in track["indices"], "Extracted CUE track lacks INDEX 01")
    require(pending_leadin is None, "Extracted CUE has an unused lead-in")
    logical, previous_end = 0, 0
    for index, track in enumerate(records):
        require(1 in track["indices"], "Extracted CUE track lacks INDEX 01")
        transition = index > 0 and track["session"] > records[index - 1]["session"]
        if transition:
            logical += track["leadin"] if track["leadin"] is not None else 4500
        else:
            require(track["leadin"] is None, "Extracted CUE lead-in requires a new session")
        logical += track["indices"][1] + (track["pregap"] or 0)
        if track["density"] == 2 and (index == 0 or records[index - 1]["density"] != 2):
            logical = 45000
        sectors = track["frames"] - track["indices"][1]
        require(previous_end <= logical < LBA_LIMIT and logical + sectors <= LBA_LIMIT,
                "Extracted CUE session geometry overlaps or exceeds LBA limits")
        previous_end = logical + sectors
        logical = previous_end + (track["postgap"] or 0)
        next_session = index + 1 < len(records) and records[index + 1]["session"] > track["session"]
        if next_session:
            logical += track["leadout"] if track["leadout"] is not None else 6750 if track["session"] == 1 else 2250
        else:
            require(track["leadout"] is None, "Extracted CUE lead-out requires a following session")
        require(logical <= LBA_LIMIT, "Extracted CUE gaps exceed LBA limits")
    return len(records), references


def expand_chd(source, stage, max_bytes, chdman, parent, timeout, drop_subchannels, progress=None):
    metadata = inspect_chd(source, max_bytes)
    if not metadata["cdrom"] and not metadata["gdrom"] and parent:
        parent_metadata = inspect_chd(parent, max_bytes)
        for key in ("cdrom", "gdrom", "metadata_tags", "subchannel_omitted"):
            metadata[key] = parent_metadata[key]
    require(metadata["cdrom"] or metadata["gdrom"], "CHD is not a CD/GD image (DVD, hard disk and laserdisc are unsupported)")
    require(not (metadata["cdrom"] and metadata["gdrom"]), "CHD has conflicting CD and GD track metadata")
    require(metadata["subchannel_omitted"] is False or drop_subchannels,
            "CHD contains subchannel data (or legacy metadata cannot prove its absence); "
            "GDI/CUE export omits it. Use --drop-subchannels to explicitly allow this copy")
    executable = shutil.which(str(chdman))
    require(executable is not None, "CHD import requires MAME chdman; install it or supply --chdman")
    common = ["-i", str(source)] + (["-ip", str(parent)] if parent else [])
    if progress:
        progress("Verifying CHD with chdman")
    run_chdman([executable, "verify", *common], stage, timeout)
    descriptor = stage / ("disc.gdi" if metadata["gdrom"] else "disc.cue")
    if progress:
        progress(f"Extracting CHD to {descriptor.name} and separate tracks")
    run_chdman([executable, "extractcd", *common, "-o", str(descriptor), "-sb"], stage, timeout)
    tracks, outputs = validate_extracted(stage, descriptor, max_bytes)
    return {"format": "CHD", **metadata, "tracks": tracks, "chdman_verified": True,
            "subchannel_preserved": False}, descriptor.name, outputs


def import_image(source, output_directory, *, max_bytes=MAX_IMAGE_BYTES,
                 chdman="chdman", parent=None, timeout=1800, drop_subchannels=False,
                 zso_codec="lz4", lzo_library=None, progress=None):
    """Convert into a new directory; return its saved report after publication."""
    require(0 < max_bytes <= MAX_IMAGE_BYTES, "Size limit must be 1..2147483648 bytes")
    require(0 < timeout <= 86400, "chdman timeout must be 1..86400 seconds")
    require(zso_codec in ("lz4", "lzo"), "ZSO codec must be lz4 or lzo; automatic guessing is unsupported")
    source = Path(source).expanduser().absolute()
    source_signature = signature(source)
    require(source_signature[2] <= max_bytes + MAX_INDEX_BYTES + MAX_BLOCK_BYTES,
            "Compressed source exceeds size limit")
    source = source.resolve()
    output = Path(output_directory).expanduser().absolute()
    require(not os.path.lexists(output), f"Output already exists: {output}")
    require(output.parent.is_dir(), "Output parent directory does not exist")
    output = output.parent.resolve() / output.name
    require(output != source.parent and source.parent not in output.parents,
            "Output must be outside the original image directory")
    require(safe_name(output.name), "Output directory name is unsafe or exceeds 127 bytes")
    with source.open("rb") as stream:
        magic = stream.read(8)
    require(magic[:4] in (b"CISO", b"ZISO") or magic == b"MComprHD",
            "Unsupported image: import accepts CSO, ZSO and CD/GD CHD")
    parent_record, parent_signature = None, None
    if parent is not None:
        require(magic == b"MComprHD", "--parent is only supported for CHD")
        parent = Path(parent).expanduser().absolute()
        parent_signature = signature(parent)
        require(parent_signature[2] <= max_bytes + MAX_INDEX_BYTES + MAX_BLOCK_BYTES,
                "CHD parent exceeds size limit")
        parent = parent.resolve()
        require(parent != source, "CHD parent must differ from the source")
        parent_record = {"path": str(parent), **hash_file(parent)}
    source_record = {"path": str(source), **hash_file(source)}
    require(signature(source) == source_signature, "Source changed while hashing")
    stage = Path(tempfile.mkdtemp(prefix=f".{output.name}.staging-", dir=output.parent))
    try:
        if magic == b"MComprHD":
            details, launch, outputs = expand_chd(source, stage, max_bytes, chdman, parent, timeout,
                                                 drop_subchannels, progress)
        else:
            launch = "disc.iso"
            details, record = expand_cso(source, stage / launch, max_bytes, progress, zso_codec, lzo_library)
            outputs = [record]
        require(signature(source) == source_signature, "Source changed during import")
        require(hash_file(source) == {key: source_record[key] for key in ("bytes", "sha256")},
                "Source content changed during import")
        if parent:
            require(signature(parent) == parent_signature and hash_file(parent) == {
                key: parent_record[key] for key in ("bytes", "sha256")}, "CHD parent changed during import")
        report = {"schema": 1, "profile": PROFILE, "tool": "tools/game_image_import.py",
                  "complete": True, "source": source_record, "parent": parent_record,
                  "image": details, "launch_file": launch, "outputs": outputs,
                  "verification": {"source_unchanged": True, "output_sha256_recorded": True,
                                   "catalogue_match_checked": False, "game_compatibility_checked": False}}
        (stage / "import-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        publish_directory(stage, output)
        return report
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help="CSO, ZSO or CD/GD CHD file")
    parser.add_argument("output", type=Path, help="New output directory outside the source folder")
    parser.add_argument("--chdman", default="chdman", help="Installed MAME chdman executable")
    parser.add_argument("--parent", type=Path, help="Parent CHD required by a delta CHD")
    parser.add_argument("--drop-subchannels", action="store_true", help="Allow CHD export to omit stored subchannel data")
    parser.add_argument("--zso-codec", choices=("lz4", "lzo"), default="lz4", help="ZSO codec: standard LZ4 or DreamShell LZO (default: lz4)")
    parser.add_argument("--lzo-library", help="Optional explicit liblzo2 shared library/DLL path")
    parser.add_argument("--max-bytes", type=int, default=MAX_IMAGE_BYTES, help="Output limit (default: 2 GiB)")
    parser.add_argument("--timeout", type=int, default=1800, help="Seconds per chdman operation (default: 1800)")
    args = parser.parse_args(argv)
    try:
        report = import_image(args.source, args.output, max_bytes=args.max_bytes, chdman=args.chdman,
                              parent=args.parent, timeout=args.timeout, drop_subchannels=args.drop_subchannels,
                              zso_codec=args.zso_codec, lzo_library=args.lzo_library,
                              progress=lambda line: print(line, file=sys.stderr))
    except (OSError, ValueError, UnicodeError) as error:
        print(f"Import failed: {error}", file=sys.stderr)
        return 1
    print(f"Imported {report['image']['format']}: {args.output / report['launch_file']}")
    print("Copy the complete output folder to SD. The original image is unchanged.")
    if report["image"].get("subchannel_omitted") is not False and report["image"]["format"] == "CHD":
        print("CHD subchannel data is omitted by GDI/CUE export; preserve the original CHD.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
