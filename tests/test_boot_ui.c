/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/boot_ui.h"
#include "kui/storage.h"
#include <assert.h>
#include <stdio.h>
static enum kui_boot_action input(struct kui_boot_ui *ui,unsigned key,bool busy,bool worker) {
    return kui_boot_ui_input(ui,key,6000,busy,worker);
}
int main(void) {
    struct kui_boot_ui ui;
    kui_boot_ui_init(&ui,1000);
    assert(ui.transport==KUI_STORAGE_AUTO);
    assert(kui_boot_ui_input(&ui,0,3999,false,true)==KUI_BOOT_NONE);
    assert(kui_boot_ui_input(&ui,0,4000,false,true)==KUI_BOOT_RUNTIME);
    assert(input(&ui,0,false,true)==KUI_BOOT_NONE); /* no repeated probes */
    assert(input(&ui,KUI_BOOT_A,false,true)==KUI_BOOT_RUNTIME); /* insert and retry */
    kui_boot_ui_init(&ui,1000);
    assert(kui_boot_ui_input(&ui,KUI_BOOT_B,3999,false,true)==KUI_BOOT_NONE);
    assert(!ui.autoboot_until && input(&ui,0,false,true)==KUI_BOOT_NONE);
    assert(input(&ui,KUI_BOOT_X,false,true)==KUI_BOOT_RECOVERY);
    assert(input(&ui,KUI_BOOT_B,false,true)==KUI_BOOT_NONE);
    input(&ui,KUI_BOOT_RIGHT,false,true);
    assert(ui.transport==KUI_STORAGE_SCIF);
    input(&ui,KUI_BOOT_LEFT,false,true);
    assert(ui.transport==KUI_STORAGE_AUTO);
    input(&ui,KUI_BOOT_DOWN,false,true);input(&ui,KUI_BOOT_DOWN,false,true);
    assert(input(&ui,KUI_BOOT_A,false,true)==KUI_BOOT_TOOLS);
    input(&ui,KUI_BOOT_DOWN,false,true);input(&ui,KUI_BOOT_A,false,true);
    assert(ui.page==KUI_BOOT_DIAGNOSTICS);
    assert(input(&ui,KUI_BOOT_A,false,true)==KUI_BOOT_PROBE);
    input(&ui,KUI_BOOT_DOWN,false,true);
    assert(input(&ui,KUI_BOOT_A,false,true)==KUI_BOOT_NONE);
    assert(ui.page==KUI_BOOT_CONFIRM);
    assert(input(&ui,KUI_BOOT_B,false,true)==KUI_BOOT_NONE);
    assert(ui.page==KUI_BOOT_DIAGNOSTICS);
    input(&ui,KUI_BOOT_A,false,true);
    assert(input(&ui,KUI_BOOT_A,false,true)==KUI_BOOT_WRITE_TEST);
    for(unsigned i=0;i<2;i++) {
        input(&ui,KUI_BOOT_DOWN,false,true);
        assert(input(&ui,KUI_BOOT_A,false,true)==KUI_BOOT_NONE);
        assert(input(&ui,KUI_BOOT_A,false,true)==(enum kui_boot_action)(KUI_BOOT_SAVE_LOG+i));
    }
    input(&ui,KUI_BOOT_B,false,true);
    assert(input(&ui,KUI_BOOT_A,true,true)==KUI_BOOT_NONE); /* no concurrent boot */
    assert(input(&ui,KUI_BOOT_X,true,true)==KUI_BOOT_NONE);
    assert(input(&ui,KUI_BOOT_B,true,true)==KUI_BOOT_STOP);
    input(&ui,KUI_BOOT_Y,true,true);
    assert(ui.page==KUI_BOOT_LOG);
    input(&ui,KUI_BOOT_UP,true,true);input(&ui,KUI_BOOT_RIGHT,true,true);
    assert(ui.scroll==1 && ui.log_column==24);
    input(&ui,KUI_BOOT_START,true,true);assert(!ui.scroll && !ui.log_column);
    input(&ui,KUI_BOOT_B,false,true);assert(ui.page==KUI_BOOT_HOME);
    assert(input(&ui,KUI_BOOT_A,false,false)==KUI_BOOT_RUNTIME); /* worker failure recovery */
    ui.page=KUI_BOOT_DIAGNOSTICS;ui.selected=1;
    assert(input(&ui,KUI_BOOT_A,false,false)==KUI_BOOT_NONE);
    assert(ui.page==KUI_BOOT_DIAGNOSTICS);
    kui_boot_ui_init(&ui,1000);
    assert(kui_boot_ui_input(&ui,0,4000,true,true)==KUI_BOOT_NONE);
    assert(!ui.autoboot_until);
    puts("boot UI: countdown, retry, source, recovery/tools, confirmations and busy ownership passed");
    return 0;
}
