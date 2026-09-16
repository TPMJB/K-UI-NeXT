/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include <arch/arch.h>
#include <arch/stack.h>
#include <dc/video.h>
#include <kos/linker.h>
#include <kos/mm.h>
#include <kos/mutex.h>
#include <malloc.h>
#include <string.h>

static mutex_t memory_lock=MUTEX_INITIALIZER;
static uint32_t peak_used;
bool kui_memory_snapshot(struct kui_memory_stats *out) {
    if(!out) return false;
    /* KOS mallinfo takes its own allocator lock. Never hold the UI/log lock
     * across this call. A before/after break check avoids counting heap growth
     * from a different sample; no allocation or storage I/O is performed. */
    struct mallinfo info;uintptr_t before=0,after=0;
    for(unsigned tries=0;tries<3;tries++) {
        before=(uintptr_t)mm_sbrk(0);info=mallinfo();after=(uintptr_t)mm_sbrk(0);
        if(before==after) break;
    }
    uintptr_t heap=((uintptr_t)end+3)&~(uintptr_t)3;
    uintptr_t limit=(uintptr_t)_arch_mem_top-THD_KERNEL_STACK_SIZE;
    if(before!=after || heap<0x8c010000u || after<heap || after>limit ||
       info.arena>after-heap || info.fordblks>info.arena || info.hblkhd) return false;
    struct kui_memory_stats s={0};
    s.physical=HW_MEMSIZE;s.firmware=65536;s.image=(uint32_t)(heap-0x8c010000u);
    s.main_stack=THD_KERNEL_STACK_SIZE;s.heap_system=(uint32_t)info.arena;
    s.heap_used=(uint32_t)info.uordblks;s.heap_free=(uint32_t)info.fordblks;
    /* In the pinned KOS dlmalloc, usmblks is peak system bytes, despite the
     * generic Newlib header's unused-field comment. It is not peak live use. */
    s.heap_max_system=(uint32_t)info.usmblks;
    s.unclaimed=(uint32_t)(limit-after);
    s.available=s.unclaimed+s.heap_free;
    if(s.available>s.physical) return false;
    s.used=s.physical-s.available;
    s.framebuffers=(uint32_t)(vid_mode->fb_size*vid_mode->fb_count);
    mutex_lock(&memory_lock);
    if(s.used>peak_used) peak_used=s.used;
    s.sampled_peak=peak_used;
    mutex_unlock(&memory_lock);
    *out=s;return true;
}
void kui_memory_log(const char *reason) {
    struct kui_memory_stats s;
    if(!kui_memory_snapshot(&s)) {kui_log("mstats: unavailable/unstable snapshot; try again");return;}
    kui_log("mstats [%s] (bytes; main RAM excludes VRAM/audio)",reason);
    kui_log("RAM total=%lu used/reserved~=%lu available~=%lu",
        (unsigned long)s.physical,(unsigned long)s.used,(unsigned long)s.available);
    kui_log("RAM sampled peak used/reserved=%lu",(unsigned long)s.sampled_peak);
    kui_log("Image+BSS=%lu firmware reserve=%lu main stack reserve=%lu",
        (unsigned long)s.image,(unsigned long)s.firmware,(unsigned long)s.main_stack);
    kui_log("Heap max system bytes=%lu",(unsigned long)s.heap_max_system);
    kui_log("Heap system bytes=%lu in use bytes=%lu free bytes=%lu",
        (unsigned long)s.heap_system,(unsigned long)s.heap_used,(unsigned long)s.heap_free);
    kui_log("Unclaimed main RAM=%lu; worker stacks included in heap",
        (unsigned long)s.unclaimed);
    kui_log("Video framebuffers=%lu bytes in separate VRAM",(unsigned long)s.framebuffers);
}
