/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_covers.h"
#include "kui/cover_image.h"
#include "kui/game_image.h"
#include "kui/game_metadata.h"
#include "kui/pvr_texture.h"
#include "kui/retail_image.h"
#include "platform.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Box art lives beside the rest of K-UI's data, never in game folders:
 * KUI/covers/<name>.kcv records (game_cover.h), optional owner images
 * <name>.png/.jpg/.jpeg, and KUI/apps/games/view.txt for the chosen view. */
#define PATH_CAP (KUI_GAMES_FILE_CAP + 3u)
static const char *const view_names[KUI_GAMES_VIEW_COUNT] = {"list", "compact", "gallery"};
static const char *const user_images[] = {".png", ".jpg", ".jpeg"};
#define COVER_CACHE_SLOTS 32u
#define COVER_CACHE_BYTES (512u * 1024u)
struct cached_cover {
    struct kui_games_cover_result result;
    uint16_t *pixels;
    unsigned used,bytes;
    bool valid;
};
static struct cached_cover covers[COVER_CACHE_SLOTS];
static unsigned cover_clock,cover_bytes,saved_view;
static bool view_known,artwork_known,artwork_exists;
void kui_games_covers_cache_clear(void) {
    for(unsigned i=0;i<COVER_CACHE_SLOTS;i++) free(covers[i].pixels);
    memset(covers,0,sizeof(covers));cover_clock=cover_bytes=saved_view=0;
    view_known=artwork_known=artwork_exists=false;
}

enum kui_cover_size kui_games_view_size(unsigned view) {
    return view == KUI_GAMES_VIEW_GALLERY ? KUI_COVER_SIZE_MEDIUM :
        view == KUI_GAMES_VIEW_COMPACT ? KUI_COVER_SIZE_SMALL : KUI_COVER_SIZE_LARGE;
}
const char *kui_games_view_name(unsigned view) {
    return view < KUI_GAMES_VIEW_COUNT ? view_names[view] : view_names[0];
}
static bool stopped(kui_cancel_fn cancel) { return cancel && cancel(); }
static bool record_path(const char *key, const char *extension, char out[PATH_CAP]) {
    int n = snprintf(out, PATH_CAP, "%s/%s%s", KUI_GAMES_COVERS_FOLDER, key, extension);
    return n > 0 && n < (int)PATH_CAP;
}
static bool make_folders(const char *const *paths, unsigned count, kui_log_fn log) {
    for(unsigned i = 0; i < count; ++i) {
        FILINFO info;
        FRESULT r = f_stat(paths[i], &info);
        if(r == FR_OK && (info.fattrib & AM_DIR)) continue;
        if(r == FR_NO_FILE || r == FR_NO_PATH) r = f_mkdir(paths[i]);
        else if(r == FR_OK) r = FR_EXIST; /* A file is in the way. */
        if(r != FR_OK) {
            if(log) log("Cannot create %s: FatFs=%u", paths[i] + 2, (unsigned)r);
            return false;
        }
    }
    return true;
}

/* ---- The saved view ---- */
static unsigned view_load(void) {
    FIL file;
    char text[16] = {0};
    UINT got = 0;
    if(f_open(&file, KUI_GAMES_VIEW_FILE, FA_READ) != FR_OK) return KUI_GAMES_VIEW_LIST;
    bool ok = f_size(&file) < sizeof(text) && f_read(&file, text, sizeof(text) - 1u, &got) == FR_OK;
    if(f_close(&file) != FR_OK || !ok) return KUI_GAMES_VIEW_LIST;
    text[got] = 0;
    char *end = strchr(text, '\n');
    if(end) *end = 0;
    for(unsigned v = 0; v < KUI_GAMES_VIEW_COUNT; ++v) if(!strcmp(text, view_names[v])) return v;
    return KUI_GAMES_VIEW_LIST;
}
static void view_save(unsigned view, kui_log_fn log) {
    static const char *const parents[] = {"0:/KUI", "0:/KUI/apps", "0:/KUI/apps/games"};
    if(!make_folders(parents, 3, log)) return;
    char text[16];
    int n = snprintf(text, sizeof(text), "%s\n", view_names[view]);
    FIL file;
    UINT wrote = 0;
    FRESULT r = f_open(&file, KUI_GAMES_VIEW_FILE, FA_WRITE | FA_CREATE_ALWAYS);
    if(r == FR_OK) {
        r = f_write(&file, text, (UINT)n, &wrote);
        FRESULT closed = f_close(&file);
        if(r == FR_OK) r = closed;
    }
    if((r != FR_OK || wrote != (UINT)n) && log) log("Games view not saved: FatFs=%u", (unsigned)r);
}

/* ---- Reading records ---- */
/* Opens a record only when it is intact and belongs to exactly this GDI;
 * the file stays open for pixel reads. */
