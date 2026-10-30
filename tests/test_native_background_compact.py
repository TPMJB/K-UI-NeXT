#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run compact Background ownership, real worker On/Off and bounded-list tests."""
from pathlib import Path
import importlib.util
import os
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("toggle_fixture", ROOT / "tests/test_vmu_native_toggle.py")
TOGGLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOGGLE)
DEFINES = (*TOGGLE.DEFINES, "KUI_NATIVE_BACKGROUND_COMPACT", "KUI_NATIVE_READAHEAD",
           "KUI_NATIVE_IRQ_REARM", "KUI_NATIVE_COOP_PUMP", "KUI_NATIVE_REQUEST_TIMING",
           "KUI_NATIVE_READER_DIAGNOSTIC", "KUI_VMU_NATIVE_BACKGROUND")
CC = shlex.split(os.environ.get("HOST_CC", "cc"))
FLAGS = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
         "-ffunction-sections", "-fdata-sections", "-fno-pie", "-no-pie",
         "-fsanitize=undefined", "-fno-sanitize-recover=all", "-Iinclude", "-Isrc/loader", "-Itests",
         *("-D" + name + "=1" for name in DEFINES)]

LAYOUT = r'''
#include <assert.h>
#include "kui/vmu_native_background.h"
#include "kui/vmu_native_passive.h"
#include "kui/vmu_readonly_overlay.h"
#include "kui/vmu_maple_observe_boot.h"
_Static_assert(KUI_VMU_NATIVE_PASSIVE_SPAN==0x12000u,"72 KiB worker");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES==0x12000u,"72 KiB worker");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET==0x12400u,"Region after full map");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==0x47000u,"284 KiB total");
_Static_assert(KUI_NATIVE_DUAL_LEASE_BYTES==0x47000u,"Both low engines share the lease");
_Static_assert(KUI_NATIVE_RA_AUX_OFFSET==0x13000u,"Compact auxiliary");
_Static_assert(KUI_NATIVE_RA_STACK_OFFSET==0x16000u,"Auxiliary stack begins");
_Static_assert(KUI_NATIVE_RA_STACK_END==0x17000u,"Full 4 KiB auxiliary stack");
_Static_assert(KUI_NATIVE_RA_DATA_OFFSET==0x17000u,"Cache follows private stack");
_Static_assert(KUI_NATIVE_RA_DATA_BYTES==0x30000u && KUI_NATIVE_RA_BLOCKS==384u,"Full 192 KiB cache");
_Static_assert(KUI_VMU_NATIVE_LITE_CHAIN_BYTES==0x1000u,"Bounded chain copies");
_Static_assert(KUI_VMU_READONLY_OVERLAY_STAGED_FRAMES==1u,"One staged A2 reply");
'''

WORKER_EXTRA = r'''
_Static_assert(WORKER_SPAN==0x12000u && TX_WORDS*4u==0x1000u,"Compact worker copies");
_Static_assert(sizeof(issued_chain)==0x1000u,"Bounded issued chain");
_Static_assert(KUI_VMU_READONLY_OVERLAY_STAGED_FRAMES==1u,"Single virtual reply");

static void compact_save_and_reopen(void) {
    uint8_t data[512];for(unsigned i=0u;i<sizeof(data);++i)data[i]=(uint8_t)(i^0x95u);
    split_setup();reader_bind();
    CHECK(config.lease.lease_bytes==0x47000u && WORKER_SPAN==0x12000u);
    CHECK(reader_readahead_exports(&observed_region));
    stage_block(42u,data);transfer_batch(13u,42u,4u,NULL);read_compare(42u,data);
    CHECK(durable_card.commits==1u && durable_card.reads==1u && release_saw_unpublished);
    protect_media();model_reopen();
    struct kui_vmu_backing backing={0};struct kui_vmu_store reopened={0};
    CHECK(!kui_vmu_backing_init(&backing,&model_bus,&backing_config));
    struct kui_vmu_store_io io=kui_vmu_backing_io(&backing);
    CHECK(!kui_vmu_store_open(&reopened,&io,&store_config));
    CHECK(!kui_vmu_store_checkpoint(&reopened));
    CHECK(!memcmp(sd.media[image_lba(42u)],data,sizeof(data)));protect_media();
}

/* A legal SDK-bank list can exceed our bounded private copy. Both VMU modes
 * must stop before editing the list, publishing RX, calling storage or TICK. */
static void compact_long_lists_stop(void) {
    static const unsigned units[4]={6u,12u,18u,1u};
    for(unsigned off=0u;off<2u;++off) {
        if(off) off_setup();else {split_setup();reader_bind();}
        stage_batch(0u,0u);
        uint32_t source=physical(input.tx_base)+packet_bank*0x3000u,at=source+16u;
        for(unsigned i=0u;i<4u;++i) {
            unsigned unit=units[i],port=unit/6u,sub=unit%6u;
            unsigned address=(port<<6)|(sub?1u<<(sub-1u):32u);
            put(at,4u,255u|(port<<16)|(i==3u?0x80000000u:0u));put(at+4u,4u,rx_at(unit));
            put(at+8u,4u,(255u<<24)|((port<<6)<<16)|(address<<8)|1u);
            memset(memory+at+12u-0x0c000000u,0x3c,255u*4u);
            memset(rx_bytes(unit),0xa5,1024u);at+=12u+255u*4u;
        }
        CHECK(at-source>sizeof(issued_chain) && at-source<=KUI_VMU_NATIVE_TX_BANK_BYTES);
        static const unsigned slots[7]={0u,2u,6u,12u,18u,1u,7u};
        static uint8_t tx[0x3000],rx[7][1024];
        memcpy(tx,memory+source-0x0c000000u,sizeof(tx));
        for(unsigned i=0u;i<7u;++i)memcpy(rx[i],rx_bytes(slots[i]),1024u);
        unsigned ticks=tick_visits;
        reject_submit();
        CHECK(terminal_error==6u && !overlay.pending && !durable_card.commits);
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes && tick_visits==ticks);
        CHECK(value_at(0xa05f6c04u)==source && !memcmp(tx,memory+source-0x0c000000u,sizeof(tx)));
        for(unsigned i=0u;i<7u;++i)CHECK(!memcmp(rx[i],rx_bytes(slots[i]),1024u));
    }
}

int main(void) {
    CHECK(!compact_toggle_suite());
    compact_save_and_reopen();compact_long_lists_stop();
    printf("Compact Background worker: %u checks; 72 KiB worker in retained 284 KiB lease, "
           "On save/reopen, Off physical pass-through with reader TICK, overlong lists stop in both modes\n",checks);
    return 0;
}
'''


