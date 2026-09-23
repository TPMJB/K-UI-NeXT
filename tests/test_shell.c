/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static struct kui_shell s;
static const struct kui_settings defaults={true,false,false};
static void reset(enum kui_shell_page page) {
    kui_shell_init(&s,&defaults); s.page=page;
}
static enum kui_shell_action press(unsigned buttons, bool busy) {
    return kui_shell_input(&s,buttons,busy);
}
static void launcher_and_confirmation(void) {
    reset(KUI_SHELL_HOME);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE);
    assert(s.page==KUI_SHELL_HOME);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(s.page==KUI_SHELL_RIPPER && !s.confirm_new);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_R,false)==KUI_SHELL_NONE);
    assert(s.confirm_new);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE);
    assert(!s.confirm_new && s.page==KUI_SHELL_RIPPER);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_MSTATS && s.confirm_new);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NEW_DUMP && !s.confirm_new);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.home_selected==2);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DIAGNOSTICS);
    press(KUI_SHELL_B,false);
    press(KUI_SHELL_UP,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_LOAD_SETTINGS);
    assert(s.page==KUI_SHELL_SETTINGS);
}
static void operation_lock_and_stop(void) {
    const unsigned launch=KUI_SHELL_A|KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_R;
    for(unsigned page=0;page<4;page++) {
        reset((enum kui_shell_page)page);
        assert(press(launch,true)==KUI_SHELL_NONE && s.page==page);
        assert(press(launch|KUI_SHELL_L|KUI_SHELL_B,true)==KUI_SHELL_STOP);
        assert(press(KUI_SHELL_L,true)==KUI_SHELL_MSTATS);
        assert(s.page==page);
    }
    reset(KUI_SHELL_RIPPER); s.confirm_new=true;
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP && !s.confirm_new);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_RESUME);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VERIFY);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_NONE);
}
static void settings_transaction(void) {
    reset(KUI_SHELL_SETTINGS);
    press(KUI_SHELL_RIGHT,false);
    assert(!s.draft.crc_only && s.saved.crc_only && kui_shell_settings_dirty(&s));
    press(KUI_SHELL_DOWN,false); press(KUI_SHELL_LEFT,false);
    /* SHA's existing schema requires saved-file verification; the visible
     * choice cannot say OFF while the engine necessarily reads everything. */
    assert(s.draft.end_readback && !s.saved.end_readback);
    press(KUI_SHELL_DOWN,false); press(KUI_SHELL_RIGHT,false);
    assert(s.draft.show_memory && !s.saved.show_memory);
    struct kui_settings changed=s.draft;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_SETTINGS);
    assert(kui_shell_settings_dirty(&s)); /* Failure leaves the draft for retry. */
    press(KUI_SHELL_LEFT|KUI_SHELL_UP,true);
    assert(s.setting_selected==2 && s.draft.show_memory);
    kui_shell_set_preferences(&s,&changed);
    assert(!kui_shell_settings_dirty(&s));
    press(KUI_SHELL_LEFT,false); assert(!s.draft.show_memory);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,false)==KUI_SHELL_DISCARD_SETTINGS);
    assert(s.page==KUI_SHELL_HOME && s.draft.show_memory && !kui_shell_settings_dirty(&s));
    reset(KUI_SHELL_SETTINGS);
    press(KUI_SHELL_LEFT|KUI_SHELL_RIGHT,false);
    assert(!kui_shell_settings_dirty(&s));
    press(KUI_SHELL_DOWN,false); press(KUI_SHELL_RIGHT,false);
    assert(s.draft.end_readback);
    press(KUI_SHELL_LEFT,false); assert(!s.draft.end_readback);
    const struct kui_settings inconsistent={false,false,true};
    kui_shell_set_preferences(&s,&inconsistent);
    assert(s.saved.end_readback && s.draft.end_readback);
    kui_shell_init(&s,NULL); assert(s.saved.show_memory);
}
static void diagnostics(void) {
    reset(KUI_SHELL_DIAGNOSTICS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DISC_PROBE);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_STORAGE_PROBE);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_SAVE_LOG);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_BENCH);
    press(KUI_SHELL_UP,true); assert(s.scroll==3);
    press(KUI_SHELL_UP,false); assert(s.scroll==6);
    press(KUI_SHELL_DOWN,false); assert(s.scroll==3);
    press(KUI_SHELL_START,true); assert(s.scroll==0);
    press(KUI_SHELL_DOWN,false); assert(s.scroll==0);
    s.scroll=UINT_MAX; press(KUI_SHELL_UP,false); assert(s.scroll==UINT_MAX);
    for(unsigned page=0;page<3;page++) {
        reset((enum kui_shell_page)page); press(KUI_SHELL_UP,false);
        assert(s.scroll==0);
    }
}
static uint16_t pixels[640*480+2];
static char drawn[8192];
static size_t drawn_size;
static void observe(void *ctx,unsigned x,unsigned y,uint16_t color,const char *value) {
    (void)ctx; (void)color;
    assert(x>=32 && x+strlen(value)*8<=608 && y>=32 && y+16<=448);
    for(size_t i=0;value[i];i++) assert(value[i]>=32 && value[i]<=126);
    size_t n=strlen(value);
    assert(drawn_size+n+2<sizeof(drawn));
    memcpy(drawn+drawn_size,value,n); drawn_size+=n;
    drawn[drawn_size++]='\n'; drawn[drawn_size]=0;
}
static void render(struct kui_shell_view *v) {
    pixels[0]=0x1234; pixels[640*480+1]=0xabcd;
    drawn_size=0; drawn[0]=0;
    kui_shell_draw(pixels+1,&s,v,observe,NULL);
    assert(pixels[0]==0x1234 && pixels[640*480+1]==0xabcd);
}
static void rendering_semantics(void) {
    const char *logs[]={"A very long diagnostic line deliberately exceeding safe frame margins 0123456789012345678901234567890"};
    struct kui_shell_view v={.build="0123456789abcdef",.log_lines=logs,.log_count=1,
        .total_log_lines=1,.done=UINT64_MAX-1,.total=UINT64_MAX};
    for(unsigned page=0;page<4;page++) {
        reset((enum kui_shell_page)page); render(&v);
    }
    assert(strstr(drawn,"...")); /* Clipped log text is visibly incomplete. */
    reset(KUI_SHELL_RIPPER);
    v.phase=4; v.outcome=KUI_SHELL_OUTCOME_COMPLETE;
    render(&v);
    assert(strstr(drawn,"Completed"));
    assert(strstr(drawn,"Saved bytes not fully reread"));
    assert(!strstr(drawn,"reread and verified"));
    v.saved_verified=true; render(&v);
    assert(strstr(drawn,"Saved bytes reread and verified"));
    s.confirm_new=true; render(&v);
    assert(strstr(drawn,"START A NEW DUMP?") && strstr(drawn,"B Cancel"));
    s.confirm_new=false; v.busy=true; v.saving=true; v.cancel_requested=true;
    render(&v); assert(strstr(drawn,"CANCELLING SAVE"));
    v.busy=false; v.saving=false; v.cancel_requested=false;
    v.outcome=KUI_SHELL_OUTCOME_STOPPED; v.job_dir=NULL;
    render(&v); assert(strstr(drawn,"Stopped") && !strstr(drawn,"partial dump kept"));
    v.job_dir="/KUI/dumps/example"; render(&v);
    assert(strstr(drawn,"partial dump kept"));
    reset(KUI_SHELL_SETTINGS); press(KUI_SHELL_RIGHT,false);
    v.settings_notice="Save failed. Review the diagnostic log.";
    render(&v);
    assert(strstr(drawn,"Unsaved changes") && strstr(drawn,"Save failed"));
    assert(strstr(drawn,"ON (SHA required)"));
}
int main(void) {
    launcher_and_confirmation(); operation_lock_and_stop(); settings_transaction();
    diagnostics(); rendering_semantics();
    puts("PASS shell: Stop priority, navigation, confirmation, busy lock, settings, log controls, safe rendering");
    return 0;
}
