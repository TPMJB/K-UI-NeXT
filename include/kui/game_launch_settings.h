/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAME_LAUNCH_SETTINGS_H
#define KUI_GAME_LAUNCH_SETTINGS_H
#include "kui/probe.h"
#include "kui/zso.h"
#include "kui/game_ram_patches.h"

/* Persistent/wire values stay 0/1/2. In the integrated native, non-CDDA
 * route BACKGROUND selects New (X), EAGER selects Legacy (Y); the historical
 * EAGER name remains for binary/JSON compatibility. CE/CDDA retain their
 * separate forced-reader rules. No saved entry is reset or renumbered. */
enum kui_game_reader {
    KUI_GAME_READER_STANDARD, KUI_GAME_READER_BACKGROUND, KUI_GAME_READER_EAGER,
    KUI_GAME_READER_COUNT
};
enum kui_game_encoding {
    KUI_GAME_ENCODING_AUTO, KUI_GAME_ENCODING_PLAIN, KUI_GAME_ENCODING_SCRAMBLED,
    KUI_GAME_ENCODING_COUNT
};
struct kui_game_launch_settings {
    unsigned reader;bool cdda;unsigned encoding;
    unsigned codec; /* Explicit ZSO dialect; default standard LZ4, never inferred. */
    struct kui_game_ram_patches patches;
};
#define KUI_GAME_LAUNCH_SETTINGS_ENTRIES 128u
#define KUI_GAME_LAUNCH_SETTINGS_PATH_CAP 512u
#define KUI_GAME_LAUNCH_SETTINGS_HEADER_SIZE 32u
#define KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX (36u + KUI_GAME_LAUNCH_SETTINGS_ENTRIES * (24u + KUI_GAME_LAUNCH_SETTINGS_PATH_CAP - 1u))
#define KUI_GAME_LAUNCH_SETTINGS_PATH_A "0:/KUI/apps/games/launch-settings-a.bin"
#define KUI_GAME_LAUNCH_SETTINGS_PATH_B "0:/KUI/apps/games/launch-settings-b.bin"
#define KUI_GAME_LAUNCH_LEGACY_PATH "0:/KUI/game-launch.json"
#define KUI_GAME_LAUNCH_LEGACY_BACKUP_PATH "0:/KUI/game-launch.json.bak"
#define KUI_GAME_LAUNCH_LEGACY_JSON_MAX (KUI_GAME_LAUNCH_SETTINGS_ENTRIES * (6u * KUI_GAME_LAUNCH_SETTINGS_PATH_CAP + 96u) + 64u)
struct kui_game_launch_entry {
    char path[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    struct kui_game_launch_settings settings;
};
struct kui_game_launch_table {
    unsigned count;
    struct kui_game_launch_entry entries[KUI_GAME_LAUNCH_SETTINGS_ENTRIES];
};

void kui_game_launch_settings_default(struct kui_game_launch_settings *out);
bool kui_game_launch_settings_valid(const struct kui_game_launch_settings *settings);
/* Keys retain the whole path, with ASCII case folded and separators normalized.
 * /Games/... and 0:/Games/... identify the same mounted-volume image. Other
 * drives, dot/parent segments, empty/trailing names and unsafe bytes fail.
 * Failure preserves output. The module does not inspect or launch the image. */
bool kui_game_launch_settings_normalize(char out[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP],const char *path);
void kui_game_launch_table_clear(struct kui_game_launch_table *table);
bool kui_game_launch_table_find(const struct kui_game_launch_table *table,const char *path,
                                struct kui_game_launch_settings *out);
/* Updating an existing key works at capacity; new keys fail without eviction.
 * Invalid input leaves the collection unchanged. */
bool kui_game_launch_table_upsert(struct kui_game_launch_table *table,const char *path,
                                  const struct kui_game_launch_settings *settings);
/* Little-endian records: 32-byte header, variable-length entries and CRC32.
 * Read v1 entries {u16 path bytes,u8 reader,u8 CDDA,u8 encoding,3 reserved zero}
 * with patches disabled. Read v2 entries {u16 path bytes,u8 reader,u8 CDDA,
 * u8 encoding,u8 patch-enable mask,u16 reserved zero,u32 address0,u32 value0,
 * u32 address1,u32 value1}; mask bits0/1 select the two patches, other bits fail.
 * Write v3 using the same 24-byte entry with u8 codec at byte6 (0 LZ4,
 * 1 LZO1X) and byte7 reserved zero. Old v1/v2 default to LZ4.
 * All entries end in canonical non-NUL path bytes. Disabled patch tuples are
 * retained, so an edited patch can be disabled and reenabled without reentry.
 * Header has version at8, sequence at16, count at24 and reserved zero at28.
 * Sequence0/unknown fields/duplicate keys/truncation/trailing bytes fail.
 * Encode/decode failure preserves caller outputs and sequence. */
bool kui_game_launch_settings_encode(void *record,size_t capacity,size_t *size,
                                     const struct kui_game_launch_table *table,uint64_t sequence);
bool kui_game_launch_settings_decode(struct kui_game_launch_table *table,uint64_t *sequence,
                                     const void *record,size_t size);
/* Strict independent K-UI JSON v1 import. Scratch table is unspecified on
 * failure and must never be used. Reader strings standard/x/y retain their
 * existing numeric bindings 0/1/2. Normalized duplicate keys/capacity overflow
 * fail; release_loader=true entries are counted and skipped, since the old
 * comparison binary has no supported equivalent. Sources remain read-only. */
bool kui_game_launch_settings_legacy_decode(struct kui_game_launch_table *table,unsigned *skipped,
                                            const void *json,size_t size);
/* Caller owns the mounted volume and excludes file workers. These functions
 * do not mount, lock, launch, or decide whether CE/CDDA/reader options are
 * supported. Only when both binary paths are absent, load/save may import
 * strict legacy JSON (or its .bak recovery) into a new verified slot. JSON is
 * never altered; unsupported Release-loader entries are skipped with a log.
 * Invalid existing binary slots never resurrect stale JSON. Missing/invalid
 * slots otherwise use defaults; real I/O or failed migration returns false.
 * Save rereads both slots, updates only this key, writes the older/invalid slot,
 * syncs/closes and verifies every byte by reread. Current valid slot is never
 * opened for writing. Transient bounded heap workspace avoids worker-stack
 * growth. Filesystem/card power-loss guarantees remain platform dependent. */
bool kui_game_launch_settings_load(const char *image_path,struct kui_game_launch_settings *out,kui_log_fn log);
bool kui_game_launch_settings_save(const char *image_path,const struct kui_game_launch_settings *settings,kui_log_fn log);
#endif
