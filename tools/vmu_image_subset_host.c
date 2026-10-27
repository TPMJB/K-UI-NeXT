/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only protocol adapter. Admission and construction stay in core C. */
#include "kui/vmu_image_subset.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static uint8_t source[KUI_VMU_IMAGE_BYTES], output[KUI_VMU_IMAGE_BYTES];
static struct kui_vmu_image_workspace workspace;

static int read_source(const char *path) {
    FILE *file = fopen(path, "rb");
    if(!file) { perror("open source snapshot"); return -1; }
    size_t got = fread(source, 1u, sizeof(source), file);
    int extra = fgetc(file);
    int failed = ferror(file);
    if(fclose(file)) failed = 1;
    if(got != sizeof(source) || extra != EOF || failed) {
        fputs("Source must be a readable image of exactly 131072 bytes.\n", stderr);
        return -1;
    }
    return 0;
}
static int read_block(void *context, uint16_t block, void *out) {
    (void)context;
    if(block >= KUI_VMU_IMAGE_BLOCKS) return -1;
    memcpy(out, source + (size_t)block * 512u, 512u);
    return 0;
}
static unsigned le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static const uint8_t *entry(unsigned slot) {
    return source + (253u - slot / 16u) * 512u + (slot % 16u) * 32u;
}
static void print_hex(const uint8_t *p, unsigned size) {
    for(unsigned i = 0; i < size; ++i) printf("%02x", (unsigned)p[i]);
}
static int inspect(void) {
    const struct kui_vmu_image_io io = {NULL, read_block, NULL, NULL};
    struct kui_vmu_image_summary summary;
    int result = kui_vmu_image_validate(&io, sizeof(source), &workspace, &summary);
    if(result) {
        fprintf(stderr, "Source filesystem admission failed (%d).\n", result);
        return 1;
    }
    printf("{\"protocol\":\"kui-vmu-subset-v1\",\"summary\":{\"files\":%u,"
        "\"game_files\":%u,\"used_blocks\":%u,\"free_blocks\":%u},\"entries\":[",
        summary.files, summary.game_files, summary.used_blocks, summary.free_blocks);
    unsigned printed = 0;
    for(unsigned slot = 0; slot < KUI_VMU_IMAGE_DIRECTORY_ENTRIES; ++slot) {
        const uint8_t *e = entry(slot);
        if(!e[0]) continue;
        if(printed++) putchar(',');
        printf("{\"slot\":%u,\"name_hex\":\"", slot);
        print_hex(e + 4, 12u);
        printf("\",\"file_type\":%u,\"protection\":%u,\"blocks\":%u,"
            "\"header_block\":%u,\"directory_hex\":\"",
            (unsigned)e[0], (unsigned)e[1], le16(e + 24), le16(e + 26));
        print_hex(e, 32u);
        fputs("\",\"chain\":[", stdout);
        unsigned at = le16(e + 2), count = le16(e + 24);
        for(unsigned n = 0; n < count; ++n) {
            if(n) putchar(',');
            printf("%u", at);
            at = le16(workspace.fat + at * 2u);
        }
        fputs("]}", stdout);
    }
    fputs("]}\n", stdout);
    return ferror(stdout) || fflush(stdout) ? 1 : 0;
}
int main(int argc, char **argv) {
    if(argc == 3 && !strcmp(argv[1], "--inspect")) {
        if(read_source(argv[2])) return 1;
        return inspect();
    }
    if(argc < 4 || argc > 211 || strcmp(argv[1], "--construct")) {
        fputs("Usage: vmu-image-subset --inspect SOURCE\n"
              "       vmu-image-subset --construct SOURCE SLOT [SLOT ...]\n", stderr);
        return 2;
    }
    uint16_t slots[KUI_VMU_IMAGE_DIRECTORY_ENTRIES];
    size_t count = (size_t)(argc - 3);
    for(size_t i = 0; i < count; ++i) {
        const char *text = argv[i + 3u];
        char *end = NULL;
        errno = 0;
        unsigned long value = strtoul(text, &end, 10);
        if(text[0] < '0' || text[0] > '9' || !end || *end || errno ||
           value >= KUI_VMU_IMAGE_DIRECTORY_ENTRIES) {
            fputs("Invalid directory slot.\n", stderr);
            return 2;
        }
        slots[i] = (uint16_t)value;
    }
    if(read_source(argv[2])) return 1;
    struct kui_vmu_image_summary summary;
    int result = kui_vmu_image_subset(source, sizeof(source), output,
        sizeof(output), slots, count, &workspace, &summary);
    if(result) {
        fprintf(stderr, "Offline subset construction failed (%d).\n", result);
        return 1;
    }
#ifdef _WIN32
    if(_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
#endif
    if(fwrite(output, 1u, sizeof(output), stdout) != sizeof(output) || fflush(stdout)) {
        fputs("Cannot emit complete derived image.\n", stderr);
        return 1;
    }
    return 0;
}
