/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_QUERY_H
#define KUI_GAMES_QUERY_H
#include <stdbool.h>
#define KUI_GAMES_QUERY_TEXT_CAP 65u
#define KUI_GAMES_QUERY_TITLE_CAP 129u
#define KUI_GAMES_QUERY_NAME_CAP 128u
#define KUI_GAMES_QUERY_REGION_CAP 9u
enum kui_games_query_region {
    KUI_GAMES_REGION_ANY,KUI_GAMES_REGION_JAPAN,KUI_GAMES_REGION_USA,
    KUI_GAMES_REGION_EUROPE,KUI_GAMES_REGION_UNKNOWN,KUI_GAMES_REGION_COUNT
};
struct kui_games_query {
    char text[KUI_GAMES_QUERY_TEXT_CAP];
    /* 0/1/2 retain the All/Favorites/Recent values from games_activity.h.
     * This standalone matcher validates, but does not apply, activity. */
    unsigned activity,region,disc;
};
void kui_games_query_default(struct kui_games_query *);
/* Text is at most 64 printable UTF-8 bytes. ASCII matching is case folded;
 * non-ASCII bytes compare exactly. disc is 0 (Any) or 1..99. */
bool kui_games_query_valid(const struct kui_games_query *);
/* Searches the displayed title/alias and the original row name. region is
 * the bounded IP.BIN J/U/E field; missing or unrecognized flags are Unknown,
 * never inferred from the title or folder. disc is 0 when not established.
 * A caller displaying a disc set applies this to its real members and keeps
 * the group when any member matches. No allocation or I/O is performed. */
bool kui_games_query_matches(const struct kui_games_query *,const char *title,
    const char *fallback_name,const char *region,unsigned disc);
/* ASCII initial after leading spaces, using title unless empty. A-Z map to
 * their uppercase bucket; numbers/punctuation/non-ASCII map to '#'. Invalid
 * or unterminated strings return 0. '#' and A-Z are valid jump targets. */
char kui_games_query_letter(const char *title,const char *fallback_name);
bool kui_games_query_jump_matches(const char *title,const char *fallback_name,char letter);
const char *kui_games_query_region_label(unsigned region);
#endif