static bool record_open(const char *key, const char *gdi_path, FIL *file, struct kui_cover_record *record) {
    char path[PATH_CAP];
    if(!record_path(key, ".kcv", path) || f_open(file, path, FA_READ) != FR_OK) return false;
    uint8_t header[KUI_COVER_HEADER_BYTES];
    UINT got = 0;
    bool ok = f_read(file, header, sizeof(header), &got) == FR_OK && got == sizeof(header) &&
        kui_cover_header_decode(record, header) && !strcmp(record->gdi_path, gdi_path) &&
        f_size(file) == (record->source == KUI_COVER_SOURCE_NONE ? KUI_COVER_HEADER_BYTES : KUI_COVER_FILE_BYTES);
    if(!ok) f_close(file);
    return ok;
}
static bool record_pixels(FIL *file, enum kui_cover_size size, uint16_t *out) {
    unsigned edge = kui_cover_edge(size);
    UINT bytes = edge * edge * 2u, got = 0;
    if(f_lseek(file, (FSIZE_t)kui_cover_offset(size)) != FR_OK ||
       f_read(file, out, bytes, &got) != FR_OK || got != bytes) return false;
    /* Stored little-endian; each value is read before it is replaced. */
    const uint8_t *b = (const uint8_t *)out;
    for(unsigned i = 0; i < edge * edge; ++i) out[i] = (uint16_t)(b[2u * i] | b[2u * i + 1u] << 8);
    return true;
}

