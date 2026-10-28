/* SPDX-License-Identifier: GPL-3.0-only */
static bool repair_catalogue_cancel(void *ctx) {return cancelled(ctx);}
static bool repair_catalogue(struct recovery_copy *s) {
    if(cancelled(s))return false;
    struct kui_known_track tracks[99];
    for(unsigned i=0;i<s->source.plan.count;++i) {
        const struct kui_capture_track *t=&s->source.plan.tracks[i];
        tracks[i]=(struct kui_known_track){t->number,t->control==4,
            (uint64_t)(t->end-t->start)*KUI_RAW_BYTES,s->crc[i]};
    }
    kui_known_check("0:/KUI/redump.db","0:/KUI/tosec.db",tracks,s->source.plan.count,
        &s->repair->catalogue,repair_catalogue_cancel,s);
    s->repair->catalogue_checked=true;
    if(cancelled(s) || s->repair->catalogue.result==KUI_KNOWN_CANCELLED)return false;
    /* No database/no match/error are explicit reference outcomes. They never
     * turn saved-byte readback into a reference match or discard a good repair. */
    logger(s)("Repair reference (%s): %s",s->repair->catalogue.catalog,
        kui_known_text(s->repair->catalogue.result));
    return line(s,"REFERENCE catalogue=%s result=%d grade=%s\nREFERENCE_TITLE %s\n",
        s->repair->catalogue.catalog,(int)s->repair->catalogue.result,
        kui_known_text(s->repair->catalogue.result),s->repair->catalogue.name) && sync_file(s,&s->log) && repair_catalogue_report(s);
}
