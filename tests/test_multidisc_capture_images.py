#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual IP ordinals through capture, checkpoint resume and FAT32/exFAT."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from test_named_capture_images import digest, export, field, job_path, named_manifest, run


def format_image(image, kind):
    formatter=os.environ.get("KUI_TEST_FS_FORMATTER")
    if formatter:
        subprocess.run([formatter,kind,str(image)],check=True)
    else:
        with image.open("wb") as stream:
            stream.truncate(96*1024*1024)
        command=["mkfs.fat","-F","32",str(image)] if kind=="fat32" else ["mkfs.exfat",str(image)]
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL)


def main():
    with tempfile.TemporaryDirectory(prefix="kui-multidisc-capture-") as temporary:
        base=Path(temporary)
        for kind in ("fat32","exfat"):
            clean=base/f"{kind}-blank.img"
            format_image(clean,kind)
            run(clean,"seed")
            image=base/f"{kind}-discs.img";shutil.copyfile(clean,image)
            identities=set()
            # Rip out of order: only the physical IP ordinal can name a disc.
            for disc in (3,1,4,2):
                output=run(image,"new",title="Shenmue",disc_info=f"{disc}/4")
                assert job_path(output)==f"0:/Games/Shenmue (Disc {disc})"
                assert field(output,"DISC_TITLE")=="Shenmue" and field(output,"GDI_NAME")=="Shenmue.gdi"
                assert f"Disc number: {disc} of 4" in output
            folders=export(image,base/f"{kind}-all")
            for disc in (1,2,3,4):
                manifest=named_manifest(folders/f"Shenmue (Disc {disc})","Shenmue.gdi")
                assert manifest["title"]=="Shenmue" and manifest["disc_number"]==disc and manifest["disc_total"]==4
                identities.add(manifest["identity"])
            assert len(identities)==4
            stopped=run(image,"new","stop-middle",title="Shenmue",disc_info="2/4",expected=3)
            assert job_path(stopped)=="0:/Games/Shenmue (Disc 2) (2)"
            resumed=run(image,"resume",title="Shenmue",disc_info="2/4")
            assert job_path(resumed)=="0:/Games/Shenmue (Disc 2) (2)"
            before=digest(image)
            verified=run(image,"verify",title="Shenmue",disc_info="2/4")
            assert "WRITES 0" in verified and digest(image)==before
            run(image,"resume",title="Shenmue",disc_info="3/4",selected="/Games/Shenmue (Disc 2) (2)",expected=1)
            assert digest(image)==before

            # Existing title/collision folders and old manifests keep their
            # exact descriptor, raw bytes, and checkpoint/content identity.
            for disc,folder in ((2,"Shenmue"),(3,"Shenmue (2)")):
                old=base/f"{kind}-old-{disc}.img";shutil.copyfile(clean,old)
                output=run(old,"new",title="Shenmue",disc_info=f"{disc}/4",folder_label=folder)
                assert job_path(output)==f"0:/Games/{folder}"
                run(old,"mutate","legacy-disc-fields",job=f"0:/Games/{folder}")
                before=digest(old)
                output=run(old,"resume",title="Shenmue",disc_info=f"{disc}/4")
                assert job_path(output)==f"0:/Games/{folder}" and digest(old)==before
                output=run(old,"verify",title="Shenmue",disc_info=f"{disc}/4",selected=f"/Games/{folder}")
                assert "WRITES 0" in output and digest(old)==before
                saved=export(old,base/f"{kind}-old-{disc}")/folder
                manifest=named_manifest(saved,"Shenmue.gdi")
                assert "disc_number" not in manifest and "disc_total" not in manifest

            partial=base/f"{kind}-old-partial.img";shutil.copyfile(clean,partial)
            run(partial,"new","stop-middle",title="Shenmue",disc_info="4/4",folder_label="Shenmue",expected=3)
            output=run(partial,"resume",title="Shenmue",disc_info="4/4")
            assert job_path(output)=="0:/Games/Shenmue"
            manifest=named_manifest(export(partial,base/f"{kind}-old-partial")/"Shenmue","Shenmue.gdi")
            assert manifest["disc_number"]==4 and manifest["disc_total"]==4

            for index,setting in enumerate((None,"0/4","5/4","2x4","1/1")):
                unknown=base/f"{kind}-unknown-{index}.img";shutil.copyfile(clean,unknown)
                output=run(unknown,"new",title="Shenmue",disc_info=setting)
                assert job_path(output)=="0:/Games/Shenmue"
            long=base/f"{kind}-long.img";shutil.copyfile(clean,long)
            output=run(long,"new",title="S"*128,disc_info="4/4")
            leaf=job_path(output).rsplit("/",1)[1]
            assert len(leaf)<=95 and leaf.endswith(" (Disc 4)")
            print(f"PASS {kind}: four out-of-order IP ordinals, copy collisions, exact/legacy resume and read-only verification, malformed fields and bounded names",flush=True)


if __name__=="__main__":
    main()
