/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/known_dumps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* --- line sources ----------------------------------------------------------- */
struct text { const char *p; };
static bool text_line(void *ctx, char *line, size_t cap) {
    struct text *t = ctx;
    size_t n = 0;
    if(!*t->p) return false;
    while(*t->p && *t->p != '\n') { if(n + 1 < cap) line[n++] = *t->p; ++t->p; }
    if(*t->p == '\n') ++t->p;
    line[n] = 0;
    return true;
}
static bool file_line(void *ctx, char *line, size_t cap) {
    return fgets(line, (int)cap, (FILE *)ctx) != NULL;   /* keeps the newline: the matcher must cope */
}
static enum kui_known_result search_text(const char *db, const struct kui_known_track *t, unsigned n, char *name) {
    struct text src = {db};
    return kui_known_search(text_line, &src, t, n, name, 192, NULL, NULL);
}
static enum kui_known_result search_db(const char *path, const struct kui_known_track *t, unsigned n, char *name) {
    FILE *f = fopen(path, "r");
    assert(f);
    enum kui_known_result r = kui_known_search(file_line, f, t, n, name, 192, NULL, NULL);
    fclose(f);
    return r;
}
static bool never(void *ctx) { (void)ctx; return false; }
static unsigned polls;
static bool cancel_after_a_while(void *ctx) { (void)ctx; return ++polls >= 3; }

#define H "DREAMSHELL_REDUMP_CRC_V1\n"

