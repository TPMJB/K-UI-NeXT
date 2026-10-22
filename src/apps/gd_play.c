/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/gd_play.h"
#include <arch/arch.h>

void kui_gd_play_boot(void) {
    /* Upstream KOS fcfa7d869471 kernel/arch/dreamcast/kernel/init.c:
     * arch_exit -> exit -> arch_exit_handler -> arch_shutdown -> arch_reboot.
     * Calling arch_reboot directly skips normal peripheral/FS shutdown.
     * The BIOS then applies its own disc-region and autostart settings. */
    arch_set_exit_path(ARCH_EXIT_REBOOT);
    arch_exit();
}