static bool same_cover(const struct cached_cover *cover,const struct kui_games_entry *entry) {
    return cover->valid && !strcmp(cover->result.path,entry->path) && !strcmp(cover->result.name,entry->name);
}
struct view_job {unsigned requested,view;};
static void page_view(void *ctx,kui_log_fn log,kui_cancel_fn cancel) {
    struct view_job *job=ctx;
    unsigned saved=view_known?saved_view:view_load();
    job->view=job->requested<KUI_GAMES_VIEW_COUNT?job->requested:saved;
    if(!stopped(cancel) && job->view!=saved) view_save(job->view,log);
}
bool kui_games_list_rows(const char *root,unsigned offset,unsigned view,
    struct kui_games_page *out,kui_log_fn log,kui_cancel_fn cancel) {
    struct view_job job={view,view<KUI_GAMES_VIEW_COUNT?view:saved_view};
    bool needs_view=!view_known || (view<KUI_GAMES_VIEW_COUNT && view!=saved_view);
    bool ok=kui_games_list_with(root,offset,out,needs_view?page_view:NULL,&job,log,cancel);
    if(!out) return false;
    if(!ok) {out->view=KUI_GAMES_VIEW_SAVED;return false;}
    saved_view=job.view;view_known=true;out->view=saved_view;
    out->artwork=artwork_known && artwork_exists;
    for(unsigned row=0;row<out->count;row++) {
        struct kui_games_entry *entry=&out->entries[row];
        kui_cover_display_title(NULL,entry->name,entry->title);entry->cover=false;
        const struct cached_cover *latest=NULL;
        for(unsigned i=0;i<COVER_CACHE_SLOTS;i++) if(same_cover(&covers[i],entry) &&
            (!latest || covers[i].used>latest->used)) latest=&covers[i];
        if(latest) strcpy(entry->title,latest->result.title);
    }
    return true;
}
bool kui_games_list_covers(const char *root, unsigned offset, unsigned view,
    struct kui_games_page *out, uint16_t (*pixels)[KUI_COVER_PIXELS], kui_log_fn log, kui_cancel_fn cancel) {
    (void)pixels;
    return kui_games_list_rows(root,offset,view,out,log,cancel);
}
static void cover_drop(unsigned slot) {
    cover_bytes-=covers[slot].bytes;free(covers[slot].pixels);memset(&covers[slot],0,sizeof(covers[slot]));
}
static unsigned cover_oldest(void) {
    unsigned oldest=COVER_CACHE_SLOTS;
    for(unsigned i=0;i<COVER_CACHE_SLOTS;i++) if(covers[i].valid &&
        (oldest==COVER_CACHE_SLOTS || covers[i].used<covers[oldest].used)) oldest=i;
    return oldest;
}
static void cover_store(const struct kui_games_cover_result *result,const uint16_t *pixels) {
    unsigned edge=kui_cover_edge(result->size),bytes=result->cover?edge*edge*2u:0;
    uint16_t *copy=bytes?malloc(bytes):NULL;
    if(bytes && !copy) return;
    if(copy) memcpy(copy,pixels,bytes);
    while(cover_bytes+bytes>COVER_CACHE_BYTES) cover_drop(cover_oldest());
    unsigned slot=0;
    while(slot<COVER_CACHE_SLOTS && covers[slot].valid) ++slot;
    if(slot==COVER_CACHE_SLOTS) {slot=cover_oldest();cover_drop(slot);}
    covers[slot].result=*result;covers[slot].pixels=copy;covers[slot].bytes=bytes;
    covers[slot].used=++cover_clock;covers[slot].valid=true;cover_bytes+=bytes;
}
bool kui_games_selected_cover(const struct kui_games_entry *entry,enum kui_cover_size size,
    uint16_t pixels[KUI_COVER_PIXELS],struct kui_games_cover_result *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return false;
    memset(out,0,sizeof(*out));out->size=size;
    if(!entry || !pixels || (unsigned)size>KUI_COVER_SIZE_SMALL ||
       !memchr(entry->name,0,sizeof(entry->name)) || !memchr(entry->path,0,sizeof(entry->path))) return false;
    strcpy(out->name,entry->name);strcpy(out->path,entry->path);
    kui_cover_display_title(NULL,entry->name,out->title);
    if(stopped(cancel)) {out->stopped=true;return false;}
    if(entry->directory || entry->disabled) return true;
    for(unsigned i=0;i<COVER_CACHE_SLOTS;i++) if(same_cover(&covers[i],entry) && covers[i].result.size==size) {
        *out=covers[i].result;covers[i].used=++cover_clock;
        if(out->cover) memcpy(pixels,covers[i].pixels,covers[i].bytes);
        return true;
    }
    char key[KUI_COVER_PATH_CAP],path[PATH_CAP];
    if(!kui_cover_key(entry->name,key) || !record_path(key,".kcv",path)) return false;
    bool connected=false,mounted=false,opened=false,ok=false,observed=false,exists=false;
    FATFS fs;FIL file;
    const char *problem="SD card unavailable";
    if(!(connected=kui_sd_connect())) goto done;
    problem="Cannot mount SD card";
    if(!(mounted=kui_mount(&fs,log))) goto done;
    if(stopped(cancel)) goto done;
    if(!artwork_known) {
        FILINFO info;FRESULT r=f_stat(KUI_GAMES_COVERS_FOLDER,&info);
        observed=r==FR_OK || r==FR_NO_FILE || r==FR_NO_PATH;
        exists=r==FR_OK && (info.fattrib&AM_DIR);
    }
    FRESULT r=f_open(&file,path,FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH) {ok=true;goto done;}
    problem="Cannot read selected box art";
    if(r!=FR_OK) goto done;
    opened=true;
    uint8_t header[KUI_COVER_HEADER_BYTES];UINT got=0;struct kui_cover_record record;
    if(stopped(cancel) || f_read(&file,header,sizeof(header),&got)!=FR_OK || got!=sizeof(header) ||
       !kui_cover_header_decode(&record,header) ||
       f_size(&file)!=(record.source==KUI_COVER_SOURCE_NONE?KUI_COVER_HEADER_BYTES:KUI_COVER_FILE_BYTES)) goto done;
    /* A same-name record for a different image is a definite missing cover. */
    if(strcmp(record.gdi_path,entry->path)) {ok=true;goto done;}
    kui_cover_display_title(record.title,entry->name,out->title);
    if(record.source!=KUI_COVER_SOURCE_NONE) {
        if(stopped(cancel) || !record_pixels(&file,size,pixels)) goto done;
        out->cover=true;
    }
    ok=true;
done:
    if(opened && f_close(&file)!=FR_OK) ok=false;
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) ok=false;
    if(connected) kui_sd_disconnect();
    if(stopped(cancel)) {ok=false;out->stopped=true;}
    if(ok) {
        if(observed) {artwork_known=true;artwork_exists=exists;}
        if(out->cover) {artwork_known=artwork_exists=true;}
        cover_store(out,pixels);
    } else {
        out->cover=false;
        if(log && !out->stopped) log("Games cover %.72s: %s",entry->name,problem);
    }
    return ok;
}

struct detail_job { const char *path; struct kui_games_detail *detail; uint16_t *pixels; };
static void detail_cover(void *ctx, kui_log_fn log, kui_cancel_fn cancel) {
    (void)log;
    struct detail_job *job = ctx;
    const char *slash = strrchr(job->path, '/');
    if(!slash || !job->pixels) return;
    /* A game folder names its record; a GDI sharing a folder names its own. */
    const char *start = slash;
    while(start > job->path && start[-1] != '/') --start;
    char folder[KUI_COVER_PATH_CAP] = "";
    size_t n = (size_t)(slash - start);
    if(n < sizeof(folder)) { memcpy(folder, start, n); folder[n] = 0; }
    const char *names[2] = {folder, slash + 1};
    for(unsigned k = 0; k < 2 && !stopped(cancel); ++k) {
        char key[KUI_COVER_PATH_CAP];
        FIL file;
        struct kui_cover_record record;
        if(!kui_cover_key(names[k], key) || !record_open(key, job->path, &file, &record)) continue;
        if(record.source != KUI_COVER_SOURCE_NONE)
            job->detail->cover = record_pixels(&file, KUI_COVER_SIZE_LARGE, job->pixels);
        f_close(&file);
        return;
    }
}
bool kui_games_inspect_cover(const char *path, struct kui_games_detail *out,
    uint16_t pixels[KUI_COVER_PIXELS], kui_log_fn log, kui_cancel_fn cancel) {
    struct detail_job job = {path, out, pixels};
    return kui_games_inspect_with(path, out, detail_cover, &job, log, cancel);
}

