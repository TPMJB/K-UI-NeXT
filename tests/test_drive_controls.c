/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/drive_controls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct backend {
    struct kui_drive_mode mode;
    unsigned queries,sets,fail_query,fail_set,poison_set;
    bool unsafe,ignore_speed,change_flags;
};
static const struct kui_drive_mode original={6,12345,0x19,7};
static bool same(struct kui_drive_mode a,struct kui_drive_mode b) {
    return a.speed==b.speed && a.standby==b.standby &&
        a.read_flags==b.read_flags && a.read_retry==b.read_retry;
}
static bool safe(void *ctx) {return !((struct backend *)ctx)->unsafe;}
static bool query(void *ctx,struct kui_drive_mode *out) {
    struct backend *b=ctx;
    assert(!b->unsafe);++b->queries;
    if(b->queries==b->fail_query)return false;
    *out=b->mode;return true;
}
static bool set(void *ctx,const struct kui_drive_mode *mode) {
    struct backend *b=ctx;
    assert(!b->unsafe);++b->sets;
    uint32_t speed=b->mode.speed;
    b->mode=*mode;
    if(b->ignore_speed)b->mode.speed=speed;
    if(b->change_flags)b->mode.read_flags^=1;
    if(b->sets==b->poison_set) {b->unsafe=true;return false;}
    return b->sets!=b->fail_set;
}
static struct kui_drive_control_ops ops(struct backend *b) {
    return (struct kui_drive_control_ops){b,safe,query,set};
}
static void all_choices(void) {
    static const unsigned retries[]={0,0,1,2,4,8};
    for(unsigned speed=0;speed<KUI_DRIVE_SPEED_CHOICES;++speed)
        for(unsigned retry=0;retry<KUI_DRIVE_RETRY_CHOICES;++retry) {
            struct backend b={.mode=original};struct kui_drive_control_ops o=ops(&b);
            struct kui_drive_controls s={0};struct kui_drive_options option={speed,retry};
            assert(kui_drive_controls_begin(&s,&o,&option)==KUI_DRIVE_CONTROL_OK);
            struct kui_drive_mode desired=original;
            if(speed)desired.speed=speed-1;
            if(retry)desired.read_retry=retries[retry];
            assert(same(b.mode,desired));
            if(!speed && !retry) {
                assert(!b.queries && !b.sets);
                assert(kui_drive_controls_reapply(&s,&o)==KUI_DRIVE_CONTROL_OK);
                assert(!b.queries && !b.sets);
            } else {
                assert(b.queries==2 && b.sets==1);
                /* INIT may reset every field. Reapplication retains the entry
                 * snapshot rather than adopting these reset defaults. */
                b.mode=(struct kui_drive_mode){0,0,0,8};
                assert(kui_drive_controls_reapply(&s,&o)==KUI_DRIVE_CONTROL_OK);
                assert(same(b.mode,desired) && same(s.original,original));
                unsigned sets=b.sets,queries=b.queries;
                assert(kui_drive_controls_begin(&s,&o,&option)==KUI_DRIVE_CONTROL_INVALID);
                assert(b.sets==sets && b.queries==queries && same(s.original,original));
            }
            assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_OK);
            assert(same(b.mode,original) && !s.active && !s.modified);
            unsigned sets=b.sets,queries=b.queries;
            assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_OK);
            assert(b.sets==sets && b.queries==queries);
        }
}
static void errors(void) {
    struct kui_drive_options option={4,2};
    for(unsigned which=0;which<8;++which) {
        struct backend b={.mode=original};struct kui_drive_control_ops o=ops(&b);
        struct kui_drive_controls s={0};
        enum kui_drive_control_result expected;
        switch(which) {
        case 0:b.unsafe=true;expected=KUI_DRIVE_CONTROL_UNSAFE;break;
        case 1:b.fail_query=1;expected=KUI_DRIVE_CONTROL_QUERY_FAILED;break;
        case 2:b.mode.standby=65536;expected=KUI_DRIVE_CONTROL_QUERY_FAILED;break;
        case 3:b.fail_set=1;expected=KUI_DRIVE_CONTROL_SET_FAILED;break;
        case 4:b.fail_query=2;expected=KUI_DRIVE_CONTROL_QUERY_FAILED;break;
        case 5:b.ignore_speed=true;expected=KUI_DRIVE_CONTROL_MISMATCH;break;
        case 6:b.change_flags=true;expected=KUI_DRIVE_CONTROL_MISMATCH;break;
        default:b.poison_set=1;expected=KUI_DRIVE_CONTROL_SET_FAILED;break;
        }
        assert(kui_drive_controls_begin(&s,&o,&option)==expected);
        if(which<3)assert(!b.sets && !s.active);
        b.ignore_speed=b.change_flags=false;
        unsigned before=b.sets;
        enum kui_drive_control_result end=kui_drive_controls_end(&s,&o);
        if(which==7)assert(end==KUI_DRIVE_CONTROL_UNSAFE && b.sets==before);
        else {
            assert(end==KUI_DRIVE_CONTROL_OK);
            if(which>=3)assert(same(b.mode,original) && b.sets==before+1);
        }
        assert(!s.active && !s.modified);
    }
    struct backend b={.mode=original};struct kui_drive_control_ops o=ops(&b);
    struct kui_drive_controls s={0};
    assert(kui_drive_controls_begin(&s,&o,&option)==KUI_DRIVE_CONTROL_OK);
    b.fail_set=2;
    assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_SET_FAILED);
    assert(!s.active && !s.modified);
    option.speed=KUI_DRIVE_SPEED_CHOICES;
    assert(!kui_drive_options_valid(&option));
    unsigned before=b.sets;
    assert(kui_drive_controls_begin(&s,&o,&option)==KUI_DRIVE_CONTROL_INVALID && b.sets==before);
    assert(!strcmp(kui_drive_speed_name(8),"12x requested"));
    assert(!strcmp(kui_drive_retry_name(1),"0"));
    assert(!strcmp(kui_drive_speed_name(99),"Invalid"));
}
int main(void) {
    all_choices();errors();
    puts("Drive controls: 54 setting combinations, reapply, restore and failure ownership passed");
    return 0;
}
