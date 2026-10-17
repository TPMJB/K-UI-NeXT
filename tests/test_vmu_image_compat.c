/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the existing real-FatFs/physical-transport test fixture unchanged.
 * Only its test main is renamed; production src/apps/vmu.c and the existing
 * save parser are compiled as separate translation units. */
#define main kui_existing_vmu_fixture_main
#include "test_vmu_app.c"
#undef main
#include "kui/vmu_image.h"
#include "kui/vmu_import.h"
#include <stddef.h>

static struct kui_vmu_image_workspace compat_work;
static uint8_t compat_payload[1024], compat_entry[32], compat_root[512];
static uint8_t compat_expected_image[256][512];

static int compat_read(void *context, uint16_t block, void *data) {
    assert(context == card && block < 256);
    memcpy(data, card[block], 512);
    return 0;
}
static int compat_write(void *context, uint16_t block, const void *data) {
    assert(context == card && block < 256);
    memcpy(card[block], data, 512);
    return 0;
}
static int compat_sync(void *context) {
    assert(context == card);
    return 0;
}
static const struct kui_vmu_image_io compat_io = {card,compat_read,compat_write,compat_sync};

static void compat_format(void) {
    memset(card, 0, sizeof(card));
    assert(!kui_vmu_image_format_blank(&compat_io, sizeof(card), NULL, &compat_work));
    struct kui_vmu_image_summary result;
    assert(!kui_vmu_image_validate(&compat_io, sizeof(card), &compat_work, &result));
    assert(!result.files && result.free_blocks == 200);
}

static unsigned compat_crc(const uint8_t *data, size_t bytes) {
    unsigned state = 0;
    for(size_t i = 0; i < bytes; ++i) {
        state ^= (unsigned)data[i] << 8;
        for(unsigned bit = 0; bit < 8; ++bit)
            state = ((state << 1) ^ ((state & 0x8000) ? 0x1021 : 0)) & 0xffff;
    }
    return state;
}

static void compat_make_save(void) {
    char error[128]; struct kui_vmu_save_info parsed;
    memset(compat_payload, 0, sizeof(compat_payload));
    memcpy(compat_payload, "K-UI VMU proof", 14);
    memcpy(compat_payload + 16, "Constructed valid save fixture", 30);
    memcpy(compat_payload + 48, "KUI-VMU-TEST", 12);
    put16(compat_payload + 64, 1); /* one icon */
    compat_payload[72] = 13;       /* application bytes, little endian */
    for(unsigned i = 128; i < 653; ++i) compat_payload[i] = (uint8_t)(i * 11 + 5);
    put16(compat_payload + 70, compat_crc(compat_payload, 653));
    assert(kui_vmu_save_parse(KUI_VMU_SAVE_VMS, compat_payload,
        sizeof(compat_payload), NULL, "VMUPROOF", &parsed, error, sizeof(error)));
    assert(parsed.payload_bytes == 1024 && parsed.padded_bytes == 1024);
    assert(parsed.entry[0] == 0x33 && parsed.entry[24] == 2);
    memcpy(compat_entry, parsed.entry, 32);
    put16(compat_entry + 2, 199);
    memcpy(card[199], compat_payload, 512);
    memcpy(card[198], compat_payload + 512, 512);
    put16(card[254] + 199 * 2, 198);
    put16(card[254] + 198 * 2, 0xfffa);
    memcpy(card[253], compat_entry, 32);
}