/* ---- The scan ---- */
struct scan {
    kui_log_fn log;
    kui_cancel_fn cancel;
    struct kui_games_scan_counts *counts;
    /* Found games as "key\0path\0" pairs, with folded key hashes for the
     * duplicate check (FAT names ignore case). */
    char *names;
    size_t used, capacity;
    uint32_t *hashes;
    unsigned found;
    uint8_t *record, *raw;       /* One record file; sixteen raw sectors. */
    uint16_t *scaled;
};
static uint32_t folded_hash(const char *s) {
    uint32_t h = 2166136261u;
    for(; *s; ++s) h = (h ^ (uint32_t)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s)) * 16777619u;
    return h;
}
static bool same_name(const char *a, const char *b) {
    for(;; ++a, ++b) {
        int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if(x != y) return false;
        if(!x) return true;
    }
}
static const char *nth_path(const char *key) { return key + strlen(key) + 1u; }
static void add_game(struct scan *s, const char *name, const char *path) {
    char key[KUI_COVER_PATH_CAP];
    if(!kui_cover_key(name, key)) {
        if(s->log) s->log("Box art: no cover name possible for %s", path);
        ++s->counts->failed;
        return;
    }
    uint32_t hash = folded_hash(key);
    const char *at = s->names;
    for(unsigned i = 0; i < s->found; ++i, at = nth_path(at) + strlen(nth_path(at)) + 1u) {
        if(s->hashes[i] == hash && same_name(at, key)) {
            if(s->log) s->log("Box art: two games are named %s; rename one to give both covers", key);
            ++s->counts->duplicates;
            return;
        }
    }
    size_t need = strlen(key) + strlen(path) + 2u;
    if(s->found >= KUI_GAMES_SCAN_MAX) { ++s->counts->failed; return; }
    if(s->used + need > s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2u : 8192u;
        while(capacity < s->used + need) capacity *= 2u;
        char *names = realloc(s->names, capacity);
        if(!names) { ++s->counts->failed; return; }
        s->names = names;
        s->capacity = capacity;
    }
    if(!s->hashes) {
        s->hashes = malloc(KUI_GAMES_SCAN_MAX * sizeof(*s->hashes));
        if(!s->hashes) { ++s->counts->failed; return; }
    }
    memcpy(s->names + s->used, key, strlen(key) + 1u);
    memcpy(s->names + s->used + strlen(key) + 1u, path, strlen(path) + 1u);
    s->used += need;
    s->hashes[s->found++] = hash;
}
/* Shares the Games payload filter: a folder with one image takes the folder
 * name; other folders are categories; loose selectors take their filename. */
struct walk_job {struct scan *scan;const char *folder;unsigned depth;};
static bool walk(struct scan *s,const char *folder,unsigned depth);
static bool walk_entry(void *ctx,const char *name,bool directory) {
    struct walk_job *job=ctx;struct scan *s=job->scan;
    if(stopped(s->cancel)) return false;
    if(directory) {
        char child[KUI_DEST_ROOT_CAP],image[KUI_GAMES_FILE_CAP];
        if(!kui_destination_join(child,job->folder,name)) return true;
        if(kui_games_single_image(child,image,s->cancel)) add_game(s,name,image);
        else if(job->depth+1u<KUI_GAMES_SCAN_DEPTH && !walk(s,child,job->depth+1u)) {
            if(stopped(s->cancel)) return false;
            ++s->counts->failed;if(s->log) s->log("Box art: %s: cannot read image folder",child);
        }
    } else {
        char image[KUI_GAMES_FILE_CAP];
        int n=snprintf(image,sizeof(image),"%s%s%s",job->folder,strcmp(job->folder,"/")?"/":"",name);
        if(n>0 && n<(int)sizeof(image)) add_game(s,name,image);
    }
    return !stopped(s->cancel);
}
static bool walk(struct scan *s,const char *folder,unsigned depth) {
    struct walk_job job={s,folder,depth};
    return kui_games_visit_images(folder,walk_entry,&job,s->cancel);
}

/* Track reads keep the current file open: reopening would walk its
 * allocation chain again for every chunk. */
