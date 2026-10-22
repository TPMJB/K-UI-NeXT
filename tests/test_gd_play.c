/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/gd_play.h"
#include <arch/arch.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>

static jmp_buf exited;
static unsigned configured;
static int exit_path;

void arch_set_exit_path(int path) {
    assert(!configured);
    ++configured;exit_path=path;
}
void arch_exit(void) {
    /* Verify the normal KOS shutdown entry point is used, with the boot
     * destination already configured. Direct BIOS jumps have no test stub. */
    assert(configured==1 && exit_path==ARCH_EXIT_REBOOT);
    longjmp(exited,1);
}
int main(void) {
    if(!setjmp(exited)) {
        kui_gd_play_boot();
    }
    assert(configured==1 && exit_path==ARCH_EXIT_REBOOT);
    puts("PASS GD Play: configured BIOS reboot through normal KOS shutdown");
    return 0;
}
