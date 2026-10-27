#!/usr/bin/env python3
"""Execute the production Background gateway body with the real async loan.

Only the CPU SR, relocated control pointer and callback admission address are
substituted. The complete acquisition/callback/release/end control flow comes
from the production source, not a second gateway implementation.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import sys
COOPERATIVE='--cooperative' in sys.argv

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/loader/vmu_native_background_resident.c").read_text()
start = source.index('int __attribute__((noipa,section(".vmu_sd_lease"))) kui_vmu_observe_sd_lease(')
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
gateway = source[start:end]
gateway, replacements = re.subn(
    r"const volatile struct kui_vmu_observe_boot_control \*c=.*?;",
    "const volatile struct kui_vmu_observe_boot_control *c=&test_control;",
    gateway, count=1, flags=re.S)
assert replacements == 1
cpu = 'uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");'
assert gateway.count(cpu) == 1
gateway = gateway.replace(cpu, "uint32_t sr=actual_sr;")

loan_fixture = (ROOT / "tests/test_vmu_native_async_loan.c").read_text()
assert loan_fixture.count("int main(void) {") == 1
loan_fixture = loan_fixture.replace("int main(void) {", "int async_loan_fixture_main(void) {")
fixture = r'''
#define kui_retail_storage_acquire fixture_storage_acquire
#define kui_retail_storage_release fixture_storage_release
#define kui_sci_sd_healthy fixture_sci_sd_healthy
''' + loan_fixture + r'''
#undef kui_retail_storage_acquire
#undef kui_retail_storage_release
#undef kui_sci_sd_healthy
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_sd_lease.h"
static struct kui_vmu_observe_boot_control test_control;
static unsigned acquisitions,callbacks;
static int acquire_result,callback_result;
static bool reject_target,callback_not_ready,callback_bus_fault,reenter;
static int transaction(void *,struct kui_loader_sd *);
static bool native_resident_transaction(const volatile struct kui_vmu_observe_boot_control *c,
    uintptr_t target) {
    return c==&test_control && !reject_target && (target==(uintptr_t)transaction
#if KUI_NATIVE_COOP_PUMP
        || !target
#endif
        );
}
enum kui_loader_sd_result kui_retail_storage_acquire(struct kui_retail_storage *owner) {
    CHECK(owner==&R.shared.card && kui_retail_hook_active==1u);
    ++acquisitions;
    return acquire_result?(enum kui_loader_sd_result)acquire_result:kui_sci_sd_acquire();
}
void kui_retail_storage_release(struct kui_retail_storage *owner) {
    CHECK(owner==&R.shared.card);kui_sci_sd_release();
}
bool kui_sci_sd_healthy(void) {return fixture_sci_sd_healthy() && R.shared.port.acquired && !R.shared.port.fault;}
bool kui_sci_sd_faulted(void) {return R.shared.port.fault;}
#define card (R.shared.card)
'''
tail = r'''
#undef card
static uint32_t transaction_ticks(void *ctx) {(void)ctx;return card.clocks*8u;}
static int transaction(void *context,struct kui_loader_sd *loan) {
    CHECK(context==&test_control && loan!=&R.shared.card.device.sd);
    CHECK(kui_retail_hook_active==1u && (actual_sr&0xf0u)==0xf0u);
    CHECK(R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_WRITER && R.shared.port.acquired);
    CHECK(loan->bus.transfer==loan_transfer && loan->bus.ticks==transaction_ticks);
    CHECK(loan->blocks==R.shared.card.device.sd.blocks && loan->ready && !loan->slow);
    ++callbacks;
    if(reenter) {
        unsigned before=acquisitions;
        CHECK(kui_vmu_observe_sd_lease(transaction,context)==KUI_VMU_SD_LEASE_BUSY);
        CHECK(acquisitions==before && kui_retail_hook_active==1u);
    }
    (void)loan->bus.transfer(loan->bus.ctx,0xff,false);
    if(callback_not_ready) loan->ready=false;
    if(callback_bus_fault) R.shared.port.fault=true;
    return callback_result;
}
static void fresh_gateway(bool opened) {
    prepare(opened);R.shared.card.device.sd.bus.ticks=transaction_ticks;
    acquisitions=callbacks=0u;acquire_result=callback_result=0;
    reject_target=callback_not_ready=callback_bus_fault=reenter=false;
}
static int invoke(void) {return kui_vmu_observe_sd_lease(transaction,&test_control);}
int main(void) {
    for(unsigned opened=0;opened<2u;++opened) {
        fresh_gateway(opened!=0u);reenter=true;
        struct kui_retail_gd saved=R.shared.service;
        struct kui_retail_cursor cursor=R.engine.cursor;
        CHECK(invoke()==KUI_VMU_SD_LEASE_OK);
        CHECK(acquisitions==1u && callbacks==1u && releases==opened+1u);
        CHECK(!R.engine.native_loan_state && !kui_retail_hook_active && !R.shared.port.acquired);
        CHECK(!memcmp(&saved,&R.shared.service,sizeof(saved)) && !memcmp(&cursor,&R.engine.cursor,sizeof(cursor)));
        read_and_compare(45003u,2u,8u,0u);
    }
    static const int refusals[]={KUI_LOADER_SD_NOT_READY,KUI_LOADER_SD_UNSUPPORTED};
    for(unsigned opened=0;opened<2u;++opened) for(unsigned i=0;i<2u;++i) {
        fresh_gateway(opened!=0u);acquire_result=refusals[i];
        CHECK(invoke()==KUI_VMU_SD_LEASE_BUSY);
        CHECK(acquisitions==1u && !callbacks && releases==opened && !kui_retail_hook_active);
        CHECK(R.shared.card.device.sd.ready && !R.engine.native_loan_state);
        acquire_result=0;CHECK(invoke()==KUI_VMU_SD_LEASE_OK);
    }
    for(unsigned fault=0;fault<5u;++fault) {
        fresh_gateway(false);
        if(fault==0u) acquire_result=KUI_LOADER_SD_TIMEOUT;
        if(fault==1u) callback_result=-1;
        if(fault==2u) callback_not_ready=true;
        if(fault==3u) callback_bus_fault=true;
        if(fault==4u) release_fails=true;
        CHECK(invoke()==KUI_VMU_SD_LEASE_FAULT);
        CHECK(acquisitions==1u && callbacks==(fault!=0u) && releases==(fault!=0u));
        quarantined();
    }
    fresh_gateway(false);actual_sr=0u;
    CHECK(invoke()==KUI_VMU_SD_LEASE_FAULT && !acquisitions && !callbacks && !releases);
    fresh_gateway(false);reject_target=true;
    CHECK(invoke()==KUI_VMU_SD_LEASE_FAULT && !acquisitions && !callbacks && !releases);
    fresh_gateway(false);R.shared.service.pending=1u;
    CHECK(invoke()==KUI_VMU_SD_LEASE_BUSY && !acquisitions && !callbacks && !releases);
#if KUI_NATIVE_COOP_PUMP
    fresh_gateway(false);
    CHECK(kui_vmu_observe_sd_lease(NULL,NULL)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(!acquisitions && !callbacks && !releases);
    void *pump=(void *)(uintptr_t)KUI_RETAIL_NATIVE_PUMP_COOKIE;
    CHECK(kui_vmu_observe_sd_lease(NULL,pump)==KUI_VMU_SD_LEASE_OK);
    CHECK(!acquisitions && !callbacks && !releases && !R.engine.in_irq);
    kui_retail_hook_active=1u;
    CHECK(kui_vmu_observe_sd_lease(NULL,pump)==KUI_VMU_SD_LEASE_BUSY);
    CHECK(kui_retail_hook_active==1u && !acquisitions && !callbacks && !releases);
    fresh_gateway(false);R.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED;
    CHECK(kui_vmu_observe_sd_lease(NULL,pump)==KUI_VMU_SD_LEASE_BUSY);
    fresh_gateway(false);actual_sr=0u;
    CHECK(kui_vmu_observe_sd_lease(NULL,pump)==KUI_VMU_SD_LEASE_FAULT);
    fresh_gateway(false);reject_target=true;
    CHECK(kui_vmu_observe_sd_lease(NULL,pump)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(!acquisitions && !callbacks && !releases);
#endif
    printf("production native Background VMU gateway: %u checks passed\n",checks);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="kui-native-background-gateway-") as tmp:
    path = Path(tmp) / "gateway.c"
    path.write_text(fixture + gateway + tail)
    output = Path(tmp) / "test-gateway"
    flags = ["KUI_RETAIL_ASYNC_TEST=1", "KUI_SCI_STREAM_TEST=1", "KUI_RETAIL_ASYNC=1",
             "KUI_RETAIL_GD_ASYNC=1", "KUI_RETAIL_TRANSPORT=1", "KUI_SCI_SD_NO_BLOCK=1",
             "KUI_GENERAL_SCI_RESUME_FIX=1", "KUI_RETAIL_NATIVE_DRIVE_STATE=1",
             "KUI_VMU_NATIVE_BACKGROUND=1", "KUI_VMU_NATIVE_PERSISTENT_PROBE=1"]
    if COOPERATIVE:flags += ['KUI_NATIVE_COOP_PUMP=1','KUI_RETAIL_NATIVE_HYBRID=1']
    subprocess.run([os.environ.get("HOST_CC", "cc"), "-std=c11", "-O2", "-g",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                    "-Iinclude", "-Isrc/loader", "-Itests"] + ["-D" + flag for flag in flags] +
                   ["src/loader/retail_async.c", "src/loader/sci_stream.c", "src/core/retail_cursor.c",
                    "src/core/retail_image.c", "src/core/retail_gd.c", str(path), "-o", str(output)],
                   cwd=ROOT, check=True)
    subprocess.run([str(output)], cwd=ROOT, check=True,
                   env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