struct image_files {
    char root[KUI_GAMES_FILE_CAP], open_name[KUI_GAME_NAME_CAP];
    kui_cancel_fn cancel;
    FIL reader;
    bool reader_open;
};
static bool image_join(const struct image_files *f, const char *name, char out[PATH_CAP]) {
    if(!kui_destination_name_valid(name)) return false;
    int n = snprintf(out, PATH_CAP, "0:%s%s%s", f->root, strcmp(f->root, "/") ? "/" : "", name);
    return n > 0 && n < (int)PATH_CAP;
}
static enum kui_game_result image_stat(void *ctx, const char *name, uint64_t *bytes) {
    struct image_files *f = ctx;
    char path[PATH_CAP];
    FILINFO info;
    if(stopped(f->cancel)) return KUI_GAME_CANCELLED;
    if(!image_join(f, name, path)) return KUI_GAME_INVALID;
    FRESULT r = f_stat(path, &info);
    if(r != FR_OK) return r == FR_NO_FILE || r == FR_NO_PATH ? KUI_GAME_NOT_FOUND : KUI_GAME_IO;
    if(info.fattrib & AM_DIR) return KUI_GAME_FILE_SIZE;
    *bytes = info.fsize;
    return KUI_GAME_OK;
}
static bool image_close(struct image_files *f) {
    if(!f->reader_open) return true;
    f->reader_open = false;
    return f_close(&f->reader) == FR_OK;
}
static enum kui_game_result image_read(void *ctx, const char *name, uint64_t offset, void *out, size_t bytes) {
    struct image_files *f = ctx;
    char path[PATH_CAP];
    if(stopped(f->cancel)) return KUI_GAME_CANCELLED;
    if(!image_join(f, name, path) || bytes > UINT_MAX || (uint64_t)(FSIZE_t)offset != offset)
        return KUI_GAME_INVALID;
    if(f->reader_open && strcmp(f->open_name, name) && !image_close(f)) return KUI_GAME_IO;
    if(!f->reader_open) {
        if(f_open(&f->reader, path, FA_READ) != FR_OK) return KUI_GAME_IO;
        f->reader_open = true;
        snprintf(f->open_name, sizeof(f->open_name), "%s", name);
    }
    FIL *file = &f->reader;
    bool ok = offset <= f_size(file) && bytes <= f_size(file) - offset;
    UINT got = 0;
    if(ok && f_tell(file) != offset) ok = f_lseek(file, (FSIZE_t)offset) == FR_OK && f_tell(file) == offset;
    if(ok) ok = f_read(file, out, (UINT)bytes, &got) == FR_OK && got == bytes;
    return stopped(f->cancel) ? KUI_GAME_CANCELLED : ok ? KUI_GAME_OK : KUI_GAME_IO;
}
static enum kui_game_metadata_io_result metadata_sector(void *ctx, uint32_t lba, uint8_t out[2048]) {
    enum kui_game_result r = kui_game_image_read(ctx, lba, 1, KUI_GAME_SECTOR_MODE1, out, 2048);
    return r == KUI_GAME_OK ? KUI_GAME_METADATA_IO_OK :
        r == KUI_GAME_CANCELLED ? KUI_GAME_METADATA_IO_CANCELLED : KUI_GAME_METADATA_IO_ERROR;
}
static bool metadata_range(void *ctx, uint32_t lba, uint32_t count) {
    return kui_game_image_check(ctx, lba, count, KUI_GAME_SECTOR_MODE1) == KUI_GAME_OK;
}

enum outcome { OUT_UNCHANGED, OUT_DISC, OUT_USER, OUT_NONE, OUT_FAILED };
static bool read_file(const char *path, uint64_t limit, uint8_t **data, size_t *size) {
    FIL file;
    *data = NULL;
    *size = 0;
    if(f_open(&file, path, FA_READ) != FR_OK) return false;
    bool ok = f_size(&file) && f_size(&file) <= limit;
    if(ok) {
        *size = (size_t)f_size(&file);
        *data = malloc(*size);
        UINT got = 0;
        ok = *data && f_read(&file, *data, (UINT)*size, &got) == FR_OK && got == *size;
    }
    if(f_close(&file) != FR_OK) ok = false;
    if(!ok) { free(*data); *data = NULL; *size = 0; }
    return ok;
}
static void stamp_of(const FILINFO *info, struct kui_cover_stamp *stamp) {
    stamp->bytes = info->fsize;
    stamp->date = info->fdate;
    stamp->time = info->ftime;
}
static bool same_stamp(const struct kui_cover_stamp *a, const struct kui_cover_stamp *b) {
    return a->bytes == b->bytes && a->date == b->date && a->time == b->time;
}
/* Scales one decoded picture into the record's three sizes. */
static bool fill_sizes(struct scan *s, uint8_t *pixels, unsigned width, unsigned height, unsigned channels) {
    kui_cover_reduce(pixels, &width, &height, channels);
    for(unsigned z = 0; z < 3; ++z) {
        enum kui_cover_size size = (enum kui_cover_size)z;
        unsigned edge = kui_cover_edge(size);
        if(!kui_cover_scale(pixels, width, height, channels, s->scaled, edge)) return false;
        uint8_t *out = s->record + kui_cover_offset(size);
        for(unsigned i = 0; i < edge * edge; ++i) {
            out[2u * i] = (uint8_t)s->scaled[i];
            out[2u * i + 1u] = (uint8_t)(s->scaled[i] >> 8);
        }
    }
    return true;
}
static bool user_art(struct scan *s, const char *key, const char *path) {
    uint8_t *data = NULL, *pixels = NULL;
    size_t size = 0;
    unsigned w = 0, h = 0, channels = 0;
    if(!read_file(path, KUI_COVER_IMAGE_MAX_BYTES, &data, &size)) {
        if(s->log) s->log("Box art: %s: cannot read %s (over 3 MB or unreadable)", key, path + 2);
        return false;
    }
    enum kui_cover_image_status status = kui_cover_image_decode(data, size, &pixels, &w, &h, &channels);
    free(data);
    if(status != KUI_COVER_IMAGE_OK) {
        if(s->log) s->log("Box art: %s: %s: %s", key, path + 2, kui_cover_image_status_text(status));
        return false;
    }
    bool ok = fill_sizes(s, pixels, w, h, channels);
    kui_cover_image_free(pixels);
    return ok;
}
/* Read cooked user bytes through the image adapter, preserving address checks
 * for the original raw GDI path. False with *io set means a read problem. */
