/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/boot_ui.h"
#include "kui/storage.h"
#include <string.h>

void kui_boot_ui_init(struct kui_boot_ui *ui,uint64_t now) {
    memset(ui,0,sizeof(*ui));
    ui->transport=KUI_STORAGE_AUTO;
    ui->autoboot_until=now+3000;
}
static void logs(struct kui_boot_ui *ui) {
    ui->return_page=ui->page;
    ui->page=KUI_BOOT_LOG;
    ui->scroll=ui->log_column=0;
}
enum kui_boot_action kui_boot_ui_input(struct kui_boot_ui *ui,
        unsigned pressed,uint64_t now,bool busy,bool worker_available) {
    if(pressed || busy) ui->autoboot_until=0;
    if(busy) {
        if(pressed&KUI_BOOT_B) return KUI_BOOT_STOP;
        if((pressed&KUI_BOOT_Y) && ui->page!=KUI_BOOT_LOG) logs(ui);
        /* Logs remain scrollable while a diagnostic task is running. */
        if(ui->page!=KUI_BOOT_LOG) return KUI_BOOT_NONE;
        pressed&=KUI_BOOT_UP|KUI_BOOT_DOWN|KUI_BOOT_LEFT|KUI_BOOT_RIGHT|KUI_BOOT_START;
    }
    if(ui->autoboot_until && now>=ui->autoboot_until) {
        ui->autoboot_until=0;
        return KUI_BOOT_RUNTIME;
    }
    if(ui->page==KUI_BOOT_LOG) {
        if(pressed&KUI_BOOT_UP) {if(ui->scroll<1500) ++ui->scroll;}
        if((pressed&KUI_BOOT_DOWN) && ui->scroll) --ui->scroll;
        if(pressed&KUI_BOOT_LEFT) ui->log_column=0;
        if(pressed&KUI_BOOT_RIGHT) ui->log_column=24;
        if(pressed&KUI_BOOT_START) ui->scroll=ui->log_column=0;
        if(pressed&(KUI_BOOT_B|KUI_BOOT_Y)) ui->page=ui->return_page;
        return KUI_BOOT_NONE;
    }
    if(ui->page==KUI_BOOT_CONFIRM) {
        if(pressed&KUI_BOOT_B) {ui->page=KUI_BOOT_DIAGNOSTICS;return KUI_BOOT_NONE;}
        if((pressed&KUI_BOOT_A) && worker_available) {
            enum kui_boot_action action=ui->confirm;
            ui->page=KUI_BOOT_DIAGNOSTICS;
            return action;
        }
        return KUI_BOOT_NONE;
    }
    if(ui->page==KUI_BOOT_HELP) {
        if(pressed&(KUI_BOOT_B|KUI_BOOT_START)) {ui->page=KUI_BOOT_HOME;ui->selected=4;}
        return KUI_BOOT_NONE;
    }
    if(pressed&KUI_BOOT_B) {
        ui->page=KUI_BOOT_HOME;ui->selected=0;
        return KUI_BOOT_NONE;
    }
    if(pressed&KUI_BOOT_Y) {logs(ui);return KUI_BOOT_NONE;}
    if(ui->page==KUI_BOOT_HOME) {
        if(pressed&KUI_BOOT_LEFT) ui->transport=(ui->transport+3)%4;
        if(pressed&KUI_BOOT_RIGHT) ui->transport=(ui->transport+1)%4;
        if(pressed&KUI_BOOT_X) {ui->selected=1;return KUI_BOOT_RECOVERY;}
    }
    if(pressed&KUI_BOOT_UP) ui->selected=(ui->selected+4)%5;
    if(pressed&KUI_BOOT_DOWN) ui->selected=(ui->selected+1)%5;
    if(!(pressed&KUI_BOOT_A)) return KUI_BOOT_NONE;
    if(ui->page==KUI_BOOT_HOME) {
        if(ui->selected<3) return (enum kui_boot_action)(KUI_BOOT_RUNTIME+ui->selected);
        ui->page=ui->selected==3?KUI_BOOT_DIAGNOSTICS:KUI_BOOT_HELP;
        ui->selected=0;
    } else {
        if(ui->selected==4) {logs(ui);return KUI_BOOT_NONE;}
        if(!worker_available) return KUI_BOOT_NONE;
        if(ui->selected==0) return KUI_BOOT_PROBE;
        ui->confirm=(enum kui_boot_action)(KUI_BOOT_WRITE_TEST+ui->selected-1);
        ui->page=KUI_BOOT_CONFIRM;
    }
    return KUI_BOOT_NONE;
}