int main(void) {
    char name[192];
    /* A disc: two data tracks and an audio track between them, like a real GD-ROM. */
    const struct kui_known_track disc[3] = {
        {1, true, 1000, 0xaaaa0001}, {2, false, 2000, 0xbbbb0002}, {3, true, 3000, 0xcccc0003}};

    /* --- the grade ladder ------------------------------------------------- */
    assert(search_text(H "G\t3\tFull Game\nT\t1\t1000\taaaa0001\nT\t2\t2000\tbbbb0002\nT\t3\t3000\tcccc0003\nE\n",
                       disc, 3, name) == KUI_KNOWN_FULL_MATCH);
    assert(!strcmp(name, "Full Game"));
    /* The audio track differs: still every DATA track matches, but not FULL. */
    assert(search_text(H "G\t3\tAudio Differs\nT\t1\t1000\taaaa0001\nT\t2\t2000\tdeadbeef\nT\t3\t3000\tcccc0003\nE\n",
                       disc, 3, name) == KUI_KNOWN_DATA_MATCH);
    /* An entry that lists only the data tracks (fewer than the capture has). */
    assert(search_text(H "G\t2\tData Only\nT\t1\t1000\taaaa0001\nT\t3\t3000\tcccc0003\nE\n",
                       disc, 3, name) == KUI_KNOWN_IDENTIFIED);
    /* The bundled Redump shape: ONE identifying data track. */
    assert(search_text(H "G\t1\tOne Track\nT\t3\t3000\tcccc0003\nE\n", disc, 3, name) == KUI_KNOWN_IDENTIFIED);
    /* One of two data tracks right, the other wrong. */
    assert(search_text(H "G\t3\tHalf\nT\t1\t1000\taaaa0001\nT\t2\t2000\tbbbb0002\nT\t3\t3000\t00000000\nE\n",
                       disc, 3, name) == KUI_KNOWN_PARTIAL);
    /* Right CRC, wrong size, and right size, wrong CRC: neither is a match. */
    assert(search_text(H "G\t1\tSize\nT\t3\t3001\tcccc0003\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    assert(search_text(H "G\t1\tCrc\nT\t3\t3000\tcccc0004\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    assert(!name[0]);   /* no match, no name */
    /* The audio track alone matching is not enough to identify anything. */
    assert(search_text(H "G\t1\tAudio Only\nT\t2\t2000\tbbbb0002\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);

    /* The best of several entries wins, whatever the order. */
    const char *two = H "G\t1\tWeaker\nT\t3\t3000\tcccc0003\nE\n"
                        "G\t3\tStronger\nT\t1\t1000\taaaa0001\nT\t2\t2000\tbbbb0002\nT\t3\t3000\tcccc0003\nE\n";
    assert(search_text(two, disc, 3, name) == KUI_KNOWN_FULL_MATCH && !strcmp(name, "Stronger"));
    const char *swapped = H "G\t3\tStronger\nT\t1\t1000\taaaa0001\nT\t2\t2000\tbbbb0002\nT\t3\t3000\tcccc0003\nE\n"
                            "G\t1\tWeaker\nT\t3\t3000\tcccc0003\nE\n";
    assert(search_text(swapped, disc, 3, name) == KUI_KNOWN_FULL_MATCH && !strcmp(name, "Stronger"));
    /* Same grade: the first entry keeps it (a tie does not replace). */
    const char *tie = H "G\t1\tFirst\nT\t3\t3000\tcccc0003\nE\nG\t1\tSecond\nT\t3\t3000\tcccc0003\nE\n";
    assert(search_text(tie, disc, 3, name) == KUI_KNOWN_IDENTIFIED && !strcmp(name, "First"));

    /* --- a disc with no data tracks can never be identified by them ----------- */
    const struct kui_known_track cd[1] = {{1, false, 2000, 0xbbbb0002}};
    assert(search_text(H "G\t1\tAudio CD\nT\t1\t2000\tbbbb0002\nE\n", cd, 1, name) == KUI_KNOWN_FULL_MATCH);

    /* --- robustness: bad input is skipped, never guessed at ------------------ */
    assert(search_text("NOT A CATALOGUE\nG\t1\tX\nE\n", disc, 3, name) == KUI_KNOWN_ERROR);   /* wrong header */
    assert(search_text("", disc, 3, name) == KUI_KNOWN_ERROR);                                  /* empty */
    assert(search_text(H, disc, 3, name) == KUI_KNOWN_NO_MATCH);                                /* header only */
    assert(search_text(H, NULL, 3, name) == KUI_KNOWN_ERROR);
    assert(search_text(H, disc, 0, name) == KUI_KNOWN_ERROR);
    assert(search_text(H, disc, 100, name) == KUI_KNOWN_ERROR);
    /* Comments, blank lines, CRLF line ends, and a last entry with no E all work. */
    assert(search_text("DREAMSHELL_REDUMP_CRC_V1\r\n# a comment\r\n\r\nG\t1\tCRLF\r\nT\t3\t3000\tcccc0003\r\nE\r\n",
                       disc, 3, name) == KUI_KNOWN_IDENTIFIED && !strcmp(name, "CRLF"));
    assert(search_text(H "G\t1\tNo Terminator\nT\t3\t3000\tcccc0003\n", disc, 3, name) == KUI_KNOWN_IDENTIFIED);
    /* A malformed entry header discards that entry (and only that entry). */
    assert(search_text(H "G\tx\tBad\nT\t3\t3000\tcccc0003\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    assert(search_text(H "G\t0\tZero\nT\t3\t3000\tcccc0003\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    assert(search_text(H "G\t100\tHuge\nT\t3\t3000\tcccc0003\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    assert(search_text(H "G\t1\tBad\nT\t3\t3000\tcccc0003\nE\nG\t1\tGood\nT\t3\t3000\tcccc0003\nE\n",
                       disc, 3, name) == KUI_KNOWN_IDENTIFIED);
    /* A malformed track line is skipped; the entry survives without it. */
    assert(search_text(H "G\t2\tSkip\nT\t1\tzz\taaaa0001\nT\t3\t3000\tcccc0003\nE\n", disc, 3, name) == KUI_KNOWN_IDENTIFIED);
    assert(search_text(H "G\t1\tTrailing\nT\t3\t3000\tcccc0003 junk\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    assert(search_text(H "G\t1\tLong Crc\nT\t3\t3000\t1cccc0003\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    /* A repeated track number, or one out of range, poisons the entry. */
    assert(search_text(H "G\t2\tDup\nT\t3\t3000\tcccc0003\nT\t3\t3000\tcccc0003\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    assert(search_text(H "G\t1\tRange\nT\t100\t3000\tcccc0003\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    assert(search_text(H "G\t1\tRange\nT\t0\t3000\tcccc0003\nE\n", disc, 3, name) == KUI_KNOWN_NO_MATCH);
    /* Upper-case hex is accepted. */
    assert(search_text(H "G\t1\tUpper\nT\t3\t3000\tCCCC0003\nE\n", disc, 3, name) == KUI_KNOWN_IDENTIFIED);
    /* A name longer than the buffer is truncated, terminated, and not an overflow. */
    {
        char db[1024] = H "G\t1\t", *p = db + strlen(db);
        memset(p, 'N', 400); p += 400;
        strcpy(p, "\nT\t3\t3000\tcccc0003\nE\n");
        char small[32];
        struct text src = {db};
        assert(kui_known_search(text_line, &src, disc, 3, small, sizeof(small), NULL, NULL) == KUI_KNOWN_IDENTIFIED);
        assert(strlen(small) == 31);
    }

    /* --- cancellation, honoured every 256 lines ------------------------------- */
    {
        static char big[64 * 1024];
        strcpy(big, H);
        for(unsigned i = 0; i < 1200; ++i) strcat(big, "G\t1\tFiller\nT\t9\t1\t00000001\nE\n");
        struct text src = {big};
        polls = 0;
        assert(kui_known_search(text_line, &src, disc, 3, name, sizeof(name), cancel_after_a_while, NULL) == KUI_KNOWN_CANCELLED);
        src.p = big;
        assert(kui_known_search(text_line, &src, disc, 3, name, sizeof(name), never, NULL) == KUI_KNOWN_NO_MATCH);
    }

    /* --- the REAL catalogues, with the CRCs K-UI-NeXT captured -------------------
     * Sword of the Berserk (USA), from docs/evidence: a complete capture. TOSEC
     * lists all three tracks; the bundled Redump catalogue lists only track 3. */
    const struct kui_known_track sword[3] = {
        {1, true, 1825152, 0xbcec7767}, {2, false, 1237152, 0x0ff934e3}, {3, true, 1185760800u, 0x2cfb5dcb}};
    assert(search_db("data/known-dumps/tosec.db", sword, 3, name) == KUI_KNOWN_FULL_MATCH);
    assert(strstr(name, "Sword of the Berserk") && strstr(name, "(US)"));
    assert(search_db("data/known-dumps/redump.db", sword, 3, name) == KUI_KNOWN_IDENTIFIED);
    assert(strstr(name, "Sword of the Berserk") && strstr(name, "(USA)"));
    /* One flipped audio-track bit: TOSEC still says the DATA tracks match. */
    struct kui_known_track flipped[3] = {sword[0], sword[1], sword[2]};
    flipped[1].crc32 ^= 1;
    assert(search_db("data/known-dumps/tosec.db", flipped, 3, name) == KUI_KNOWN_DATA_MATCH);
    /* One flipped bit in the big data track: no longer any full or data match. */
    flipped[1] = sword[1]; flipped[2].crc32 ^= 0x80000000u;
    assert(search_db("data/known-dumps/tosec.db", flipped, 3, name) == KUI_KNOWN_PARTIAL);
    assert(search_db("data/known-dumps/redump.db", flipped, 3, name) == KUI_KNOWN_NO_MATCH);
    /* The European release has a different track 3 CRC and must be told apart. */
    const struct kui_known_track eu[3] = {sword[0], sword[1], {3, true, 1185760800u, 0x05c635c3}};
    assert(search_db("data/known-dumps/redump.db", eu, 3, name) == KUI_KNOWN_IDENTIFIED && strstr(name, "Europe"));
    /* An MDK2 capture that has not reached its identifying track (31) cannot match. */
    const struct kui_known_track mdk2_partial[2] = {{1, true, 33988752, 0x97aad8a5}, {2, false, 1237152, 0x48fff429}};
    assert(search_db("data/known-dumps/redump.db", mdk2_partial, 2, name) == KUI_KNOWN_NO_MATCH);

    puts("PASS known dumps: grade ladder, tie/order, robustness, cancellation, real Redump/TOSEC catalogues");
    return 0;
}