def compile_and_run(directory, name, source, sources=(), extra=()):
    path, binary = directory / (name + ".c"), directory / name
    path.write_text(source)
    subprocess.run([*CC, *FLAGS, *extra, str(path), *sources, "-Wl,--gc-sections", "-o", str(binary)],
                   cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)


class CompactBackground(unittest.TestCase):
    def test_exact_combined_layout_and_actual_low_guards(self):
        source = (ROOT / "src/loader/vmu_native_background_resident.c").read_text()
        match = re.search(r"/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/",
                          source, re.S)
        self.assertIsNotNone(match)
        main = r'''
int main(void) {
    uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    struct kui_vmu_observe_boot_control c={0};uint32_t begin,end;
    c.status=KUI_VMU_OBS_BOOT_INSTALLED;c.lease=0x0c600020u;
    c.worker_end=(c.lease|0xa0000000u)+0x12000u;
    c.terminal=(c.lease|0x80000000u)+80u;c.sd_transaction=c.terminal+2u;
    assert(native_resident_lease(&c,&begin,&end));
    assert(begin==c.lease-32u && end==c.lease+0x47000u);
    assert(native_resident_terminal(&c) && native_resident_transaction(&c,c.sd_transaction));
    for(unsigned a=0u;a<3u;++a) {
        for(uint32_t offset=0u;offset<0x47000u;offset+=32u)
            assert(!native_resident_destination(&c,(c.lease+offset)|aliases[a],32u));
        assert(!native_resident_destination(&c,begin|aliases[a],32u));
        assert(!native_resident_destination(&c,(end-1u)|aliases[a],2u));
        assert(native_resident_destination(&c,end|aliases[a],512u));
        assert(native_resident_destination(&c,(begin-32u)|aliases[a],32u));
        assert(kui_vmu_native_background_map_address(c.lease|aliases[a],(c.lease|0x80000000u)+0x12000u));
        assert(kui_vmu_native_background_region_address(c.lease|aliases[a],(c.lease|0x80000000u)+0x12400u));
    }
    c.worker_end+=32u;assert(!native_resident_destination(&c,end,32u));c.worker_end-=32u;
    c.lease=0u;assert(!native_resident_terminal(&c) && !native_resident_destination(&c,end,32u));
    c.lease=0x0d000000u-0x47000u;assert(native_resident_lease(&c,&begin,&end) && end==0x0d000000u);
    c.lease+=32u;assert(!native_resident_lease(&c,&begin,&end));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="compact-background-layout-") as tmp:
            compile_and_run(Path(tmp), "layout", LAYOUT + match.group(1) + main)

    def test_actual_general_worker_on_off_and_no_truncation(self):
        fixture = (ROOT / "tests/test_vmu_native_toggle.c").read_text()
        # Adapt only the old fixture's expected geometry, never production
        # code or On/Off behavior; retain its legacy capability tests too.
        old = "config.lease.lease_bytes==UINT32_C(0x51000)"
        self.assertEqual(fixture.count(old), 1)
        fixture = fixture.replace(old, "config.lease.lease_bytes==KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES")
        self.assertEqual(fixture.count("int main(void)"), 1)
        fixture = fixture.replace("int main(void)", "int compact_toggle_suite(void)")
        with tempfile.TemporaryDirectory(prefix="compact-background-worker-") as tmp:
            compile_and_run(Path(tmp), "worker", fixture + WORKER_EXTRA, TOGGLE.SOURCES,
                            ("-mcmodel=large", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
                             "-Wl,--section-start=.isolation_tick=0x8c714000"))

    def test_compact_single_reply_staging(self):
        with tempfile.TemporaryDirectory(prefix="compact-background-overlay-") as tmp:
            compile_and_run(Path(tmp), "overlay", '#include "test_vmu_lite_overlay.c"\n',
                            ("src/core/vmu_readonly.c", "src/core/vmu_emulation.c", "src/core/vmu_image.c",
                             "src/loader/vmu_readonly_overlay.c"))


if __name__ == "__main__":
    unittest.main()
