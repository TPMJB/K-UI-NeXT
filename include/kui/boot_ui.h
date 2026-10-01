/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_BOOT_UI_H
#define KUI_BOOT_UI_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_BOOT_LOG_ROWS 12u
enum kui_boot_page { KUI_BOOT_HOME, KUI_BOOT_DIAGNOSTICS, KUI_BOOT_LOG,
    KUI_BOOT_HELP, KUI_BOOT_CONFIRM };
enum kui_boot_button { KUI_BOOT_UP=1u, KUI_BOOT_DOWN=2u, KUI_BOOT_LEFT=4u,
    KUI_BOOT_RIGHT=8u, KUI_BOOT_A=16u, KUI_BOOT_B=32u, KUI_BOOT_X=64u,
    KUI_BOOT_Y=128u, KUI_BOOT_START=256u };
enum kui_boot_action { KUI_BOOT_NONE, KUI_BOOT_RUNTIME, KUI_BOOT_RECOVERY,
    KUI_BOOT_TOOLS, KUI_BOOT_PROBE, KUI_BOOT_WRITE_TEST, KUI_BOOT_SAVE_LOG,
    KUI_BOOT_BENCH, KUI_BOOT_STOP };
struct kui_boot_ui {
    enum kui_boot_page page, return_page;
    enum kui_boot_action confirm;
    unsigned selected, transport, scroll, log_column;
    uint64_t autoboot_until;
};
struct kui_boot_view {
    bool busy, cancelled, worker_available;
    unsigned countdown, line_count, total_lines;
    const char *build, *status;
    const char *lines[KUI_BOOT_LOG_ROWS];
};
/* Edge-triggered input. Any input ends automatic startup. No card polling or
 * I/O: a retry happens only when the caller receives a boot action. */
void kui_boot_ui_init(struct kui_boot_ui *ui, uint64_t now);
enum kui_boot_action kui_boot_ui_input(struct kui_boot_ui *ui,
    unsigned pressed, uint64_t now, bool busy, bool worker_available);
/* No allocation, filesystem access, or KOS calls; the same RGB565 renderer is
 * used on hardware and in the host preview. Safe TV area: x32..607, y24..455. */
void kui_boot_ui_draw(uint16_t *frame, const struct kui_boot_ui *ui,
    const struct kui_boot_view *view);
#endif
