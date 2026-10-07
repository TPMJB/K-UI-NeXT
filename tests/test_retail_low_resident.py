#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check exact native layouts and production mapping/firmware preflight.

The fixed firmware vector page conflicts with ASan shadow RAM; the extracted
production C helpers run under UBSan. No owner game bytes are used.
"""
import contextlib
import io
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import retail_package as placement
import check_retail_loader_layout as checker
import report_retail_sizes as sizes
import test_retail_package as fixtures


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    at = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[at] == "{") - (source[at] == "}")
        at += 1
    return source[start:at]


HARNESS = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/mman.h>
#include "kui/retail_gd.h"
#include "kui/retail_loader_layout.h"
/* RUNTIME_DECLARATIONS */

static unsigned checks, purges, copies, stops, markers;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"low resident assertion at line %u: %s\n",__LINE__,#x); abort(); \
} } while(0)
static void purge(uint32_t address,uint32_t bytes) {
    CHECK(address>=KUI_RETAIL_IP_ADDRESS && bytes && bytes<=KUI_RETAIL_RAM_END-address);
    ++purges;
}
/* MAP_GUEST */
/* GD_GUEST */
/* FRONTEND_LE32 */
/* FRONTEND_LAYOUT */
/* FIRMWARE_CODE */

static jmp_buf stopped_jump;
static void stopped(const char *reason,uint32_t detail) {
    (void)reason;(void)detail;++stops;longjmp(stopped_jump,1);
}
static void retail_display_hex(const char *label,uint32_t value) {(void)label;(void)value;}
static void retail_display_line(const char *label) {
    CHECK(!strcmp(label,"NATIVE LOW RESIDENT"));++markers;
}
/* FIRMWARE_PREFLIGHT */

static unsigned char source_blob[16]={1,2,3,4};
static size_t resident_bytes=sizeof(source_blob);
static uint32_t resident_limit=KUI_RETAIL_STANDARD_LIMIT;
static const uint8_t *resident_blob=source_blob;
static int manifest,card,display;
typedef int (*kui_retail_resident_entry)(const int *,const int *,uint32_t,const int *);
static void *copy_resident(void *target,const void *source,size_t bytes) {
    CHECK(target==(void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS);
    CHECK(bytes==sizeof(source_blob));++copies;
    return memcpy(target,source,bytes);
}
static void kui_retail_stage_sync(void) {longjmp(stopped_jump,2);}
#define memcpy copy_resident
/* INSTALL_RESIDENT */
#undef memcpy

static void put(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));
}
static void header(uint8_t *data,int ce,int low) {
    memset(data,0,0x2010u);
    uint8_t *h=data+0x100u;
    const uint32_t stage=ce?0x8ce10000u:0x8ce00000u;
    const uint32_t words[14]={1,64,0x1000,4096,stage,16,stage,0x8c010000,
        0xc00000,0x8cff0000,0x2000,low?0x8c004000u:0x8c008300u,
        low?0x8c007800u:0x8c00bb00u,0};
    memcpy(h,ce?"KUIRCE01":"KUIRBT01",8);
    for(unsigned i=0;i<14u;i++)put(h+8u+4u*i,words[i]);
}
static void frontend(void) {
    uint8_t data[0x2010];
    struct kui_runtime_image image={.data=data,.info={.payload_bytes=sizeof(data),.memory_bytes=sizeof(data)}};
    for(int ce=0;ce<2;ce++)for(int low=0;low<2;low++) {
        header(data,ce,low);CHECK(layout(&image,ce)==!(ce&&low));
        if(ce&&low)continue;
        for(unsigned at=0x100u;at<0x140u;at++) {
            data[at]^=1u;CHECK(!layout(&image,ce));data[at]^=1u;
        }
        const uint32_t pairs[][2]={{0x8c004000,0x8c00bb00},{0x8c008300,0x8c007800},
            {0x8c004000,0x8c007ba0},{0x8c004004,0x8c007800},{0x8c004000,0x8c007804}};
        for(unsigned i=0;i<sizeof(pairs)/sizeof(pairs[0]);i++) {
            header(data,ce,low);put(data+0x134,pairs[i][0]);put(data+0x138,pairs[i][1]);
            CHECK(!layout(&image,ce));
        }
        header(data,ce,low);data[0x1000]=1;CHECK(!layout(&image,ce));
    }
}
static void mapped(struct kui_retail_gd *gd,uint32_t address,uint32_t bytes,
                   uint32_t align,int valid) {
    unsigned before=purges;
    uint8_t *result=guest(gd,address,bytes,align,KUI_RETAIL_MAP_VALIDATE);
    CHECK((result!=NULL)==valid);CHECK(purges==before);
    if(valid)CHECK((uintptr_t)result==((address&0x1fffffffu)|0xa0000000u));
}
static void mapping(void) {
    struct kui_retail_gd gd={.guest_begin=0x8c008000u,.guest_end=0x8d000000u,
        .ops={.map=map_guest}};
    CHECK(KUI_RETAIL_IP_ADDRESS==0x8c008000u);
    const uint32_t aliases[]={0x0c000000u,0x8c000000u,0xac000000u};
    for(unsigned i=0;i<3u;i++) {
        uint32_t a=aliases[i];
        /* Actual photographed native request parameter pointers. */
        mapped(&gd,a+0xb9f4u,16,4,KUI_RETAIL_LOW_RESIDENT);
        mapped(&gd,a+0xb33cu,16,4,KUI_RETAIL_LOW_RESIDENT);
        mapped(&gd,a+0x7ffcu,4,4,0);
        mapped(&gd,a+0x8000u,4,4,1);
        mapped(&gd,a+0x82fcu,4,4,1);
        mapped(&gd,a+0x82fcu,8,4,KUI_RETAIL_LOW_RESIDENT);
        mapped(&gd,a+0xc000u,4,4,1);
        mapped(&gd,a+0xfffffcu,4,4,1);
        mapped(&gd,a+0xfffffcu,8,4,0);
        mapped(&gd,a+0x10000u,0,4,0);
        mapped(&gd,a+0x10001u,4,4,0);
        mapped(&gd,a+0x10000u,0xffffffffu,4,0);
        mapped(&gd,a+0x3ffeu,4,2,0);
        mapped(&gd,a+0x4000u,4,4,0);
        mapped(&gd,a+0x7d00u,4,4,0);
    }
    for(unsigned area=0;area<256u;area++) {
        uint32_t address=(area<<24)|0x10000u;
        mapped(&gd,address,4,4,area==0x0cu||area==0x8cu||area==0xacu);
    }
    mapped(&gd,0x8d000000u,4,4,0);
    CHECK(map_guest(NULL,0x8c010000u,32,1)==(uint8_t *)(uintptr_t)0xac010000u);
    CHECK(purges==1u);
    CHECK(map_guest(NULL,0x8c004000u,32,1)==NULL);CHECK(purges==1u);
}
static int firmware_expected(uint32_t address,int gd) {
    uint32_t area=address&0xff000000u,physical=address&0x1fffffffu;
    if(address&1u)return 0;
    if(area==0x0c000000u||area==0x8c000000u||area==0xac000000u)
        return physical>=0x0c000100u&&physical<0x0c004000u;
    return !gd&&(area==0||area==0x80000000u||area==0xa0000000u)&&
        physical>=2u&&physical<0x200000u;
}
static void firmware_predicate(void) {
    const uint32_t offsets[]={0,1,2,0xfe,0xff,0x100,0x101,0x102,0x3ffe,0x3fff,
        0x4000,0x4001,0x7ffe,0x8000,0x1ffffe,0x1fffff,0x200000,0xfffffe};
    for(unsigned area=0;area<256u;area++)for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)
        for(int gd=0;gd<2;gd++) {
            uint32_t a=(area<<24)|offsets[i];
            CHECK(low_firmware_code(a,gd)==firmware_expected(a,gd));
        }
    /* Independently stated stock RAM and ROM boundary expectations. */
    CHECK(low_firmware_code(0x8c000100u,1));CHECK(low_firmware_code(0xac003ffeu,1));
    CHECK(!low_firmware_code(0x8c004000u,1));CHECK(!low_firmware_code(0x8c003fffu,1));
    CHECK(!low_firmware_code(0x80001000u,1));CHECK(low_firmware_code(0x80001000u,0));
    CHECK(!low_firmware_code(0x80200000u,0));CHECK(!low_firmware_code(0x80000000u,0));
}
#if KUI_RETAIL_LOW_RESIDENT
static const uint32_t vectors[4]={0x8c0000b0u,0x8c0000b4u,0x8c0000b8u,0x8c0000e0u};
static void vector(uint32_t address,uint32_t value) {*(uint32_t *)(uintptr_t)address=value;}
static void defaults(void) {
    vector(0x8c0000bcu,0x8c001000u);
    for(unsigned i=0;i<4u;i++)vector(vectors[i],0x80001000u+i*2u);
    copies=stops=markers=0;
    memset((void *)(uintptr_t)0x8c004000u,0xa5,16);
}
static void attempted(int valid) {
    int result=setjmp(stopped_jump);
    if(!result)install_resident();
    CHECK(result==(valid?2:1));CHECK(copies==(unsigned)valid);CHECK(stops==(unsigned)!valid);
    CHECK(markers==(unsigned)valid);
    const uint8_t *actual=(const uint8_t *)(uintptr_t)0x8c004000u;
    for(unsigned i=0;i<16u;i++)CHECK(actual[i]==(valid?source_blob[i]:0xa5));
}
static void before_copy(void) {
    void *memory=mmap((void *)(uintptr_t)0x8c000000u,0x8000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(memory==(void *)(uintptr_t)0x8c000000u);
    const uint32_t bad[]={0,1,0x8c0000fe,0x8c003fff,0x8c004000,0x8c007800,
        0xac004000,0x0c004000,0x8c008000,0x8e001000,0x80000000,0x80200000,0xa0200000};
    defaults();attempted(1);
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        defaults();vector(0x8c0000bcu,bad[i]);attempted(0);
        for(unsigned j=0;j<4u;j++) {
            defaults();vector(vectors[j],bad[i]);attempted(0);
        }
    }
    const uint32_t good_gd[]={0x0c000100u,0x8c003ffeu,0xac001000u};
    const uint32_t good_service[]={2,0x1ffffe,0x80000002u,0xa01ffffeu,
        0x0c000100u,0x8c003ffeu,0xac001000u};
    for(unsigned i=0;i<sizeof(good_gd)/sizeof(good_gd[0]);i++) {
        defaults();vector(0x8c0000bcu,good_gd[i]);attempted(1);
    }
    for(unsigned i=0;i<sizeof(good_service)/sizeof(good_service[0]);i++)for(unsigned j=0;j<4u;j++) {
        defaults();vector(vectors[j],good_service[i]);attempted(1);
    }
    CHECK(!munmap(memory,0x8000u));
}
#endif
int main(void) {
    frontend();mapping();firmware_predicate();
#if KUI_RETAIL_LOW_RESIDENT
    before_copy();
#endif
    printf("PASS native %s %s: %u assertions; production header, guest mapping and pre-copy firmware checks\n",
        KUI_RETAIL_LOW_RESIDENT?"low":"legacy",
#ifdef KUI_RETAIL_ASYNC
        "background",
#else
        "standard",
#endif
        checks);
    return 0;
}
'''


class LowPlacement(unittest.TestCase):
    def fixture(self, low=False):
        fixture = fixtures.RetailLinkedLayout("test_valid_resident_stage_and_entry")
        fixture.setUp()
        self.addCleanup(fixture.tearDown)
        if low:
            shift = placement.LOW_RESIDENT_ADDRESS - placement.RESIDENT_ADDRESS
            for transport in checker.RESIDENTS:
                name = "resident-" + transport
                fixture.bases[name] += shift
                for symbol in fixture.symbols[name]:
                    fixture.symbols[name][symbol] += shift
            fixture.payload["entry"][0x100:0x140] = \
                placement.relocation_header(len(fixture.payload["stage"]), low=True)
            fixture.write()
        return fixture

    def test_linked_native_layouts_preserve_all_stack_budgets(self):
        old, low = self.fixture(), self.fixture(low=True)
        legacy = checker.check_directory(old.directory)
        relocated = checker.check_directory(low.directory)
        self.assertEqual(legacy["resident_stacks"], relocated["resident_stacks"])
        for transport in checker.RESIDENTS:
            self.assertEqual(relocated["resident-" + transport]["payload_bytes"], 128)
        for transport in checker.RESIDENTS:
            low.bases["resident-" + transport] = placement.RESIDENT_ADDRESS
            low.write()
            with self.subTest(transport=transport), self.assertRaisesRegex(ValueError, "fixed entry"):
                checker.check_directory(low.directory)
            low.bases["resident-" + transport] = placement.LOW_RESIDENT_ADDRESS
        low.write()
        low.payload["entry"][0x100:0x140] = placement.relocation_header(len(low.payload["stage"]))
        low.write()
        with self.assertRaisesRegex(ValueError, "fixed entry"):
            checker.check_directory(low.directory)

    def test_low_header_never_selects_a_ce_layout_or_arbitrary_reservation(self):
        fixture = self.fixture(low=True)
        with self.assertRaisesRegex(ValueError, "relocation header mismatch"):
            checker.check_directory(fixture.directory, ce=True)
        for base, limit in ((0x8c004004, 0x8c007800), (0x8c004000, 0x8c007ba0),
                            (0x8c008300, 0x8c007800), (0x8c004000, 0x8c00bb00)):
            struct.pack_into("<II", fixture.payload["entry"], 0x134, base, limit)
            fixture.write()
            with self.subTest(base=base, limit=limit), self.assertRaisesRegex(ValueError, "header mismatch"):
                checker.check_directory(fixture.directory)

    def test_size_report_uses_selected_linked_base_and_limits(self):
        for low in (False, True):
            fixture = self.fixture(low)
            def nm(args, **unused):
                transport = Path(args[-1]).stem.removeprefix("resident-")
                values = fixture.symbols["resident-" + transport]
                return subprocess.CompletedProcess(args, 0, "\n".join(
                    f"{value:08x} T {name}" for name, value in values.items()), "")
            output = io.StringIO()
            with mock.patch.object(sys, "argv", ["report_retail_sizes.py", str(fixture.directory)]), \
                 mock.patch.object(sizes.subprocess, "run", side_effect=nm), \
                 mock.patch.object(sizes, "symbols", return_value=[]), \
                 contextlib.redirect_stdout(output):
                sizes.main()
            report = json.loads(output.getvalue())
            base = placement.LOW_RESIDENT_ADDRESS if low else placement.RESIDENT_ADDRESS
            for transport in checker.RESIDENTS:
                self.assertEqual(report[transport]["code_data"], 128)
                self.assertEqual(report[transport]["free"],
                    checker.resident_limit(transport, low=low) - base - fixture.memory["resident-" + transport])
                self.assertIsInstance(report[transport]["stack"], int)

    def test_production_c_header_mapping_and_before_copy_checks(self):
        substitutions = {
            "MAP_GUEST": ("src/loader/retail_resident.c", "static uint8_t *map_guest("),
            "GD_GUEST": ("src/core/retail_gd.c", "static uint8_t *guest("),
            "FRONTEND_LE32": ("src/apps/games_retail.c", "static uint32_t le32("),
            "FRONTEND_LAYOUT": ("src/apps/games_retail.c", "static bool layout("),
            "FIRMWARE_CODE": ("src/loader/retail_stage.c", "static int low_firmware_code("),
            "FIRMWARE_PREFLIGHT": ("src/loader/retail_stage.c", "static void low_firmware_preflight("),
            "INSTALL_RESIDENT": ("src/loader/retail_stage.c", "static void install_resident("),
        }
        source = HARNESS
        for name, (path, signature) in substitutions.items():
            source = source.replace("/* " + name + " */", function(path, signature))
        # runtime.h's public interface also includes FatFs via probe.h.
        # This bounds-only fixture uses the actual two production struct
        # declarations, keeping console-only CI independent of FatFs fetches.
        runtime = "\n".join(function("include/kui/runtime.h", signature) + ";"
                            for signature in ("struct kui_runtime_info {",
                                              "struct kui_runtime_image {"))
        source = source.replace("/* RUNTIME_DECLARATIONS */", runtime)
        with tempfile.TemporaryDirectory(prefix="kui-low-resident-") as temp:
            directory = Path(temp)
            generated = directory / "fixture.c"
            generated.write_text(source)
            for low, asynchronous in ((0, False), (1, False), (0, True), (1, True)):
                binary = directory / f"fixture-{low}-{int(asynchronous)}"
                command = shlex.split(os.environ.get("CC", "cc")) + [
                    "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-g",
                    "-I" + str(ROOT / "include"),
                    "-DKUI_RETAIL_LOW_RESIDENT=" + str(low),
                ]
                if asynchronous:
                    command += ["-DKUI_RETAIL_ASYNC=1"]
                subprocess.run(command + [str(generated), "-o", str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
                print(result.stdout.strip(), flush=True)


if __name__ == "__main__":
    unittest.main()
