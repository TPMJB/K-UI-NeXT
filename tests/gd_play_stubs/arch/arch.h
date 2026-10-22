/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_GD_PLAY_ARCH_H
#define KUI_TEST_GD_PLAY_ARCH_H
#define ARCH_EXIT_RETURN 1
#define ARCH_EXIT_MENU 2
#define ARCH_EXIT_REBOOT 3
void arch_set_exit_path(int path);
void arch_exit(void) __attribute__((noreturn));
#endif
