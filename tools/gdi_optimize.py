#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a separate GDI copy with 2048-byte Mode 1 data tracks.

Raw data is framed/address-checked, not EDC/ECC or catalogue-verified. Audio is
copied byte-for-byte. This tool never edits the original dump.
"""
import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
from typing import NamedTuple
import zlib

RAW_BYTES = 2352
DATA_BYTES = 2048
GDI_LIMIT = 32768
TRACK_LIMIT = 99
# Matches the console's extended MSF address limit (159:59:74).
LBA_LIMIT = 719999 - 150 + 1
COPY_BYTES = RAW_BYTES * 128
PROFILE = "kui-gdi-mode1-2048-copy-v1"
OTHER_IMAGE_SUFFIXES = {".cue", ".cdi", ".iso", ".bin", ".img", ".chd", ".cso", ".zso"}
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
RECORD = re.compile(
    r'[ \t]*(\d+)[ \t]+(\d+)[ \t]+(\d+)[ \t]+(\d+)[ \t]+'
    r'(?:"([^"\r\n]+)"|([^ \t"\r\n]+))[ \t]+(\d+)[ \t]*'
)


class Track(NamedTuple):
    number: int
    start_lba: int
    control: int
    sector_bytes: int
    name: str
    path: Path
    file_bytes: int
    sectors: int
    signature: tuple
    descriptor_name: str


def require(condition, message):
    if not condition:
        raise ValueError(message)


def file_signature(path):
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode), f"Not a regular file: {path}")
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def safe_name(name):
    return (0 < len(name.encode("utf-8")) < 128 and not name.startswith((".", " "))
            and not name.endswith((".", " "))
            and all(ord(c) >= 32 and not 127 <= ord(c) <= 159
                    and c not in '\\/:*?"<>|' for c in name))


def track_path(directory, name, number):
    """Resolve harmless case differences without guessing among host files."""
    exact = directory / name
    if os.path.lexists(exact):
        return exact
    with os.scandir(directory) as scan:
        matches = [Path(entry.path) for entry in scan if entry.name.casefold() == name.casefold()]
    require(len(matches) <= 1, f"Track {number}: ambiguous case-insensitive filename: {name}")
    require(matches, f"Track {number}: track file missing: {name}")
    return matches[0]


def load_gdi(path):
    """Validate the whole descriptor and all file sizes before creating output."""
    signature = file_signature(path)
    require(0 < signature[2] <= GDI_LIMIT, "GDI must be 1..32768 bytes")
    with path.open("rb") as source:
        descriptor = source.read(GDI_LIMIT + 1)
    require(len(descriptor) == signature[2], "GDI changed while reading")
    try:
        text = descriptor.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise ValueError("GDI must be UTF-8 text (an optional UTF-8 BOM is accepted)") from error
    # Common editors/exporters add blank lines or use CR rather than CRLF.
    # Output is rebuilt with ASCII fields, normalized track names and LF rows.
    lines = [line for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
             if line.strip(" \t")]
    require(lines and re.fullmatch(r"[ \t]*[0-9]+[ \t]*", lines[0]),
            "Invalid GDI track count")
    count = int(lines[0])
    require(1 <= count <= TRACK_LIMIT and len(lines) == count + 1,
            "GDI must contain exactly 1..99 consecutive tracks")
    tracks, names = [], set()
    for number, line in enumerate(lines[1:], 1):
        match = RECORD.fullmatch(line)
        require(match is not None, f"Track {number}: invalid GDI row")
        index, lba, control, size = (int(match[i]) for i in range(1, 5))
        name, offset = match[5] or match[6], int(match[7])
        require(index == number, f"Track {number}: track numbers must be consecutive")
        require(control in (0, 4) and offset == 0,
                f"Track {number}: only data/audio with zero file offsets are supported")
        require(size == RAW_BYTES or (size == DATA_BYTES and control == 4),
                f"Track {number}: data must be 2352/2048 bytes; audio must be 2352")
        require(safe_name(name), f"Track {number}: unsafe filename")
        require(name.casefold() not in names, f"Track {number}: duplicate filename")
        require(0 <= lba < LBA_LIMIT, f"Track {number}: LBA out of range")
        source_path = track_path(path.parent, name, number)
        source_signature = file_signature(source_path)
        byte_count = source_signature[2]
        require(byte_count > 0 and byte_count % size == 0,
                f"Track {number}: file size is not a positive whole number of sectors")
        sectors = byte_count // size
        require(sectors <= LBA_LIMIT - lba, f"Track {number}: track exceeds LBA limit")
        require(lba >= 45000 or lba + sectors <= 45000,
                f"Track {number}: track crosses the low/high-density session boundary")
        if tracks:
            previous = tracks[-1]
            require(previous.start_lba + previous.sectors <= lba,
                    f"Track {number}: overlapping or out-of-order track")
        names.add(name.casefold())
        tracks.append(Track(number, lba, control, size, source_path.name, source_path,
                            byte_count, sectors, source_signature, name))
    return descriptor, signature, tracks


def digest(data):
    return {"bytes": len(data), "crc32": f"{zlib.crc32(data):08x}",
            "sha256": hashlib.sha256(data).hexdigest()}


def msf(lba):
    fad = lba + 150
    minute, remainder = divmod(fad, 4500)
    second, frame = divmod(remainder, 75)
    # GD-ROM uses the same packed-decimal operation above minute 99.
    return bytes(((value // 10 << 4) | value % 10) for value in (minute, second, frame))


def stream_track(track, output):
    """Bounded streaming; source/output digests refer to their own byte streams."""
    source_sha, output_sha = hashlib.sha256(), hashlib.sha256()
    source_crc, output_crc, consumed, written = 0, 0, 0, 0
    convert = track.control == 4 and track.sector_bytes == RAW_BYTES
    with track.path.open("rb") as source, output.open("xb") as destination:
        require(file_signature(track.path) == track.signature,
                f"Track {track.number}: source changed before reading")
        while consumed < track.file_bytes:
            wanted = min(COPY_BYTES, track.file_bytes - consumed)
            block = source.read(wanted)
            require(len(block) == wanted, f"Track {track.number}: truncated while reading")
            source_sha.update(block)
            source_crc = zlib.crc32(block, source_crc)
            if convert:
                cooked = bytearray()
                for offset in range(0, len(block), RAW_BYTES):
                    sector = block[offset:offset + RAW_BYTES]
                    sector_index = (consumed + offset) // RAW_BYTES
                    address = track.start_lba + sector_index
                    label = f"Track {track.number}, sector {sector_index} (LBA {address})"
                    require(sector[:12] == SYNC, f"{label}: invalid raw sync header")
                    require(sector[15] == 1, f"{label}: unsupported sector mode {sector[15]} (need Mode 1)")
                    require(sector[12:15] == msf(address), f"{label}: sector address does not match GDI")
                    cooked.extend(sector[16:16 + DATA_BYTES])
                result = cooked
            else:
                result = block
            destination.write(result)
            output_sha.update(result)
            output_crc = zlib.crc32(result, output_crc)
            consumed += len(block)
            written += len(result)
        require(not source.read(1), f"Track {track.number}: source grew while reading")
        require(file_signature(track.path) == track.signature,
                f"Track {track.number}: source changed while reading")
        destination.flush()
        os.fsync(destination.fileno())
    return {
        "number": track.number, "start_lba": track.start_lba,
        "end_lba_exclusive": track.start_lba + track.sectors,
        "control": track.control, "sectors": track.sectors,
        "operation": "extract-mode1-payload" if convert else "copy",
        "raw_mode1_headers_checked": convert,
        "source": {"file": track.name, "descriptor_file": track.descriptor_name,
                   "sector_bytes": track.sector_bytes,
                   "bytes": consumed, "crc32": f"{source_crc:08x}", "sha256": source_sha.hexdigest()},
        "output": {"file": output.name, "sector_bytes": DATA_BYTES if track.control == 4 else RAW_BYTES,
                   "bytes": written, "crc32": f"{output_crc:08x}", "sha256": output_sha.hexdigest()},
    }


def publish_directory(source, destination):
    """Atomically publish without replacing even a newly-created empty folder."""
    if os.name == "nt":
        os.rename(source, destination)  # Windows rename refuses any existing target.
        return
    libc = ctypes.CDLL(None, use_errno=True)
    if sys.platform.startswith("linux") and hasattr(libc, "renameat2"):
        rename = libc.renameat2
        rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        rename.restype = ctypes.c_int
        result = rename(-100, os.fsencode(source), -100, os.fsencode(destination), 1)  # RENAME_NOREPLACE
    elif sys.platform == "darwin" and hasattr(libc, "renamex_np"):
        rename = libc.renamex_np
        rename.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        rename.restype = ctypes.c_int
        result = rename(os.fsencode(source), os.fsencode(destination), 4)  # RENAME_EXCL
    else:
        raise OSError(errno.ENOTSUP, "Platform cannot guarantee atomic publication without overwriting")
    if result:
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), str(destination))


def optimize(gdi, output_directory, progress=None):
    """Return the report only after the complete new directory is published."""
    gdi = Path(gdi).expanduser().absolute()
    require(not gdi.is_symlink(), "GDI must not be a symbolic link")
    gdi = gdi.resolve()
    output = Path(output_directory).expanduser().absolute()
    require(not os.path.lexists(output), f"Output already exists: {output}")
    require(output.parent.is_dir(), f"Output parent directory does not exist: {output.parent}")
    output = output.parent.resolve() / output.name
    require(output != gdi.parent and gdi.parent not in output.parents,
            "Output must be outside the original dump directory")
    descriptor, signature, tracks = load_gdi(gdi)
    require(safe_name(gdi.name) and gdi.suffix.lower() == ".gdi", "Input must have a safe .gdi filename")
    records, rows = [], [str(len(tracks))]
    stage = Path(tempfile.mkdtemp(prefix=f".{output.name}.staging-", dir=output.parent))
    try:
        for track in tracks:
            name = f"track{track.number:02d}.{'iso' if track.control == 4 else 'raw'}"
            if progress:
                progress(f"Track {track.number}/{len(tracks)}: {track.name} -> {name}")
            record = stream_track(track, stage / name)
            records.append(record)
            rows.append(f'{track.number} {track.start_lba} {track.control} '
                        f'{record["output"]["sector_bytes"]} "{name}" 0')
        require(file_signature(gdi) == signature and gdi.read_bytes() == descriptor,
                "GDI changed during conversion")
        for track in tracks:
            require(file_signature(track.path) == track.signature,
                    f"Track {track.number}: source changed during conversion")
        new_descriptor = ("\n".join(rows) + "\n").encode("ascii")
        source_data = sum(t["source"]["bytes"] for t in records if t["control"] == 4)
        output_data = sum(t["output"]["bytes"] for t in records if t["control"] == 4)
        report = {
            "schema": 1, "profile": PROFILE,
            "tool": "tools/gdi_optimize.py", "complete": True,
            "verification": {
                "raw_mode1_checks": ["sync", "mode", "sector-address"],
                "edc_ecc_checked": False, "catalogue_match_checked": False,
                "note": "Conversion hashes describe separate source/output byte streams. Preserve the verified raw dump for archival use.",
            },
            "source_gdi": {"path": str(gdi), **digest(descriptor)},
            "output_gdi": {"file": gdi.name, **digest(new_descriptor)},
            "source_data_bytes": source_data, "output_data_bytes": output_data,
            "saved_data_bytes": source_data - output_data,
            "source_track_bytes": sum(t["source"]["bytes"] for t in records),
            "output_track_bytes": sum(t["output"]["bytes"] for t in records),
            "tracks": records,
        }
        # Both metadata files are private in staging; the directory becomes
        # visible at its requested name in one exclusive rename only when done.
        (stage / "conversion.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (stage / gdi.name).write_bytes(new_descriptor)
        publish_directory(stage, output)
        return report
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def no_symlink_directories(path):
    # Check from the filesystem root down, before any child is inspected.
    for directory in reversed((path, *path.parents)):
        require(not directory.is_symlink(), f"Symbolic-link directory is not allowed: {directory}")


def converted_directory(directory):
    """A completed report identifies our outputs without relying on suffixes."""
    marker = directory / "conversion.json"
    try:
        info = marker.lstat()
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= 1024 * 1024:
            return False
        report = json.loads(marker.read_text(encoding="utf-8"))
        if not isinstance(report, dict):
            return False
        output = report.get("output_gdi", {})
        name = output.get("file") if isinstance(output, dict) else None
        if not (report.get("schema") == 1 and report.get("profile") == PROFILE
                and report.get("complete") is True and isinstance(name, str)
                and safe_name(name) and Path(name).suffix.lower() == ".gdi"):
            return False
        descriptor = directory / name
        signature = file_signature(descriptor)
        if not 0 < signature[2] <= GDI_LIMIT or digest(descriptor.read_bytes()) != {
                key: output.get(key) for key in ("bytes", "crc32", "sha256")}:
            return False
        tracks = report.get("tracks")
        if not isinstance(tracks, list) or not 1 <= len(tracks) <= TRACK_LIMIT:
            return False
        for number, track in enumerate(tracks, 1):
            if not isinstance(track, dict) or track.get("number") != number:
                return False
            record = track.get("output")
            if not isinstance(record, dict) or track.get("control") not in (0, 4):
                return False
            size = DATA_BYTES if track["control"] == 4 else RAW_BYTES
            expected_name = f"track{number:02d}.{'iso' if track['control'] == 4 else 'raw'}"
            sectors = track.get("sectors")
            if (not isinstance(sectors, int) or sectors <= 0 or record.get("file") != expected_name
                    or record.get("sector_bytes") != size or record.get("bytes") != sectors * size
                    or file_signature(directory / expected_name)[2] != record["bytes"]
                    or not re.fullmatch(r"[0-9a-f]{64}", str(record.get("sha256")))
                    or not re.fullmatch(r"[0-9a-f]{8}", str(record.get("crc32")))):
                return False
        return True
    except (OSError, UnicodeError, ValueError):
        return False


def batch_snapshot(root):
    """Discover before writing, so new outputs cannot become new inputs."""
    candidates, notices, pending = [], [], [root]
    while pending:
        directory = pending.pop()
        try:
            no_symlink_directories(directory)
            if converted_directory(directory):
                notices.append(("skipped", directory,
                                "recognized conversion output; left unchanged (not reverified)"))
                continue
            if os.path.lexists(directory / "conversion.json"):
                notices.append(("skipped", directory,
                                "invalid/unrecognized conversion.json; left unchanged, not verified"))
                continue
            with os.scandir(directory) as scan:
                entries = sorted(scan, key=lambda entry: (entry.name.casefold(), entry.name))
            descriptors, children, other_images = [], [], []
            for entry in entries:
                path = Path(entry.path)
                if entry.is_symlink():
                    notices.append(("skipped", path, "symbolic link"))
                    continue
                if entry.is_dir(follow_symlinks=False):
                    if entry.name.startswith("."):
                        notices.append(("skipped", path, "hidden directory"))
                    else:
                        children.append(path)
                elif not entry.name.startswith(".") and entry.name.lower().endswith(".gdi"):
                    descriptors.append(path)
                elif not entry.name.startswith(".") and path.suffix.lower() in OTHER_IMAGE_SUFFIXES:
                    other_images.append(entry.name)
            pending.extend(reversed(children))
            if len(descriptors) > 1:
                notices.append(("failed", directory,
                                "multiple GDI descriptors; choose one with single-file mode"))
            elif descriptors:
                candidates.append((descriptors[0], directory.with_name(directory.name + "-2048")))
            elif other_images:
                names = ", ".join(other_images[:3])
                if len(other_images) > 3:
                    names += f", and {len(other_images) - 3} more"
                notices.append(("skipped", directory,
                                f"no GDI descriptor; batch conversion accepts GDI only (found {names})"))
        except (ValueError, OSError) as error:
            notices.append(("failed", directory, str(error)))
    # A case-insensitive card must not receive two indistinguishable outputs.
    destinations = {}
    for gdi, output in candidates:
        destinations.setdefault(str(output).casefold(), []).append(gdi)
    unique = []
    for gdi, output in candidates:
        if len(destinations[str(output).casefold()]) > 1:
            notices.append(("failed", gdi, f"case-insensitive output-name collision: {output}"))
        else:
            unique.append((gdi, output))
    return unique, notices


def existing_copy_for(gdi, output):
    if not converted_directory(output):
        return False
    report = json.loads((output / "conversion.json").read_text(encoding="utf-8"))
    source = report.get("source_gdi")
    if not isinstance(source, dict) or source.get("path") != str(gdi.resolve()):
        return False
    if not 0 < file_signature(gdi)[2] <= GDI_LIMIT:
        return False
    return digest(gdi.read_bytes()) == {key: source.get(key) for key in ("bytes", "crc32", "sha256")}


def optimize_batch(root, progress=None):
    """Convert independent games; completed copies survive failures or Ctrl+C."""
    root = Path(root).expanduser().absolute()
    no_symlink_directories(root)
    require(root.is_dir(), f"Batch root is not a directory: {root}")
    root = root.resolve()
    result = {"schema": 1, "tool": "tools/gdi_optimize.py", "root": str(root),
              "converted": 0, "skipped": 0, "failed": 0, "cancelled": False,
              "saved_data_bytes": 0, "entries": []}
    candidates, current = [], None

    def notice(status, path, message, output=None):
        result[status] += 1
        entry = {"status": status, "path": str(path), "message": message}
        if output is not None:
            entry["output"] = str(output)
        result["entries"].append(entry)
        if progress:
            progress(f"{status.upper()}: {path}: {message}")

    try:
        candidates, notices = batch_snapshot(root)
        for status, path, message in notices:
            notice(status, path, message)
        for position, (gdi, output) in enumerate(candidates):
            current = position
            try:
                no_symlink_directories(gdi.parent)
                no_symlink_directories(output.parent)
                # Also reject a differently-cased existing name before copying.
                with os.scandir(output.parent) as scan:
                    occupied = [entry for entry in scan if entry.name.casefold() == output.name.casefold()]
                if occupied:
                    if (len(occupied) == 1 and occupied[0].name == output.name
                            and occupied[0].is_dir(follow_symlinks=False)
                            and existing_copy_for(gdi, output)):
                        notice("skipped", gdi, f"existing copy {output} left unchanged (not reverified)", output)
                        continue
                    raise ValueError(f"Output already exists: {output}")
                emit = (lambda message: progress(f"{gdi.parent}: {message}")) if progress else None
                report = optimize(gdi, output, progress=emit)
                result["saved_data_bytes"] += report["saved_data_bytes"]
                notice("converted", gdi, f"created {output}; originals unchanged", output)
            except (ValueError, OSError) as error:
                notice("failed", gdi, str(error), output)
            current = None
    except KeyboardInterrupt:
        result["cancelled"] = True
        if current is None:
            result["entries"].append({"status": "cancelled", "path": str(root),
                                      "message": "Batch discovery/reporting was interrupted"})
            for gdi, output in candidates:
                result["entries"].append({"status": "not-started", "path": str(gdi),
                                          "output": str(output),
                                          "message": "Not attempted after cancellation"})
        else:
            for position in range(current, len(candidates)):
                gdi, output = candidates[position]
                # A progress callback may interrupt after a job was already
                # published/reported; never label a completed copy cancelled.
                if any(entry["path"] == str(gdi) and entry["status"] in ("converted", "skipped", "failed")
                       for entry in result["entries"]):
                    continue
                result["entries"].append({
                    "status": "cancelled" if position == current else "not-started",
                    "path": str(gdi), "output": str(output),
                    "message": "Current conversion interrupted; staging cleaned" if position == current
                               else "Not attempted after cancellation"})
    return result


def batch_report_path(path, root):
    """Reports live outside the original collection and never replace files."""
    path = Path(path).expanduser().absolute()
    no_symlink_directories(path.parent)
    require(path.parent.is_dir(), f"Report parent directory does not exist: {path.parent}")
    path = path.parent.resolve() / path.name
    root = Path(root).expanduser().resolve()
    require(path != root and root not in path.parents,
            "Batch report must be outside the original collection directory")
    require(not os.path.lexists(path), f"Report already exists: {path}")
    return path


def write_batch_report(result, path):
    """Publish a complete JSON report atomically, without overwriting a target."""
    path = batch_report_path(path, result["root"])
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.staging-", dir=path.parent)
    stage = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(result, output, indent=2, ensure_ascii=False)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        publish_directory(stage, path)
    finally:
        if stage.exists():
            stage.unlink()
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--batch", type=Path, metavar="ROOT",
                        help="Recursively convert one-GDI folders into sibling <folder>-2048 copies")
    parser.add_argument("--report", type=Path, metavar="PATH",
                        help="Write per-game batch results to a new JSON file outside ROOT")
    parser.add_argument("gdi", nargs="?", type=Path, help="Original .gdi descriptor")
    parser.add_argument("output", nargs="?", type=Path,
                        help="New folder outside the original dump folder; must not exist")
    args = parser.parse_args()
    if args.batch is not None:
        if args.gdi is not None or args.output is not None:
            parser.error("--batch ROOT cannot be combined with single-file arguments")
        try:
            report_path = batch_report_path(args.report, args.batch) if args.report else None
            result = optimize_batch(args.batch, progress=print)
        except (ValueError, OSError) as error:
            print(f"Batch failed: {error}", file=sys.stderr)
            return 1
        except KeyboardInterrupt:
            print("Batch cancelled before conversion; originals unchanged.", file=sys.stderr)
            return 130
        print(f'Batch summary: {result["converted"]} converted, {result["skipped"]} skipped, '
              f'{result["failed"]} failed; {result["saved_data_bytes"]:,} data bytes saved.')
        failures = [entry for entry in result["entries"] if entry["status"] == "failed"]
        if failures:
            print("Failed games/folders (originals unchanged):", file=sys.stderr)
            for entry in failures:
                print(f'  {entry["path"]}: {entry["message"]}', file=sys.stderr)
        if report_path:
            try:
                write_batch_report(result, report_path)
                print(f"Batch report: {report_path}")
            except (ValueError, OSError) as error:
                print(f"Cannot write batch report: {error}; completed copies kept.", file=sys.stderr)
                return 130 if result["cancelled"] else 1
            except KeyboardInterrupt:
                print("Batch report cancelled; completed copies kept, report staging cleaned.", file=sys.stderr)
                return 130
        if result["cancelled"]:
            print("Batch cancelled; completed copies kept, current staging cleaned.", file=sys.stderr)
            return 130
        if not any(result[key] for key in ("converted", "skipped", "failed")):
            print("No GDI descriptors found.")
        return 1 if result["failed"] else 0
    if args.report is not None:
        parser.error("--report PATH requires --batch ROOT")
    if args.gdi is None or args.output is None:
        parser.error("provide ORIGINAL.gdi OUTPUT_FOLDER, or --batch ROOT")
    try:
        report = optimize(args.gdi, args.output, progress=print)
    except (ValueError, OSError) as error:
        print(f"Conversion failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Conversion cancelled; no completed output folder published.", file=sys.stderr)
        return 130
    saved = report["saved_data_bytes"]
    print(f"Created {args.output}: {saved:,} data bytes saved; originals unchanged.")
    print("Source/output CRC32 and SHA-256 recorded in conversion.json; no catalogue verification performed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