static void compat_native_layout(void) {
    /* These offsets are independently checked against pinned KOS vmufs.h.
     * Existing host transport stubs retain those exact public structures. */
    _Static_assert(sizeof(vmu_root_t) == 512, "pinned KOS root size");
    _Static_assert(offsetof(vmu_root_t, timestamp) == 48, "pinned KOS timestamp");
    _Static_assert(offsetof(vmu_root_t, fat_loc) == 70, "pinned KOS FAT location");
    _Static_assert(offsetof(vmu_root_t, dir_loc) == 74, "pinned KOS directory location");
    _Static_assert(offsetof(vmu_root_t, blk_cnt) == 80, "pinned KOS user count");
    _Static_assert(sizeof(vmu_dir_t) == 32, "pinned KOS directory size");
    _Static_assert(offsetof(vmu_dir_t, firstblk) == 2, "pinned KOS first block");
    _Static_assert(offsetof(vmu_dir_t, filesize) == 24, "pinned KOS file size");
    vmu_root_t root;
    memcpy(&root, card[255], sizeof(root));
    assert(root.fat_loc == 254 && root.fat_size == 1);
    assert(root.dir_loc == 253 && root.dir_size == 13 && root.blk_cnt == 200);
    assert(!memcmp(root.magic, "UUUUUUUUUUUUUUUU", 16));
    assert(((const uint8_t *)&root.timestamp)[0] == 0x20);
    assert(((const uint8_t *)&root.timestamp)[2] == 1);
    uint16_t fat[256]; memcpy(fat, card[root.fat_loc], sizeof(fat));
    assert(fat[255] == 0xfffa && fat[254] == 0xfffa);
    for(unsigned i = 253; i > 241; --i) assert(fat[i] == i - 1);
    assert(fat[241] == 0xfffa);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    char *seed_args[] = {argv[0],argv[1],"seed",NULL};
    assert(!kui_existing_vmu_fixture_main(3,seed_args));
    image = fopen(argv[1],"r+b"); assert(image);
    struct stat st; assert(!fstat(fileno(image),&st));
    image_blocks = (uint64_t)st.st_size / 512;
    struct kui_vmu_view view;
    struct kui_vmu_image_summary result;
    FATFS fs;
    const char *empty_path = "0:/KUI/backups/vmu/A1-0001/empty.vmd";
    const char *proof_path = "0:/KUI/backups/vmu/A1-0001/proof.vmd";
    const char *bundle = "0:/KUI/backups/vmu/A1-0002/001_VMUPROOF.vms";

    compat_format(); compat_native_layout();
    memcpy(compat_root,card[255],512);
    kui_vmu_app_run(0,0,0,0,&view,log_line,cancelled,progress);
    assert(view.status.passed && view.present && !view.total && view.free_blocks == 200);
    assert(!vmu_writes && !locks);
    mount_image(&fs); write_file(empty_path,card,sizeof(card));
    assert(f_mount(NULL,"0:",0)==FR_OK);
    unsigned physical_reads = reads, physical_enumerations = enumerations;
    kui_vmu_image_run(empty_path,0,0,0,&view,log_line,cancelled,image_progress);
    assert(view.status.passed && view.present && !view.total && view.free_blocks == 200);
    assert(reads == physical_reads && enumerations == physical_enumerations && !vmu_writes);

    compat_make_save();
    assert(!kui_vmu_image_validate(&compat_io,sizeof(card),&compat_work,&result));
    assert(result.files == 1 && result.used_blocks == 2 && result.free_blocks == 198);
    kui_vmu_app_run(0,0,0,0,&view,log_line,cancelled,progress);
    assert(view.status.passed && view.total == 1 && view.free_blocks == 198);
    assert(view.entries[0].bytes == 1024 && !strcmp(view.entries[0].name,"VMUPROOF"));
    memcpy(compat_expected_image,card,sizeof(card));
    mount_image(&fs); write_file(proof_path,card,sizeof(card));
    assert(f_mount(NULL,"0:",0)==FR_OK);
    physical_reads = reads; physical_enumerations = enumerations;
    kui_vmu_image_run(proof_path,0,0,0,&view,log_line,cancelled,image_progress);
    assert(view.status.passed && view.total == 1 && view.free_blocks == 198);
    assert(!strcmp(view.entries[0].name,"VMUPROOF"));
    kui_vmu_image_run(proof_path,0,0,1,&view,log_line,cancelled,image_progress);
    assert(view.status.passed && view.status.complete && view.status.done == 1024);
    assert(reads == physical_reads && enumerations == physical_enumerations && !vmu_writes);
    mount_image(&fs);
    equal_file(bundle,compat_payload,sizeof(compat_payload));
    equal_file("0:/KUI/backups/vmu/A1-0002/001_VMUPROOF.dir",compat_entry,32);
    equal_file(proof_path,compat_expected_image,sizeof(compat_expected_image));
    verify_preserved(); assert(f_mount(NULL,"0:",0)==FR_OK);

    /* Existing restore admission + commit into another freshly formatted
     * virtual card, using the unchanged physical-transport simulation. */
    compat_format();
    kui_vmu_restore_run(bundle,0,false,&view,log_line,cancelled,progress);
    assert(view.status.passed && view.restore_ready && !vmu_writes);
    kui_vmu_restore_run(bundle,0,true,&view,log_line,cancelled,progress);
    assert(view.status.passed && view.status.complete && vmu_writes);
    assert(!memcmp(card[199],compat_payload,512));
    assert(!memcmp(card[198],compat_payload+512,512));
    assert(!memcmp(card[253],compat_entry,32));
    assert(!memcmp(card[255],compat_root,512));
    assert(!kui_vmu_image_validate(&compat_io,sizeof(card),&compat_work,&result));
    assert(result.files == 1 && result.used_blocks == 2 && result.free_blocks == 198);
    kui_vmu_app_run(0,0,0,0,&view,log_line,cancelled,progress);
    assert(view.status.passed && view.total == 1 && view.free_blocks == 198);
    mount_image(&fs);
    equal_file(proof_path,compat_expected_image,sizeof(compat_expected_image));
    verify_preserved(); assert(f_mount(NULL,"0:",0)==FR_OK);
    assert(!locks && !fclose(image));
    puts("PASS VMU image compatibility: current manager, VMS parser, VMD browse/extract, verified restore");
    return 0;
}
