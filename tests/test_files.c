/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/files.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void paths(void) {
    static const char *const good[] = {"/", "/Games", "/Games/Crazy Taxi/disc.gdi", "/KUI/covers/User Art.png",
        "/caf\xc3\xa9", "/a b/c.d.e"};
    static const char *const bad[] = {"", "Games", "//", "/Games/", "/Games//x", "/./x", "/../x", "/a/..",
        "/con", "/x/aux.txt", "/trailing.", "/trailing ", "/a:b", "/a\\b", "/tab\tname", "/bad\xc3"};
    for(size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i) assert(kui_files_path_valid(good[i]));
    for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) assert(!kui_files_path_valid(bad[i]));
    assert(!kui_files_path_valid(NULL));
    char longest[KUI_FILES_PATH_CAP + 8];
    memset(longest, 0, sizeof(longest));
    longest[0] = '/';
    for(size_t at = 1; at < KUI_FILES_PATH_CAP - 1u; ++at) longest[at] = at % 100u ? 'x' : '/';
    assert(kui_files_path_valid(longest) && strlen(longest) == KUI_FILES_PATH_CAP - 1u);
    longest[KUI_FILES_PATH_CAP - 1u] = 'y';
    assert(!kui_files_path_valid(longest));
    char name[KUI_FILES_NAME_CAP + 1];
    memset(name, 'n', sizeof(name) - 1u); name[sizeof(name) - 1u] = 0;
    char path[KUI_FILES_NAME_CAP + 4];
    snprintf(path, sizeof(path), "/%s", name);
    assert(!kui_files_path_valid(path));
    path[KUI_FILES_NAME_CAP] = 0; /* 127 letters: the longest name */
    assert(kui_files_path_valid(path));

    char out[KUI_FILES_PATH_CAP];
    assert(kui_files_join(out, "/", "Games") && !strcmp(out, "/Games"));
    assert(kui_files_join(out, "/Games", "Crazy Taxi") && !strcmp(out, "/Games/Crazy Taxi"));
    assert(!kui_files_join(out, "/Games", "a/b") && !out[0]);
    assert(!kui_files_join(out, "/Games", "..") && !kui_files_join(out, "/Games", "") && !kui_files_join(out, "Games", "x"));
    longest[KUI_FILES_PATH_CAP - 1u] = 0;
    assert(!kui_files_join(out, longest, "more") && !out[0]);
    assert(kui_files_parent(out, "/Games/Crazy Taxi") && !strcmp(out, "/Games"));
    assert(kui_files_parent(out, "/Games") && !strcmp(out, "/"));
    assert(kui_files_parent(out, "/") && !strcmp(out, "/"));
    assert(!kui_files_parent(out, "Games") && !out[0]);
    assert(!strcmp(kui_files_leaf("/Games/Crazy Taxi"), "Crazy Taxi") && !strcmp(kui_files_leaf("/"), ""));

    assert(kui_files_within("/Games/Crazy Taxi", "/Games") && kui_files_within("/Games", "/Games"));
    assert(kui_files_within("/games/crazy taxi", "/GAMES") && kui_files_within("/anything", "/"));
    assert(!kui_files_within("/Games2", "/Games") && !kui_files_within("/Game", "/Games"));
    assert(!kui_files_within("/Games", "/Games/Crazy Taxi") && !kui_files_within("bad", "/"));
    puts("PASS files paths: validation, join, parent, leaf, within");
}
static void protection(void) {
    static const char *const kept[] = {"/", "/KUI", "/kui", "/KUI/runtime.kui", "/KUI/RUNTIME.KUI", "/KUI/apps",
        "/KUI/apps/games", "/KUI/apps/games/retail-boot.kui"};
    static const char *const free_to_change[] = {"/KUI/covers", "/KUI/apps/music", "/KUI/apps/games/probe.kui",
        "/KUI/apps/games/image-probe.kui", "/KUI/runtime.kui.bak", "/Games", "/KUI2", "/KUI/runtime"};
    for(size_t i = 0; i < sizeof(kept) / sizeof(kept[0]); ++i) assert(kui_files_protected(kept[i]));
    for(size_t i = 0; i < sizeof(free_to_change) / sizeof(free_to_change[0]); ++i)
        assert(!kui_files_protected(free_to_change[i]));
    puts("PASS files protection: the runtime, the Games loader and their folders");
}
static void names(void) {
    char out[KUI_FILES_NAME_CAP];
    assert(kui_files_numbered(out, "track03.bin", false, 2) && !strcmp(out, "track03 (2).bin"));
    assert(kui_files_numbered(out, "archive.tar.gz", false, 12) && !strcmp(out, "archive.tar (12).gz"));
    assert(kui_files_numbered(out, "Crazy Taxi", true, 3) && !strcmp(out, "Crazy Taxi (3)"));
    assert(kui_files_numbered(out, "v1.2", true, 2) && !strcmp(out, "v1.2 (2)"));
    assert(kui_files_numbered(out, ".hidden", false, 2) && !strcmp(out, ".hidden (2)"));
    assert(kui_files_numbered(out, "README", false, 99) && !strcmp(out, "README (99)"));
    assert(!kui_files_numbered(out, "x", false, 1) && !kui_files_numbered(out, "a/b", false, 2));
    char name[KUI_FILES_NAME_CAP];
    memset(name, 'n', sizeof(name) - 1u); name[sizeof(name) - 1u] = 0;
    assert(!kui_files_numbered(out, name, true, 2));
    name[sizeof(name) - 5u] = 0; /* 123 letters: " (2)" fits, " (10)" does not */
    assert(kui_files_numbered(out, name, true, 2) && strlen(out) == 127u);
    assert(!kui_files_numbered(out, name, true, 10));
    assert(kui_files_part_name("KUI-copy-1.kui-part") && kui_files_part_name("x.KUI-PART"));
    assert(!kui_files_part_name(".kui-part") && !kui_files_part_name("kui-part") && !kui_files_part_name(NULL));

    assert(kui_files_kind("Crazy Taxi", true) == KUI_FILES_KIND_FOLDER);
    assert(kui_files_kind("disc.GDI", false) == KUI_FILES_KIND_GDI);
    assert(kui_files_kind("song.ogg", false) == KUI_FILES_KIND_AUDIO && kui_files_kind("a.WAV", false) == KUI_FILES_KIND_AUDIO);
    assert(kui_files_kind("a.png", false) == KUI_FILES_KIND_PICTURE && kui_files_kind("a.JPEG", false) == KUI_FILES_KIND_PICTURE);
    assert(kui_files_kind("0GDTEX.PVR", false) == KUI_FILES_KIND_PICTURE && kui_files_kind("a.jpg", false) == KUI_FILES_KIND_PICTURE);
    assert(kui_files_kind("track01.bin", false) == KUI_FILES_KIND_OTHER && kui_files_kind(".gdi", false) == KUI_FILES_KIND_OTHER);
    assert(kui_files_kind("gdi", false) == KUI_FILES_KIND_OTHER && kui_files_kind(NULL, false) == KUI_FILES_KIND_OTHER);
    puts("PASS files names: numbered copies, partial copies, kinds");
}
static void order(void) {
    /* Folders first; then letters without case; then exact bytes. */
    static const struct { bool directory; const char *name; } sorted[] = {
        {true, "Alpha"}, {true, "beta"}, {true, "Beta2"}, {true, "zeta"}, {false, "a.txt"}, {false, "B.txt"},
        {false, "b.txt"}, {false, "b.txt2"}, {false, "caf\xc3\xa9"}, {false, "~last"}};
    size_t n = sizeof(sorted) / sizeof(sorted[0]);
    for(size_t i = 0; i < n; ++i)
        for(size_t j = 0; j < n; ++j) {
            int c = kui_files_compare(sorted[i].directory, sorted[i].name, sorted[j].directory, sorted[j].name);
            assert(i < j ? c < 0 : i > j ? c > 0 : c == 0);
        }
    puts("PASS files order: folders first, case-insensitive names, total order");
}
static void texts(void) {
    char out[20];
    kui_files_size_text(out, 0); assert(!strcmp(out, "0 B"));
    kui_files_size_text(out, 1023); assert(!strcmp(out, "1023 B"));
    kui_files_size_text(out, 1024); assert(!strcmp(out, "1.0 KB"));
    kui_files_size_text(out, 1536); assert(!strcmp(out, "1.5 KB"));
    kui_files_size_text(out, 102348); assert(!strcmp(out, "99.9 KB"));
    kui_files_size_text(out, 102350); assert(!strcmp(out, "100 KB"));
    kui_files_size_text(out, 1048576); assert(!strcmp(out, "1.0 MB"));
    kui_files_size_text(out, 1181616048ull); assert(!strcmp(out, "1.1 GB"));
    kui_files_size_text(out, 21474836480ull); assert(!strcmp(out, "20.0 GB"));
    kui_files_size_text(out, 0xffffffffffffffffull); assert(!strcmp(out, "16777216 TB"));
    kui_files_date_text(out, (uint16_t)((46u << 9) | (9u << 5) | 26u), (uint16_t)((14u << 11) | (3u << 5)));
    assert(!strcmp(out, "2026-09-26 14:03"));
    kui_files_date_text(out, 0, 0); assert(!strcmp(out, "No date"));
    kui_files_date_text(out, (uint16_t)((46u << 9) | (13u << 5) | 1u), 0); assert(!strcmp(out, "No date"));
    kui_files_date_text(out, (uint16_t)((46u << 9) | (1u << 5)), 0); assert(!strcmp(out, "No date"));
    kui_files_date_text(out, (uint16_t)((46u << 9) | (1u << 5) | 1u), (uint16_t)(24u << 11)); assert(!strcmp(out, "No date"));
    puts("PASS files text: sizes and dates");
}
int main(void) {
    paths(); protection(); names(); order(); texts();
    return 0;
}
