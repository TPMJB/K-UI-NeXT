/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/files.h"
#include "kui/cover_image.h"
#include "kui/game_cover.h"
#include "kui/pvr_texture.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* File Manager picture view: the same decoders and scaling as Games box art. */
_Static_assert(KUI_FILES_PICTURE_EDGE <= KUI_COVER_SCALE_MAX, "picture view larger than the scaler fills");
static bool stopped(kui_cancel_fn cancel) { return cancel && cancel(); }
static const char *load(const char *path, uint8_t **data, size_t *size, kui_log_fn log, kui_cancel_fn cancel) {
    char c[KUI_FILES_PATH_CAP + 2u];
    if(snprintf(c, sizeof(c), "0:%s", path) >= (int)sizeof(c)) return "Invalid picture";
    if(!kui_sd_connect()) return "SD card unavailable";
    FATFS fs;
    const char *problem = NULL;
    if(!kui_mount(&fs, log)) problem = "Cannot mount SD card";
    else {
        FIL file;
        FRESULT r = f_open(&file, c, FA_READ);
        if(r != FR_OK) problem = r == FR_NO_FILE || r == FR_NO_PATH ? "This picture is no longer on the card" :
            "Cannot open this picture";
        else {
            uint64_t bytes = (uint64_t)f_size(&file);
            UINT got = 0;
            if(!bytes) problem = "This picture file is empty";
            else if(bytes > KUI_FILES_PICTURE_MAX_BYTES) problem = "Pictures up to 3 MB can be shown";
            else if(!(*data = malloc((size_t)bytes))) problem = "Insufficient memory for this picture";
            else if(stopped(cancel)) problem = "Stopped";
            else if(f_read(&file, *data, (UINT)bytes, &got) != FR_OK || got != bytes) problem = "Cannot read this picture";
            *size = (size_t)bytes;
            if(f_close(&file) != FR_OK && !problem) problem = "Cannot read this picture";
        }
    }
    if(f_mount(NULL, "0:", 0) != FR_OK && !problem) problem = "Cannot release SD filesystem";
    kui_sd_disconnect();
    return problem;
}
bool kui_files_picture(const char *path, uint16_t *pixels, struct kui_files_picture *out,
                       kui_log_fn log, kui_cancel_fn cancel) {
    if(!out) return false;
    memset(out, 0, sizeof(*out));
    out->format = "";
    if(!pixels || !path || !memchr(path, 0, KUI_FILES_PATH_CAP) || !kui_files_path_valid(path) || !path[1]) {
        snprintf(out->message, sizeof(out->message), "Invalid picture");
        return false;
    }
    strcpy(out->path, path);
    uint8_t *data = NULL, *rgba = NULL;
    size_t size = 0;
    unsigned width = 0, height = 0, channels = 0;
    bool from_stb = false;
    const char *problem = kui_files_kind(kui_files_leaf(path), false) != KUI_FILES_KIND_PICTURE ?
        "Only PNG, JPEG and PVR pictures can be shown" : NULL;
    if(!problem) problem = load(path, &data, &size, log, cancel);
    if(!problem) {
        out->bytes = size;
        static const uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
        bool is_png = size >= 8 && !memcmp(data, png, 8);
        bool is_jpeg = size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
        struct kui_pvr_info info;
        enum kui_pvr_status parsed = is_png || is_jpeg ? KUI_PVR_NOT_PVR : kui_pvr_parse(data, size, &info);
        if(is_png || is_jpeg) {
            out->format = is_png ? "PNG" : "JPEG";
            enum kui_cover_image_status s = kui_cover_image_decode(data, size, &rgba, &width, &height, &channels);
            if(s != KUI_COVER_IMAGE_OK) problem = kui_cover_image_status_text(s);
            from_stb = true;
        } else if(parsed == KUI_PVR_OK) {
            out->format = "PVR texture";
            width = info.width; height = info.height; channels = 4;
            rgba = malloc((size_t)width * height * 4u);
            enum kui_pvr_status s = rgba ? kui_pvr_decode(data, size, &info, rgba) : KUI_PVR_OK;
            if(!rgba) problem = "Insufficient memory for this picture";
            else if(s != KUI_PVR_OK) problem = kui_pvr_status_text(s);
        } else problem = parsed == KUI_PVR_NOT_PVR ? "This file is not a picture K-UI can show" :
            kui_pvr_status_text(parsed);
        free(data);
        data = NULL;
    }
    if(!problem && stopped(cancel)) problem = "Stopped";
    if(!problem) {
        out->width = width;
        out->height = height;
        kui_cover_reduce(rgba, &width, &height, channels);
        if(!kui_cover_scale(rgba, width, height, channels, pixels, KUI_FILES_PICTURE_EDGE))
            problem = "Cannot scale this picture";
    }
    if(from_stb) kui_cover_image_free(rgba); else free(rgba);
    free(data);
    out->ok = !problem;
    snprintf(out->message, sizeof(out->message), "%s", problem ? problem : "");
    if(log) log("Files picture %s: %s", path, problem ? problem : out->format);
    return out->ok;
}
