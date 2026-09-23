/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell.h"
#include <limits.h>
#include <stdio.h>
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
    kui_system_settings_default(&s->system_saved);
    s->system_draft=s->system_saved;
    snprintf(s->music_path,sizeof(s->music_path),"/Music");
    kui_destination_default(s->destination);
    memcpy(s->browse_path,s->destination,sizeof(s->browse_path));
}
void kui_shell_set_system_preferences(struct kui_shell *s,const struct kui_system_settings *p) {
    if(!s || !p || !kui_system_settings_valid(p)) return;
    s->system_saved=*p; s->system_draft=*p;
}
bool kui_shell_system_dirty(const struct kui_shell *s) {
    return s && (s->system_saved.video_mode!=s->system_draft.video_mode ||
        s->system_saved.show_memory!=s->system_draft.show_memory ||
        s->system_saved.music_enabled!=s->system_draft.music_enabled ||
        s->system_saved.music_volume!=s->system_draft.music_volume ||
        s->system_saved.startup_chime!=s->system_draft.startup_chime ||
        s->system_saved.startup_app!=s->system_draft.startup_app);
}
void kui_shell_set_vmu(struct kui_shell *s,const struct kui_vmu_view *view) {
    if(!s || !view || view->slot!=s->vmu_slot || view->page!=s->vmu_page) return;
    s->vmu=*view;
    if(s->vmu.count>KUI_VMU_ROWS) s->vmu.count=KUI_VMU_ROWS;
    for(unsigned i=0;i<s->vmu.count;i++) s->vmu.entries[i].name[sizeof(s->vmu.entries[i].name)-1]=0;
    if(s->vmu_selected>=s->vmu.count) s->vmu_selected=0;
}
void kui_shell_set_vmu_backups(struct kui_shell *s,const struct kui_vmu_backup_view *view) {
    if(!s || !view || s->page!=KUI_SHELL_VMU_RESTORE || view->page!=s->backup_page) return;
    s->backups=*view;
    s->backups.status.message[sizeof(s->backups.status.message)-1]=0;
    if(s->backups.count>KUI_VMU_ROWS) s->backups.count=KUI_VMU_ROWS;
    for(unsigned i=0;i<s->backups.count;i++) {
        struct kui_vmu_backup_entry *e=&s->backups.entries[i];
        e->name[sizeof(e->name)-1]=0;e->folder[sizeof(e->folder)-1]=0;
        /* An unterminated path is unusable, never silently truncated. */
        if(!memchr(e->path,0,sizeof(e->path))) e->path[0]=0;
    }
    if(s->backup_selected>=s->backups.count) s->backup_selected=0;
}
void kui_shell_set_vmu_restore_preview(struct kui_shell *s,const struct kui_vmu_view *view) {
    if(!s || !view || s->page!=KUI_SHELL_VMU_RESTORE || view->slot!=s->vmu_slot) return;
    s->backups.status=view->status;
    s->backups.status.message[sizeof(s->backups.status.message)-1]=0;
    s->confirm_vmu_restore=view->restore_ready && s->restore_path[0];
}
void kui_shell_set_clock(struct kui_shell *s,const struct kui_datetime *value,const char *notice) {
    if(!s) return;
    bool read_valid=value && kui_datetime_valid(value);
    if(read_valid) s->clock_draft=*value;
    else if(!kui_datetime_valid(&s->clock_draft))
        s->clock_draft=(struct kui_datetime){1980,1,1,0,0,0};
    /* A dead/reset RTC must still be repairable. The explicit read-error
     * notice distinguishes this editable fallback from an observed time. */
    s->clock_valid=true;
    s->confirm_clock=false;
    snprintf(s->clock_notice,sizeof(s->clock_notice),"%s",notice?notice:
        read_valid?"":"Clock unavailable. Edit the fallback date before applying.");
}
static enum kui_shell_action list_backups(struct kui_shell *s,bool first) {
    if(first) s->backup_page=0;
    s->backup_selected=0;s->restore_path[0]=0;s->confirm_vmu_restore=false;
    memset(&s->backups,0,sizeof(s->backups));
    return KUI_SHELL_VMU_BACKUPS_LIST;
}
void kui_shell_set_music_listing(struct kui_shell *s,const struct kui_music_player_page *page) {
    if(!s || !page || !memchr(page->root,0,sizeof(page->root)) ||
       strcmp(page->root,s->music_path)) return;
    s->music_listing=*page;
    if(s->music_listing.count>KUI_MUSIC_PLAYER_ROWS) s->music_listing.count=KUI_MUSIC_PLAYER_ROWS;
    s->music_listing.message[sizeof(s->music_listing.message)-1]=0;
    for(unsigned i=0;i<s->music_listing.count;i++) {
        struct kui_music_player_entry *entry=&s->music_listing.entries[i];
        if(!memchr(entry->name,0,sizeof(entry->name))) {
            snprintf(entry->name,sizeof(entry->name),"[Name too long]");
            entry->disabled=true;
        }
    }
    if(s->music_selected>=s->music_listing.count) s->music_selected=0;
}
static enum kui_shell_action list_music(struct kui_shell *s,bool first) {
    if(first) s->music_page=0;
    s->music_selected=0;
    memset(&s->music_listing,0,sizeof(s->music_listing));
    return KUI_SHELL_MUSIC_LIST;
}
bool kui_shell_phase_eta(const struct kui_shell_view *v,uint64_t *seconds) {
    if(!v || !seconds || !v->busy || v->saving || v->cancel_requested ||
       v->phase<1 || v->phase>3 || !v->total || !v->rate_kib ||
       v->phase_elapsed_ms<2000 || v->progress_age_ms>3000) return false;
    uint64_t bytes_per_second=(uint64_t)v->rate_kib*1024u;
    uint64_t remaining=v->done<v->total?v->total-v->done:0;
    *seconds=remaining/bytes_per_second+(remaining%bytes_per_second!=0);
    return true;
}
void kui_shell_destination_error(struct kui_shell *s, const char *message) {
    if(s) snprintf(s->destination_notice,sizeof(s->destination_notice),"%s",
        message ? message : "Could not open this folder.");
}
void kui_shell_set_destination(struct kui_shell *s, const char *path) {
    char normalized[KUI_DEST_ROOT_CAP];
    if(!s || !kui_destination_normalize(normalized,path)) return;
    snprintf(s->destination,sizeof(s->destination),"%s",normalized);
    snprintf(s->browse_path,sizeof(s->browse_path),"%s",normalized);
    s->destination_notice[0]=0;
    if(s->page==KUI_SHELL_DESTINATION || s->page==KUI_SHELL_KEYBOARD)
        s->page=KUI_SHELL_RIPPER;
}
void kui_shell_set_listing(struct kui_shell *s, const struct kui_destination_page *page) {
    if(!s || !page) return;
    s->listing=*page;
    if(s->listing.count>KUI_DEST_PAGE_SIZE) s->listing.count=KUI_DEST_PAGE_SIZE;
    for(unsigned i=0;i<s->listing.count;i++) {
        struct kui_destination_entry *entry=&s->listing.entries[i];
        if(!memchr(entry->name,0,sizeof(entry->name))) {
            snprintf(entry->name,sizeof(entry->name),"[Name too long]");
            entry->disabled=true;
        }
    }
    s->browser_selected=0;
    s->destination_notice[0]=0;
}
static enum kui_shell_action list_destination(struct kui_shell *s, bool first_page) {
    if(first_page) s->browser_page=0;
    s->browser_selected=0;
    memset(&s->listing,0,sizeof(s->listing));
    s->destination_notice[0]=0;
    return KUI_SHELL_DEST_LIST;
}
static enum kui_shell_action save_destination(struct kui_shell *s, const char *path) {
    char normalized[KUI_DEST_ROOT_CAP];
    if(!kui_destination_normalize(normalized,path)) {
        kui_shell_destination_error(s,"Use an absolute folder path without '..' or reserved names.");
        return KUI_SHELL_NONE;
    }
    snprintf(s->browse_path,sizeof(s->browse_path),"%s",normalized);
    s->destination_notice[0]=0;
    return KUI_SHELL_DEST_SAVE;
}
static const char keyboard_keys[]="qwertyuiopasdfghjkl/zxcvbnm-_.0123456789";
const char *kui_shell_key_label(unsigned key, bool uppercase) {
    static char character[2];
    if(key>=KUI_SHELL_KEY_COUNT) return "";
    if(key==40) return "SPACE";
    if(key==41) return "BACK";
    if(key==42) return "DONE";
    char c=keyboard_keys[key];
    character[0]=uppercase && c>='a' && c<='z' ? (char)(c-'a'+'A') : c;
    character[1]=0;
    return character;
}
static void keyboard_backspace(struct kui_shell *s) {
    size_t n=strlen(s->keyboard);
    if(!n) return;
    /* A folder already on the card may contain UTF-8 even though this small
     * keyboard enters ASCII. Backspace removes a whole encoded character. */
    --n;
    while(n && ((unsigned char)s->keyboard[n]&0xc0u)==0x80u) --n;
    s->keyboard[n]=0;
    s->destination_notice[0]=0;
}
static unsigned keyboard_move(unsigned key,unsigned buttons) {
    if(key>=KUI_SHELL_KEY_COUNT) key=0;
    unsigned row=key<40?key/10:4, col=key<40?key%10:key-40;
    unsigned horizontal=buttons&(KUI_SHELL_LEFT|KUI_SHELL_RIGHT);
    unsigned vertical=buttons&(KUI_SHELL_UP|KUI_SHELL_DOWN);
    unsigned count=row==4?3:10;
    if(horizontal==KUI_SHELL_LEFT) col=col?col-1:count-1;
    else if(horizontal==KUI_SHELL_RIGHT) col=(col+1)%count;
    if(vertical==KUI_SHELL_UP || vertical==KUI_SHELL_DOWN) {
        unsigned old_row=row;
        row=vertical==KUI_SHELL_UP?(row?row-1:4):(row+1)%5;
        if(old_row==4) col=col==0?1:col==1?4:8;
        if(row==4) col=col<3?0:col<7?1:2;
    }
    return row==4?40+col:row*10+col;
}
static enum kui_shell_action keyboard_input(struct kui_shell *s,unsigned buttons) {
    s->keyboard_selected=keyboard_move(s->keyboard_selected,buttons);
    if(buttons&KUI_SHELL_X) keyboard_backspace(s);
    else if(buttons&KUI_SHELL_Y) s->keyboard_upper=!s->keyboard_upper;
    else if(buttons&KUI_SHELL_START) return save_destination(s,s->keyboard);
    else if(buttons&KUI_SHELL_A) {
        if(s->keyboard_selected==42) return save_destination(s,s->keyboard);
        if(s->keyboard_selected==41) keyboard_backspace(s);
        else {
            size_t n=strlen(s->keyboard);
            if(n+1<sizeof(s->keyboard)) {
                s->keyboard[n]=s->keyboard_selected==40?' ':
                    *kui_shell_key_label(s->keyboard_selected,s->keyboard_upper);
                s->keyboard[n+1]=0; s->destination_notice[0]=0;
            } else kui_shell_destination_error(s,"This folder path is too long.");
        }
    }
    return KUI_SHELL_NONE;
}
bool kui_shell_settings_dirty(const struct kui_shell *s) {
    return s && (s->saved.crc_only != s->draft.crc_only ||
        s->saved.end_readback != s->draft.end_readback);
}
static unsigned move_count(unsigned selected, unsigned buttons,unsigned count) {
    if(!count) return 0;
    if(selected>=count) selected=0;
    if((buttons & (KUI_SHELL_UP | KUI_SHELL_DOWN)) == KUI_SHELL_UP)
        return selected ? selected - 1 : count-1;
    if((buttons & (KUI_SHELL_UP | KUI_SHELL_DOWN)) == KUI_SHELL_DOWN)
        return (selected + 1) % count;
    return selected;
}
static void scroll(struct kui_shell *s, unsigned buttons) {
    if(buttons & KUI_SHELL_START) s->scroll = 0;
    else if((buttons & (KUI_SHELL_UP | KUI_SHELL_DOWN)) == KUI_SHELL_UP) {
        if(s->scroll <= UINT_MAX - 3) s->scroll += 3;
    } else if((buttons & (KUI_SHELL_UP | KUI_SHELL_DOWN)) == KUI_SHELL_DOWN)
        s->scroll = s->scroll > 3 ? s->scroll - 3 : 0;
}
static void edit_clock(struct kui_shell *s,unsigned buttons) {
    if(!s->clock_valid) return;
    s->clock_selected=move_count(s->clock_selected,buttons,6);
    unsigned horizontal=buttons&(KUI_SHELL_LEFT|KUI_SHELL_RIGHT);
    if(horizontal!=KUI_SHELL_LEFT && horizontal!=KUI_SHELL_RIGHT) return;
    struct kui_datetime *d=&s->clock_draft;
    unsigned values[]={d->year,d->month,d->day,d->hour,d->minute,d->second};
    static const unsigned low[]={1980,1,1,0,0,0}, high[]={2085,12,31,23,59,59};
    unsigned at=s->clock_selected, v=values[at], maximum=high[at];
    if(at==2) {
        struct kui_datetime last=*d;last.day=31;
        while(last.day>1 && !kui_datetime_valid(&last)) --last.day;
        maximum=last.day;
    }
    if(horizontal==KUI_SHELL_LEFT) v=v<=low[at]?maximum:v-1;
    else v=v>=maximum?low[at]:v+1;
    values[at]=v;
    d->year=(uint16_t)values[0];d->month=(uint8_t)values[1];d->day=(uint8_t)values[2];
    d->hour=(uint8_t)values[3];d->minute=(uint8_t)values[4];d->second=(uint8_t)values[5];
    /* Month/year changes retain the nearest valid day (e.g. Feb 29 -> Feb 28). */
    while(d->day>1 && !kui_datetime_valid(d)) --d->day;
    s->clock_notice[0]=0;
}
enum kui_shell_action kui_shell_input(struct kui_shell *s,
        unsigned buttons, bool busy) {
    if(!s) return KUI_SHELL_NONE;
    if(s->video_trial) {
        if(buttons&KUI_SHELL_B) return KUI_SHELL_CANCEL_VIDEO;
        if(buttons&KUI_SHELL_A) return KUI_SHELL_CONFIRM_VIDEO;
        return KUI_SHELL_NONE;
    }
    /* Stop/back wins even over a simultaneous confirmation or launch. */
    if(buttons & KUI_SHELL_B) {
        if(busy) {
            s->confirm_new=false; s->confirm_quick_resume=false; s->confirm_gd_boot=false;
            s->confirm_clock=false;s->confirm_defaults=false;s->confirm_vmu_restore=false;
            return KUI_SHELL_STOP;
        }
        if(s->confirm_clock) {s->confirm_clock=false;return KUI_SHELL_NONE;}
        if(s->confirm_defaults) {s->confirm_defaults=false;return KUI_SHELL_NONE;}
        if(s->confirm_vmu_restore) {s->confirm_vmu_restore=false;return KUI_SHELL_NONE;}
        if(s->page==KUI_SHELL_CLOCK) {s->page=KUI_SHELL_SETTINGS;return KUI_SHELL_NONE;}
        if(s->page==KUI_SHELL_CRC_SCAN) {s->page=KUI_SHELL_ADVANCED;return KUI_SHELL_NONE;}
        if(s->page==KUI_SHELL_VMU_RESTORE) {s->page=KUI_SHELL_VMU;return KUI_SHELL_NONE;}
        if(s->confirm_quick_resume) { s->confirm_quick_resume=false; return KUI_SHELL_NONE; }
        if(s->confirm_gd_boot) { s->confirm_gd_boot=false; return KUI_SHELL_NONE; }
        if(s->confirm_new) { s->confirm_new = false; return KUI_SHELL_NONE; }
        if(s->page == KUI_SHELL_MUSIC) {
            char parent[KUI_DEST_ROOT_CAP];
            if(kui_destination_parent(parent,s->music_path) && strcmp(parent,s->music_path)) {
                snprintf(s->music_path,sizeof(s->music_path),"%s",parent);
                return list_music(s,true);
            }
            s->page=KUI_SHELL_HOME; return KUI_SHELL_NONE;
        }
        if(s->page == KUI_SHELL_KEYBOARD) {
            snprintf(s->browse_path,sizeof(s->browse_path),"%s",s->keyboard_original);
            s->page=KUI_SHELL_DESTINATION; s->destination_notice[0]=0;
            return KUI_SHELL_NONE;
        }
        if(s->page == KUI_SHELL_DESTINATION) {
            char parent[KUI_DEST_ROOT_CAP];
            if(kui_destination_parent(parent,s->browse_path) &&
               strcmp(parent,s->browse_path)) {
                snprintf(s->browse_path,sizeof(s->browse_path),"%s",parent);
                return list_destination(s,true);
            }
            return KUI_SHELL_NONE;
        }
        if(s->page == KUI_SHELL_RIPPER_SETTINGS) {
            s->draft = s->saved;
            s->page = s->settings_return;
            return KUI_SHELL_DISCARD_SETTINGS;
        }
        if(s->page == KUI_SHELL_SETTINGS) {
            s->system_draft=s->system_saved; s->page=KUI_SHELL_HOME;
            return KUI_SHELL_DISCARD_SYSTEM;
        }
        if(s->page == KUI_SHELL_ADVANCED) {
            s->page=KUI_SHELL_RIPPER;
            return KUI_SHELL_NONE;
        }
        s->page = KUI_SHELL_HOME;
        return KUI_SHELL_NONE;
    }
    /* Music is independent of foreground I/O. Main queues a request when the
     * worker cannot load a different song yet. B and modal confirmations keep
     * priority, and simultaneous triggers do not choose an arbitrary song. */
    bool song_page=s->page==KUI_SHELL_HOME || s->page==KUI_SHELL_RIPPER;
    bool confirming=s->confirm_new || s->confirm_quick_resume || s->confirm_gd_boot ||
        s->confirm_clock || s->confirm_defaults || s->confirm_vmu_restore;
    if(song_page && !confirming && !(buttons & ~(KUI_SHELL_L|KUI_SHELL_R))) {
        unsigned triggers=buttons & (KUI_SHELL_L|KUI_SHELL_R);
        if(triggers==KUI_SHELL_L) return KUI_SHELL_MUSIC_PREVIOUS;
        if(triggers==KUI_SHELL_R) return KUI_SHELL_MUSIC_NEXT;
    }
    if(!song_page && (buttons & KUI_SHELL_L)) return KUI_SHELL_MSTATS;
    if(busy) {
        if(s->page == KUI_SHELL_DIAGNOSTICS) scroll(s, buttons);
        return KUI_SHELL_NONE;
    }
    if(s->confirm_clock) {
        if(buttons&KUI_SHELL_A) {s->confirm_clock=false;return KUI_SHELL_CLOCK_WRITE;}
        return KUI_SHELL_NONE;
    }
    if(s->confirm_defaults) {
        if(buttons&KUI_SHELL_A) {
            s->confirm_defaults=false;kui_system_settings_default(&s->system_draft);
            s->system_selected=0;
        }
        return KUI_SHELL_NONE;
    }
    if(s->confirm_vmu_restore) {
        if(buttons&KUI_SHELL_A) {s->confirm_vmu_restore=false;return KUI_SHELL_VMU_RESTORE_COMMIT;}
        return KUI_SHELL_NONE;
    }
    if(s->confirm_gd_boot) {
        if(buttons & KUI_SHELL_A) {
            s->confirm_gd_boot=false; return KUI_SHELL_GD_BOOT;
        }
        return KUI_SHELL_NONE;
    }
    if(s->confirm_quick_resume) {
        if(buttons & KUI_SHELL_A) {
            s->confirm_quick_resume=false; s->page=KUI_SHELL_RIPPER;
            return KUI_SHELL_RESUME_QUICK;
        }
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
        if(buttons & KUI_SHELL_Y) return KUI_SHELL_MUSIC_CYCLE;
        s->home_selected = move_count(s->home_selected, buttons,8);
        if(buttons & KUI_SHELL_A) {
            static const enum kui_shell_page pages[]={KUI_SHELL_RIPPER,KUI_SHELL_VMU,
                KUI_SHELL_MEMORY,KUI_SHELL_NETWORK,KUI_SHELL_SETTINGS,KUI_SHELL_DIAGNOSTICS,
                KUI_SHELL_GD_PLAY,KUI_SHELL_MUSIC};
            s->page=pages[s->home_selected];
            if(s->page == KUI_SHELL_SETTINGS) {
                s->system_draft=s->system_saved;
                return KUI_SHELL_LOAD_SYSTEM;
            }
            if(s->page==KUI_SHELL_VMU) return KUI_SHELL_VMU_LIST;
            if(s->page==KUI_SHELL_MUSIC) return list_music(s,true);
        }
        break;
    case KUI_SHELL_RIPPER:
        if(buttons & KUI_SHELL_A) s->confirm_new = true;
        else if(buttons & KUI_SHELL_X) return KUI_SHELL_RESUME;
        else if(buttons & KUI_SHELL_Y) return KUI_SHELL_VERIFY;
        else if(buttons & KUI_SHELL_START) s->page=KUI_SHELL_ADVANCED;
        break;
    case KUI_SHELL_ADVANCED:
        s->advanced_selected=move_count(s->advanced_selected,buttons,6);
        if(buttons & KUI_SHELL_A) {
            if(s->advanced_selected<2) {
                s->page=KUI_SHELL_RIPPER;
                return s->advanced_selected?KUI_SHELL_RESUME:KUI_SHELL_VERIFY;
            }
            if(s->advanced_selected==3) {
                s->confirm_quick_resume=true; return KUI_SHELL_NONE;
            }
            if(s->advanced_selected==4 || s->advanced_selected==5) {
                s->browse_for_scan=s->advanced_selected==5;
                s->page=KUI_SHELL_DESTINATION;
                snprintf(s->browse_path,sizeof(s->browse_path),"%s",s->destination);
                return list_destination(s,true);
            }
            s->settings_return=KUI_SHELL_ADVANCED;
            s->draft=s->saved; s->page=KUI_SHELL_RIPPER_SETTINGS;
            return KUI_SHELL_LOAD_SETTINGS;
        }
        break;
    case KUI_SHELL_DESTINATION:
        if(buttons & KUI_SHELL_START) {
            s->page=s->browse_for_scan?KUI_SHELL_ADVANCED:KUI_SHELL_RIPPER;
            snprintf(s->browse_path,sizeof(s->browse_path),"%s",s->destination);
            s->destination_notice[0]=0;
        } else if((buttons & KUI_SHELL_X) && !s->browse_for_scan) {
            snprintf(s->keyboard,sizeof(s->keyboard),"%s",s->browse_path);
            snprintf(s->keyboard_original,sizeof(s->keyboard_original),"%s",s->browse_path);
            s->keyboard_selected=0; s->keyboard_upper=false;
            s->destination_notice[0]=0; s->page=KUI_SHELL_KEYBOARD;
        } else if(buttons & KUI_SHELL_Y) {
            if(s->browse_for_scan) {s->page=KUI_SHELL_CRC_SCAN;return KUI_SHELL_ADVANCED_CRC;}
            return save_destination(s,s->browse_path);
        }
        else if(buttons & KUI_SHELL_A) {
            if(s->browser_selected<s->listing.count) {
                const struct kui_destination_entry *entry=&s->listing.entries[s->browser_selected];
                char child[KUI_DEST_ROOT_CAP];
                if(!entry->disabled && kui_destination_join(child,s->browse_path,entry->name)) {
                    snprintf(s->browse_path,sizeof(s->browse_path),"%s",child);
                    return list_destination(s,true);
                }
                kui_shell_destination_error(s,"This folder cannot be selected. Choose another folder.");
            }
        } else if((buttons&(KUI_SHELL_LEFT|KUI_SHELL_RIGHT))==KUI_SHELL_LEFT && s->browser_page) {
            --s->browser_page; return list_destination(s,false);
        } else if((buttons&(KUI_SHELL_LEFT|KUI_SHELL_RIGHT))==KUI_SHELL_RIGHT &&
                  s->listing.has_more && s->browser_page<UINT_MAX/KUI_DEST_PAGE_SIZE-1) {
            ++s->browser_page; return list_destination(s,false);
        } else if(s->listing.count) {
            unsigned vertical=buttons&(KUI_SHELL_UP|KUI_SHELL_DOWN);
            if(vertical==KUI_SHELL_UP) s->browser_selected=s->browser_selected?
                s->browser_selected-1:s->listing.count-1;
            else if(vertical==KUI_SHELL_DOWN)
                s->browser_selected=(s->browser_selected+1)%s->listing.count;
        }
        break;
    case KUI_SHELL_KEYBOARD:
        return keyboard_input(s,buttons);
    case KUI_SHELL_RIPPER_SETTINGS:
        s->setting_selected = move_count(s->setting_selected, buttons,2);
        if((buttons & (KUI_SHELL_LEFT | KUI_SHELL_RIGHT)) == KUI_SHELL_LEFT ||
           (buttons & (KUI_SHELL_LEFT | KUI_SHELL_RIGHT)) == KUI_SHELL_RIGHT) {
            if(s->setting_selected == 0) {
                s->draft.crc_only = !s->draft.crc_only;
                if(!s->draft.crc_only) s->draft.end_readback = true;
            }
            if(s->setting_selected == 1 && s->draft.crc_only)
                s->draft.end_readback = !s->draft.end_readback;
        }
        if(buttons & KUI_SHELL_A) return KUI_SHELL_SAVE_SETTINGS;
        break;
    case KUI_SHELL_SETTINGS: {
        s->system_selected=move_count(s->system_selected,buttons,8);
        unsigned horizontal=buttons&(KUI_SHELL_LEFT|KUI_SHELL_RIGHT);
        if(horizontal==KUI_SHELL_LEFT || horizontal==KUI_SHELL_RIGHT) {
            if(s->system_selected==0) s->system_draft.video_mode=(s->system_draft.video_mode+
                (horizontal==KUI_SHELL_LEFT?KUI_VIDEO_MODE_COUNT-1:1))%KUI_VIDEO_MODE_COUNT;
            if(s->system_selected==1) s->system_draft.show_memory=!s->system_draft.show_memory;
            if(s->system_selected==2) s->system_draft.music_enabled=!s->system_draft.music_enabled;
            if(s->system_selected==3) {
                unsigned volume=s->system_draft.music_volume;
                s->system_draft.music_volume=horizontal==KUI_SHELL_LEFT?
                    (volume>5?volume-5:0):(volume<95?volume+5:100);
            }
        }
        if(horizontal==KUI_SHELL_LEFT || horizontal==KUI_SHELL_RIGHT) {
            if(s->system_selected==4) s->system_draft.startup_chime=!s->system_draft.startup_chime;
            if(s->system_selected==5) s->system_draft.startup_app=(s->system_draft.startup_app+
                (horizontal==KUI_SHELL_LEFT?KUI_STARTUP_APP_COUNT-1:1))%KUI_STARTUP_APP_COUNT;
        }
        if((buttons&KUI_SHELL_A) && s->system_selected==6) {
            s->page=KUI_SHELL_CLOCK;s->clock_valid=false;s->clock_selected=0;
            snprintf(s->clock_notice,sizeof(s->clock_notice),"Reading console clock...");
            return KUI_SHELL_CLOCK_READ;
        }
        if((buttons&KUI_SHELL_A) && s->system_selected==7) {
            s->confirm_defaults=true;return KUI_SHELL_NONE;
        }
        if(buttons&KUI_SHELL_A) return s->system_draft.video_mode!=s->system_saved.video_mode?
            KUI_SHELL_PREVIEW_VIDEO:KUI_SHELL_SAVE_SYSTEM;
        if((buttons&KUI_SHELL_X) && s->system_selected>=2 && s->system_selected<=3) return KUI_SHELL_MUSIC_NEXT;
        break;
    }
    case KUI_SHELL_CLOCK:
        edit_clock(s,buttons);
        if(buttons&KUI_SHELL_X) {s->clock_valid=false;return KUI_SHELL_CLOCK_READ;}
        if((buttons&KUI_SHELL_A) && s->clock_valid) s->confirm_clock=true;
        break;
    case KUI_SHELL_CRC_SCAN:
        if(buttons&KUI_SHELL_A) return KUI_SHELL_ADVANCED_CRC;
        break;
    case KUI_SHELL_VMU_RESTORE: {
        unsigned horizontal=buttons&(KUI_SHELL_LEFT|KUI_SHELL_RIGHT);
        if(horizontal==KUI_SHELL_LEFT || horizontal==KUI_SHELL_RIGHT) {
            s->vmu_slot=(s->vmu_slot+(horizontal==KUI_SHELL_LEFT?7:1))%8;
            s->restore_path[0]=0;
        }
        s->backup_selected=move_count(s->backup_selected,buttons,s->backups.count);
        if(buttons&KUI_SHELL_X) return list_backups(s,false);
        if(buttons&KUI_SHELL_START) {
            if(s->backup_page<UINT_MAX/KUI_VMU_ROWS-1 &&
               (s->backup_page+1)*KUI_VMU_ROWS<s->backups.total) ++s->backup_page;
            else s->backup_page=0;
            return list_backups(s,false);
        }
        if((buttons&KUI_SHELL_A) && s->backup_selected<s->backups.count) {
            const struct kui_vmu_backup_entry *e=&s->backups.entries[s->backup_selected];
            if(e->path[0]) {
                snprintf(s->restore_path,sizeof(s->restore_path),"%s",e->path);
                snprintf(s->restore_name,sizeof(s->restore_name),"%s",e->name);
                s->restore_bytes=e->bytes;return KUI_SHELL_VMU_RESTORE_PREVIEW;
            }
        }
        break;
    }
    case KUI_SHELL_GD_PLAY:
        if(buttons & KUI_SHELL_A) s->confirm_gd_boot=true;
        break;
    case KUI_SHELL_MUSIC:
        if(buttons & KUI_SHELL_START) { s->page=KUI_SHELL_HOME; break; }
        if(buttons & KUI_SHELL_Y) return KUI_SHELL_MUSIC_STOP;
        if(buttons & KUI_SHELL_X) return list_music(s,false);
        if(buttons & KUI_SHELL_A) {
            if(s->music_selected<s->music_listing.count) {
                const struct kui_music_player_entry *entry=&s->music_listing.entries[s->music_selected];
                char child[KUI_DEST_ROOT_CAP];
                if(!entry->disabled && kui_destination_join(child,s->music_path,entry->name)) {
                    if(entry->directory) {
                        snprintf(s->music_path,sizeof(s->music_path),"%s",child);
                        return list_music(s,true);
                    }
                    snprintf(s->music_selected_path,sizeof(s->music_selected_path),"%s",child);
                    return KUI_SHELL_MUSIC_PLAY;
                }
                snprintf(s->music_listing.message,sizeof(s->music_listing.message),
                    "This path cannot be opened. Choose another item.");
            }
        } else if((buttons&(KUI_SHELL_LEFT|KUI_SHELL_RIGHT))==KUI_SHELL_LEFT && s->music_page) {
            --s->music_page; return list_music(s,false);
        } else if((buttons&(KUI_SHELL_LEFT|KUI_SHELL_RIGHT))==KUI_SHELL_RIGHT &&
                  s->music_listing.has_more && s->music_page<UINT_MAX/KUI_MUSIC_PLAYER_ROWS-1) {
            ++s->music_page; return list_music(s,false);
        } else s->music_selected=move_count(s->music_selected,buttons,s->music_listing.count);
        break;
    case KUI_SHELL_MEMORY:
        if(buttons&KUI_SHELL_A) return KUI_SHELL_MEMORY_TEST;
        break;
    case KUI_SHELL_NETWORK:
        if(buttons&KUI_SHELL_A) return KUI_SHELL_NETWORK_TEST;
        break;
    case KUI_SHELL_VMU: {
        if(buttons&KUI_SHELL_R) {s->page=KUI_SHELL_VMU_RESTORE;return list_backups(s,true);}
        unsigned horizontal=buttons&(KUI_SHELL_LEFT|KUI_SHELL_RIGHT);
        if(horizontal==KUI_SHELL_LEFT || horizontal==KUI_SHELL_RIGHT) {
            s->vmu_slot=(s->vmu_slot+(horizontal==KUI_SHELL_LEFT?7:1))%8;
            s->vmu_page=0; s->vmu_selected=0; memset(&s->vmu,0,sizeof(s->vmu));
            return KUI_SHELL_VMU_LIST;
        }
        s->vmu_selected=move_count(s->vmu_selected,buttons,s->vmu.count);
        if(buttons&KUI_SHELL_A) return KUI_SHELL_VMU_LIST;
        if((buttons&KUI_SHELL_X) && s->vmu.present && s->vmu.count)
            return KUI_SHELL_VMU_BACKUP;
        if((buttons&KUI_SHELL_Y) && s->vmu.present && s->vmu.total)
            return KUI_SHELL_VMU_BACKUP_ALL;
        if(buttons&KUI_SHELL_START) {
            if(s->vmu_page<UINT_MAX/KUI_VMU_ROWS-1 &&
               (s->vmu_page+1)*KUI_VMU_ROWS<s->vmu.total) ++s->vmu_page;
            else s->vmu_page=0;
            s->vmu_selected=0; memset(&s->vmu,0,sizeof(s->vmu));
            return KUI_SHELL_VMU_LIST;
        }
        break;
    }
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