static bool disc_art(struct scan *s, const char *key, const struct kui_game_image *image,
                     uint32_t lba, uint32_t bytes, bool *io) {
    uint32_t sectors = (bytes + 2047u) / 2048u;
    uint8_t *file = malloc((size_t)sectors * 2048u);
    if(!file) { *io = true; return false; }
    for(uint32_t done = 0; done < sectors;) {
        uint32_t n = sectors - done > 16u ? 16u : sectors - done;
        bool raw=image->format==KUI_GAME_IMAGE_GDI &&
            kui_game_image_check(image,lba+done,n,KUI_GAME_SECTOR_RAW)==KUI_GAME_OK;
        enum kui_game_result r = kui_game_image_read(image, lba + done, n,
            raw?KUI_GAME_SECTOR_RAW:KUI_GAME_SECTOR_MODE1,s->raw,
            (size_t)n*(raw?KUI_GAME_RAW_BYTES:KUI_GAME_DATA_BYTES));
        if(r != KUI_GAME_OK) {
            *io = r == KUI_GAME_IO || r == KUI_GAME_CANCELLED;
            if(s->log) s->log("Box art: %s: artwork unreadable: %s", key, kui_game_result_name(r));
            free(file);
            return false;
        }
        if(!raw) memcpy(file+(size_t)done*2048u,s->raw,(size_t)n*2048u);
        else for(uint32_t k = 0; k < n; ++k) {
            const uint8_t *sector = s->raw + (size_t)k * KUI_GAME_RAW_BYTES;
            if(kui_retail_sector_header(sector, lba + done + k) != KUI_RETAIL_HEADER_OK) {
                if(s->log) s->log("Box art: %s: artwork sector %lu is not data", key, (unsigned long)(lba + done + k));
                free(file);
                return false;
            }
            memcpy(file + (size_t)(done + k) * 2048u, sector + 16, 2048);
        }
        done += n;
    }
    struct kui_pvr_info info;
    enum kui_pvr_status status = kui_pvr_parse(file, bytes, &info);
    uint8_t *rgba = status == KUI_PVR_OK ? malloc((size_t)info.width * info.height * 4u) : NULL;
    if(status == KUI_PVR_OK && !rgba) { free(file); *io = true; return false; }
    if(status == KUI_PVR_OK) status = kui_pvr_decode(file, bytes, &info, rgba);
    free(file);
    bool ok = status == KUI_PVR_OK && fill_sizes(s, rgba, info.width, info.height, 4);
    if(status != KUI_PVR_OK && s->log) s->log("Box art: %s: %s", key, kui_pvr_status_text(status));
    else if(ok && s->log) s->log("Box art: %s: disc artwork %ux%u%s", key, info.width, info.height, info.vq ? " VQ" : "");
    free(rgba);
    return ok;
}
static bool record_write(struct scan *s, const char *key, const struct kui_cover_record *record) {
    char final[PATH_CAP], temp[PATH_CAP];
    if(!record_path(key, ".kcv", final) || !record_path(key, ".new", temp) ||
       !kui_cover_header_encode(s->record, record)) return false;
    UINT size = record->source == KUI_COVER_SOURCE_NONE ? KUI_COVER_HEADER_BYTES : KUI_COVER_FILE_BYTES, wrote = 0;
    FIL file;
    FRESULT r = f_open(&file, temp, FA_WRITE | FA_CREATE_ALWAYS);
    if(r == FR_OK) {
        r = f_write(&file, s->record, size, &wrote);
        FRESULT closed = f_close(&file);
        if(r == FR_OK) r = closed;
        if(r == FR_OK && wrote != size) r = FR_DISK_ERR;
        /* The finished file replaces the old one only once it is complete. */
        if(r == FR_OK) {
            r = f_unlink(final);
            if(r == FR_NO_FILE) r = FR_OK;
        }
        if(r == FR_OK) r = f_rename(temp, final);
        if(r != FR_OK) f_unlink(temp);
    }
    if(r != FR_OK && s->log) s->log("Box art: %s: cannot save cover: FatFs=%u", key, (unsigned)r);
    return r == FR_OK;
}
static enum outcome build(struct scan *s, const char *key, const char *gdi_path) {
    const char *slash = strrchr(gdi_path, '/');
    if(!slash || !kui_game_image_name_supported(slash+1)) {
        if(s->log) s->log("Box art: %s: compressed image requires offline import",key);
        return OUT_FAILED;
    }
    struct kui_cover_record record;
    memset(&record, 0, sizeof(record));
    char path[PATH_CAP], user[PATH_CAP] = "";
    FILINFO info;
    snprintf(path, sizeof(path), "0:%s", gdi_path);
    if(strlen(gdi_path) >= sizeof(record.gdi_path) || f_stat(path, &info) != FR_OK) return OUT_FAILED;
    strcpy(record.gdi_path, gdi_path);
    stamp_of(&info, &record.gdi);
    for(unsigned i = 0; i < sizeof(user_images) / sizeof(user_images[0]); ++i) {
        if(record_path(key, user_images[i], user) && f_stat(user, &info) == FR_OK && !(info.fattrib & AM_DIR)) {
            stamp_of(&info, &record.user);
            break;
        }
        user[0] = 0;
    }
    {
        FIL file;
        struct kui_cover_record old;
        if(record_open(key, gdi_path, &file, &old)) {
            f_close(&file);
            /* Records produced before a layout could be parsed have no disc
             * title. Retry those so newly supported images can acquire art. */
            if(old.title[0] && same_stamp(&old.gdi, &record.gdi) && same_stamp(&old.user, &record.user)) return OUT_UNCHANGED;
        }
    }
    /* Bounded image metadata, then IP and the root directory for title/art. */
    struct image_files files = {.cancel = s->cancel};
    size_t root = (size_t)(slash - gdi_path);
    if(root >= sizeof(files.root)) return OUT_FAILED;
    if(root) { memcpy(files.root, gdi_path, root); files.root[root] = 0; } else strcpy(files.root, "/");
    struct kui_game_image *image = malloc(sizeof(*image));
    bool io = false, have_image = false, art = false;
    struct kui_game_metadata meta;
    memset(&meta, 0, sizeof(meta));
    if(!image) io = true;
    else {
        struct kui_game_file_ops ops = {&files, image_stat, image_read};
        enum kui_game_result r = kui_game_image_open_named(slash+1, &ops, image);
        io = r == KUI_GAME_IO || r == KUI_GAME_CANCELLED || r == KUI_GAME_NOT_FOUND;
        have_image = r == KUI_GAME_OK;
        if(!have_image && !io && s->log) s->log("Box art: %s: %s", key, kui_game_result_name(r));
    }
    struct kui_game_metadata_ops meta_ops = {image, metadata_sector, metadata_range};
    if(have_image) {
        enum kui_game_metadata_status m = kui_game_metadata_read(&meta_ops,image->data_lba,&meta);
        if(m == KUI_GAME_METADATA_IO || m == KUI_GAME_METADATA_CANCELLED) io = true;
        if(meta.ip_valid) {
            snprintf(record.title, sizeof(record.title), "%s", meta.title);
            snprintf(record.product, sizeof(record.product), "%s", meta.product);
            snprintf(record.version, sizeof(record.version), "%s", meta.version);
            snprintf(record.region, sizeof(record.region), "%s", meta.region);
        }
    }
    if(!io && user[0] && user_art(s, key, user)) {
        record.source = KUI_COVER_SOURCE_USER;
        art = true;
        if(s->log) s->log("Box art: %s: your image %s", key, user + 2);
    }
    if(!io && !art && have_image && meta.root_bytes) {
        uint32_t lba = 0, bytes = 0;
        enum kui_game_metadata_status m = kui_game_metadata_find(&meta_ops, &meta, "0GDTEX.PVR",
            KUI_GAMES_ART_MAX_BYTES, &lba, &bytes);
        if(m == KUI_GAME_METADATA_OK && disc_art(s, key, image, lba, bytes, &io)) {
            record.source = KUI_COVER_SOURCE_DISC;
            art = true;
        } else if(m == KUI_GAME_METADATA_IO || m == KUI_GAME_METADATA_CANCELLED) io = true;
        else if(m != KUI_GAME_METADATA_OK && s->log) s->log("Box art: %s: no artwork on the disc", key);
    }
    if(!image_close(&files)) io = true;
    free(image);
    if(io || stopped(s->cancel)) return OUT_FAILED;
    if(!record_write(s, key, &record)) return OUT_FAILED;
    return record.source == KUI_COVER_SOURCE_USER ? OUT_USER : record.source == KUI_COVER_SOURCE_DISC ? OUT_DISC : OUT_NONE;
}
static void summary(struct kui_app_status *status, const struct kui_games_scan_counts *c) {
    status->line_count = 0;
    char (*l)[KUI_APP_LINE_CAP] = status->lines;
    snprintf(l[status->line_count++], KUI_APP_LINE_CAP, "Games found: %u", c->games);
    snprintf(l[status->line_count++], KUI_APP_LINE_CAP, "New covers from discs: %u", c->disc);
    snprintf(l[status->line_count++], KUI_APP_LINE_CAP, "New covers from your images: %u", c->user);
    snprintf(l[status->line_count++], KUI_APP_LINE_CAP, "No artwork found: %u", c->none);
    snprintf(l[status->line_count++], KUI_APP_LINE_CAP, "Unchanged since last scan: %u", c->unchanged);
    if(c->duplicates)
        snprintf(l[status->line_count++], KUI_APP_LINE_CAP, "Skipped, same name as another game: %u", c->duplicates);
    if(c->failed) snprintf(l[status->line_count++], KUI_APP_LINE_CAP, "Could not be read or saved: %u", c->failed);
}
bool kui_games_scan(struct kui_app_status *status, struct kui_games_scan_counts *counts,
    kui_app_progress_fn progress, kui_log_fn log, kui_cancel_fn cancel) {
    if(!status || !counts) return false;
    kui_games_covers_cache_clear();
    memset(status, 0, sizeof(*status));
    memset(counts, 0, sizeof(*counts));
    struct scan s = {.log = log, .cancel = cancel, .counts = counts};
    snprintf(status->message, sizeof(status->message), "Finding games...");
    if(progress) progress(status);
    const char *problem = NULL;
    bool connected = false, mounted = false;
    FATFS fs;
    if(stopped(cancel)) problem = "Box art scan stopped";
    else if(!(connected = kui_sd_connect())) problem = "SD card unavailable";
    else if(!(mounted = kui_mount(&fs, log))) problem = "Cannot mount SD card";
    static const char *const folders[] = {"0:/KUI", KUI_GAMES_COVERS_FOLDER};
    if(!problem && !make_folders(folders, 2, log)) problem = "Cannot create KUI/covers";
    if(!problem) {
        s.record = malloc(KUI_COVER_FILE_BYTES);
        s.raw = malloc(16u * KUI_GAME_RAW_BYTES);
        s.scaled = malloc(KUI_COVER_PIXELS * sizeof(uint16_t));
        if(!s.record || !s.raw || !s.scaled) problem = "Insufficient memory for the box art scan";
    }
    if(!problem && !walk(&s, KUI_GAMES_SCAN_ROOT, 0))
        problem = stopped(cancel) ? "Box art scan stopped" : "Cannot read the /Games folder";
    counts->games = s.found;
    const char *at = s.names;
    for(unsigned i = 0; !problem && i < s.found; ++i) {
        const char *key = at, *path = nth_path(at);
        at = path + strlen(path) + 1u;
        if(stopped(cancel)) { problem = "Box art scan stopped"; break; }
        status->done = i;
        status->total = s.found;
        snprintf(status->message, sizeof(status->message), "Box art %u of %u: %.90s", i + 1u, s.found, key);
        summary(status, counts);
        if(progress) progress(status);
        switch(build(&s, key, path)) {
        case OUT_UNCHANGED: ++counts->unchanged; break;
        case OUT_DISC: ++counts->disc; break;
        case OUT_USER: ++counts->user; break;
        case OUT_NONE: ++counts->none; break;
        default:
            if(stopped(cancel)) problem = "Box art scan stopped";
            else { ++counts->failed; if(log) log("Box art: %s: skipped after a read or save problem", key); }
            break;
        }
        status->done = i + 1u;
    }
    free(s.names); free(s.hashes); free(s.record); free(s.raw); free(s.scaled);
    if(mounted && f_mount(NULL, "0:", 0) != FR_OK && !problem) problem = "Cannot release SD filesystem";
    if(connected) kui_sd_disconnect();
    summary(status, counts);
    status->complete = true;
    status->stopped = problem && stopped(cancel);
    status->errors = counts->failed;
    status->passed = !problem && !counts->failed;
    if(problem) snprintf(status->message, sizeof(status->message), "%s after %u of %u games", problem,
                         (unsigned)status->done, counts->games);
    else snprintf(status->message, sizeof(status->message), "Box art ready: %u new, %u unchanged, %u without art",
                  counts->disc + counts->user, counts->unchanged, counts->none);
    if(log) {
        log("%s", status->message);
        for(unsigned i = 0; i < status->line_count; ++i) log("  %s", status->lines[i]);
    }
    if(progress) progress(status);
    return !problem;
}
