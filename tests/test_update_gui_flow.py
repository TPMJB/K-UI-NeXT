#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the actual GUI update worker against production FatFs/update services.

Only audio, the exclusive storage owner and progress publishing are stubbed.
No selected SH payload is executed; boot/handoff checks validate its bytes.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import struct
import sys
import tempfile

from test_games_library_images import fatfs_source, private_formatter
from test_update_images import run

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from make_update import make_update
from runtime_package import envelope, verify as verify_runtime
import update_package as update


def gui(build, engine):
    body = bytearray(512)
    body[32:48] = update.CAPABILITY
    body[48:64] = (b"KUIENG" + str(engine).encode() + b"!" +
                   struct.pack("<II", engine, 0xffffffff - engine))
    body[80:100] = struct.pack("<IIIII", 0x5349554b, 0x544f4f42, 1, 3, 0xfffffffc)
    body[128:320] = update.HANDOFF_HEADER + bytes(176)
    return envelope(bytes(body), len(body), build)


def actual_function(source, name):
    marker = source.index(name + "(")
    begin = source.rfind("static ", 0, marker)
    brace = source.index("{", marker)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--real-stage", type=Path, help="Build a current KUP from exact existing staged software and test those bytes")
    parser.add_argument("--runtime", type=Path, help="Existing real current-GUI image; use with --package")
    parser.add_argument("--package", type=Path, help="Existing exact canonical KUP; use with --runtime")
    args = parser.parse_args()
    if bool(args.runtime) != bool(args.package) or args.real_stage and (args.runtime or args.package):
        parser.error("Choose --real-stage, or both --runtime and --package")
    source = fatfs_source()
    with tempfile.TemporaryDirectory(prefix="kui-gui-update-flow-") as temporary:
        base = Path(temporary)
        real = bool(args.real_stage or args.runtime)
        packages = []
        if real:
            runtime = (args.real_stage / "runtime.kui" if args.real_stage else args.runtime).resolve()
            package = base / "canonical.kup" if args.real_stage else args.package.resolve()
            if args.real_stage:
                make_update(args.real_stage, package)
            current = runtime.read_bytes()
            info = verify_runtime(current)
            update.member_info_v2("runtime.kui", current)
            report = update.verify(package)
            update.current_consumer_check(package)
            entry = next(row for row in report["members"] if row["path"] == "runtime.kui")
            assert entry["bytes"] == len(current) and entry["sha256"] == hashlib.sha256(current).hexdigest(), "Root and packaged GUI differ"
            print("REAL INPUT: GUI " + info["build"] + "; KUP " + report["build"] + "; SHA256 " + hashlib.sha256(package.read_bytes()).hexdigest(), flush=True)
        else:
            current = gui("111111111111", update.ENGINE_CURRENT)
            runtime = base / "runtime.kui"
            runtime.write_bytes(current)
            (base / "old.kui").write_bytes(gui("222222222222", 2))
            for name, helper, full in (("full", b"full helper", True),
                                       ("partial-a", b"test A helper", False),
                                       ("partial-b", b"test B helper", False)):
                stage = base / name / "KUI"
                path = stage / "apps/games/flow-helper.bin"
                path.parent.mkdir(parents=True)
                path.write_bytes(helper)
                if full:
                    (stage / "runtime.kui").write_bytes(current)
                package = base / (name + ".kup")
                report = make_update(stage, package, full=full)
                assert report["flags"] & update.REQUIRES_ENGINE3
                packages.append(package)
        build = verify_runtime(current)["build"]
        main_source = (ROOT / "src/dreamcast/main.c").read_text()
        helpers = ("update_package_path", "update_storage_read", "update_storage_same",
                   "update_activation_current", "update_activation_same", "update_view_error",
                   "update_packages_list", "update_space", "update_work")
        (base / "actual_gui_update_helpers.inc").write_text(
            "\n\n".join(actual_function(main_source, name) for name in helpers) + "\n")
        flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
                 "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                 "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                 "-Iinclude", "-I" + str(source), "-I" + str(base), "-Isrc/dreamcast",
                 "-Ifirmware/kui-wifi/components/kwlink/include",
                 "-Ithird_party/miniz", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_STDIO",
                 "-DMINIZ_NO_TIME", "-DMINIZ_NO_ZLIB_APIS", "-DMINIZ_NO_MALLOC",
                 '-DKUI_BUILD_ID="' + build + '"']
        miniz = base / "miniz.o"
        run(*flags, "-w", "-c", "third_party/miniz/miniz.c", "-o", str(miniz))
        binary = base / "gui-update-flow"
        run(*flags, "tests/update_gui_flow.c", "src/core/update.c", "src/core/update_file.c",
            "src/core/update_boot.c", "src/core/update_browser.c", "src/core/update_zip.c",
            "src/core/update_software_file.c", "src/core/update_handoff.c", "src/core/runtime_image.c",
            "src/core/runtime_file.c", "src/core/storage_policy.c", "src/core/hash.c", "src/core/data.c",
            "src/core/diskio.c", "src/core/game_launch_settings.c", "src/core/game_ram_patches.c",
            "src/apps/game_launch_settings_file.c", "src/core/games_selection.c",
            "src/apps/games_selection.c", "src/core/destination.c", str(miniz),
            str(source / "ff.c"), str(source / "ffunicode.c"),
            "-Wl,--wrap=f_open,--wrap=f_stat", "-o", str(binary))
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            image = base / (kind + ".img")
            run(str(formatter), kind, str(image))
            command = ([str(binary), "--real", str(image), str(runtime), str(package)] if real else
                       [str(binary), str(image), str(runtime), str(base / "old.kui"), *(str(path) for path in packages)])
            print(kind + ":\n" + run(*command).strip(), flush=True)
    print("PASS actual GUI update worker and production software/data services on both filesystems; SH payloads validated, not executed", flush=True)


if __name__ == "__main__":
    main()
