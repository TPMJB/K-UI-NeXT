/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/drive_controls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct backend {
    struct kui_drive_mode mode;
    unsigned queries,sets,fail_query,fail_set,poison_set,poison_query;
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
    if(b->queries==b->poison_query) {b->unsafe=true;return false;}
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
static struct kui_drive_mode trial_expected(struct kui_drive_options option) {
    static const unsigned retries[]={0,0,1,2,4,8};
    struct kui_drive_mode expected=original;
    if(option.speed)expected.speed=option.speed-1;
    if(option.retries)expected.read_retry=retries[option.retries];
    return expected;
}
static void trial_switches(void) {
    struct backend b={.mode=original};struct kui_drive_control_ops o=ops(&b);
    struct kui_drive_controls s={0};
    assert(kui_drive_controls_trial_begin(&s,&o)==KUI_DRIVE_CONTROL_OK);
    assert(s.active && s.trial && s.snapshot_valid && s.verified && !s.modified);
    assert(b.queries==1 && b.sets==0 && same(s.original,original));
    assert(same(s.requested,original));
    unsigned queries=b.queries,sets=b.sets;
    assert(kui_drive_controls_trial_begin(&s,&o)==KUI_DRIVE_CONTROL_INVALID);
    assert(b.queries==queries && b.sets==sets && same(s.original,original));
    /* Even a baseline-only trial needs reapplication after INIT. It has a
     * captured original tuple although no override has been selected yet. */
    b.mode=(struct kui_drive_mode){0,0,0,8};
    assert(kui_drive_controls_reapply(&s,&o)==KUI_DRIVE_CONTROL_OK);
    assert(same(b.mode,original) && same(s.original,original) && s.verified);

    /* Every partially unchanged profile must derive from the entry snapshot,
     * including after a very different preceding profile. Two opposite orders
     * exercise unchanged fields after both higher and lower selections. */
    for(unsigned order=0;order<2;++order)for(unsigned n=0;n<54;++n) {
        unsigned choice=order?53-n:n;
        struct kui_drive_options option={choice/KUI_DRIVE_RETRY_CHOICES,
            choice%KUI_DRIVE_RETRY_CHOICES};
        struct kui_drive_mode expected=trial_expected(option);
        assert(kui_drive_controls_select(&s,&o,&option)==KUI_DRIVE_CONTROL_OK);
        assert(same(b.mode,expected) && same(s.requested,expected));
        assert(same(s.original,original) && s.snapshot_valid && s.verified);
    }
    const struct kui_drive_options fast={8,1},unchanged={0,0};
    assert(kui_drive_controls_select(&s,&o,&fast)==KUI_DRIVE_CONTROL_OK);
    assert(b.mode.speed==7 && b.mode.read_retry==0);
    assert(kui_drive_controls_select(&s,&o,&unchanged)==KUI_DRIVE_CONTROL_OK);
    assert(same(b.mode,original));
    sets=b.sets;queries=b.queries;
    assert(kui_drive_controls_select(&s,&o,&unchanged)==KUI_DRIVE_CONTROL_OK);
    assert(b.sets==sets && b.queries==queries); /* verified tuple needs no command */

    /* Recovery INIT may reset the drive's mode. Reapply must restore the last
     * profile while preserving the session's original restoration authority. */
    const struct kui_drive_options slow={3,2};
    assert(kui_drive_controls_select(&s,&o,&slow)==KUI_DRIVE_CONTROL_OK);
    struct kui_drive_mode expected=trial_expected(slow);
    b.mode=(struct kui_drive_mode){0,0,0,8};
    assert(kui_drive_controls_reapply(&s,&o)==KUI_DRIVE_CONTROL_OK);
    assert(same(b.mode,expected) && same(s.original,original));
    assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_OK);
    assert(same(b.mode,original));
    assert(!s.active && !s.modified && !s.snapshot_valid && !s.trial && !s.verified);
    queries=b.queries;sets=b.sets;
    assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_OK);
    assert(b.queries==queries && b.sets==sets);
}
static void trial_rejection_and_rollback(void) {
    const struct kui_drive_options first={3,2},second={4,3},unchanged={0,0};
    for(unsigned which=0;which<3;++which) {
        struct backend b={.mode=original};struct kui_drive_control_ops o=ops(&b);
        struct kui_drive_controls s={0};
        assert(kui_drive_controls_trial_begin(&s,&o)==KUI_DRIVE_CONTROL_OK);
        assert(kui_drive_controls_select(&s,&o,&first)==KUI_DRIVE_CONTROL_OK);
        struct kui_drive_mode previous=trial_expected(first);
        enum kui_drive_control_result expected;
        if(which==0) {b.fail_set=b.sets+1;expected=KUI_DRIVE_CONTROL_SET_FAILED;}
        else if(which==1) {b.fail_query=b.queries+1;expected=KUI_DRIVE_CONTROL_QUERY_FAILED;}
        else {b.ignore_speed=true;expected=KUI_DRIVE_CONTROL_MISMATCH;}
        unsigned before=b.sets;
        assert(kui_drive_controls_select(&s,&o,&second)==expected);
        assert(b.sets==before+2 && same(b.mode,previous));
        assert(s.active && s.trial && s.snapshot_valid && s.verified);
        assert(same(s.original,original) && same(s.requested,previous));
        b.ignore_speed=false;
        /* A skipped profile must leave subsequent selection usable. */
        assert(kui_drive_controls_select(&s,&o,&second)==KUI_DRIVE_CONTROL_OK);
        assert(same(b.mode,trial_expected(second)));
        assert(kui_drive_controls_select(&s,&o,&unchanged)==KUI_DRIVE_CONTROL_OK);
        assert(same(b.mode,original));
        assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_OK);
        assert(same(b.mode,original));
    }
}
static void trial_unsafe_rollback(void) {
    const struct kui_drive_options option={4,2};
    for(unsigned which=0;which<4;++which) {
        struct backend b={.mode=original};struct kui_drive_control_ops o=ops(&b);
        struct kui_drive_controls s={0};
        assert(kui_drive_controls_trial_begin(&s,&o)==KUI_DRIVE_CONTROL_OK);
        if(which==0)b.poison_set=1;
        else if(which==1)b.poison_query=2;
        else if(which==2)b.change_flags=true; /* repeat mismatch during rollback */
        else {b.fail_query=2;b.fail_set=2;} /* rollback SET itself rejected */
        assert(kui_drive_controls_select(&s,&o,&option)==KUI_DRIVE_CONTROL_UNSAFE);
        assert(s.active && s.snapshot_valid && !s.verified && same(s.original,original));
        unsigned sets=b.sets,queries=b.queries;
        assert(kui_drive_controls_select(&s,&o,&option)!=KUI_DRIVE_CONTROL_OK);
        assert(b.sets==sets && b.queries==queries);
        if(b.unsafe) {
            assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_UNSAFE);
            assert(b.sets==sets && b.queries==queries);
        } else {
            b.change_flags=false;
            assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_OK);
            assert(same(b.mode,original));
        }
        assert(!s.active && !s.snapshot_valid && !s.trial && !s.verified);
    }
}
static void trial_admission(void) {
    const struct kui_drive_options option={3,2},defaults={0,0};
    struct backend b={.mode=original};struct kui_drive_control_ops o=ops(&b);
    struct kui_drive_controls s={0};
    assert(kui_drive_controls_select(&s,&o,&option)==KUI_DRIVE_CONTROL_INVALID);
    assert(!b.sets && !b.queries);
    assert(kui_drive_controls_begin(&s,&o,&defaults)==KUI_DRIVE_CONTROL_OK);
    assert(kui_drive_controls_select(&s,&o,&option)==KUI_DRIVE_CONTROL_INVALID);
    assert(!b.sets && !b.queries); /* the normal default remains command-free */
    assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_OK);
    for(unsigned which=0;which<3;++which) {
        b=(struct backend){.mode=original};s=(struct kui_drive_controls){0};
        enum kui_drive_control_result expected;
        if(which==0) {b.unsafe=true;expected=KUI_DRIVE_CONTROL_UNSAFE;}
        else if(which==1) {b.fail_query=1;expected=KUI_DRIVE_CONTROL_QUERY_FAILED;}
        else {b.mode.read_flags=256;expected=KUI_DRIVE_CONTROL_QUERY_FAILED;}
        assert(kui_drive_controls_trial_begin(&s,&o)==expected);
        assert(!s.active && !s.snapshot_valid && !b.sets);
    }
    b=(struct backend){.mode=original};s=(struct kui_drive_controls){0};
    assert(kui_drive_controls_trial_begin(&s,&o)==KUI_DRIVE_CONTROL_OK);
    const struct kui_drive_options invalid={KUI_DRIVE_SPEED_CHOICES,0};
    unsigned sets=b.sets,queries=b.queries;
    assert(kui_drive_controls_select(&s,&o,&invalid)==KUI_DRIVE_CONTROL_INVALID);
    assert(b.sets==sets && b.queries==queries && s.verified);
    assert(kui_drive_controls_end(&s,&o)==KUI_DRIVE_CONTROL_OK);
    assert(b.sets==sets && b.queries==queries); /* query-only session changed nothing */
}
int main(void) {
    all_choices();errors();trial_switches();trial_rejection_and_rollback();
    trial_unsafe_rollback();trial_admission();
    puts("Drive controls: 54 setting combinations, 108 trial switches, rollback, original restoration and failure ownership passed");
    return 0;
}
