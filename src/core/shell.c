/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell.h"
#include <limits.h>
#include <string.h>

void kui_shell_set_preferences(struct kui_shell *s, const struct kui_settings *p) {
    if(!s || !p) return;
    s->saved = *p;
    if(!s->saved.crc_only) s->saved.end_readback = true;
    s->draft = s->saved;
}
void kui_shell_init(struct kui_shell *s, const struct kui_settings *p) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
    const struct kui_settings initial = {true, false, true};
    kui_shell_set_preferences(s, p ? p : &initial);
}
bool kui_shell_settings_dirty(const struct kui_shell *s) {
    return s && (s->saved.crc_only != s->draft.crc_only ||
        s->saved.end_readback != s->draft.end_readback ||
        s->saved.show_memory != s->draft.show_memory);
}
static unsigned move(unsigned selected, unsigned buttons) {
    if((buttons & (KUI_SHELL_UP | KUI_SHELL_DOWN)) == KUI_SHELL_UP)
        return selected ? selected - 1 : 2;
    if((buttons & (KUI_SHELL_UP | KUI_SHELL_DOWN)) == KUI_SHELL_DOWN)
        return (selected + 1) % 3;
    return selected;
}
static void scroll(struct kui_shell *s, unsigned buttons) {
    if(buttons & KUI_SHELL_START) s->scroll = 0;
    else if((buttons & (KUI_SHELL_UP | KUI_SHELL_DOWN)) == KUI_SHELL_UP) {
        if(s->scroll <= UINT_MAX - 3) s->scroll += 3;
    } else if((buttons & (KUI_SHELL_UP | KUI_SHELL_DOWN)) == KUI_SHELL_DOWN)
        s->scroll = s->scroll > 3 ? s->scroll - 3 : 0;
}
enum kui_shell_action kui_shell_input(struct kui_shell *s,
        unsigned buttons, bool busy) {
    if(!s) return KUI_SHELL_NONE;
    /* Stop/back wins even over a simultaneous confirmation or launch. */
    if(buttons & KUI_SHELL_B) {
        if(busy) { s->confirm_new = false; return KUI_SHELL_STOP; }
        if(s->confirm_new) { s->confirm_new = false; return KUI_SHELL_NONE; }
        if(s->page == KUI_SHELL_SETTINGS) {
            s->draft = s->saved;
            s->page = KUI_SHELL_HOME;
            return KUI_SHELL_DISCARD_SETTINGS;
        }
        s->page = KUI_SHELL_HOME;
        return KUI_SHELL_NONE;
    }
    if(buttons & KUI_SHELL_L) return KUI_SHELL_MSTATS;
    if(busy) {
        if(s->page == KUI_SHELL_DIAGNOSTICS) scroll(s, buttons);
        return KUI_SHELL_NONE;
    }
    if(s->confirm_new) {
        if(buttons & KUI_SHELL_A) {
            s->confirm_new = false;
            return KUI_SHELL_NEW_DUMP;
        }
        return KUI_SHELL_NONE;
    }
    switch(s->page) {
    case KUI_SHELL_HOME:
        s->home_selected = move(s->home_selected, buttons);
        if(buttons & KUI_SHELL_A) {
            s->page = (enum kui_shell_page)(s->home_selected + 1);
            if(s->page == KUI_SHELL_SETTINGS) {
                s->draft = s->saved;
                return KUI_SHELL_LOAD_SETTINGS;
            }
        }
        break;
    case KUI_SHELL_RIPPER:
        if(buttons & KUI_SHELL_A) s->confirm_new = true;
        else if(buttons & KUI_SHELL_X) return KUI_SHELL_RESUME;
        else if(buttons & KUI_SHELL_Y) return KUI_SHELL_VERIFY;
        break;
    case KUI_SHELL_SETTINGS:
        s->setting_selected = move(s->setting_selected, buttons);
        if((buttons & (KUI_SHELL_LEFT | KUI_SHELL_RIGHT)) == KUI_SHELL_LEFT ||
           (buttons & (KUI_SHELL_LEFT | KUI_SHELL_RIGHT)) == KUI_SHELL_RIGHT) {
            if(s->setting_selected == 0) {
                s->draft.crc_only = !s->draft.crc_only;
                if(!s->draft.crc_only) s->draft.end_readback = true;
            }
            if(s->setting_selected == 1 && s->draft.crc_only)
                s->draft.end_readback = !s->draft.end_readback;
            if(s->setting_selected == 2) s->draft.show_memory = !s->draft.show_memory;
        }
        if(buttons & KUI_SHELL_A) return KUI_SHELL_SAVE_SETTINGS;
        break;
    case KUI_SHELL_DIAGNOSTICS:
        scroll(s, buttons);
        if(buttons & KUI_SHELL_A) return KUI_SHELL_DISC_PROBE;
        if(buttons & KUI_SHELL_X) return KUI_SHELL_STORAGE_PROBE;
        if(buttons & KUI_SHELL_Y) return KUI_SHELL_SAVE_LOG;
        if(buttons & KUI_SHELL_R) return KUI_SHELL_BENCH;
        break;
    }
    return KUI_SHELL_NONE;
}
